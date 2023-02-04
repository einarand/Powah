#include <WiFi.h>
#include <ModbusRTU.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <WiFiManager.h>
#include <Update.h>
#include <AsyncTimer.h>
#include <esp_task_wdt.h>

#define MBUS_TXD_PIN 23
#define LED_PIN 2
#define BUTTON_PIN 21
#define SLAVE_ID 1
#define APP_NAME "Powah!"
#define POWAH_VERSION "1.11"
#define WDT_TIMEOUT 30  //Watchdog timeout in seconds

ModbusRTU mb;
WebServer server(80);
AsyncTimer t;
WiFiManager wm;

const char* mainPage =
  "<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
  "<meta name='viewport' content='width=device-width, initial-scale=1'>"
  "<link rel='stylesheet' href='https://www.w3schools.com/w3css/4/w3.css'>"
  "<style>"
  "body { font-family: \"Courier New\"; font-size: 5px;}"
  "th, td { padding: 4px;}"
  "</style>"
  "<br><br><br>"
  "<table width='500px' bgcolor='e6e6e6' align='center'>"
  "<tr>"
  "<td colspan=3><center><font size=4><b>--={ " APP_NAME " v" POWAH_VERSION " }=--<b/></font></center></td>"
  "</tr>"
  "<tr><font size=2>"
  "<td style='text-align:center'><a href='/'><b>home</b></a></td>"
  "<td style='text-align:center'><a href='/update'>firmware update</a></td>"
  "<td style='text-align:center'><a href='/measurement'>measurements</a></td></font>"
  "</tr>"
  "<tr></tr>"
  "<tr>"
  "<td colspan=3><font size=2>" APP_NAME " is a modbus controller for the Carlo Gavazzi EM340 meter. It will measure data on demand when calling the <a href='/measurement'>measurement</a> endpoint. EM340 documentation can be found <a href='https://gavazzi.se/app/uploads/2020/11/em330_em340_et330_et340_cp.pdf' target='_blank'>here</a></font></td>"
  "</tr>"
  "<tr></tr>"
  "<tr><td>Modbus status:</td><td colspan=2><div id='resultCode'>-</div></td></tr>"
  "<tr><td>Meter type:</td><td colspan=2><div id='meterType'>-</div></td></tr>"
  "<tr><td colspan=3><br><font size=1><center>an EiJoVi product</center></font></td></tr>"
  "</table>"
  "<script>"
  "fetch('/config').then(function(response) {"
  "return response.json();"
  "}).then(function(json) {"
  "$('#resultCode').html(''+ json.resultCode);"
  "$('#meterType').html(''+ json.meterName);"
  "console.log(json);"
  "});"
  "</script>";

const char* updatePage =
  "<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
  "<meta name='viewport' content='width=device-width, initial-scale=1'>"
  "<link rel='stylesheet' href='https://www.w3schools.com/w3css/4/w3.css'>"
  "<style>"
  "body { font-family: \"Courier New\"; font-size: 5px;}"
  "th, td { padding: 4px;}"
  "</style>"
  "<br><br><br>"
  "<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
  "<table width='500px' bgcolor='e6e6e6' align='center'>"
  "<tr>"
  "<td colspan=3><center><font size=4><b>--={ " APP_NAME " v" POWAH_VERSION " }=--<b/></font></center></td>"
  "</tr>"
  "<tr><font size=2>"
  "<td style='text-align:center'><a href='/'>home</a></td>"
  "<td style='text-align:center'><a href='/update'><b>firmware update</b></a></td>"
  "<td style='text-align:center'><a href='/measurement'>measurements</a></td></font>"
  "</tr>"
  "<tr></tr>"
  "<td colspan=3><font size=2>Choose Powah.bin-file to upload.</td>"
  "<tr>"
  "<td colspan=2><input type='file' name='update'></td>"
  "<td><input type='submit' value='Update'><br></td>"
  "</tr>"
  "<tr>"
  "<td colspan=3><div class='w3-light-grey'><div id='bar' class='w3-container w3-green w3-round' style='height:7px;width:0%'></div></div></td>"
  "</tr>"
  "<tr>"
  "<td colspan=3><font size=2><center><div id='prg'>update not started</div></center></font></td>"
  "</tr>"
  "<tr>"
  "<td colspan=3><br><font size=1><center>an EiJoVi product</center></font></td>"
  "</tr>"
  "</form>"
  "<script>"
  "$('form').submit(function(e){"
  "e.preventDefault();"
  "var form = $('#upload_form')[0];"
  "var data = new FormData(form);"
  "$.ajax({"
  "url: '/uploadfw',type: 'POST', data: data, contentType: false, processData:false, xhr: function() {"
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

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  //Setup Serial and Modbus
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1);
  mb.begin(&Serial2, MBUS_TXD_PIN);
  mb.master();

  Serial.println("\n\n*** Welcome to Powah! ***");

  //Setup Wifi
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
  server.onNotFound(handleNotFound);

  server.on("/", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", mainPage);
  });
  server.on("/config", configPage);
  server.on("/measurement", meaurement);
  server.on("/update", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", updatePage);
  });
  server.on("/updateSuccess", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", updateSuccessPage);
  });
  /*handling uploading firmware file */
  server.on(
    "/uploadfw", HTTP_POST, []() {
      server.sendHeader("Connection", "close");
      server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
      t.setTimeout([]() {
        ESP.restart();
      },
                   500);
    },
    []() {
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("Update: %s\n", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {  //start with max available size
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        /* flashing firmware to ESP*/
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {  //true to set the size to the current progress
          Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
        } else {
          Update.printError(Serial);
        }
      }
    });
  server.begin();
  Serial.println("HTTP server started");

  //Set up MDNS
  MDNS.addService("powah", "tcp", 80);
  String name = "" APP_NAME " [" + id + "]";
  MDNS.addServiceTxt("powah", "tcp", "name", name);
  MDNS.addServiceTxt("powah", "tcp", "id", id);
  MDNS.addServiceTxt("powah", "tcp", "version", POWAH_VERSION);
  MDNS.addServiceTxt("powah", "tcp", "endpoint", "/measurement");

  //Start watchdog
  esp_task_wdt_init(WDT_TIMEOUT, true);  //enable panic so ESP32 restarts
  esp_task_wdt_add(NULL);                //add current thread to WDT watch

  //Setup complete
  Serial.println("Setup done.");
  digitalWrite(LED_PIN, LOW);
}

char buttonState = 0;
char lastButtonState = HIGH;
bool buttonDown = false;
unsigned long buttonPressTime = 0;
const unsigned long BUTTON_TIME_FOR_FACTORY_RESET = 5000;

void loop() {
  server.handleClient();
  t.handle();
  esp_task_wdt_reset();  //Reset watchdog

  //Button logic
  buttonState = digitalRead(BUTTON_PIN);
  if (buttonState != lastButtonState) {
    if (buttonState == LOW) {
      buttonPressTime = millis();
      buttonDown = true;
      Serial.println("Button pressed");
    } else {
      // the button just got released
      buttonDown = false;
    }
    // Delay a little bit to avoid bouncing
    delay(50);
  }
  lastButtonState = buttonState;
  if (buttonDown == true && millis() - buttonPressTime >= BUTTON_TIME_FOR_FACTORY_RESET) {
    buttonDown = false;
    Serial.println("Factory reset!");
    digitalWrite(LED_PIN, HIGH);
    wm.resetSettings();
    ESP.restart();
  }
  delay(2);
}

Modbus::ResultCode lastResultCode;

uint16_t readMeterData(uint16_t startAt, uint16_t registersToRead, uint16_t* buffer) {
  lastResultCode = Modbus::ResultCode::EX_DEVICE_FAILED_TO_RESPOND;  //Using this to wait for data
  digitalWrite(LED_PIN, HIGH);
  mb.readHreg(SLAVE_ID, startAt, buffer, registersToRead, [](Modbus::ResultCode resultCode, uint16_t transactionId, void* data) {
    lastResultCode = resultCode;
    if (resultCode != Modbus::ResultCode::EX_SUCCESS) {
      Serial.printf_P("Error reading Modbus! Result: 0x%02X, ESP Memory: %d\n", resultCode, ESP.getFreeHeap());
    }
    return true;
  });
  while (mb.slave()) {  // Check if transaction is active
    mb.task();
    delay(10);
  }
  while (lastResultCode == Modbus::ResultCode::EX_DEVICE_FAILED_TO_RESPOND) {
    delay(1);
  }
  digitalWrite(LED_PIN, LOW);
  return lastResultCode;
}

String toDoubleString(uint16_t bufferPosition, uint16_t* buffer, uint16_t divider = 1, uint16_t decimals = 0) {
  int32_t value = (((int32_t)buffer[bufferPosition + 1]) << 16) | buffer[bufferPosition];  //MSB->LSB, LSW->MSW
  double doubleValue = (double)value / divider;
  return String(doubleValue, decimals);
}

String toFloatString(uint16_t bufferPosition, uint16_t* buffer, uint16_t divider = 1, uint16_t decimals = 0) {
  int16_t value = buffer[bufferPosition];
  float floatValue = (float)value / divider;
  return String(floatValue, decimals);
}

String toThreeDecimalDoubleString(uint16_t bufferPosition, uint16_t* buffer, uint16_t decimals = 3) {
  int32_t integer = (((int32_t)buffer[bufferPosition + 1]) << 16) | buffer[bufferPosition];  //Integer value Value=INT(kWh)*1
  int16_t decimal = buffer[bufferPosition + 2];                                              //Decimal value. Value=DEC(kWh)*1000
  double doubleValue = ((double)integer) + ((double)decimal) / 1000;
  return String(doubleValue, decimals);
}

const char* configBody =
  "{"
  "\"resultCode\": \"0x%02X\","
  "\"meterTypeId\": %d,"
  "\"meterName\": \"%s\""
  "}";

void configPage() {
  uint16_t bodySize = 200;
  char temp[bodySize];
  uint16_t buffer[1];
  if (!mb.slave()) {  // Check if no transaction in progress
    uint16_t result = readMeterData(0x0B, 1, buffer);
    if (result == Modbus::ResultCode::EX_SUCCESS) {
      snprintf(
        temp, bodySize, configBody,
        result,
        buffer[0x00],  //0x000b Carlo Gavazzi Controls identification code
        meterType(buffer[0x00]));
      server.send(200, "Application/json", temp);
    } else {
      snprintf(temp, bodySize, "{\"resultCode\": \"0x%02X\"}", result);
      server.send(200, "Application/json", temp);
    }
  }
}

char* meterType(uint16_t meterId) {
  switch (meterId) {
    case 341:
      return "EM340-DIN AV2 3 X S1";
    default:
      return "Unknown";
  }
}

// Web server related stuff here:
void meaurement() {
  uint16_t bodySize = 700;
  char temp[bodySize];
  int uptime = millis() / 1000;

  if (!mb.slave()) {  // Check if no transaction in progress

    uint16_t buffer[71];
    uint16_t result1 = readMeterData(0x00, 35, &buffer[0]);
    uint16_t result2 = readMeterData(0x22, 36, &buffer[34]);

    uint16_t bufferkWhTot[3];
    uint16_t resultkWhTot = readMeterData(0x400, 3, &bufferkWhTot[0]);

    if (result1 == Modbus::ResultCode::EX_SUCCESS && result2 == Modbus::ResultCode::EX_SUCCESS && resultkWhTot == Modbus::ResultCode::EX_SUCCESS) {
      snprintf(temp, bodySize,
               "{\n\
  \"version\": \"%s\",\n\
  \"uptimeSeconds\": %d,\n\
  \"ESPFreeHeap\": %d,\n\
  \"buttonState\": %d,\n\
  \"resultCodes\": [\"0x%02X\", \"0x%02X\", \"0x%02X\"],\n\
  \"meterType\": %d,\n\
  \"V_L1N\": %s,\n\
  \"V_L2N\": %s,\n\
  \"V_L3N\": %s,\n\
  \"V_L1L2\": %s,\n\
  \"V_L2L3\": %s,\n\
  \"V_L3L1\": %s,\n\
  \"A_L1\": %s,\n\
  \"A_L2\": %s,\n\
  \"A_L3\": %s,\n\
  \"W_L1\": %s,\n\
  \"W_L2\": %s,\n\
  \"W_L3\": %s,\n\
  \"var_L1\": %s,\n\
  \"var_L2\": %s,\n\
  \"var_L3\": %s,\n\
  \"W_SYS\": %s,\n\
  \"VA_SYS\": %s,\n\
  \"var_SYS\": %s,\n\
  \"PF_L1\": %s,\n\
  \"PF_L2\": %s,\n\
  \"PF_L3\": %s,\n\
  \"PF_SYS\": %s,\n\
  \"PS\": %d,\n\
  \"Hz\": %s,\n\
  \"Kvarh_TOT\": %s,\n\
  \"kWh_L1\": %s,\n\
  \"kWh_L2\": %s,\n\
  \"kWh_L3\": %s,\n\
  \"kWh_TOT\": %s\n\
}",
               POWAH_VERSION,
               uptime,
               ESP.getFreeHeap(),
               digitalRead(BUTTON_PIN),
               result1, result2, resultkWhTot,                 //Modbus result codes
               buffer[0x0B],                                   //Carlo Gavazzi Controls identification code
               toDoubleString(0x00, buffer, 10, 1),            //V L1-N
               toDoubleString(0x02, buffer, 10, 1),            //V L2-N
               toDoubleString(0x04, buffer, 10, 1),            //V L3-N
               toDoubleString(0x06, buffer, 10, 1),            //V L1-L2
               toDoubleString(0x08, buffer, 10, 1),            //V L2-L3
               toDoubleString(0x0A, buffer, 10, 1),            //V L3-L1
               toDoubleString(0x0C, buffer, 1000, 3),          //A L1
               toDoubleString(0x0E, buffer, 1000, 3),          //A L2
               toDoubleString(0x10, buffer, 1000, 3),          //A L3
               toDoubleString(0x12, buffer, 10, 1),            //W L1
               toDoubleString(0x14, buffer, 10, 1),            //W L3
               toDoubleString(0x16, buffer, 10, 1),            //W L3
               toDoubleString(0x1E, buffer, 10, 1),            //var L1
               toDoubleString(0x20, buffer, 10, 1),            //var L3
               toDoubleString(0x22, buffer, 10, 1),            //var L3
               toDoubleString(0x28, buffer, 10, 1),            //W sys
               toDoubleString(0x2A, buffer, 10, 1),            //VA sys
               toDoubleString(0x2C, buffer, 10, 1),            //var sys
               toFloatString(0x2E, buffer, 1000, 3),           //PF L1
               toFloatString(0x2F, buffer, 1000, 3),           //PF L3
               toFloatString(0x30, buffer, 1000, 3),           //PF L3
               toFloatString(0x31, buffer, 1000, 3),           //PF sys
               buffer[0x32],                                   //Phase sequence (PS)
               toFloatString(0x33, buffer, 10, 1),             //Hz
               toDoubleString(0x36, buffer, 10, 1),            //Kvarh (+) TOT
               toDoubleString(0x40, buffer, 10, 1),            //kWh L1
               toDoubleString(0x42, buffer, 10, 1),            //kWh L2
               toDoubleString(0x44, buffer, 10, 1),            //kWh L3
               toThreeDecimalDoubleString(0x00, bufferkWhTot)  //kWh (+) TOT (3 decimals)
      );
      server.send(200, "Application/json", temp);
    } else {  //Error
      String error = "unknown";
      if (result1 == Modbus::ResultCode::EX_TIMEOUT || result2 == Modbus::ResultCode::EX_TIMEOUT || resultkWhTot == Modbus::ResultCode::EX_TIMEOUT) {
        error = "timeout";
      } else if (result1 == Modbus::ResultCode::EX_DATA_MISMACH || result2 == Modbus::ResultCode::EX_DATA_MISMACH || resultkWhTot == Modbus::ResultCode::EX_DATA_MISMACH) {
        error = "dataMismatch";
      }
      snprintf(temp, 504,
               "{\n\
  \"version\": \"%s\",\n\
  \"uptimeSeconds\": %d,\n\
  \"resultCodes\": [\"0x%02X\", \"0x%02X\", \"0x%02X\"],\n\
  \"error\": \"%s\"\n\
}",
               POWAH_VERSION, uptime, result1, result2, resultkWhTot, error);
      server.send(200, "Application/json", temp);
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
