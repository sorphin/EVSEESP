/*
 * Open EVSE ESP8266 Interface Code
 * Portions of this are derived from various research online.
 *
 * Copyright (c) 2016+ Daniel Benedict <sorphin@gmail.com>
 * All included libaries are Copyright (c) their respective authors.
 *
 * This Software Module is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.

 * This Software Module is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this code; see the file COPYING.  If not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <ESP8266HTTPUpdateServer.h>
#include <SPI.h>
#include <ESP8266AVRISP.h>
#include <WebSocketsServer.h>
#include <Hash.h>
#include "index.h"
#include <FS.h>
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <ESP8266HTTPClient.h>
#include <Ticker.h>

//#define DEBUGOUT // Define to output debugging information to the serial

#define RESETPIN 5

//Default SSID and PASSWORD for AP Access Point Mode
const char* host = "esp8266-webupdate-evse";
const char* update_username = "admin";
const char* update_password = "admin";
const char* update_path = "/update";

const char* www_username = "admin";
const char* www_password = "openevse";
const uint16_t espAVRport = 328;
char privateKey[33] = "";

//SERVER strings and interfers for OpenEVSE Energy Monitoring
const char* emhost = "data.openevse.com";
const char* e_url = "/emoncms/input/post.json?node=";
const char* inputID_AMP   = "OpenEVSE_AMP:";
const char* inputID_VOLT   = "OpenEVSE_VOLT:";
const char* inputID_TEMP1   = "OpenEVSE_TEMP1:";
const char* inputID_TEMP3   = "OpenEVSE_TEMP3:";
const char* inputID_PILOT   = "OpenEVSE_PILOT:";
const char* inputID_COST   = "OpenEVSE_COST:";
const char* inputID_KWH   = "OpenEVSE_KWH:";
const char* inputID_TIME   = "OpenEVSE_TIME:";
const char* inputID_TOTALCOST   = "OpenEVSE_TOTALCOST:";
const char* inputID_TOTALKWH   = "OpenEVSE_TOTALKWH:";
const char* inputID_TOTALTIME   = "OpenEVSE_TOTALTIME:";

String node = "0";
String response;

unsigned long Timer;
unsigned long Timer2;

int amp = 0;
int volt = 0;
int temp1 = 0;
int temp3 = 0;
int pilot = 0;
int icost = 0;
int ikwh = 0;
int itime = 0;
int totalcost = 0;
int totalkwh = 0;
int totaltime = 0;

Ticker ticker;

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater(1);
ESP8266AVRISP avrprog(espAVRport, RESETPIN);
WebSocketsServer webSocket = WebSocketsServer(81);

HTTPClient http;

String inputString = "";         // a string to hold incoming data
boolean stringComplete = false;  // whether the string is complete

void printer(String line, int crln, int noser) {
  if (!noser) {
    if (crln) {
      Serial.println(line);
    } else {
      Serial.print(line);
    }
  }

  if (crln) {
    webSocket.broadcastTXT(line + '\n');
  } else {
    webSocket.broadcastTXT(line);
  }
}

void tick() {
  //toggle state
  int state = digitalRead(BUILTIN_LED);  // get the current state of GPIO1 pin
  digitalWrite(BUILTIN_LED, !state);     // set pin to the opposite state
}

//gets called when WiFiManager enters configuration mode
void configModeCallback (WiFiManager *myWiFiManager) {
#ifdef DEBUGOUT
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
#endif //DEBUGOUT
  //entered config mode, make led toggle faster
  ticker.attach(0.2, tick);
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);

  if (SPIFFS.begin()) {
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        if (json.success()) {
          strcpy(privateKey, json["privateKey"]);
        }
      }
    }
  }

  avrprog.setReset(false); // let the AVR run

  WiFi.setOutputPower(20.5);

  //set led pin as output
  pinMode(BUILTIN_LED, OUTPUT);
  // start ticker with 0.5 because we start in AP mode and try to connect
  ticker.attach(0.6, tick);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
#ifndef DEBUGOUT
  wifiManager.setDebugOutput(false);
#endif //DEBUGOUT

  //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wifiManager.setAPCallback(configModeCallback);

  if (!wifiManager.autoConnect("EVSE", "Ch4rg3M3Up")) {
#ifdef DEBUGOUT
    Serial.println("failed to connect and hit timeout");
#endif //DEBUGOUT
    //reset and try again, or maybe put it to deep sleep
    //    ESP.reset();
    ESP.restart();
    delay(1000);
  }

  //if you get here you have connected to the WiFi
#ifdef DEBUGOUT
  Serial.println("Connected");
#endif //DEBUGOUT
  ticker.detach();
  //keep LED on
  digitalWrite(BUILTIN_LED, LOW);

  server.on("/", [] () {
    if (!server.authenticate(www_username, www_password))
      return server.requestAuthentication();
    String s;
    s = "<html><font size='20'><font color=006666>Open</font><b>EVSE</b></font><p><b>Open Source Hardware</b><p>";
    s += "<p>";
    s += "<p>";
    s += "<p><a href='/config'>Set Device Key</a><p><a href='/status'>Status </a><p><a href='/rapi'>RAPI </a><p><a href='/rebootevse'>Reboot EVSE</a><p><a href='/rebootevse'>Reboot ESP</a><br><br>";
    //    s += "<p><b>Firmware Update</b><p>";
    //    s += "<iframe style='width:380px; height:50px;' frameborder='0' scrolling='no' marginheight='0' marginwidth='0' src='/update'></iframe>";
    s += "</html>\r\n\r\n";
    server.send(200, "text/html", s);

#ifdef DEBUGOUT
    File f = SPIFFS.open("/config.json", "r");
    if (!f) {
      Serial.println("file open failed");
    }

    String foo = f.readStringUntil('\n');
    printer(foo, 1, 0);
#endif //DEBUGOUT
  });

  server.on("/reset", [] () {
    if (!server.authenticate(www_username, www_password))
      return server.requestAuthentication();
    String s;
    s = "<html><font size='20'><font color=006666>Open</font><b>EVSE</b></font><p><b>Open Source Hardware</b><p>Wireless Configuration<p>Reset to Defaults:<p>";
    s += "<p><b>Clearing the settings</b><p>";
    s += "</html>\r\n\r\n";
    server.send(200, "text/html", s);
    WiFi.disconnect(true);
    delay(2000);
    //    ESP.reset();
    ESP.restart();
  });

  server.on("/r", [] () {
    if (!server.authenticate(www_username, www_password))
      return server.requestAuthentication();
    String s;
    String rapiString;
    String rapi = server.arg("rapi");
    rapi.replace("%24", "$");
    rapi.replace("+", " ");
    Serial.flush();
    printer(rapi, 1, 0);
    delay(100);
    while (Serial.available()) {
      rapiString = Serial.readStringUntil('\r');
    }
    printer(rapiString, 1, 1);
    s = "<html><font size='20'><font color=006666>Open</font><b>EVSE</b></font><p><b>Open Source Hardware</b><p>RAPI Command Sent<p>Common Commands:<p>Set Current - $SC XX<p>Set Service Level - $SL 1 - $SL 2 - $SL A<p>Get Real-time Current - $GG<p>Get Temperatures - $GP<p>";
    s += "<p>";
    s += "<form method='get' action='r'><label><b><i>RAPI Command:</b></i></label><input name='rapi' length=32><p><input type='submit'></form>";
    s += rapi;
    s += "<p>>";
    s += rapiString;
    s += "<br><br><a href='/'>Home</a>";
    s += "<p></html>\r\n\r\n";
    server.send(200, "text/html", s);
  });

  server.on("/ra", [] () {
    if (!server.authenticate(www_username, www_password))
      return server.requestAuthentication();
    String s;
    String rapiString;
    String rapi = server.arg("rapi");
    rapi.replace("%24", "$");
    rapi.replace("+", " ");
    Serial.flush();
    printer(rapi, 1, 0);
    delay(100);
    while (Serial.available()) {
      rapiString = Serial.readStringUntil('\r');
    }
    printer(rapiString, 1, 1);
    server.send(200, "text/plain", rapiString);
  });

  server.on("/rapi", [] () {
    if (!server.authenticate(www_username, www_password))
      return server.requestAuthentication();
    String s;
    s = "<html><font size='20'><font color=006666>Open</font><b>EVSE</b></font><p><b>Open Source Hardware</b><p>Send RAPI Command<p>Common Commands:<p>Set Current - $SC XX<p>Set Service Level - $SL 1 - $SL 2 - $SL A<p>Get Real-time Current - $GG<p>Get Temperatures - $GP<p>";
    s += "<p>";
    s += "<form method='get' action='r'><label><b><i>RAPI Command:</b></i></label><input name='rapi' length=32><p><input type='submit'></form>";
    s += "<br><br><a href='/'>Home</a>";
    s += "</html>\r\n\r\n";
    server.send(200, "text/html", s);
  });

  server.on("/config", [] () {
    if (!server.authenticate(www_username, www_password))
      return server.requestAuthentication();
    String s;
    String qkey = server.arg("ekey");

    if (qkey != "") {
#ifdef DEBUGOUT
      Serial.println("Writing EMON Key to Memory:");
#endif //DEBUGOUT
      s = "<META http-equiv=\"refresh\" content=\"2;URL=/\"><html><font size='20'><font color=006666>Open</font><b>EVSE</b></font><p><b>Open Source Hardware</b><p>EMon Portal Device Key<p>";
      s += "<p>Saved to Memory...</html>\r\n\r\n";
      server.send(200, "text/html", s);

      strcpy(privateKey, qkey.c_str());
      DynamicJsonBuffer jsonBuffer;
      JsonObject& json = jsonBuffer.createObject();
      json["privateKey"] = privateKey;

      File configFile = SPIFFS.open("/config.json", "w");
      json.printTo(configFile);
      configFile.close();
    }
    else {
      s = "<html><font size='20'><font color=006666>Open</font><b>EVSE</b></font><p><b>Open Source Hardware</b><p>EMon Portal Device Key<p>";
      s += "<p>";
      s += "<p>";
      s += "<form method='get' action='config'><label><b><i>Device Access Key:</b></i></label><input name='ekey' length=100><p><input type='submit'></form>";
      s += "</html>\r\n\r\n";
      server.send(200, "text/html", s);
    }
  });

  server.on("/status", [] () {
    if (!server.authenticate(www_username, www_password))
      return server.requestAuthentication();
    String s;
    s = "<html><iframe style='width:480px; height:320px;' frameborder='0' scrolling='no' marginheight='0' marginwidth='0' src='http://data.openevse.com/emoncms/vis/realtime?feedid=2564&embed=1&apikey=483ebbe86fcd4a31705582086504d87d'></iframe>";
    s += "</html>\r\n\r\n";
    server.send(200, "text/html", s);
  });

  server.on("/rebootevse", [] () {
    if (!server.authenticate(www_username, www_password))
      return server.requestAuthentication();
    String s;
    s = "<META http-equiv=\"refresh\" content=\"5;URL=/\"><html><font size='20'><font color=006666>Open</font><b>EVSE</b></font><p><b>Open Source Hardware</b><p>Rebooting EVSE....<p>";
    s += "</html>\r\n\r\n";
    server.send(200, "text/html", s);
    delay(500);
    avrprog.setReset(true);
    delay(500);
    avrprog.setReset(false);
  });

  server.on("/rebootesp", [] () {
    if (!server.authenticate(www_username, www_password))
      return server.requestAuthentication();
    String s;
    s = "<META http-equiv=\"refresh\" content=\"5;URL=/\"><html><font size='20'><font color=006666>Open</font><b>EVSE</b></font><p><b>Open Source Hardware</b><p>Rebooting ESP....<p>";
    s += "</html>\r\n\r\n";
    server.send(200, "text/html", s);
    //    ESP.reset();
    ESP.restart();
  });

  server.on("/serialcon", [] () {
    if (!server.authenticate(www_username, www_password))
      return server.requestAuthentication();
    server.send(200, "text/html", PAGE_Index);
  });

  server.on("/ffs", [] () {
    if (!server.authenticate(www_username, www_password))
      return server.requestAuthentication();
    SPIFFS.format();
  });

  MDNS.begin(host);
  MDNS.enableArduino(328);
  //  httpUpdater.setup(&server, update_path, update_username, update_password);
  httpUpdater.setup(&server, update_path);
  server.begin();
  webSocket.begin();
  MDNS.addService("http", "tcp", 80);
  MDNS.addService("arduino", "tcp", 328);
  MDNS.addServiceTxt("arduino", "tcp", "tcp_check", "yes");
  MDNS.addServiceTxt("arduino", "tcp", "ssh_upload", "no");
  MDNS.addServiceTxt("arduino", "tcp", "board", "ARDUINO_AVR_PRO");
  MDNS.addServiceTxt("arduino", "tcp", "auth_upload", "no");
  avrprog.begin();
#ifdef DEBUGOUT
  printer("HTTP server started\n", 1, 0);
  Serial.printf("HTTPUpdateServer ready! Open http://%s.local%s in your browser and login with username '%s' and password '%s'\r\n", host, update_path, update_username, update_password);
  printer("Use your avrdude:\n", 1, 0);
  printer("avrdude -c arduino -p atmega328p -P net:", 0, 0);
  printer(String(WiFi.localIP().toString()), 0, 0);
  printer(":", 0, 0);
  printer(String(espAVRport), 0, 0);
  printer(" -Uflash:w:<sketch.hex>\n", 1, 0);
#endif //DEBUGOUT
  Timer = millis();
}

void loop() {
  server.handleClient();

  if (privateKey != 0) {
    if ((millis() - Timer) >= 30000) {
      Timer = millis();
      Serial.flush();
      Serial.println("$GE*B0");
      delay(100);
      while (Serial.available()) {
        String rapiString = Serial.readStringUntil('\r');
        if ( rapiString.startsWith("$OK") ) {
          String qrapi;
          qrapi = rapiString.substring(rapiString.indexOf(' '));
          pilot = qrapi.toInt();
        }
      }

      delay(100);
      Serial.flush();
      Serial.println("$GG*B2");
      delay(100);
      while (Serial.available()) {
        String rapiString = Serial.readStringUntil('\r');
        if ( rapiString.startsWith("$OK") ) {
          String qrapi;
          qrapi = rapiString.substring(rapiString.indexOf(' '));
          amp = qrapi.toInt();
          String qrapi1;
          qrapi1 = rapiString.substring(rapiString.lastIndexOf(' '));
          volt = qrapi1.toInt();
        }
      }
      delay(100);
      Serial.flush();
      Serial.println("$GP*BB");
      delay(100);
      while (Serial.available()) {
        String rapiString = Serial.readStringUntil('\r');
        if (rapiString.startsWith("$OK") ) {
          String qrapi;
          qrapi = rapiString.substring(rapiString.indexOf(' '));
          temp1 = qrapi.toInt();
          String qrapi2;
          qrapi2 = rapiString.substring(rapiString.lastIndexOf(' '));
          temp3 = qrapi2.toInt();
        }
      }

      delay(100);
      Serial.flush();
      Serial.println("$To");
      delay(100);
      while (Serial.available()) {
        String rapiString = Serial.readStringUntil('\r');
        if ( rapiString.startsWith("$OK ") ) {
          String qrapi;
          qrapi = rapiString.substring(rapiString.indexOf(' '));
          icost = qrapi.toInt();
        }
      }

      delay(100);
      Serial.flush();
      Serial.println("$Tk");
      delay(100);
      while (Serial.available()) {
        String rapiString = Serial.readStringUntil('\r');
        if ( rapiString.startsWith("$OK ") ) {
          String qrapi;
          qrapi = rapiString.substring(rapiString.indexOf(' '));
          ikwh = qrapi.toInt();
        }
      }

      delay(100);
      Serial.flush();
      Serial.println("$Tt");
      delay(100);
      while (Serial.available()) {
        String rapiString = Serial.readStringUntil('\r');
        if ( rapiString.startsWith("$OK ") ) {
          String qrapi;
          qrapi = rapiString.substring(rapiString.indexOf(' '));
          itime = qrapi.toInt();
        }
      }

      delay(100);
      Serial.flush();
      Serial.println("$TO");
      delay(100);
      while (Serial.available()) {
        String rapiString = Serial.readStringUntil('\r');
        if ( rapiString.startsWith("$OK ") ) {
          String qrapi;
          qrapi = rapiString.substring(rapiString.indexOf(' '));
          totalcost = qrapi.toInt();
        }
      }

      delay(100);
      Serial.flush();
      Serial.println("$TK");
      delay(100);
      while (Serial.available()) {
        String rapiString = Serial.readStringUntil('\r');
        if ( rapiString.startsWith("$OK ") ) {
          String qrapi;
          qrapi = rapiString.substring(rapiString.indexOf(' '));
          totalkwh = qrapi.toInt();
        }
      }

      delay(100);
      Serial.flush();
      Serial.println("$TT");
      delay(100);
      while (Serial.available()) {
        String rapiString = Serial.readStringUntil('\r');
        if ( rapiString.startsWith("$OK ") ) {
          String qrapi;
          qrapi = rapiString.substring(rapiString.indexOf(' '));
          totaltime = qrapi.toInt();
        }
      }


      // We now create a URL for OpenEVSE RAPI data upload request
      String url = e_url;
      String url_amp = inputID_AMP;
      url_amp += amp;
      url_amp += ",";
      String url_volt = inputID_VOLT;
      url_volt += volt;
      url_volt += ",";
      String url_temp1 = inputID_TEMP1;
      url_temp1 += temp1;
      url_temp1 += ",";
      String url_temp3 = inputID_TEMP3;
      url_temp3 += temp3;
      url_temp3 += ",";
      String url_pilot = inputID_PILOT;
      url_pilot += pilot;
      url_pilot += ",";
      /////
      String url_cost = inputID_COST;
      url_cost += icost;
      url_cost += ",";
      String url_kwh = inputID_KWH;
      url_kwh += ikwh;
      url_kwh += ",";
      String url_time = inputID_TIME;
      url_time += itime;
      url_time += ",";
      String url_totalcost = inputID_TOTALCOST;
      url_totalcost += totalcost;
      url_totalcost += ",";
      String url_totalkwh = inputID_TOTALKWH;
      url_totalkwh += totalkwh;
      url_totalkwh += ",";
      String url_totaltime = inputID_TOTALTIME;
      url_totaltime += totaltime;
      //////
      url += node;
      url += "&json={";
      url += url_amp;
      url += url_volt;
      url += url_temp1;
      url += url_temp3;
      url += url_pilot;
      url += url_cost;
      url += url_kwh;
      url += url_time;
      url += url_totalcost;
      url += url_totalkwh;
      url += url_totaltime;

      url += "}&devicekey=";
      url += privateKey;

      String fullurl = "http://data.openevse.com" + url;
      //    #ifdef DEBUGOUT
      printer(fullurl, 1, 1);
      //    #endif //DEBUGOUT
      http.begin(fullurl);
      http.setUserAgent("OpenEVSE");
      int httpCode = http.GET();
#ifdef DEBUGOUT
      printer(String(httpCode, DEC), 1, 1);
#endif //DEBUGOUT
      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
#ifdef DEBUGOUT
        printer(payload, 1, 1);
#endif //DEBUGOUT
      }
#ifdef DEBUGOUT
      Serial.println(host);
      Serial.println(url);
#endif //DEBUGOUT
    }
  }

  static AVRISPState_t last_state = AVRISP_STATE_IDLE;
  AVRISPState_t new_state = avrprog.update();
  if (last_state != new_state) {
    switch (new_state) {
      case AVRISP_STATE_IDLE: {
#ifdef DEBUGOUT
          printer("[AVRISP] now idle\r\n", 0, 0);
#endif //DEBUGOUT
          // Use the SPI bus for other purposes
          break;
        }
      case AVRISP_STATE_PENDING: {
#ifdef DEBUGOUT
          printer("[AVRISP] connection pending\r\n", 0, 0);
#endif //DEBUGOUT
          // Clean up your other purposes and prepare for programming mode
          break;
        }
      case AVRISP_STATE_ACTIVE: {
#ifdef DEBUGOUT
          printer("[AVRISP] programming mode\r\n", 0, 0);
#endif //DEBUGOUT
          // Stand by for completion
          break;
        }
    }
    last_state = new_state;
  }
  // Serve the client
  if (last_state != AVRISP_STATE_IDLE) {
    avrprog.serve();
  }

  webSocket.loop();
}
