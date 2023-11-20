#include "RTClib.h"
#include <NTPClient.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <time.h>
#include <EEPROM.h>

RTC_DS3231 rtc;

// https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
#define MY_NTP_SERVER "ptbtime1.ptb.de"
#define MY_TZ "CET-1CEST,M3.5.0,M10.5.0/3"

// Pin-definitions
#define LED_RED_PIN 19
#define LED_BLUE_PIN 5
#define LED_GREEN_PIN 18    

// Define The Number of Bytes You Need
#define EEPROM_SIZE 1  // This is 1-Byte

// WiFi settings:
const char* wifiSSID = "private_wifi";
const char* wifiPassword = "password123";

// WiFi AP settings
const char* AP_WIFI_SSID     = "Nixie-Uhr";
const char* AP_WIFI_Password = "123456789";
int AP_WIFI_CHANNEL = 1;      // Wi-Fi channel number (1-13)
int AP_HIDE_SSID = 0;         // (0 = broadcast SSID, 1 = hide SSID)
int AP_MAX_CONNECTIONS = 2;   // maximum simultaneous connected clients (1-4)

// Server Setting
WiFiServer server(80); // Set port to 80

/**
 * 0 - Time
 * 1 - Date
 * 2 - Random
 */
int displayMode = 0;
int counter = 0;

void setup() {
  // activate serial connection
  Serial.begin(115200);

  // init LED Pins
  pinMode(LED_RED_PIN, OUTPUT);
  pinMode(LED_BLUE_PIN, OUTPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);

  // Set LED Color
  analogWrite(LED_RED_PIN,   255);
  analogWrite(LED_GREEN_PIN, 30);
  analogWrite(LED_BLUE_PIN,  0);

  // set pin
  setupShiftRegister();

  // setup RTC module via i2c
  Serial.println("Setup RTC module...");
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    Serial.flush();
    abort();
  }

  // reset display
  writeNumbers(0,0,0);
  refreshShiftRegister();

  // setup random 
  randomSeed(analogRead(11));
  writeNumbers(0,0,1);
  refreshShiftRegister();

  // setup NTP
  Serial.println("Setup NTP settings...");
  configTime(0, 0, MY_NTP_SERVER);  // 0, 0 because we will use TZ in the next line
  setenv("TZ", MY_TZ, 1);           // Set environment variable with your time zone
  tzset();
  writeNumbers(0,0,11);
  refreshShiftRegister();

  writeNumbers(0,1,11);
  refreshShiftRegister();

  // start AP
  Serial.println("Setup WiFi-AP");
  WiFi.softAP(AP_WIFI_SSID, AP_WIFI_Password);
  // WiFi.softAP(AP_WIFI_SSID, AP_WIFI_Password, AP_WIFI_CHANNEL, AP_HIDE_SSID, AP_MAX_CONNECTIONS);
  Serial.print("IP address: ");
  Serial.println(WiFi.softAPIP());
  writeNumbers(0,11,11);
  refreshShiftRegister();

  // start web-server
  Serial.println("Start Web-Server");
  server.begin();
  writeNumbers(1,11,11);
  refreshShiftRegister();

  // Setup Wifi
  Serial.print("Setup Wifi connection...");
  WiFi.begin(wifiSSID, wifiPassword);
  int timeout = 20;
  while (WiFi.status() != WL_CONNECTED && timeout > 0) {
    delay(500);
    timeout--;
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Wifi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    writeNumbers(11,11,11);
    refreshShiftRegister();
  } else {
    WiFi.disconnect();
    // open Access Point
    Serial.println("WiFi connection failed");
  }


  // Allocate The Memory Size Needed
  EEPROM.begin(EEPROM_SIZE);

  EEPROM.get(0, displayMode);
  Serial.print("Read current Mode(");
  Serial.print(displayMode);
  Serial.println(") from EEPROM");

  // just animate some random numbers
  randomAnimation(25, 25);
}

void loop() {
  // look for NTP-Server
  syncTime();

  // WebServer Handler
  handleWebClient(); 

  // calculate next output
  switch(displayMode) {
    case 1:
    default:
      // show current time
      displayTime();
      break;
    case 2:
      // show current date
      displayDate();
      break;
    case 3:
      // show random numbers
      displayRandomNumbers();
      delay(50);
      break;
    case 4:
      // count from 0-9 and repeat it all time
      if (counter > 9 ) {
        counter = 0;
      }
      writeNumbers(counter*10 + counter, counter*10 + counter, counter*10 + counter);
      counter++;
      delay(1000);
      break;
    case 5:
      // show same number on all tubes
      writeNumbers(counter*10 + counter, counter*10 + counter, counter*10 + counter);
      break;
    case 6:
      // count down from given time to zero
      DateTime dateTime = rtc.now();
      if (dateTime.secondstime() > getLastChangeTimestamp()) {
        if (counter >= 10) {
          writeNumbers(counter/60/60, (counter/60) % 60, counter%60);
        } else {
          // last 10 sec are shown in every tube
          writeNumbers(counter*10 + counter, counter*10 + counter, counter*10 + counter);
        }
        Serial.print("Count down: ");
        Serial.println(counter);


        // Countdown finished
        if (counter <= 0) {
          // now show time
          displayMode = 1;
          // some fancy animation for the user
          randomAnimation(500, 25);
        }
        counter--;
      }
      break;
  }

  // display 
  refreshShiftRegister();
}



/**
 * synchronize ntp time against rtc time
 */
void syncTime() {
  time_t now;  // this are the seconds since Epoch (1970) - UTC
  tm tm;       // the structure tm holds time information in a more convenient way

  time(&now);              // read the current time
  localtime_r(&now, &tm);  // update the structure tm with the current time

  // check if year is under lower then 2000 (100 + 1900), to make sure that the ntp time was received
  if (tm.tm_year <= 100) {
    if (tm.tm_sec % 59 == 0) {
      Serial.println("Waiting for NTP-Connection");
    }
  } else {
    // System time synchronized with NTP time
    DateTime dateTime = rtc.now();
    if ((tm.tm_year + 1900) != dateTime.year() || (tm.tm_mon + 1) != dateTime.month() || tm.tm_mday != dateTime.day() || tm.tm_hour != dateTime.hour() || tm.tm_min != dateTime.minute() || tm.tm_sec != dateTime.second()) {
      Serial.print("NTP: ");
      Serial.print(tm.tm_year + 1900);
      Serial.print("-");
      Serial.print(tm.tm_mon + 1);
      Serial.print("-");
      Serial.print(tm.tm_mday);
      Serial.print(" | ");
      Serial.print(tm.tm_hour);
      Serial.print(":");
      Serial.print(tm.tm_min);
      Serial.print(":");
      Serial.println(tm.tm_sec);
      Serial.print("RTC: ");
      Serial.print(dateTime.year());
      Serial.print("-");
      Serial.print(dateTime.month());
      Serial.print("-");
      Serial.print(dateTime.day());
      Serial.print(" | ");
      Serial.print(dateTime.hour());
      Serial.print(":");
      Serial.print(dateTime.minute());
      Serial.print(":");
      Serial.println(dateTime.second());
      // connected
      // set NTP time to RTC
      rtc.adjust(DateTime(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec));
      Serial.println("update time...");
    }
  }
}

/**
 * Handle all incoming http-get requests
 */
void handleWebClient() {
  String header;
  WiFiClient client = server.available();   // Listen for incoming clients
  DateTime dateTime = rtc.now();

  if (client) {                             // If a new client connects,
    String request = "";                    
    while (client.connected()) {            // loop while the request is completed received
      if (client.available()) {             // client request is available
        char c = client.read();             // read client request
        Serial.write(c);
        header += c;                        // add current byte to header
        if (c == '\n') {
          if (request.length() == 0) {
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();

            // print header
            client.println("<!DOCTYPE html><html>");
            client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
            client.println("  <link rel=\"icon\" href=\"data:,\">");
            client.println("  <style>");
            client.println("    html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
            client.println("    .submit-button { margin: 5px; font-size: 18px; font-family: Helvetica, Arial, sans-serif; color: #ffffff; font-weight: bold; text-decoration: none; border-radius: 5px; background-color: #1F7F4C; border-top: 12px solid #1F7F4C; border-bottom: 12px solid #1F7F4C; border-right: 18px solid #1F7F4C; border-left: 18px solid #1F7F4C; display: inline-block;}");
            client.println("  </style>");
            client.println("</head>");
            client.println("<body>");
            client.println("  <h1>Nixie Uhr</h1>");

            // based on current path, triggerd by user input
            if (header.indexOf("GET /time") >= 0) {
              Serial.println("Display time");
              displayMode = 1;
              randomAnimation(10, 25);
            } else if (header.indexOf("GET /date") >= 0) {
              Serial.println("Display date");
              displayMode = 2;
              randomAnimation(10, 25);
            } else if (header.indexOf("GET /random") >= 0) {
              Serial.println("Display random numbers");
              displayMode = 3;
              randomAnimation(10, 25);
            } else if (header.indexOf("GET /countUp") >= 0) {
              displayMode = 4;
              counter = 0;
              randomAnimation(10, 25);
            } else if (header.indexOf("GET /showNumber?") >= 0) {
              int posStart = header.lastIndexOf('?');
              int posEnd = header.indexOf('&');
              displayMode = 5;
              counter = header.substring(posStart+1, posEnd).toInt();
            } else if (header.indexOf("GET /countDown?") >= 0) {
              Serial.println("Countdown:");
              int posH = header.indexOf('h');
              int posM = header.indexOf('m');
              int posS = header.indexOf('s');
              int posEnd = header.indexOf('&');
              Serial.print("posH: ");
              Serial.println(posH);
              Serial.print("posM: ");
              Serial.println(posM);
              Serial.print("posS: ");
              Serial.println(posS);
              Serial.print("posEnd: ");
              Serial.println(posEnd);
              Serial.print("Hour (String): ");
              Serial.println(header.substring(posH+1, posM));
              Serial.print("Minutee (String): ");
              Serial.println(header.substring(posM+1, posS));
              Serial.print("Secon (String): ");
              Serial.println(header.substring(posS+1, posEnd));

              displayMode = 6;
              int hour = header.substring(posH+1, posM).toInt();
              int minute = header.substring(posM+1, posS).toInt();
              int sec = header.substring(posS+1, posEnd).toInt();

              counter = hour * 60 * 60 + minute * 60 + sec;
              Serial.print("Start Countdown with ");
              Serial.println(counter);
            } else if (header.indexOf("GET /set/time/sec/increase") >= 0) {
              rtc.adjust(DateTime(dateTime.year(), dateTime.month(), dateTime.day(), dateTime.hour(), dateTime.minute(), dateTime.second()+1));
            } else if (header.indexOf("GET /set/time/sec/reduce") >= 0) {
              rtc.adjust(DateTime(dateTime.year(), dateTime.month(), dateTime.day(), dateTime.hour(), dateTime.minute(), dateTime.second()-1));
            } else if (header.indexOf("GET /set/time/min/increase") >= 0) {
              rtc.adjust(DateTime(dateTime.year(), dateTime.month(), dateTime.day(), dateTime.hour(), dateTime.minute()+1, dateTime.second()));
            } else if (header.indexOf("GET /set/time/min/reduce") >= 0) {
              rtc.adjust(DateTime(dateTime.year(), dateTime.month(), dateTime.day(), dateTime.hour(), dateTime.minute()-1, dateTime.second()));
            } else if (header.indexOf("GET /set/time/hour/increase") >= 0) {
              rtc.adjust(DateTime(dateTime.year(), dateTime.month(), dateTime.day(), dateTime.hour()+1, dateTime.minute(), dateTime.second()));
            } else if (header.indexOf("GET /set/time/hour/reduce") >= 0) {
              rtc.adjust(DateTime(dateTime.year(), dateTime.month(), dateTime.day(), dateTime.hour()-1, dateTime.minute(), dateTime.second()));
            } else if (header.indexOf("GET /set/date/day/increase") >= 0) {
              rtc.adjust(DateTime(dateTime.year(), dateTime.month(), dateTime.day()+1, dateTime.hour(), dateTime.minute(), dateTime.second()));
            } else if (header.indexOf("GET /set/date/day/reduce") >= 0) {
              rtc.adjust(DateTime(dateTime.year(), dateTime.month(), dateTime.day()-1, dateTime.hour(), dateTime.minute(), dateTime.second()));
            } else if (header.indexOf("GET /set/date/month/increase") >= 0) {
              rtc.adjust(DateTime(dateTime.year(), dateTime.month()+1, dateTime.day(), dateTime.hour(), dateTime.minute(), dateTime.second()));
            } else if (header.indexOf("GET /set/date/month/reduce") >= 0) {
              rtc.adjust(DateTime(dateTime.year(), dateTime.month()-1, dateTime.day(), dateTime.hour(), dateTime.minute(), dateTime.second()));
            } else if (header.indexOf("GET /set/date/year/increase") >= 0) {
              rtc.adjust(DateTime(dateTime.year()+1, dateTime.month(), dateTime.day(), dateTime.hour(), dateTime.minute(), dateTime.second()));
            } else if (header.indexOf("GET /set/date/year/reduce") >= 0) {
              rtc.adjust(DateTime(dateTime.year()-1, dateTime.month(), dateTime.day(), dateTime.hour(), dateTime.minute(), dateTime.second()));
            } else if (header.indexOf("GET /?r") >= 0) {
              // find position of color values
              int pos1 = header.indexOf('r');
              int pos2 = header.indexOf('g');
              int pos3 = header.indexOf('b');
              int pos4 = header.indexOf('&');
              analogWrite(LED_RED_PIN,   header.substring(pos1+1, pos2).toInt());
              analogWrite(LED_GREEN_PIN, header.substring(pos2+1, pos3).toInt());
              analogWrite(LED_BLUE_PIN,  header.substring(pos3+1, pos4).toInt());
            }

            // print current date
            client.print("<span style=\"font-style: italic;\">");
            client.print(dateTime.day());
            client.print(".");
            client.print(dateTime.month());
            client.print(".");
            client.print(dateTime.year());
            client.println("</span>");

            // Modus Selection panel
            client.println("</br>");
            client.println("<a class='submit-button' href=\"/time\">Zeit</a>");
            client.println("<a class='submit-button' href=\"/date\">Datum</a>");
            client.println("<a class='submit-button' href=\"/random\">Zufall</a>");
            client.println("<a class='submit-button' href=\"/countUp\">Zähler</a>");
            client.println("</br>");
            client.println("<a class='submit-button' href=\"/showNumber?0&\">0</a>");
            client.println("<a class='submit-button' href=\"/showNumber?1&\">1</a>");
            client.println("<a class='submit-button' href=\"/showNumber?2&\">2</a>");
            client.println("<a class='submit-button' href=\"/showNumber?3&\">3</a>");
            client.println("<a class='submit-button' href=\"/showNumber?4&\">4</a>");
            client.println("<a class='submit-button' href=\"/showNumber?5&\">5</a>");
            client.println("<a class='submit-button' href=\"/showNumber?6&\">6</a>");
            client.println("<a class='submit-button' href=\"/showNumber?7&\">7</a>");
            client.println("<a class='submit-button' href=\"/showNumber?8&\">8</a>");
            client.println("<a class='submit-button' href=\"/showNumber?9&\">9</a>");

            client.print("<style>");
            client.print("  .container {margin: 0 auto; text-align: center; margin-top: 100px;}");
            client.print("  .round-button {color: white; width: 50px; height: 50px;");
            client.print("  border-radius: 50%; margin: 5px; border: none; font-size: 20px; outline: none; transition: all 0.2s;}");
            client.print("  .blue {background-color: rgb(5, 87, 180);}");
            client.print("  .off {background-color: grey;}");
            client.print("  button:hover{cursor: pointer; opacity: 0.7;}");
            client.print("</style>");
            client.print("<div class='container'>");

            if (header.indexOf("GET /set") >= 0) {
              client.print("<h2 style=\"margin-bottom: 0px;\">Zeit Einstellen</h2>");
              client.print("<p style=\"margin-top: 0px; font-weight: lighter; font-size: smaller; font-style: italic;\">Wird automatisch durch NTP überschrieben.</p>");
              client.print("<button class='round-button blue' type='submit' onmousedown='location.href=\"/set/date/year/increase\"'>+</button>");
              client.print("<span style=\"font-size: xx-large;\">Jahr</span>");
              client.print("<button class='round-button off' type='submit' onmousedown='location.href=\"/set/date/year/reduce\"'>-</button><br>");
              client.print("<button class='round-button blue' type='submit' onmousedown='location.href=\"/set/date/month/increase\"'>+</button>");
              client.print("<span style=\"font-size: xx-large;\">Monat</span>");
              client.print("<button class='round-button off' type='submit' onmousedown='location.href=\"/set/date/month/reduce\"'>-</button><br>");
              client.print("<button class='round-button blue' type='submit' onmousedown='location.href=\"/set/date/day/increase\"'>+</button>");
              client.print("<span style=\"font-size: xx-large;\">Tag</span>");
              client.print("<button class='round-button off' type='submit' onmousedown='location.href=\"/set/date/day/reduce\"'>-</button><br>");
              client.print("<button class='round-button blue' type='submit' onmousedown='location.href=\"/set/time/hour/increase\"'>+</button>");
              client.print("<span style=\"font-size: xx-large;\">Stunde</span>");
              client.print("<button class='round-button off' type='submit' onmousedown='location.href=\"/set/time/hour/reduce\"'>-</button><br>");
              client.print("<button class='round-button blue' type='submit' onmousedown='location.href=\"/set/time/min/increase\"'>+</button>");
              client.print("<span style=\"font-size: xx-large;\">Minute</span>");
              client.print("<button class='round-button off' type='submit' onmousedown='location.href=\"/set/time/min/reduce\"'>-</button><br>");
              client.print("<button class='round-button blue' type='submit' onmousedown='location.href=\"/set/time/sec/increase\"'>+</button>");
              client.print("<span style=\"font-size: xx-large;\">Sekunde</span>");
              client.print("<button class='round-button off' type='submit' onmousedown='location.href=\"/set/time/sec/reduce\"'>-</button><br>");
            } else {
              client.println("<p><a href=\"/set\">Zeit einstellen</a></p>");
            }
            client.print("</div>");


            client.println("<fieldset class=\"border rounded-3 p-3\" style=\"border: solid !important;\">");
            client.println("  <legend class=\"float-none w-auto px-3\">");
            client.println("    <h2>Countdown</h2>");
            client.println("  </legend>");
            client.println("  <input class=\"\" id=\"hour\" value=\"0\"> hours");
            client.println("  <br/>");
            client.println("  <input class=\"\" id=\"minute\" value=\"0\"> minutes");
            client.println("  <br/>");
            client.println("  <input class=\"\" id=\"second\" value=\"30\"> seconds");
            client.println("  <br/>");
            client.println("  <button onclick=\"startCountdown()\">Start Countdown</button>");
            client.println("</fieldset>");

            client.println("<script>");
            client.println("  function startCountdown() {");
            client.println("    var url = window.location.href.substring(0, window.location.href.indexOf(\"/\"))");
            client.println("    var myWindow = window.open(url + \"/countDown?h\" + document.getElementById('hour').value + \"m\" + document.getElementById('minute').value + \"s\" + document.getElementById('second').value + \"&\", \"_self\");");
            client.println("  }");
            client.println("</script>");
            client.println("<div class=\"container\">");
            client.println("  <div class=\"row\"><h1>ESP Color Picker</h1></div>");
            client.println("  <a class=\"btn btn-primary btn-lg\" href=\"#\" id=\"change_color\" role=\"button\">Change Color</a>");
            client.println("  <input class=\"jscolor {onFineChange:'update(this)'}\" id=\"rgb\">");
            client.println("</div>");
            client.println("<script>function update(picker) {document.getElementById('rgb').innerHTML = Math.round(picker.rgb[0]) + ', ' +  Math.round(picker.rgb[1]) + ', ' + Math.round(picker.rgb[2]);");
            client.println("document.getElementById(\"change_color\").href=\"?r\" + Math.round(picker.rgb[0]) + \"g\" +  Math.round(picker.rgb[1]) + \"b\" + Math.round(picker.rgb[2]) + \"&\";}</script>");
            client.println("</body>");
            client.println("</html>");
            client.println();

            break;
          } else { // reset current request
            request = "";
          }
        } else if (c != '\r') {
          request += c; // append requests with c
        }
      }
    }

    client.stop();
    Serial.println("Client disconnected.");
    Serial.println("");
  }
}
