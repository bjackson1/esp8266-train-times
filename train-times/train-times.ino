#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
char serialRead;
String serialBuf;
int wifiAttempts = 0;
bool wifiConnected = false;
int nextConnect = 10000;
const char* server = "lite.realtime.nationalrail.co.uk";
const int maxRows = 10;
String destinations[maxRows];
String runTimes[maxRows];
String dueTimes[maxRows];
String platforms[maxRows];
String carriages[maxRows];
const String soapBody1 = "<soap:Envelope"
  " xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\""
  " xmlns:typ=\"http://thalesgroup.com/RTTI/2013-11-28/Token/types\""
  " xmlns:ldb=\"http://thalesgroup.com/RTTI/2016-02-16/ldb/\">"
   "<soap:Header><typ:AccessToken><typ:TokenValue>";
const String soapBody2 = "</typ:TokenValue></typ:AccessToken></soap:Header>"
   "<soap:Body>"
    "<ldb:GetDepartureBoardRequest>"
     "<ldb:numRows>";
const String soapBody3 = "</ldb:numRows>"
     "<ldb:crs>";
const String soapBody4 = "</ldb:crs>"
     "</ldb:GetDepartureBoardRequest>"
   "</soap:Body>"
  "</soap:Envelope>";
  
const String httpRequest = "POST /OpenLDBWS/ldb9.asmx HTTP/1.1\r\n"
  "Host: lite.realtime.nationalrail.co.uk\r\n"
  "SOAPAction: http://thalesgroup.com/RTTI/2012-01-13/ldb/GetDepartureBoard\r\n"
  "Content-Type: text/xml; charset=iso-8859-1\r\n"
  "cache-control: no-cache\r\n"
  "Connection: close\r\n"
  "Content-Length: ";
WiFiClientSecure client;
void setup() {
  Serial.begin(115200);
  EEPROM.begin(1024);
  client.setInsecure();
  Serial.println("\n\nok");
  
  ConnectWiFi();
}
void loop() {
  if (Serial.available() > 0) {
    serialRead = Serial.read();
    Serial.print(serialRead);
    ParseWiFiCommands(serialRead);
  }
  if (millis() > nextConnect) {
    GetTrainTimes();
    Serial.println("\n\nWaiting 90 seconds until next download...");
    nextConnect = millis() + 90000;
  }
}
void GetTrainTimes() {
  Serial.println("Connecting to " + String(server) + "...");
  if (!client.connect(server, 443)) {
        Serial.println("Connection failed!");
    } else {
        Serial.println("Connected to server!");
        String body = soapBody1 + "406464b2-43b3-4835-84da-bed20b4cdd75" + soapBody2 + "10" + soapBody3 + "LDS" + soapBody4;
        
        client.print(httpRequest);
        client.println(body.length());
        client.println();
        client.println(body);
       
        Serial.println("Waiting for response...\n");
        while (!client.available()){
            delay(50); //
            //Serial.print(".");
        }
        String buf = "";
        String path = "";
        bool readingElementName = false;
        int serviceIndex = -1;
        
        /* if data is available then receive and print to Terminal */
        while (client.available()) {
            char c = client.read();
            buf = buf + c;
            if (c == '<') {
              readingElementName = true;
              if (buf.length() > 1) {
                buf = buf.substring(0, buf.length() - 1);
                if (path == "/?xml/soap:Envelope/soap:Body/GetDepartureBoardResponse/GetStationBoardResult/lt5:trainServices/lt5:service/lt5:destination/lt4:location/lt4:locationName") {
                  destinations[serviceIndex] = buf;
                } else if (path == "/?xml/soap:Envelope/soap:Body/GetDepartureBoardResponse/GetStationBoardResult/lt5:trainServices/lt5:service/lt4:std") {
                  runTimes[serviceIndex] = buf;
                } else if (path == "/?xml/soap:Envelope/soap:Body/GetDepartureBoardResponse/GetStationBoardResult/lt5:trainServices/lt5:service/lt4:etd") {
                  dueTimes[serviceIndex] = buf;
                } else if (path == "/?xml/soap:Envelope/soap:Body/GetDepartureBoardResponse/GetStationBoardResult/lt5:trainServices/lt5:service/lt4:platform") {
                  platforms[serviceIndex] = buf;
                } else if (path == "/?xml/soap:Envelope/soap:Body/GetDepartureBoardResponse/GetStationBoardResult/lt5:trainServices/lt5:service/lt4:length") {
                  carriages[serviceIndex] = buf;
                }
              }
              
              buf = "";
            } else if (readingElementName && buf.length() > 1 && (c == ' ' || c == '>' || c == '/')) {
              buf = buf.substring(0, buf.length() - 1);
              if (buf[0] == '/') {
                path = path.substring(0, path.length() - buf.length());
              } else {
                path = path + '/' + buf;
              }
              if(buf == "lt5:service") {
                serviceIndex++;
              }
              readingElementName = false;
              buf = "";
            }
        }
        Serial.println("Time   Destination               Due       Platform  Cars");
        Serial.println("-----  ------------------------  -------  --------  ----");
        for (int i = 0; i < maxRows; i++) {
          Serial.print(runTimes[i]);
          Serial.print("  ");
          Serial.print(destinations[i] + pad(destinations[i], 24));
          Serial.print("  ");
          Serial.print(pad(dueTimes[i], 7) + dueTimes[i]);
          Serial.print("  ");
          Serial.print(pad(platforms[i], 8) + platforms[i]);
          Serial.print("  ");
          Serial.print(pad(carriages[i], 4) + carriages[i]);
          Serial.print("\n");
        }
        /* if the server disconnected, stop the client */
        if (!client.connected()) {
            Serial.println();
            Serial.println("Server disconnected");
            client.stop();
        }
    }
}
void WriteEepromWord(String wrd, int offset) {
  byte checksum = GetStringChecksum(wrd);
  EEPROM.write(offset, checksum);
  
  for (int i; i < wrd.length(); i++) {
    EEPROM.write(offset + i + 1, char(wrd[i]));
  }
  EEPROM.write(offset + wrd.length() + 1, 0);
  EEPROM.commit();
}
String ReadEepromWord(int offset, int len) {
  String ret = "";
  byte checksum = EEPROM.read(offset);
  for (int i = offset + 1; i <= offset + len; i++) {
    char c = EEPROM.read(i);
    if (c == 0) {
      if (ValidateChecksum(ret, checksum)) {
        return ret;
      }
      return "";
    }
    ret = ret + c;
  }
  return "";
}
bool ValidateChecksum(String wrd, byte expectedChecksum) {
  byte checksum = GetStringChecksum(wrd);
  return checksum == expectedChecksum;
}
byte GetStringChecksum(String toChecksum) {
  byte len = toChecksum.length();
  byte plain[toChecksum.length()];
  toChecksum.getBytes(plain, toChecksum.length());
  const byte *data = plain;
  
  byte crc = 0x00;
  while (len--) {
    byte extract = *data++;
    for (byte tempI = 8; tempI; tempI--) {
      byte sum = (crc ^ extract) & 0x01;
      crc >>= 1;
      if (sum) {
        crc ^= 0x8C;
      }
      extract >>= 1;
    }
  }
  return crc;
}
void ConnectWiFi() {
  Serial.println("Reading WiFi Credentials from EEPROM... ");
  String wifiSsid = ReadEepromWord(0, 32);
  Serial.println("  WiFi SSID: " + wifiSsid);
  String wifiPwd = ReadEepromWord(32, 32);
  Serial.println("  WiFi Password: " + wifiPwd.substring(0, 5) + "****");
  if (wifiSsid != "" && wifiPwd != "") {
    Serial.print("Connecting to " + wifiSsid + "...");
    WiFi.begin(wifiSsid, wifiPwd);
    while (WiFi.status() != WL_CONNECTED && ++wifiAttempts < 120)
    {
      delay(1000);
      Serial.print(".");
    }
    
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("Connected\nIP address: ");
      Serial.println(WiFi.localIP());
      wifiConnected = true;
    } else {
      Serial.println("WARN: Failed to connect to WiFi");
    }
  } else {
    Serial.println("WARN: WiFi credentials not configured");
  }
}
void ParseWiFiCommands(char serialRead) {
  if (serialRead == '\n') {
    ParseAndSaveParameter("WIFI_SSID", 0, serialBuf);
    ParseAndSaveParameter("WIFI_PWD", 32, serialBuf);
    ParseAndSaveParameter("API_KEY", 64, serialBuf);
    
    serialBuf = "";
  } else {
    serialBuf = serialBuf + serialRead;
  }
}
void ParseAndSaveParameter(String parameterName, int eepromOffset, String rawString) {
  if (rawString.length() > parameterName.length()) {
    if (rawString.substring(0, parameterName.length() + 1) == parameterName + ":") {
      String parameterValue = rawString.substring(parameterName.length() + 1, rawString.length());
      WriteEepromWord(parameterValue, eepromOffset);
      Serial.print("Saved " + parameterName + " = '" + parameterValue + "'");
    }
  }
}
String pad(String str, int padLen) {
  String ret = "";
  for (int i = 0; i < padLen - str.length(); i++) {
    ret = ret + ' ';
  }
  return ret;
}


//#include <WiFiClientSecure.h>
//#include <Wire.h>  
//#include "SSD1306Wire.h"
//
//// for 128x64 displays:
//SSD1306Wire display(0x3c, SDA, SCL);  // ADDRESS, SDA, SCL
//
//
//
//const char* wifiSsid     = "PLUSNET-96M5";
//const char* wifiPassword = "658943e69d";
//const char* server = "lite.realtime.nationalrail.co.uk";
//const int maxRows = 6;
// String destinations[maxRows];
// String runTimes[maxRows];
// String dueTimes[maxRows];
// String platforms[maxRows];
// String carriages[maxRows];
//
//const String soapBody = "<soap:Envelope"
//  " xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\""
//  " xmlns:typ=\"http://thalesgroup.com/RTTI/2013-11-28/Token/types\""
//  " xmlns:ldb=\"http://thalesgroup.com/RTTI/2016-02-16/ldb/\">"
//   "<soap:Header><typ:AccessToken><typ:TokenValue>406464b2-43b3-4835-84da-bed20b4cdd75</typ:TokenValue></typ:AccessToken></soap:Header>"
//   "<soap:Body>"
//    "<ldb:GetDepartureBoardRequest>"
//     "<ldb:numRows>6</ldb:numRows>"
//     "<ldb:crs>SAE</ldb:crs>"
//     "</ldb:GetDepartureBoardRequest>"
//   "</soap:Body>"
//  "</soap:Envelope>";
//  
//const String httpRequest = "POST /OpenLDBWS/ldb9.asmx HTTP/1.1\r\n"
//  "Host: lite.realtime.nationalrail.co.uk\r\n"
//  "SOAPAction: http://thalesgroup.com/RTTI/2012-01-13/ldb/GetDepartureBoard\r\n"
//  "Content-Type: text/xml; charset=iso-8859-1\r\n"
//  "cache-control: no-cache\r\n"
//  "Connection: close\r\n"
//  "Content-Length: ";
//
//WiFiClientSecure client;
//
//void setup() {
//  Serial.begin(115200);         // Start the Serial communication to send messages to the computer
//  delay(10);
//  Serial.println('\n');
//
//
//  display.init();
//  display.flipScreenVertically();
//  display.clear();
////    display.setTextAlignment(TEXT_ALIGN_LEFT);
////    display.setFont(ArialMT_Plain_10);
////  display.drawString(0, 0, "Hello Pippa!");
//  //display.setPixel(2, 2);
//  display.display();
//  
//  WiFi.begin(wifiSsid, wifiPassword);             // Connect to the network
//  Serial.print("Connecting to ");
//  Serial.print(wifiSsid);
// 
//  while (WiFi.status() != WL_CONNECTED) { // Wait for the Wi-Fi to connect
//    delay(500);
//    Serial.print('.');
//  }
// 
//  Serial.println('\n');
//  Serial.println("Connection established!");  
//  Serial.print("IP address:\t");
//  Serial.println(WiFi.localIP());
//
//Serial.println ("Scanning I2C device...");
// 
//  Wire.begin();
//  for (byte i = 8; i < 120; i++)
//  {
//    Wire.beginTransmission (i);
//    if (Wire.endTransmission () == 0)
//    {
//      Serial.print ("Address found->");
//      Serial.print (" (0x");
//      Serial.print (i, HEX);
//      Serial.println (")");
//
//    }
//  }
//}
//
//void loop() {
//    Serial.println("Connect to server via port 443");
//    if (!client.connect(server, 443)){
//        Serial.println("Connection failed!");
//    } else {
//        Serial.println("Connected to server!");
//        client.print(httpRequest);
//        client.println(soapBody.length());
//        client.println();
//        client.println(soapBody);
//       
//        Serial.println("Waiting for response...\n");
//        while (!client.available()){
//            delay(50); //
//            //Serial.print(".");
//        }
//
//        String buf = "";
//        String path = "";
//        bool readingElementName = false;
//        int serviceIndex = -1;
//        
//        /* if data is available then receive and print to Terminal */
//        while (client.available()) {
//            char c = client.read();
//            buf = buf + c;
//            //Serial.write(c);
//
//            if (c == '<') {
//              readingElementName = true;
//
//              if (buf.length() > 1) {
//                buf = buf.substring(0, buf.length() - 1);
////                Serial.print("non-element: ");
////                Serial.print(buf);
////                Serial.print(" (");
////                Serial.print(buf.length());
////                Serial.println(")");
//
////                Serial.print(path);
////                Serial.print(" = ");
////                Serial.print(buf);
////                Serial.print(" (");
////                Serial.print(serviceIndex);
////                Serial.println(")");
//
//                if (path == "/?xml/soap:Envelope/soap:Body/GetDepartureBoardResponse/GetStationBoardResult/lt5:trainServices/lt5:service/lt5:destination/lt4:location/lt4:locationName") {
//                  destinations[serviceIndex] = buf;
//                } else if (path == "/?xml/soap:Envelope/soap:Body/GetDepartureBoardResponse/GetStationBoardResult/lt5:trainServices/lt5:service/lt4:std") {
//                  runTimes[serviceIndex] = buf;
//                } else if (path == "/?xml/soap:Envelope/soap:Body/GetDepartureBoardResponse/GetStationBoardResult/lt5:trainServices/lt5:service/lt4:etd") {
//                  dueTimes[serviceIndex] = buf;
//                } else if (path == "/?xml/soap:Envelope/soap:Body/GetDepartureBoardResponse/GetStationBoardResult/lt5:trainServices/lt5:service/lt4:platform") {
//                  platforms[serviceIndex] = buf;
//                } else if (path == "/?xml/soap:Envelope/soap:Body/GetDepartureBoardResponse/GetStationBoardResult/lt5:trainServices/lt5:service/lt4:length") {
//                  carriages[serviceIndex] = buf;
//                }
//              }
//              
//              buf = "";
//            } else if (readingElementName && buf.length() > 1 && (c == ' ' || c == '>' || c == '/')) {
//              buf = buf.substring(0, buf.length() - 1);
//
////              Serial.print("element: ");
////              Serial.print(buf);
////              Serial.print(" (");
////              Serial.print(buf.length());
////              Serial.print(")");
//
//              if (buf[0] == '/') {
////                Serial.println(" -");
//                path = path.substring(0, path.length() - buf.length());
//              } else {
////                Serial.println(" +");
//                path = path + '/' + buf;
//              }
//              //Serial.println(path);
//
//              if(buf == "lt5:service") {
//                serviceIndex++;
//              }
//
//              readingElementName = false;
//              buf = "";
//            }
//        }
//
////        Serial.print("\n\nbuf: ");
////        Serial.println(buf);
//
//          display.clear();
//
//        Serial.println("Time   Destination               Due       Platform  Cars");
//        Serial.println("-----  ------------------------  -------  --------  ----");
//        for (int i = 0; i < maxRows; i++) {
//          Serial.print(runTimes[i]);
//          Serial.print("  ");
//          Serial.print(destinations[i] + pad(destinations[i], 24));
//          Serial.print("  ");
//          Serial.print(pad(dueTimes[i], 7) + dueTimes[i]);
//          Serial.print("  ");
//          Serial.print(pad(platforms[i], 8) + platforms[i]);
//          Serial.print("  ");
//          Serial.print(pad(carriages[i], 4) + carriages[i]);
//          Serial.print("\n");
//          display.drawString(0, i * 10, runTimes[i]);
//          display.drawString(30, i * 10, destinations[i]);
//          display.setColor(BLACK);
//          display.fillRect(90,  i * 10, 42, 10);
//          display.setColor(WHITE);
//          display.drawString(93, i * 10, dueTimes[i]);
////          display.draw
////          display.drawString(0, i * 10, runTimes[i]);
//        }
//
//          display.display();
//
//        /* if the server disconnected, stop the client */
//        if (!client.connected()) {
//            Serial.println();
//            Serial.println("Server disconnected");
//            client.stop();
//        }
//    }
//
//    Serial.println("\n\nWaiting 60 seconds...");
//    delay(60000);
//}
//
//String pad(String str, int padLen) {
//  String ret = "";
//  for (int i = 0; i < padLen - str.length(); i++) {
//    ret = ret + ' ';
//  }
//
//  return ret;
//}
//

