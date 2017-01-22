#include <Wire.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266Ping.h>
#include <EEPROM.h>
#include "I2Cdev.h"
#include "ADXL345.h"

#define SDA         4
#define SCL         5

ESP8266WebServer server(80);

//ADXL345
ADXL345 accel;
int16_t aX, aY, aZ;
const int offsetX = -152;
const int offsetY = -80;
const int offsetZ = 1238;
unsigned long startTimer, stopTimer;

//ESP8266
const String ssid = "ESP8266-12F";
const String password = "1234567890";
const String Product = "Smart Metering";
const int clientPort = 2561;
String wifiList;
String wifiListOption;
String content;
String clientList;
String clientListOption;
bool wifiConnected = false;
int statusCode;
int clientShift;
String clientIdList[10];
String clientIpList[10];

//EEPROM
//  0 ~  31: Wifi AP SSID
// 32 ~  95: Wifi AP Password
// 96 ~ 100: client memory shift (binary)
//101 ~ 105: first client device id (5 chars)
//106 ~ 120: first client ip (15 chars)
//...
//281 ~ 285: 10th client device id (5 chars)
//286 ~ 300: 10th client ip (15 chars)

void cleanAllClientData() {
  for (int i = 97; i < 301; ++i) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
}

void cleanClientData(int shift) {
  for (int i = 101 + shift * 20; i < 101 + (shift + 1) * 20; i++) {
    EEPROM.write(i, 0);
  }
  
  bitWrite(clientShift, shift, 0);
  char writeTmp[5];
  sprintf(writeTmp, "%05d", clientShift);
  for (int i = 96, j = 0; i < 101; i++, j++) {
    EEPROM.write(i, writeTmp[j]);
  }
  EEPROM.commit();
}

void cleanWifiData() {
  for (int i = 0; i < 96; ++i) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
}

void readClientShift() {
  String readTmp;
  for (int i = 96; i < 101; i++) {
    readTmp += char(EEPROM.read(i));
  }
  clientShift = readTmp.toInt();
}

void readClientList() {
  readClientShift();
  for (int i = 0; i < 10; i++) {
    int stored = bitRead(clientShift, i);
    if (stored == 1) {
      String readTmp = "";
      for (int j = 101 + i * 20; j < 106 + i * 20; j++) {
        clientIdList[i] += char(EEPROM.read(i));
      }
      for (int j = 106 + i * 20; j < 101 + (i + 1) * 20; j++) {
        clientIpList[i] += char(EEPROM.read(i));
      }
    }
  }
}

void writeClientData(int shift, String clientId, String clientIp) {
  int clientIdLen = clientId.length();
  int clientIpLen = clientIp.length();
  for (int i = 101 + shift * 20, j = 0; i < 106 + shift * 20; i++, j++) {
    if (j < clientIdLen) {
      EEPROM.write(i, clientId[j]);
    } else {
      EEPROM.write(i, 0);
    }
  }
  for (int i = 106 + shift * 20, j = 0; i < 101 + (shift + 1) * 20; i++, j++) {
    if (j < clientIpLen) {
      EEPROM.write(i, clientIp[j]);
    } else {
      EEPROM.write(i, 0);
    }
  }

  bitWrite(clientShift, shift, 1);
  char writeTmp[5];
  sprintf(writeTmp, "%05d", clientShift);
  for (int i = 96, j = 0; i < 101; i++, j++) {
    EEPROM.write(i, writeTmp[j]);
  }
  
  EEPROM.commit();
}

int checkClientId(String id) {
  for (int i = 0; i < 10; i++) {
    if (bitRead(clientShift, i) == 1) {
      if (clientIdList[i] == id) {
        return i;
      }
    }
  }
  return -1;
}

int getFreeShift() {
  for (int i = 0; i < 10; i++) {
    if (bitRead(clientShift, i) == 0) {
      return i;
    }
  }
  return -1;
}

void scanAccessWifi() {
  Serial.println("scan start");
  int n = WiFi.scanNetworks();
  Serial.println("scan done");
  if (n == 0)
    Serial.println("no networks found");
  else {
    Serial.print(n);
    Serial.println(" networks found");
    for (int i = 0; i < n; ++i)
    {
      // Print SSID and RSSI for each network found
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(")");
      Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*");
      delay(10);
    }
  }
  Serial.println("");
  wifiList = "<ol>";
  wifiListOption = "";
  for (int i = 0; i < n; ++i) {
    // Print SSID and RSSI for each network found
    wifiList += "<li>";
    wifiList += WiFi.SSID(i);
    wifiList += " (";
    wifiList += WiFi.RSSI(i);
    wifiList += ")";
    wifiList += (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*";
    wifiList += "</li>";
    wifiListOption += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + "</option>";
  }
  wifiList += "</ol>";
  delay(100);
}

bool testWifi(void) {
  int testCounts = 0;
  Serial.println("Waiting for Wifi to connect");
  while ( testCounts < 20 ) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      return true;
    }
    delay(500);
    Serial.print(WiFi.status());
    testCounts++;
  }
  Serial.println("");
  Serial.println("Connect timed out, opening AP");
  return false;
}

void createWebServer(int webType) {
  if (webType == 1) {
    Serial.println("Create Web Server " + String(webType));
    server.on("/", []() {
      scanAccessWifi();
      IPAddress ip = WiFi.softAPIP();
      String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
      content = "<!DOCTYPE HTML>\r\n<html>Hello, this is  ";
      content += Product + " at ";
      content += ipStr;
      content += ".<br>These are the Wifi signals we found near by you, please select one.<p>";
      content += wifiList;
      content += "</p><form method='get' action='setting'><label style='width:80px;display:inline-block;'>SSID: </label><select name='ssid'>";
      //content += "<input name='ssid' length=32>";
      content += wifiListOption;
      content += "</select><br><label style='width:80px;display:inline-block;'>Password: </label><input name='pass' length=64><br><input type='submit' value='submit'></form>";
      content += "</html>";
      server.send(200, "text/html", content);
    });

    server.on("/setting", []() {
      String qSsid = server.arg("ssid");
      String qPassword = server.arg("pass");
      if (qSsid.length() > 0 && qPassword.length() > 0) {
        Serial.println("cleaning eeprom");
        for (int i = 0; i < 96; ++i) {
          EEPROM.write(i, 0);
        }
        Serial.print("SSID: ");
        Serial.println(qSsid);
        Serial.print("Password: ");
        Serial.println(qPassword);

        for (int i = 0; i < qSsid.length(); ++i) {
          EEPROM.write(i, qSsid[i]);
        }
        for (int i = 0; i < qPassword.length(); ++i) {
          EEPROM.write(32 + i, qPassword[i]);
        }
        EEPROM.commit();
        content = "{\"Success\":\"saved to eeprom... reset to boot into new wifi\"}";
        statusCode = 200;
      } else {
        content = "{\"Error\":\"404 not found\"}";
        statusCode = 404;
        Serial.println("Sending 404");
      }
      server.send(statusCode, "application/json", content);
    });
  } else if (webType == 0) {
    server.on("/", []() {
      IPAddress ip = WiFi.localIP();
      String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
      content = "<!DOCTYPE HTML>\r\n<html>Hello, this is  ";
      content += Product;
      content += "<br>Your wifi IP is ";
      content += ipStr;
      content += ".<br>";
      content += "<div><a href='/cleanWifi'>clean wifi data.</a></div>";
      content += "<div><a href='/cleanClient'>clean client data.</a></div>";
      content += "<div><a href='/checkClient'>Check and add a new client.</a></div>";
      content += "</html>";
      server.send(200, "text/html", content);
    });

    server.on("/cleanWifi", []() {
      content = "<!DOCTYPE HTML>\r\n<html>";
      content += "<p>Clearing the Wifi data!</p></html>";
      server.send(200, "text/html", content);
      Serial.println("disconnect wifi");
      WiFi.disconnect();
      Serial.println("cleaning wifi eeprom");
      cleanWifiData();
    });

    server.on("/cleanClient", []() {
      content = "<!DOCTYPE HTML>\r\n<html>";
      content += "<p>Clearing all Client data!</p></html>";
      server.send(200, "text/html", content);
      Serial.println("cleaning client eeprom");
      cleanAllClientData();
    });

    server.on("/checkClient", []() {
      IPAddress ip = WiFi.localIP();
      String ipStrFirst = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.';
      content = "<!DOCTYPE HTML>\r\n<html>";
      content += "<script language='javascript'>var loadingStr = true; function loading() { if (loadingStr) {document.getElementById('responseContent').innerHTML = document.getElementById('responseContent').innerHTML + '.';} } myTimer = window.setInterval(loading, 1000); document.onreadystatechange = function() { if (document.readyState == 'complete') { var xhttp = new XMLHttpRequest(); xhttp.onreadystatechange = function() { if (this.readyState == 4 && this.status == 200) {loadingStr = false; clearInterval(myTimer); document.getElementById('responseContent').innerHTML = this.responseText; } }; xhttp.open('GET', '/getClientList', true); xhttp.send(); } }</script>";
      content += "Hello, this is  ";
      content += Product;
      content += "<br>Your wifi IP is ";
      content += ipStrFirst + String(ip[3]);
      content += ".<br>You have registered these client devices:<br>";
      for (int i = 0; i < 10; i++) {
        int stored = bitRead(clientShift, i);
        if (stored == 1) {
          content += clientIdList[i] + ": " + clientIpList[i] + "<br>";
        }
      }
      content += ".<br>These are the client signals we found near by you, please select one.<p>";
      content += "<div id='responseContent'>Please wait, loading.</div>";
      content += "</html>";
      server.send(200, "text/html", content);
    });

    server.on("/getClientList", []() {
      String ipStrFirst = server.arg("ipStrFirst");
      content = "";
      clientListOption = "";
      WiFiClient client;
      for (int i = 1; i < 255; i++) {
        String scanningIp = ipStrFirst + String(i);
        //if (!client.connect(scanningIp.c_str(), clientPort)) {
        if (!Ping.ping(scanningIp.c_str(), 1)) {
          Serial.println(scanningIp + " no found!");
        } else {
          if (!client.connect(scanningIp.c_str(), clientPort)) {
            Serial.println(scanningIp + " no port!");
          } else {
            client.print("GET /checkStatus HTTP/1.1\r\n");
            client.print("Host: ");
            client.print(scanningIp);
            client.print("\r\n");
            client.print("Connection: close\r\n\r\n");
  
            bool clientConnected = true;
            unsigned long timeout = millis();
            while (client.available() == 0) {
              if (millis() - timeout > 5000) {
                Serial.println(ipStrFirst + String(i) + " client timeout !");
                client.stop();
                clientConnected = false;
                break;
              }
            }
  
            if (clientConnected) {
              String response = client.readStringUntil('\r');
              client.stop();
              int firstBracket = response.indexOf('{');
              int secondBracket = response.indexOf('}', firstBracket + 1);
              response.remove(secondBracket + 1);
              response.remove(0, firstBracket);
              int colon = response.indexOf(":");
              String statusCode = response.substring(0, colon - 1);
              String clientId = response.substring(colon + 1);
              if (!checkClientId(clientId)) {
                clientListOption += "<option value='" + clientId + ":" + scanningIp + "'>" + clientId + " at " + scanningIp + "</option>";
              }
            }
          }
        }
      }

      if (clientListOption != "") {
        content += "<form method='get' action='addCilent'><select name='clientData'>";
        content += clientListOption;
        content += "</select><input type='submit' value='submit'></form><br>";
      } else {
        content += "No found!";
      }
      
      server.send(200, "text/html", content);
    });

    server.on("/addClient", []() {
      statusCode = 200;
      content = "<!DOCTYPE HTML>\r\n<html>Hello, this is  ";
      content += Product;
      content += "<br><br>";

      String clientData = server.arg("clientData");
      if (clientData.length() > 0) {
        int colon = clientData.indexOf(":");
        String clientId = clientData.substring(0, colon);
        String clientIp = clientData.substring(colon + 1);
        int clientIdLen = clientId.length();
        int clientIpLen = clientIp.length();

        Serial.println(clientId);
        Serial.println(clientIp);
        int stored = checkClientId(clientId);
        if (stored == -1) {
          int freeShift = getFreeShift();
          Serial.println(freeShift);
          if (getFreeShift() == -1) {
            content += "You already have 10 clients, please remove some clients first.";
          } else {
            writeClientData(freeShift, clientId, clientIp);
            content += "Add this client: " + clientId + " @" + clientIp + ".<br>";
            readClientList();
          }
        } else {
          writeClientData(stored, clientId, clientIp);
          content += "Modify this client: " + clientId + " @" + clientIp + ".<br>";
          readClientList();
        }
      } else {
        content += "Error client data";
        Serial.println("-5");
      }
      content += "</html>";
      server.send(statusCode, "text/html", content);
    });

    server.on("/removeCilent", []() {
      statusCode = 200;
      content = "<!DOCTYPE HTML>\r\n<html>Hello, this is  ";
      content += Product;

      String clientData = server.arg("clientData");
      if (clientData.length() > 0) {
        int colon = clientData.indexOf(":");
        String clientId = clientData.substring(0, colon - 1);
        String clientIp = clientData.substring(colon + 1);
        int clientIdLen = clientId.length();
        int clientIpLen = clientIp.length();
        char clientIdChar[clientIdLen];
        char clientIpChar[clientIpLen];

        Serial.println(clientId);
        Serial.println(clientIp);
        int stored = checkClientId(clientId);
        if (stored == -1) {
          content += "No this client: " + clientId + ".<br>";
        } else {
          cleanClientData(stored);
          content += "Remove this client: " + clientId + " @" + clientIp + ".<br>";
          readClientList();
        }
      } else {
        content += "Error client data";
        Serial.println("-6");
      }
      content += "</html>";
      server.send(statusCode, "text/html", content);
    });
  }
}

void launchWeb(int webType) {
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("Local IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("SoftAP IP: ");
  Serial.println(WiFi.softAPIP());
  createWebServer(webType);
  // Start the server
  server.begin();
  Serial.println("Server started");
}

void setupAP() {
  Serial.println("Set Wifi Mode");
  WiFi.mode(WIFI_AP_STA);
  Serial.println("Wifi disconnect");
  WiFi.disconnect();
  delay(100);
  scanAccessWifi();
  WiFi.softAP(ssid.c_str(), password.c_str(), 6);
  Serial.println("softap");
  //launchWeb(1);
  //Serial.println("over");
}

double calRoll(double gX, double gY, double gZ) {
  //return ((atan2(gZ, gX) * 180) / 3.14159265) + 180;
  return ((atan2(-gY, gZ) * 180) / 3.14159265);
}

double calPitch(double gX, double gY, double gZ) {
  //return ((atan2(gZ, gY) * 180) / 3.14159265) + 180;
  return ((atan2(gX, sqrt(gY * gY + gZ * gZ)) * 180) / 3.14159265);
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);

  /*
    Serial.println("ESP8266 data");
    //Serial.println("Chip ID:" + ESP.getChipId());
    Serial.println(ESP.getFlashChipId());
    Serial.println(ESP.getFlashChipSize());
    Serial.println(ESP.getFlashChipRealSize());
    Serial.println(ESP.getFlashChipSpeed());
  */

  Wire.begin(SDA, SCL);
  Serial.println("Initializing I2C devices...");
  accel.initialize();
  Serial.println("Testing device connections...");
  Serial.println(accel.testConnection() ? "ADXL345 connection successful" : "ADXL345 connection failed");
  accel.setFullResolution(1);
  accel.setRange(0x03);
  Serial.println(accel.getRange());
  //readOffset();
  //Serial.println(offsetX);
  //Serial.println(offsetY);
  //Serial.println(offsetZ);

  Serial.println("Reading EEPROM ssid");
  String eeSsid;
  for (int i = 0; i < 32; ++i) {
    eeSsid += char(EEPROM.read(i));
  }
  Serial.print("SSID: ");
  Serial.println(eeSsid);
  Serial.println("Reading EEPROM pass");
  String eePassword = "";
  for (int i = 32; i < 96; ++i) {
    eePassword += char(EEPROM.read(i));
  }
  Serial.print("PASS: ");
  Serial.println(eePassword);

  readClientShift();

  int clientCount = 0;
  for (int i = 0; i < 10; i++) {
    if (bitRead(clientShift, i)) {
      clientCount++;
    }
  }
  Serial.println("Already have " + String(clientCount) + " clients.");

  WiFi.persistent(false);

  setupAP();

  if (eeSsid.length() > 1) {
    Serial.println("Wifi begin");
    WiFi.begin(eeSsid.c_str(), eePassword.c_str());
    Serial.println("Test wifi");
    if (testWifi()) {
      launchWeb(0);
      return;
    } else {
      launchWeb(1);
      return;
    }
  } else {
    launchWeb(1);
    return;
  }

  startTimer = 0;
  stopTimer = 0;
}

void loop() {
  server.handleClient();

  if (startTimer > 0) {
    stopTimer = millis();
    if (stopTimer - startTimer > 5000) {
      double gX, gY, gZ, roll, pitch;
      accel.getAcceleration(&aX, &aY, &aZ);
      gX = (aX - offsetX) / 307.0;
      gY = (aY - offsetY) / 305.0;
      gZ = (aZ - offsetZ) / 279.0;
      roll = calRoll(gX, gY, gZ);
      pitch = calPitch(gX, gY, gZ);
      Serial.println(startTimer);
      Serial.println(stopTimer);
      Serial.print("accel:\t");
      Serial.print(aX); Serial.print("\t");
      Serial.print(aY); Serial.print("\t");
      Serial.println(aZ);
      Serial.print(offsetX); Serial.print("\t");
      Serial.print(offsetY); Serial.print("\t");
      Serial.println(offsetZ);
      Serial.print(gX); Serial.print("\t");
      Serial.print(gY); Serial.print("\t");
      Serial.println(gZ);
      Serial.println(roll);
      Serial.println(pitch);
      startTimer = millis();
    }
  } else {
    startTimer = millis();
  }
}
