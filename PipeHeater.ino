#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>

#include <OneWire.h>
#include <DallasTemperature.h>

#include <ArduinoJson.h>
#include "FS.h"

#include <NTPClient.h>

#include <WiFiUdp.h>

#include <Dusk2Dawn.h>

#include <time.h>

#define ONE_WIRE_BUS 14

#define OUTPUT_1 4
#define OUTPUT_2 5

/* Set these to your desired credentials. */
String host = "pipeheater";

/* Save important state for pipe heater operation */
String temperature = "10";
String latitude = "";
String longitude = "";

time_t timestamp;
int sunrise;
int sunset;
struct tm *datetime;
bool control;

std::unique_ptr<ESP8266WebServer> server;
ESP8266HTTPUpdateServer httpUpdater(true);

OneWire oneWire(ONE_WIRE_BUS);

DallasTemperature sensors(&oneWire);

WiFiUDP ntpUDP;

NTPClient timeClient(ntpUDP, "europe.pool.ntp.org");

// Library modified to allow location to be set later.
Dusk2Dawn location(0, 0, 0);


bool loadConfig()
{
  const char *temp;
  
  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile) {
    Serial.println("Failed to open config file");
    return false;
  }

  size_t size = configFile.size();
  if (size > 1024) {
    Serial.println("Config file size is too large");
    return false;
  }

  // Allocate a buffer to store contents of the file.
  std::unique_ptr<char[]> buf(new char[size]);

  // We don't use String here because ArduinoJson library requires the input
  // buffer to be mutable. If you don't use ArduinoJson, you may as well
  // use configFile.readString instead.
  configFile.readBytes(buf.get(), size);

  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(buf.get());

  if (!json.success()) {
    Serial.println("Failed to parse config file");
    return false;
  }

  temp = json["host_name"];
  if (temp)
    host = temp;

  temp = json["required_temp"];
  if (temp)
    temperature = temp;

  temp = json["latitude"];
  if (temp)
    latitude = temp;

  temp = json["longitude"];
  if (temp)
    longitude = temp;

  return true;
}


bool saveConfig()
{
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();
  json["host_name"] = host;
  json["required_temp"] = temperature;
  json["latitude"] = latitude;
  json["longitude"] = longitude;

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("Failed to open config file for writing");
    return false;
  }

  json.printTo(configFile);
  return true;
}


void handleInput1Interrupt(void)
{
  unsigned long now;
  int state;
  
  state = digitalRead(OUTPUT_1);

  if (state == HIGH)
    digitalWrite(OUTPUT_1, LOW);
  else
    digitalWrite(OUTPUT_1, HIGH);
}


void handleInput2Interrupt(void)
{
  unsigned long now;
  int state;

  state= digitalRead(OUTPUT_2);

  if (state == HIGH)
    digitalWrite(OUTPUT_2, LOW);
  else
    digitalWrite(OUTPUT_2, HIGH);
}


void basicPage(String title, String content)
{
  String message = " \
    <html><head> \
    <title>";
  message += title;
  message += "</title> \
    <meta name=\"viewport\" content=\"width=device-width; initial-scale=1.0;\"> \
    </head> \
    <body> \
    <h1>";
  message += title;
  message += "</h1> \
    <ul> \
    <li><a href=\"index.html\">Main</a></li> \
    <li><a href=\"config.html\">Configuration</a></li> \
    <li><a href=\"update\">Update</a></li> \
    </ul> \
  ";
  message += content;
  message += "\
    </body></html> \
  ";
  server->send(200, "text/html", message);  
}


void mainPage()
{
  String tmp_str;
  String old_host = host;
  String ssid = WiFi.SSID();
  String passwd = WiFi.psk();

  if (server->arg("output1") != "") {
    handleInput1Interrupt();
  }

  if (server->arg("output2") != "") {
    handleInput2Interrupt();
  }

  tmp_str = server->arg("host");
  if (tmp_str != "") {
    if (tmp_str != String(host)) {
      host = tmp_str;
      saveConfig();
    }
  }

  tmp_str = server->arg("temperature");
  if (tmp_str != "") {
    if (tmp_str != String(temperature)) {
      temperature = tmp_str;
      saveConfig();
    }
  }

  tmp_str = server->arg("latitude");
  if (tmp_str != "") {
    if (tmp_str != String(latitude)) {
      latitude = tmp_str;
      location.changeloc(latitude.toFloat(), longitude.toFloat());
      saveConfig();
    }
  }

  tmp_str = server->arg("longitude");
  if (tmp_str != "") {
    if (tmp_str != String(longitude)) {
      longitude = tmp_str;
      location.changeloc(latitude.toFloat(), longitude.toFloat());
      saveConfig();
    }
  }


  tmp_str = server->arg("ssid");
  if (tmp_str != "") {
    ssid = tmp_str;
  }

  tmp_str = server->arg("passwd");
  if (tmp_str != "") {
    passwd = tmp_str;
  }

  if ((host != old_host) || (ssid != WiFi.SSID()) || (passwd != WiFi.psk())) {
    basicPage("PipeHeater - Config Change", "Switching Hostname, SSID and/or password.");
    network_config(ssid, passwd);
  }
  
  int state1 = digitalRead(OUTPUT_1);

  sensors.requestTemperatures();

  float temp = sensors.getTempCByIndex(0);

  datetime = gmtime(&timestamp);

  char dateStr[20];
  
  strftime(dateStr, sizeof(dateStr), "%Y-%m-%d %H:%M:%S", datetime);

  /* Remember, struct tm tm_mon is zero indexed... */
  char sunriseStr[6];
  Dusk2Dawn::min2str(sunriseStr, sunrise);
  char sunsetStr[6];
  Dusk2Dawn::min2str(sunsetStr, sunset);
 
  String message = " \
    </font></center></p> \
    <form method=\"get\" action=\"/index.html\"> \
    <ul> \
    <li>Temperature is currently: \
  ";
  message += String(temp);
  message += " deg C</li> \
    <li>Heater state: ";
  if (state1 == HIGH)
    message += "On";
  else
    message += "Off";
  if (control == false) 
    message += " (Disabled)";
  message += "</li> \
    <li>Required temperature: <input type=\"number\" name=\"temperature\" min=\"1\" max=\"25\" value=\"";
  message += String(temperature);
  message += "\"> deg C \
    <input type=\"submit\" value=\"Update\" /></li> \
    <li>Current date/time (UTC): ";
  message += dateStr;
  message += "</li> \
    <li>Sunrise time (UTC): ";
  message += String(sunriseStr);
  message += "</li> \
    <li>Sunset time (UTC): ";
  message += String(sunsetStr);
  message += "</li> \
    </ul> \
    </form> \
  ";
  basicPage("PipeHeater - Main", message);  
}


void configPage()
{
  float temp = sensors.getTempCByIndex(0);
   
  String message = " \
    <form method=\"get\" action=\"/index.html\"> \
    <table><tr> \
    <td>Variable</td><td>Value</td>\
    </tr><tr> \
    <td>Host Name</td><td><input type=\"text\" name=\"host\" value=\"";
  message += String(host);
  message += "\" \
    </td> \
    </tr><tr> \
    <td>Wifi SSID</td><td><input type=\"text\" name=\"ssid\" value=\"";
  message += String(WiFi.SSID());
  message += "\" \
    </td> \
    </tr><tr> \
    <td>Wifi Password</td><td><input type=\"text\" name=\"passwd\" value=\"";
  message += String(WiFi.psk());
  message += "\" \
    </td> \
    </tr><tr> \
    <td>Location Latitude</td><td><input type=\"text\" name=\"latitude\" value=\"";
  message += String(latitude);
  message += "\" \
    </td> \
    </tr><tr> \
    <td>Location Longitude</td><td><input type=\"text\" name=\"longitude\" value=\"";
  message += String(longitude);
  message += "\" \
    </td> \
    </tr></table> \
    <input type=\"submit\" value=\"Save\" /> \
    </form> \
  ";
  basicPage("PipeHeater - Config", message);  
}

void network_config(String ssid, String passwd) {
  WiFi.disconnect();
  
  Serial.println("SSID: " + WiFi.SSID());
  Serial.println("Password: " + WiFi.psk());
  Serial.println("Hostname: " + String(host));

  WiFi.hostname(host);

  if (ssid == "")
    WiFi.begin();
  else
    WiFi.begin(ssid.c_str(), passwd.c_str());

  // Give WiFi a chance to connect.
  delay(2000);

  // WifiManager will check to see if we are already connected before running portal.
  WiFiManager wifiManager;
  wifiManager.autoConnect(host.c_str(), "defpass");

  //Serial.print("Connecting to WiFi... ");
  //while (WiFi.status() == WL_CONNECTED) {
  //  delay(500);
  //  Serial.print(".");
  //}
  //Serial.println("");

  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  server.reset(new ESP8266WebServer(80));

  server->on("/", mainPage);
  server->on("/index.html", mainPage);
  server->on("/config.html", configPage);

  httpUpdater.setup(&*server);

  server->begin();
  Serial.println("HTTP server started");
}

void setup() {
	Serial.begin(115200);
  delay(10);

  Serial.println("Read Config...");

  if (!SPIFFS.begin()) {
    Serial.println("Failed to mount file system");
    return;
  }

  if (!loadConfig()) {
    Serial.println("Failed to load config");
  } else {
    Serial.println("Config loaded");
  }

  location.changeloc(latitude.toFloat(), longitude.toFloat());

  network_config("", "");

	pinMode(OUTPUT_1, OUTPUT);
	pinMode(OUTPUT_2, OUTPUT);


	digitalWrite(OUTPUT_1, LOW);
	digitalWrite(OUTPUT_2, LOW);

  sensors.begin();

  timeClient.begin();
}


void loop() {
  
  float temp;
  int now;

	server->handleClient();

  timeClient.update();

  timestamp = timeClient.getEpochTime();

  datetime = gmtime(&timestamp);

  sunrise = location.sunrise(datetime->tm_year + 1900, datetime->tm_mon + 1, datetime->tm_mday, false);
  sunset = location.sunset(datetime->tm_year + 1900, datetime->tm_mon + 1, datetime->tm_mday, false);

  now = datetime->tm_hour * 60 + datetime->tm_min;

  sensors.requestTemperatures();

  temp = sensors.getTempCByIndex(0);

  // Let's start heating 15 mins before sunrise
  if ((now > (sunrise - 15)) && (now < sunset)) {
    control = true;
    if (temperature.toInt() > temp)
      digitalWrite(OUTPUT_1, HIGH);
    else
      digitalWrite(OUTPUT_1, LOW);
  } else {
    control = false;
    digitalWrite(OUTPUT_1, LOW);
  }
}
