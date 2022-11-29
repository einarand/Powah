#include <WiFi.h>
#include <ModbusRTU.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <WiFiManager.h>
#include <Update.h>
#include <AsyncTimer.h>

#define MBUS_TXD_PIN 23
#define SLAVE_ID 1
#define APP_NAME "Powah!"
#define POWAH_VERSION "1.06"

ModbusRTU mb;
WebServer server(80);
Modbus::ResultCode lastResultCode = Modbus::ResultCode::EX_CANCEL;
AsyncTimer t;

const char* mainPage =
"<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
"<meta name='viewport' content='width=device-width, initial-scale=1'>"
"<link rel='stylesheet' href='https://www.w3schools.com/w3css/4/w3.css'>"
"<style>"
  "body { font-family: \"Courier New\";}"
  "th, td { padding: 5px;}"
"</style>"
"<br><br><br>"
  "<table width='30%' bgcolor='e6e6e6' align='center'>"
    "<tr>"
      "<td colspan=2><center><font size=4><b>--={ " APP_NAME " v" POWAH_VERSION " }=--<b/></font></center><br></td>"
    "</tr>"
    "<tr>"
      "<td colspan=2><font size=2>" APP_NAME " is a modbus controller for the Carlo Gavazzi EM340 meter. It will measure data on demand when calling the <a href='/measurement'>measurement</a> endpoint. EM340 documentation can be found <a href='https://gavazzi.se/app/uploads/2020/11/em330_em340_et330_et340_cp.pdf' target='_blank'>here</a></font></td>"
    "</tr>"
    "<tr>"
      "<td><a href='/update'>Firmware update</a></td>"
    "</tr>"
    "<tr>"
      "<td colspan=2><br><font size=1><center>an EiJoVi product</center></font></td>"
    "</tr>"
  "</table>";

const char* updatePage =
"<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
"<meta name='viewport' content='width=device-width, initial-scale=1'>"
"<link rel='stylesheet' href='https://www.w3schools.com/w3css/4/w3.css'>"
"<style>"
  "body { font-family: \"Courier New\";}"
  "th, td { padding: 5px;}"
"</style>"
"<br><br><br>"
"<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
  "<table width='30%' bgcolor='e6e6e6' align='center'>"
    "<tr>"
      "<td colspan=2><center><font size=4><b>--={ " APP_NAME " v" POWAH_VERSION " }=--<b/><br>Firmware update</font></center><br></td>"
    "</tr>"
    "<tr>"
      "<td><input type='file' name='update'></td>"
      "<td><input type='submit' value='Update'><br></td>"
    "</tr>"
    "<tr>"
    "<td colspan=2>"
      "<div class='w3-light-grey'>"
        "<div id='bar' class='w3-container w3-green w3-round' style='height:7px;width:0%'>"
     "</div>"
    "</td>"
    "<tr>"
      "<td colspan=2><font size=2><center><div id='prg'>update not started</div></center></font></td>"
    "</tr>"
    "<tr>"
      "<td colspan=2><br><font size=1><center>an EiJoVi product</center></font></td>"
    "</tr>"
"</form>"
 "<script>"
  "$('form').submit(function(e){"
    "e.preventDefault();"
    "var form = $('#upload_form')[0];"
    "var data = new FormData(form);"
    " $.ajax({"
      "url: '/uploadfw',"
      "type: 'POST',"
      "data: data,"
      "contentType: false,"
      "processData:false,"
      "xhr: function() {"
        "var xhr = new window.XMLHttpRequest();"
        "xhr.upload.addEventListener('progress', function(evt) {"
          "if (evt.lengthComputable) {"
            "var bar = document.getElementById('bar');"
            "var prg = document.getElementById('prg');"
            "var per = Math.round((evt.loaded / evt.total) * 100);"
            "bar.style.width = per + '%';"
            "prg.textContent = per + '%';"
          "}"
      "}, false);"
    "return xhr;"
  "},"
    "success:function(d, s) {"
      "console.log('success!');"
      "alert('Firmware uploaded successfully!');"
      "window.location.assign('/updateSuccess');"
    "},"
    "error: function (a, b, c) {"
      "console.log('Update failure!');"
      "alert('Firmware upload failure! Try again.');"
      "window.location.assign('/update');"
    "}"
  "});"
 "});"
 "</script>";

 const char* updateSuccessPage =
  "<style>"
  "body { font-family: \"Courier New\";}"
  "</style>"
  "<br><br><br>"
   "<table width='30%' align='center'>"
    "<tr>"
      "<td><center><font size=4><b>Firmware successfully updated to v" POWAH_VERSION " </b></font><br><br><font size=2><a href='/'>back home</a></font></center></td>"
    "</tr>"
  "</table>";

bool cbWrite(Modbus::ResultCode resultCode, uint16_t transactionId, void* data) {
  lastResultCode = resultCode;
  if (resultCode != Modbus::ResultCode::EX_SUCCESS) {
    Serial.printf_P("Error reading Modbus! Result: 0x%02X, ESP Memory: %d\n", resultCode, ESP.getFreeHeap());
    return false;
  }
  return true;
}

void setup() {
  //Setup Serial and Modbus
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1);
  mb.begin(&Serial2, MBUS_TXD_PIN);
  mb.master();

  Serial.println("\n\n*** Welcome to Powah! ***");

  //Setup Wifi
  WiFiManager wm;
  bool res = wm.autoConnect("Powah!", "password");
  if (!res) {
    Serial.println("Failed to connect");
    wm.resetSettings();
    ESP.restart();
  }

  //Create and set hostName
  String id = WiFi.macAddress();
  id.remove(0, 8);
  id.replace(":", "");
  String hostName = "Powah-" + id;
  WiFi.hostname(hostName);

  //Setup local DNS: hostName.local
  if (MDNS.begin(hostName.c_str())) {
    Serial.println("MDNS responder started. MDNS name: " + hostName);
  }

  //Setup web server
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", mainPage);
  });
  server.on("/measurement", meaurement);
  server.onNotFound(handleNotFound);
  server.on("/update", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", updatePage);
  });
    server.on("/updateSuccess", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", updateSuccessPage);
  });
  /*handling uploading firmware file */
  server.on("/uploadfw", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    t.setTimeout([]() {
      ESP.restart();
    }, 500);
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("Update: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      /* flashing firmware to ESP*/
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });
  server.begin();
  Serial.println("HTTP server started");

  Serial.println("Setup done.");
}

void readMeterData(uint16_t startAt, uint16_t registersToRead, uint16_t* buffer) {
  mb.readHreg(SLAVE_ID, startAt, buffer, registersToRead, cbWrite);
  while (mb.slave()) {  // Check if transaction is active
    mb.task();
    delay(10);
  }
}

String toDoubleString(uint16_t bufferPosition, uint16_t* buffer, uint16_t divider = 1, uint16_t decimals = 0) {
  uint32_t value = ((uint32_t)buffer[bufferPosition + 1]) << 16 | buffer[bufferPosition];
  double doubleValue = ((double)value) / divider;
  return String(doubleValue, decimals);
}

String toFloatString(uint16_t bufferPosition, uint16_t* buffer, uint16_t divider = 1, uint16_t decimals = 0) {
  float floatValue = ((float)buffer[bufferPosition]) / divider;
  return String(floatValue, decimals);
}

double readKWh() {
  uint16_t res[3];
  mb.readHreg(SLAVE_ID, 0x400, res, 3, cbWrite);  // Send Read Hreg from Modbus Server
  while (mb.slave()) {                            // Check if transaction is active
    mb.task();
    delay(10);
  }
  uint32_t integer = ((uint32_t)res[1]) << 16 | res[0];  //Integer value Value=INT(kWh)*1
  uint16_t decimal = res[2];                             //Decimal value. Value=DEC(kWh)*1000

  return ((double)integer) + ((double)decimal) / 1000;
}

void loop() {
  server.handleClient();
  t.handle();
  delay(2);
}

// Web server related stuff here:
void meaurement() {
  uint16_t bodySize = 500;
  char temp[bodySize];
  int uptime = millis() / 1000;

  if (!mb.slave()) {  // Check if no transaction in progress

    uint16_t buffer[71];
    readMeterData(0x00, 35, &buffer[0]);
    readMeterData(0x22, 36, &buffer[34]);

    if (lastResultCode == Modbus::ResultCode::EX_SUCCESS) {
      snprintf(temp, bodySize,
               "{\n\
  \"version\": \"%s\",\n\
  \"uptimeSeconds\": %d,\n\
  \"lastResultCode\": \"0x%02X\",\n\
  \"V_L1L2\": %s,\n\
  \"V_L2L3\": %s,\n\
  \"V_L3L1\": %s,\n\
  \"A_L1\": %s,\n\
  \"A_L2\": %s,\n\
  \"A_L3\": %s,\n\
  \"W_L1\": %s,\n\
  \"W_L2\": %s,\n\
  \"W_L3\": %s,\n\
  \"W_SYS\": %s,\n\
  \"Hz\": %s,\n\
  \"kWh_TOT\": %s,\n\
  \"kWh_L1\": %s,\n\
  \"kWh_L2\": %s,\n\
  \"kWh_L3\": %s\n\
}",
               POWAH_VERSION,
               uptime,
               lastResultCode,
               toDoubleString(0x06, buffer, 10, 1),    //V_L1-L2
               toDoubleString(0x08, buffer, 10, 1),    //V_L2-L3
               toDoubleString(0x0A, buffer, 10, 1),    //V_L3-L1
               toDoubleString(0x0C, buffer, 1000, 3),  //A_L1
               toDoubleString(0x0E, buffer, 1000, 3),  //A_L2
               toDoubleString(0x10, buffer, 1000, 3),  //A_L3
               toDoubleString(0x12, buffer, 10, 1),    //W_L1
               toDoubleString(0x14, buffer, 10, 1),    //W_L3
               toDoubleString(0x16, buffer, 10, 1),    //W_L3
               toDoubleString(0x28, buffer, 10, 1),    //W sys
               toFloatString(0x33, buffer, 10, 1),     //Hz
               toDoubleString(0x40, buffer, 10, 1),    //W_L1
               toDoubleString(0x42, buffer, 10, 1),    //W_L3
               toDoubleString(0x44, buffer, 10, 1),    //W_L3
               String(readKWh(), 3)                    //kWh (+) TOT (3 decimals)

      );
      server.send(200, "Application/json", temp);
    } else if (lastResultCode == Modbus::ResultCode::EX_TIMEOUT) {
      snprintf(temp, 200,
               "{\n\
  \"version\": \"%s\",\n\
  \"uptimeSeconds\": %d,\n\
  \"lastResultCode\": \"0x%02X\",\n\
  \"error\": \"timeout\"\n\
}",
               POWAH_VERSION, uptime, lastResultCode);
      server.send(504, "Application/json", temp);
    } else {
      snprintf(temp, 200,
               "{\n\
  \"uptimeSeconds\": %d,\n\
  \"lastResultCode\": \"0x%02X\",\n\
  \"error\": \"unknown\"\n\
}",
               uptime, lastResultCode);
      server.send(500, "Application/json", temp);
    }
  } else {
    server.send(500, "Application/json", "Internal Server error");
  }
}

void handleNotFound() {
  String message = "404 Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }

  server.send(404, "text/plain", message);
}

