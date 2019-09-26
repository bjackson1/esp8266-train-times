#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <U8g2lib.h>
#include <U8x8lib.h>
#include <Timezone.h>

#include "SPI.h"
#include "TFT_eSPI.h"



char serialRead;
String serialBuf;
int wifiAttempts = 0;
bool wifiConnected = false;
int nextConnect = 10000;
String apiKey;
String stationCode;
String screenType;
String wifiSsid = "acquiring...";
String ipAddress = "acquiring...";
String currentTime = "00:00";
int displayWidth = 100;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
int timeMillisBaseline = 0;
int nextTimeSync = 0;
int nextTimeDisplayUpdate = 0;
TimeChangeRule BST = {"BST", Last, Sun, Mar, 2, 60};     // British Summer Time
TimeChangeRule GMT = {"GMT", Last, Sun, Oct, 3, 0};      // GMT
Timezone ukTime(BST, GMT);
time_t localTime;

const char *server = "lite.realtime.nationalrail.co.uk";
const int maxRows = 30;

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
U8G2_SSD1322_NHD_256X64_F_4W_SW_SPI u8g2(U8G2_R2, D5, D7, D8, D6);
// Use hardware SPI
TFT_eSPI tft = TFT_eSPI();

void setup()
{
    Serial.begin(115200);
    EEPROM.begin(1024);
    client.setInsecure();

    timeClient.begin();


    Serial.println("\n\nok");

    screenType = ReadEepromWord(136, 8);

    if (screenType == "TFT") {
        tft.begin();
        tft.setRotation(0);
        displayWidth = 240;
    } else if (screenType == "OLED") {
        u8g2.begin();
        displayWidth = u8g2.getDisplayWidth();
    }

    Serial.print("Display width: ");
    Serial.println(displayWidth);

    apiKey = ReadEepromWord(64, 64);
    stationCode = ReadEepromWord(128, 8);

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);

    // analogWrite(A0, 255);

    Serial.println("API Key: " + apiKey);
    Serial.println("Station Code: " + stationCode);

    DisplayIntro();
    wifiConnected = ConnectWiFi();
    DisplayString(0, 64, "Getting train times...");
    // u8g2.sendBuffer();
    DisplaySend();
}

void loop()
{
    if (Serial.available() > 0)
    {
        serialRead = Serial.read();
        Serial.print(serialRead);
        ParseWiFiCommands(serialRead);
    }

    if (wifiConnected) {
        if (millis() > nextTimeSync) {
            Serial.println("Getting time from NTP...");
            while(timeClient.getEpochTime() < 1569503684) {
                timeClient.update();
                Serial.print("NTP Time: ");
                Serial.println(timeClient.getEpochTime());
                delay(1000);
            }

            timeMillisBaseline = timeClient.getEpochTime() - (millis() / 1000);
            nextTimeSync = millis() + 3 * 60 * 60 * 1000;
        }

        if (millis() > nextConnect)
        {
            GetTrainTimes();
            Serial.println("\n\nWaiting 90 seconds until next download...");
            nextConnect = millis() + 90 * 1000;
        }

        if (millis() > nextTimeDisplayUpdate) {
            localTime = ukTime.toLocal(timeMillisBaseline + (millis() / 1000));
            DisplayTime();
            nextTimeDisplayUpdate = (millis() - (millis() % 1000)) + 1000;
        }
    }
}

void GetTrainTimes()
{
    Serial.println("Connecting to " + String(server) + "...");
    if (!client.connect(server, 443))
    {
        Serial.println("Connection failed!");
    }
    else
    {
        Serial.println("Connected to server!");
        String body = soapBody1 + apiKey + soapBody2 + String(maxRows) + soapBody3 + stationCode + soapBody4;

        client.print(httpRequest);
        client.println(body.length());
        client.println();
        client.println(body);

        Serial.println("Waiting for response...\n");
        while (!client.available())
        {
            delay(50);
        }
        String buf = "";
        String path = "";
        bool readingElementName = false;
        int serviceIndex = -1;

        /* if data is available then receive and print to Terminal */
        while (client.available())
        {
            char c = client.read();
            buf = buf + c;
            if (c == '<')
            {
                readingElementName = true;
                if (buf.length() > 1)
                {
                    buf = buf.substring(0, buf.length() - 1);
                    if (path == "/?xml/soap:Envelope/soap:Body/GetDepartureBoardResponse/GetStationBoardResult/lt5:trainServices/lt5:service/lt5:destination/lt4:location/lt4:locationName")
                    {
                        destinations[serviceIndex] = buf;
                    }
                    else if (path == "/?xml/soap:Envelope/soap:Body/GetDepartureBoardResponse/GetStationBoardResult/lt5:trainServices/lt5:service/lt4:std")
                    {
                        runTimes[serviceIndex] = buf;
                    }
                    else if (path == "/?xml/soap:Envelope/soap:Body/GetDepartureBoardResponse/GetStationBoardResult/lt5:trainServices/lt5:service/lt4:etd")
                    {
                        dueTimes[serviceIndex] = buf;
                    }
                    else if (path == "/?xml/soap:Envelope/soap:Body/GetDepartureBoardResponse/GetStationBoardResult/lt5:trainServices/lt5:service/lt4:platform")
                    {
                        platforms[serviceIndex] = buf;
                    }
                    else if (path == "/?xml/soap:Envelope/soap:Body/GetDepartureBoardResponse/GetStationBoardResult/lt5:trainServices/lt5:service/lt4:length")
                    {
                        carriages[serviceIndex] = buf;
                    }
                    else if (path == "/?xml/soap:Envelope/soap:Body/GetDepartureBoardResponse/GetStationBoardResult/lt4:generatedAt")
                    {
                        currentTime = buf;
                    }
                }

                buf = "";
            }
            else if (readingElementName && buf.length() > 1 && (c == ' ' || c == '>' || c == '/'))
            {
                buf = buf.substring(0, buf.length() - 1);
                if (buf[0] == '/')
                {
                    path = path.substring(0, path.length() - buf.length());
                }
                else
                {
                    path = path + '/' + buf;
                }
                if (buf == "lt5:service")
                {
                    serviceIndex++;
                }
                readingElementName = false;
                buf = "";
            }
        }
        Serial.println("Time   Destination               Due       Platform  Cars");
        Serial.println("-----  ------------------------  -------  --------  ----");
        for (int i = 0; i < maxRows; i++)
        {
            Serial.print(runTimes[i]);
            Serial.print("  ");
            Serial.print(destinations[i] + pad(destinations[i], 24));
            Serial.print("  ");
            Serial.print(pad(dueTimes[i], 10) + dueTimes[i]);
            Serial.print("  ");
            Serial.print(pad(platforms[i], 8) + platforms[i]);
            Serial.print("  ");
            Serial.print(pad(carriages[i], 4) + carriages[i]);
            Serial.print("\n");
        }

        DisplayTimetable();

        /* if the server disconnected, stop the client */
        if (!client.connected())
        {
            Serial.println();
            Serial.println("Server disconnected");
            client.stop();
        }
    }
}

void WriteEepromWord(String wrd, int offset)
{
    byte checksum = GetStringChecksum(wrd);
    EEPROM.write(offset, checksum);

    for (int i; i < wrd.length(); i++)
    {
        EEPROM.write(offset + i + 1, char(wrd[i]));
    }
    EEPROM.write(offset + wrd.length() + 1, 0);
    EEPROM.commit();
}

String ReadEepromWord(int offset, int len)
{
    String ret = "";
    byte checksum = EEPROM.read(offset);
    for (int i = offset + 1; i <= offset + len; i++)
    {
        char c = EEPROM.read(i);
        if (c == 0)
        {
            if (ValidateChecksum(ret, checksum))
            {
                return ret;
            }
            return "";
        }
        ret = ret + c;
    }
    return "";
}

bool ValidateChecksum(String wrd, byte expectedChecksum)
{
    byte checksum = GetStringChecksum(wrd);
    return checksum == expectedChecksum;
}

byte GetStringChecksum(String toChecksum)
{
    byte len = toChecksum.length();
    byte plain[toChecksum.length()];
    toChecksum.getBytes(plain, toChecksum.length());
    const byte *data = plain;

    byte crc = 0x00;
    while (len--)
    {
        byte extract = *data++;
        for (byte tempI = 8; tempI; tempI--)
        {
            byte sum = (crc ^ extract) & 0x01;
            crc >>= 1;
            if (sum)
            {
                crc ^= 0x8C;
            }
            extract >>= 1;
        }
    }
    return crc;
}

bool ConnectWiFi()
{
    Serial.println("Reading WiFi Credentials from EEPROM... ");
    wifiSsid = ReadEepromWord(0, 32);
    DisplayIntro();

    Serial.println("  WiFi SSID: " + wifiSsid);
    String wifiPwd = ReadEepromWord(32, 32);
    Serial.println("  WiFi Password: " + wifiPwd.substring(0, 5) + "****");
    if (wifiSsid != "" && wifiPwd != "")
    {
        Serial.print("Connecting to " + wifiSsid + "...");
        WiFi.begin(wifiSsid, wifiPwd);
        while (WiFi.status() != WL_CONNECTED && ++wifiAttempts < 120)
        {
            delay(1000);
            Serial.print(".");
        }

        Serial.println();
        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.print("Connected\nIP address: ");
            Serial.println(WiFi.localIP());
            ipAddress = WiFi.localIP().toString();
            DisplayIntro();
            return true;
        }
        else
        {
            Serial.println("WARN: Failed to connect to WiFi");
            return false;
        }
    }
    else
    {
        Serial.println("WARN: WiFi credentials not configured");
        return false;
    }
}

void ParseWiFiCommands(char serialRead)
{
    if (serialRead == '\n')
    {
        Serial.print("\nline");
        ParseAndSaveParameter("WIFI_SSID", 0, serialBuf);
        ParseAndSaveParameter("WIFI_PWD", 32, serialBuf);
        ParseAndSaveParameter("API_KEY", 64, serialBuf);
        ParseAndSaveParameter("STATION_CODE", 128, serialBuf);
        ParseAndSaveParameter("SCREEN_TYPE", 136, serialBuf);

        serialBuf = "";
    }
    else
    {
        Serial.print("+");
        serialBuf = serialBuf + serialRead;
    }
}

void ParseAndSaveParameter(String parameterName, int eepromOffset, String rawString)
{
    if (rawString.length() > parameterName.length())
    {
        if (rawString.substring(0, parameterName.length() + 1) == parameterName + ":")
        {
            String parameterValue = rawString.substring(parameterName.length() + 1, rawString.length());
            WriteEepromWord(parameterValue, eepromOffset);
            Serial.print("Saved " + parameterName + " = '" + parameterValue + "'");
        }
    }
}

String pad(String str, int padLen)
{
    return pad(str, padLen, " ");
}

String pad(String str, int padLen, String padChar)
{
    String ret = "";
    for (int i = 0; i < padLen - str.length(); i++)
    {
        ret = ret + padChar;
    }
    return ret;
}

String padL(String str, int padLen, String padChar) {
    return pad(str, padLen, padChar) + str;
}

String padR(String str, int padLen, String padChar) {
    return str + pad(str, padLen, padChar);
}

void DisplayIntro()
{
    DisplayClear();

    SetFont(0);
    DisplayString(0, 7, "Initialising...");

    DisplayString(10, 20, "Station Code:");
    DisplayString(80, 20, stationCode);

    DisplayString(10, 30, "WiFi Network:");
    DisplayString(80, 30, wifiSsid);

    DisplayString(10, 40, "IP Address:");
    DisplayString(80, 40, ipAddress);
}

void DisplayClear() {
    if (screenType == "TFT") {
        tft.fillScreen(TFT_BLACK);
    } else if (screenType == "OLED") {
        u8g2.clearBuffer();
    }
}

void SetFont(int sizeIndex) {
    if (screenType == "TFT") {
        switch (sizeIndex)
        {
            case 0:
                tft.setTextFont(0);
                break;
        }
    } else if (screenType == "OLED") {
        switch (sizeIndex)
        {
            case 0:
                u8g2.setFont(u8g2_font_finderskeepers_tr);
                break;
        }
    }
}

void DisplaySend() {
    if (screenType == "OLED") {
        u8g2.sendBuffer();
    }
}

void DisplayString(int x, int y, String str)
{
    char c[str.length() + 1];
    str.toCharArray(c, str.length() + 1);

    if (screenType == "TFT") {
        tft.setCursor(x, y);
        tft.print(str);
    } else if (screenType == "OLED") {
        u8g2.drawStr(x, y, c);
    }
}

int MeasureString(String str)
{
    if (screenType == "TFT") {
        return str.length() * 6;
    } else if (screenType == "OLED") {
        char c[str.length() + 1];
        str.toCharArray(c, str.length() + 1);
        return u8g2.getStrWidth(c);
    }
}

char *StringToChar(String str)
{
    char c[str.length() + 1];
    str.toCharArray(c, str.length() + 1);

    return c;
}

void DisplayTime()
{
    String bareHour = String(hour(localTime));
    String bareMinute = String(minute(localTime));
    String bareSecond = String(second(localTime));

    String hour = padL(bareHour, 2, "0");
    String minute = padL(bareMinute, 2, "0");
    String second = padL(bareSecond, 2, "0");

    String hourMinute = hour + ":" + minute;

    int timeWidth = 60;
    int timeLeft = 98;

    // u8g2.setDrawColor(0);
    // u8g2.drawBox(timeLeft, 52, timeWidth, 12);
    // u8g2.setDrawColor(1);

    // u8g2.setFont(u8g2_font_8x13B_mn);
    DisplayString(timeLeft, 64, hourMinute);
    // u8g2.setFont(u8g2_font_7x13B_mr);
    DisplayString(timeLeft + 45, 64, second);

    // u8g2.drawBox(timeLeft + 41, 62, 2, 2);

    // u8g2.updateDisplayArea(11, 0, 10, 2);
}

void DisplayRow(int row, String runTime, String destination, String due)
{
    int x = ((row + 1) * 10) - 1;
    int dueWidth = MeasureString(due);

    DisplayString(0, x, runTime);
    DisplayString(35, x, destination);
    DisplayString(displayWidth - dueWidth, x, due);
}

void DisplayTimetable()
{
    DisplayClear();
    DisplayTime();

    SetFont(0);

    for (int i = 0; i < maxRows; i++)
    {
        DisplayRow(i, runTimes[i], destinations[i], dueTimes[i]);
    }

    DisplaySend();
    // u8g2.sendBuffer();
}
