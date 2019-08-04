#include <WiFiClientSecure.h>
#include <Wire.h>  
#include "SSD1306Wire.h"

// for 128x64 displays:
SSD1306Wire display(0x3c, SDA, SCL);  // ADDRESS, SDA, SCL



const char* wifiSsid     = "PLUSNET-96M5";
const char* wifiPassword = "658943e69d";
const char* server = "lite.realtime.nationalrail.co.uk";
const int maxRows = 10;
 String destinations[maxRows];
 String runTimes[maxRows];
 String dueTimes[maxRows];
 String platforms[maxRows];
 String carriages[maxRows];

const String soapBody = "<soap:Envelope"
  " xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\""
  " xmlns:typ=\"http://thalesgroup.com/RTTI/2013-11-28/Token/types\""
  " xmlns:ldb=\"http://thalesgroup.com/RTTI/2016-02-16/ldb/\">"
   "<soap:Header><typ:AccessToken><typ:TokenValue>406464b2-43b3-4835-84da-bed20b4cdd75</typ:TokenValue></typ:AccessToken></soap:Header>"
   "<soap:Body>"
    "<ldb:GetDepartureBoardRequest>"
     "<ldb:numRows>10</ldb:numRows>"
     "<ldb:crs>LDS</ldb:crs>"
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
  Serial.begin(115200);         // Start the Serial communication to send messages to the computer
  delay(10);
  Serial.println('\n');


  display.init();
  display.flipScreenVertically();
  display.clear();
//    display.setTextAlignment(TEXT_ALIGN_LEFT);
//    display.setFont(ArialMT_Plain_10);
//    display.drawString(0, 0, "Hello world");
    display.setPixel(2, 2);
  display.display();
  
  WiFi.begin(wifiSsid, wifiPassword);             // Connect to the network
  Serial.print("Connecting to ");
  Serial.print(wifiSsid);
 
  while (WiFi.status() != WL_CONNECTED) { // Wait for the Wi-Fi to connect
    delay(500);
    Serial.print('.');
  }
 
  Serial.println('\n');
  Serial.println("Connection established!");  
  Serial.print("IP address:\t");
  Serial.println(WiFi.localIP());

Serial.println ("Scanning I2C device...");
 
  Wire.begin();
  for (byte i = 8; i < 120; i++)
  {
    Wire.beginTransmission (i);
    if (Wire.endTransmission () == 0)
    {
      Serial.print ("Address found->");
      Serial.print (" (0x");
      Serial.print (i, HEX);
      Serial.println (")");

    }
  }
}

void loop() {
    Serial.println("Connect to server via port 443");
    if (!client.connect(server, 443)){
        Serial.println("Connection failed!");
    } else {
        Serial.println("Connected to server!");
        client.print(httpRequest);
        client.println(soapBody.length());
        client.println();
        client.println(soapBody);
       
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
            //Serial.write(c);

            if (c == '<') {
              readingElementName = true;

              if (buf.length() > 1) {
                buf = buf.substring(0, buf.length() - 1);
//                Serial.print("non-element: ");
//                Serial.print(buf);
//                Serial.print(" (");
//                Serial.print(buf.length());
//                Serial.println(")");

//                Serial.print(path);
//                Serial.print(" = ");
//                Serial.print(buf);
//                Serial.print(" (");
//                Serial.print(serviceIndex);
//                Serial.println(")");

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

//              Serial.print("element: ");
//              Serial.print(buf);
//              Serial.print(" (");
//              Serial.print(buf.length());
//              Serial.print(")");

              if (buf[0] == '/') {
//                Serial.println(" -");
                path = path.substring(0, path.length() - buf.length());
              } else {
//                Serial.println(" +");
                path = path + '/' + buf;
              }
              //Serial.println(path);

              if(buf == "lt5:service") {
                serviceIndex++;
              }

              readingElementName = false;
              buf = "";
            }
        }

//        Serial.print("\n\nbuf: ");
//        Serial.println(buf);

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

    Serial.println("\n\nWaiting 60 seconds...");
    delay(60000);
}

String pad(String str, int padLen) {
  String ret = "";
  for (int i = 0; i < padLen - str.length(); i++) {
    ret = ret + ' ';
  }

  return ret;
}


