#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Ticker.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include <SoftwareSerial.h>
#include "pms7003.h"

#define BUILD_VERSION 20221208  //YYYYMMDD Pongsathorn Utsahawattanasuk 6210554784

const char* mqtt_server = "broker.netpie.io";
const int mqtt_port = 1883;
char mqtt_client[40] = "";
char mqtt_username[40] = "";
char mqtt_password[40] = "";

String cmdstring;
String parmstring;
int sepIndex;
bool noparm = 0;

WiFiClient espClient;
PubSubClient client(espClient);
bool shouldSaveConfig = false;

Adafruit_BME280 bme;  // I2C

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

SoftwareSerial _serial(D5, D6);  // RX, TX
pms7003 pms(_serial, Serial);

uint8_t is_1s = 0, sleep = 0;
unsigned int count_1s = 0;
char msg[100];
unsigned int send_period = 60;

//btn
#define TRIGGER_PIN 0  // NodeMCU FLASH button for start wifi config portal
unsigned int btn_push_sec = 0, btn_push_loop = 0;
#define WIFI_CFG_BTN_PUSH_SEC 5
uint8_t pressed = 0;
boolean buttonState = LOW;

void tick_1s() {
  is_1s = 1;
  count_1s++;
  sleep++;
}
void configModeCallback(WiFiManager* myWiFiManager) {
  Serial.println("# Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
}

Ticker ticker;

//WiFi setup
uint8_t macAddr[6];

union {
  struct {
    uint8_t d0 : 1;
    uint8_t d1 : 1;
    uint8_t d2 : 1;
    uint8_t d3 : 1;
    uint8_t d4 : 1;
    uint8_t d5 : 1;
    uint8_t d6 : 1;
    uint8_t d7 : 1;
  } bit;
  uint8_t b8;
} crc = { .b8 = 0 };

void setup() {
  Serial.begin(38400);
  _serial.begin(9600);

  ticker.attach(1, tick_1s);

  unsigned status = bme.begin(0x76);
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("SSD1306 allocation failed");
  } else {
    Serial.println("ArdinoAll OLED Start Work !!!");
  }
  display.clearDisplay();
  display.setTextColor(WHITE, BLACK);
  display.setCursor(0, 0);
  display.println(("Starting"));
  display.display();

  pinMode(TRIGGER_PIN, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);

  //wifi
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  delay(1000);
  WiFi.printDiag(Serial);
  display.println(mqtt_server);
  display.print(F("Port:"));
  display.println(mqtt_port);
  display.display();

  Serial.print(F("# Wait for Wifi connected "));
  int retry = 40;
  while (WiFi.status() != WL_CONNECTED && retry > 0) {
    Serial.print('.');
    retry--;
    delay(500);
    if (digitalRead(TRIGGER_PIN) == LOW) return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" connected");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("!! NOT connect");
  }
  load_config();
  Serial.print(F("# mqtt_server: "));
  Serial.println(mqtt_server);
  Serial.print(F("# mqtt_port: "));
  Serial.println(mqtt_port);
  Serial.print(F("# mqtt_client: "));
  Serial.println(mqtt_client);
  Serial.print(F("# mqtt_username: "));
  Serial.println(mqtt_username);
  Serial.print(F("# mqtt_password: "));
  Serial.println(mqtt_password);
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqtt_callback);
}

void loop() {
  //Serial.print("btn_push_loop = "); Serial.println(btn_push_loop);
  if (btn_push_sec >= WIFI_CFG_BTN_PUSH_SEC) {
    wifiConfigDisplay();
    startWifiConfig();
    Serial.println("Start Wifi Config");
    btn_push_sec = 0;
  }
  if (digitalRead(TRIGGER_PIN) == LOW) {
    btn_push_loop += 1;
    //Serial.print("btn_push_loop = "); Serial.println(btn_push_loop);
    if (5 < btn_push_loop < 25) {
      display.ssd1306_command(SSD1306_DISPLAYON);
      sleep = 1;
    }
  } else {
    btn_push_loop = 0;
    btn_push_sec = 0;
  }

  if (digitalRead(TRIGGER_PIN) == LOW && buttonState == LOW) {
    //oledDisplay(pressed);
    pressed++;
    buttonState = HIGH;
    if (pressed >= 2) pressed = 0;
  } else if (digitalRead(TRIGGER_PIN) == HIGH && buttonState == HIGH) {
    buttonState = LOW;
  }
  if (is_1s) {
    if (!client.connected()) {
      reconnect();
    }
    client.loop();

    if (digitalRead(TRIGGER_PIN) == LOW) {
      btn_push_sec += 1;
    }
    //Serial.print("btn_push_sec = "); Serial.println(btn_push_sec);
    //printValues();
    float temp = bme.readTemperature();
    float hum = bme.readHumidity();
    float pres = bme.readPressure() / 100.0F;

    oledDisplay(temp, pres, hum, pressed);
    is_1s = 0;
    //Serial.printf("pms7003(ug/m3) :{pm1:%d ,pm2_5:%d ,pm10:%d}\n", _pm1, _pm25, _pm10);

    if (sleep % send_period == 0) {  //every 1,5,10 min
      //----------------------------------------
      //idea: https://arduinojson.org/book/serialization_tutorial6.pdf#page=12
      StaticJsonDocument<200> doc;

      JsonObject data = doc.createNestedObject("data");
      data["pm1"] = pms.pm1Value;
      data["pm2_5"] = pms.pm25Value;
      data["pm10"] = pms.pm10Value;

      data["humidity"] = hum;
      data["pressure"] = pres;
      data["temperature"] = temp;

      char buffer[256];
      serializeJson(doc, buffer);
      client.publish("@shadow/data/update", buffer);
      Serial.print("publish: ");
      serializeJson(doc, Serial);
      Serial.println();
      //----------------------------------------
      //      String data = "{\"data\": {\"humidity\":" + String(hum) + ",\"temperature\":" + String(temp) + ",\"pressure\":" + String(pres) + "}}";
      //      Serial.print("publish : "); Serial.println(data);
      //      data.toCharArray(msg, (data.length() + 1));
      //      client.publish("@shadow/data/update", msg);
    }
    if (sleep % 180 == 0) {  //every 3 min
      display.ssd1306_command(SSD1306_DISPLAYOFF);
      //      client.subscribe("@shadow/data/updated");
    }
  }
  InputCommand();
  pms.readSensor();
  delay(5);
}

void InputCommand() {
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    sepIndex = input.indexOf('=');
    if (sepIndex == -1) {
      cmdstring = input;
      noparm = 1;
    } else {
      cmdstring = input.substring(0, sepIndex);
      cmdstring.trim();
      parmstring = input.substring(sepIndex + 1);
      parmstring.trim();
      noparm = 0;
    }
    if (cmdstring.equalsIgnoreCase("client")) {
      if (noparm == 0) {
        for (int i = 0; i < 40; i++) mqtt_client[i] = 0;
        parmstring.toCharArray(mqtt_client, parmstring.length() + 1);
        Serial.print("New MQTT Client ID = ");
        Serial.println(mqtt_client);
      }
    } else if (cmdstring.equalsIgnoreCase("username")) {
      if (noparm == 0) {
        for (int i = 0; i < 40; i++) mqtt_username[i] = 0;
        parmstring.toCharArray(mqtt_username, parmstring.length() + 1);
        Serial.print("New MQTT Username = ");
        Serial.println(mqtt_username);
      }
    } else if (cmdstring.equalsIgnoreCase("password")) {
      if (noparm == 0) {
        for (int i = 0; i < 40; i++) mqtt_password[i] = 0;
        parmstring.toCharArray(mqtt_password, parmstring.length() + 1);
        Serial.print("New MQTT Password = ");
        Serial.println(mqtt_password);
      }
    } else if (cmdstring.equalsIgnoreCase("saveConfig")) {
      save_config();
      Serial.println("Save config");
    }
  }
}

int get_config_size() {
  int s = 0;
  s = sizeof(s);
  s += sizeof(mqtt_client);
  s += sizeof(mqtt_username);
  s += sizeof(mqtt_password);
  return s;
}

void save_config() {
  int s, ss;
  byte csum;
  int eeprom_size;
  s = get_config_size();
  ss = s + sizeof(csum);
  if (ss % 4 == 0) {
    eeprom_size = ss;
  } else {
    eeprom_size = ss + (4 - (ss % 4));
  }
  EEPROM.begin(eeprom_size);
  EEPROM.put(0, s);
  s = sizeof(s);
  EEPROM.put(s, mqtt_client);
  s += sizeof(mqtt_client);
  EEPROM.put(s, mqtt_username);
  s += sizeof(mqtt_username);
  EEPROM.put(s, mqtt_password);
  s += sizeof(mqtt_password);
  uint8_t bi;
  uint8_t out1;
  crc.b8 = 0;
  for (int i = 0, csum = 0; i < s; i++) {
    bi = EEPROM[i];
    for (int b = 0; b < 8; b++) {
      out1 = crc.bit.d7 ^ (bi & 0x01);
      crc.bit.d4 = crc.bit.d7 ^ crc.bit.d4;
      crc.b8 = crc.b8 << 1;
      crc.bit.d0 = out1;
      bi << 1;
    }
  }
  csum = crc.b8;
  EEPROM.put(s, csum);
  if (EEPROM.commit()) {
    Serial.printf("# EEPROM successfully committed. size=%d, csum=%02X\n", s, csum);
  } else {
    Serial.println(F("# ERROR! EEPROM commit failed"));
  }
  EEPROM.end();
}

int load_config() {
  int s, ss, eeprom_size;
  byte csum, csum2;
  int ee_conf_size;
  s = get_config_size();
  ss = s + sizeof(csum);
  if (ss % 4 == 0) {
    eeprom_size = ss;
  } else {
    eeprom_size = ss + (4 - (ss % 4));
  }
  EEPROM.begin(eeprom_size);
  EEPROM.get(0, ee_conf_size);  //get size of config from eeprom
  if (ee_conf_size > s) {
    Serial.print(F("# Warning configuration size missmatch "));
    Serial.print(s);
    Serial.print("!=");
    Serial.println(ee_conf_size);
    return -1;
  }
  uint8_t bi;
  uint8_t out1;
  crc.b8 = 0;
  for (int i = 0, csum = 0; i < ee_conf_size; i++) {
    bi = EEPROM[i];
    for (int b = 0; b < 8; b++) {
      out1 = crc.bit.d7 ^ (bi & 0x01);
      crc.bit.d4 = crc.bit.d7 ^ crc.bit.d4;
      crc.b8 = crc.b8 << 1;
      crc.bit.d0 = out1;
      bi << 1;
    }
  }
  csum = crc.b8;
  EEPROM.get(ee_conf_size, csum2);
  if (csum != csum2) {
    Serial.print(F("# configuration checksum missmatch "));
    Serial.print(csum, HEX);
    Serial.print("!=");
    Serial.println(csum2, HEX);
    return -2;
  }
  Serial.print(F("# load EEPROM config ... "));
  do {
    s = sizeof(s);
    EEPROM.get(s, mqtt_client);
    s += sizeof(mqtt_client);
    EEPROM.get(s, mqtt_username);
    s += sizeof(mqtt_username);
    EEPROM.get(s, mqtt_password);
    s += sizeof(mqtt_password);
    if (s >= ee_conf_size) break;
  } while (false);
  Serial.println(F("Done"));
  EEPROM.end();
  return s;
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting NETPIE2020 connection…");
    if (client.connect(mqtt_client, mqtt_username, mqtt_password)) {
      Serial.println("NETPIE2020 connected");
      client.subscribe("@msg/#");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println("try again in 5 seconds");
      delay(5000);
    }
    InputCommand();
  }
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  Serial.print(F("# Message arrived ["));
  Serial.print(topic);
  Serial.print("] ");
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);

  if (String(topic) == "@msg/led") {
    if (message == "on") {
      digitalWrite(LED_BUILTIN, 0);
      client.publish("@shadow/data/update", "{\"data\":{\"led\":\"on\"}}");
      Serial.println("LED ON");
    } else if (message == "off") {
      digitalWrite(LED_BUILTIN, 1);
      client.publish("@shadow/data/update", "{\"data\":{\"led\":\"off\"}}");
      Serial.println("LED OFF");
    }
  } else if (String(topic) == "@msg/reset") {
    if (message == "True") ESP.reset();
  } else if (String(topic) == "@msg/sendPeriod") {
    if (message == "1") {
      send_period = 60 * 1;
      client.publish("@shadow/data/update", "{\"data\":{\"sendPeriod\":\"1\"}}");
      Serial.println("Send data period is set to 1 minute");
    } else if (message == "5") {
      send_period = 60 * 5;
      client.publish("@shadow/data/update", "{\"data\":{\"sendPeriod\":\"5\"}}");
      Serial.println("Send data period is set to 5 minutes");
    } else if (message == "10") {
      send_period = 60 * 10;
      client.publish("@shadow/data/update", "{\"data\":{\"sendPeriod\":\"10\"}}");
      Serial.println("Send data period is set to 10 minutes");
    }
  }
}
/*
  display.ssd1306_command(SSD1306_DISPLAYOFF); // To switch display off
  display.ssd1306_command(SSD1306_DISPLAYON); // To switch display back on
*/
void wifiConfigDisplay() {
  display.clearDisplay();
  display.setTextColor(WHITE, BLACK);
  display.setCursor(0, 10);
  display.setTextSize(2);
  display.println(" WIFI");
  display.println(" CONFIG...");
  display.drawRect(0, 0, 120, 50, WHITE);
  display.display();
  display.startscrollright(0x00, 0x07);
  delay(2000);
  display.stopscroll();
  delay(1000);
  display.startscrollleft(0x00, 0x07);
  delay(2000);
  display.stopscroll();
  delay(1000);
  display.startscrolldiagright(0x00, 0x07);
  delay(2000);
  display.startscrolldiagleft(0x00, 0x07);
  delay(2000);
  display.stopscroll();
}

void oledDisplay(float temp, float pres, float hum, uint8_t oledMode) {
  display.clearDisplay();
  display.setTextColor(WHITE, BLACK);
  display.setCursor(0, 0);
  display.setTextSize(1);
  char wifi_status, mqtt_status;
  WiFi.status() == WL_CONNECTED ? wifi_status = 'Y' : wifi_status = 'N';
  client.connected() ? mqtt_status = 'Y' : mqtt_status = 'N';

  display.clearDisplay();
  display.setTextColor(WHITE, BLACK);
  display.setCursor(0, 0);
  display.setTextSize(1);
  switch (oledMode) {
    case 0:
      display.print("BME280");
      display.print(" WIFI:");
      display.print(wifi_status);
      display.print(" MQTT:");
      display.println(mqtt_status);
      display.println();
      display.print("Temp: ");
      display.setTextSize(2);
      display.print(temp);
      display.cp437(true);
      display.write(167);
      display.println("C");
      display.setTextSize(1);
      display.print("Pres: ");
      display.setTextSize(2);
      display.print(pres);
      display.println();
      display.setTextSize(1);
      display.print("Humi: ");
      display.setTextSize(2);
      display.print(hum);
      display.println(" %");
      display.setCursor(0, 40);
      display.setTextSize(1);
      display.print("(hPa)");
      display.display();
      break;
    case 1:
      display.cp437(true);
      display.print("PMS7003");
      display.print(" WIFI:");
      display.print(wifi_status);
      display.print(" MQTT:");
      display.println(mqtt_status);
      display.println();
      display.print("PM1:  ");
      display.setTextSize(2);
      display.print(pms.pm1Value);
      display.write(230);
      display.println("g/m3");
      display.setTextSize(1);
      display.print("PM2.5:");
      display.setTextSize(2);
      display.print(pms.pm25Value);
      display.write(230);
      display.println("g/m3");
      display.setTextSize(1);
      display.print("PM10: ");
      display.setTextSize(2);
      display.print(pms.pm10Value);
      display.write(230);
      display.println("g/m3");
      display.display();
      break;
  }
}

void startWifiConfig() {
  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  WiFiManagerParameter custom_mqtt_client("client", "MQTT client", mqtt_client, 40);
  WiFiManagerParameter custom_mqtt_mqtt_username("Username", "Username", mqtt_username, 40);
  WiFiManagerParameter custom_mqtt_password("Password", "Password", mqtt_password, 40);

  wifiManager.setTimeout(300);
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setShowInfoUpdate(false);
  wifiManager.addParameter(&custom_mqtt_client);
  wifiManager.addParameter(&custom_mqtt_mqtt_username);
  wifiManager.addParameter(&custom_mqtt_password);
  wifiManager.setBreakAfterConfig(true);
  wifiManager.setConnectTimeout(30);

  if (!wifiManager.startConfigPortal()) {
    Serial.println(F("# failed to connect or hit timeout"));
  }
}
void saveConfigCallback() {
  Serial.println("# Should save config");
  shouldSaveConfig = true;
}
