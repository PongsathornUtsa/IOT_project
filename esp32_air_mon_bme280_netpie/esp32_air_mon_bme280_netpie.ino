#include <PMS.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Ticker.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <PubSubClient.h>

#define BUILD_VERSION 20221211  //YYYYMMDD Pongsathorn Utsahawattanasuk 6210554784

//------------------------------Command------------------------------
String cmdstring;
String parmstring;
int sepIndex;
bool noparm = 0;

//------------------------------WiFi/MQTT setup------------------------------
const char* mqtt_server = "broker.netpie.io";
const int mqtt_port = 1883;
char mqtt_client[40] = "";
char mqtt_username[40] = "";
char mqtt_password[40] = "";

WiFiClient espClient;
PubSubClient client(espClient);
bool shouldSaveConfig = false;
uint8_t macAddr[6];
//------------------------------BME280------------------------------
Adafruit_BME280 bme;  
float temp, pres, hum;
//------------------------------OLED------------------------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

//------------------------------eeprom------------------------------
int eeinitflag = 0;  // EEPROM initialization flag
const int EEINITCODE = 7;
#define EE_INIT 200  // write EEINITCODE this address if EEPROM is initialized
//------------------------------EEPROM addresses ------------------------------
#define EE_MQTTCID_ADDR 0
#define EE_MQTTUSER_ADDR 40
#define EE_MQTTPWD_ADDR 80
bool eesaveflag = 0;

//------------------------------pms------------------------------
PMS pms(Serial1);  //GPIO1,GPIO3
PMS::DATA data;
uint8_t pm1, pm2_5, pm10;
//------------------------------timer------------------------------
uint8_t is_1s = 0, timeOut = 0;
unsigned int count_1s = 0;
char msg[100];
unsigned int send_period = 60;
Ticker ticker;
//------------------------------btn------------------------------
#define TRIGGER_PIN 0  // NodeMCU FLASH button for start wifi config portal
unsigned int btn_push_sec = 0, btn_push_loop = 0;
#define WIFI_CFG_BTN_PUSH_SEC 5
uint8_t pressed = 0;
boolean buttonState = LOW;
#define LED 2

void tick_1s() {
  is_1s = 1;
  count_1s++;
  timeOut++;
}

void configModeCallback(WiFiManager* myWiFiManager) {
  Serial.println("# Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
}

//------------------------------FreeRtos:/------------------------------

TaskHandle_t Task0 = NULL;

void Task0_code(void* parameter) {  //core 0
  for (;;) {
    temp = bme.readTemperature();
    hum = bme.readHumidity();
    pres = bme.readPressure() / 100.0F;
    vTaskDelay(10);
  }
  vTaskDelete(NULL);
}

TaskHandle_t Task1 = NULL;

void Task1_code(void* parameter) {  //core 0
  for (;;) {
    pms.read(data);
    pm1 = data.PM_AE_UG_1_0;
    pm2_5 = data.PM_AE_UG_2_5;
    pm10 = data.PM_AE_UG_10_0;
    vTaskDelay(5);
  }
  vTaskDelete(NULL);
}

TaskHandle_t Task2 = NULL;

void Task2_code(void* parameter) {  //core 0
  for (;;) {
    oledDisplay(temp, pres, hum, pressed);
    vTaskDelay(1000);
  }
  vTaskDelete(NULL);
}

void freertos_init(void) {
  xTaskCreatePinnedToCore(Task0_code, "bme280", 10000, NULL, 3, &Task0, 0);
  xTaskCreatePinnedToCore(Task1_code, "pms7003", 10000, NULL, 2, &Task1, 0);
  xTaskCreatePinnedToCore(Task2_code, "oled", 10000, NULL, 1, &Task2, 0);
}

void setup() {
  Serial.begin(9600);   // for debug GPIO1,3
  Serial1.begin(9600);  //for pms GPIO2 or D4
  //EEPROM
  EEPROM.begin(256);
  eeinitflag = EEPROM.read(EE_INIT);
  if (eeinitflag == EEINITCODE) {
    Serial.println("Reading parameters from EEPROM");
    EEPROM_getparms();  // get parameters from EEPROM
  }

  ticker.attach(1, tick_1s);
  Serial.println("Strarting...");
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
  pinMode(LED, OUTPUT);

  //wifi
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  delay(1000);
  WiFi.printDiag(Serial);

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
  //mqtt
  display.println(mqtt_server);
  display.print(F("Port:"));
  display.println(mqtt_port);
  display.display();
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

  freertos_init();
}
// core1
void loop() {
  if (is_1s) {
    if (!client.connected()) {
      reconnect();
    }
    client.loop();
    is_1s = 0;
    if (timeOut % send_period == 0) {  //every 1,5,10 min
      //----------------------------------------
      //idea: https://arduinojson.org/book/serialization_tutorial6.pdf#page=12
      StaticJsonDocument<200> doc;

      JsonObject data = doc.createNestedObject("data");

      data["pm1"] = pm1;
      data["pm2_5"] = pm2_5;
      data["pm10"] = pm10;

      data["humidity"] = hum;
      data["pressure"] = pres;
      data["temperature"] = temp;

      char buffer[256];
      serializeJson(doc, buffer);
      client.publish("@shadow/data/update", buffer);
      Serial.print("publish: ");
      serializeJson(doc, Serial);
      Serial.println();
    }
  }
  InputCommand();
  delay(5);
}

void InputCommand() {
  eesaveflag = 0;
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    if (input.equalsIgnoreCase("wifiPortal")) {
      startWifiConfig();
    }
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
      EEPROM_saveparms();
      Serial.println("Save config");
    }
  }
}

// save parameters to EEPROM
void EEPROM_saveparms(void) {
  EEPROM.put(EE_MQTTCID_ADDR, mqtt_client);
  EEPROM.put(EE_MQTTUSER_ADDR, mqtt_username);
  EEPROM.put(EE_MQTTPWD_ADDR, mqtt_password);

  if (eeinitflag != EEINITCODE) EEPROM.write(EE_INIT, EEINITCODE);
  delay(10);
  EEPROM.commit();
  EEPROM.end();
  Serial.println("Parameters saved to EEPROM.");
  Serial.println("Press reset button.");
  eesaveflag = 1;
}

void EEPROM_getparms(void) {
  EEPROM.get(EE_MQTTCID_ADDR, mqtt_client);
  EEPROM.get(EE_MQTTUSER_ADDR, mqtt_username);
  EEPROM.get(EE_MQTTPWD_ADDR, mqtt_password);
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting NETPIE2020 connection???");
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
      digitalWrite(LED, 0);
      client.publish("@shadow/data/update", "{\"data\":{\"led\":\"on\"}}");
      Serial.println("LED ON");
    } else if (message == "off") {
      digitalWrite(LED, 1);
      client.publish("@shadow/data/update", "{\"data\":{\"led\":\"off\"}}");
      Serial.println("LED OFF");
    }
  } else if (String(topic) == "@msg/reset") {
    if (message == "True") ESP.restart();
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
      display.print(data.PM_AE_UG_1_0);
      display.write(230);
      display.println("g/m3");
      display.setTextSize(1);
      display.print("PM2.5:");
      display.setTextSize(2);
      display.print(data.PM_AE_UG_2_5);
      display.write(230);
      display.println("g/m3");
      display.setTextSize(1);
      display.print("PM10: ");
      display.setTextSize(2);
      display.print(data.PM_AE_UG_10_0);
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
  //  WiFiManagerParameter custom_mqtt_client("client", "MQTT client", mqtt_client, 40);
  //  WiFiManagerParameter custom_mqtt_mqtt_username("Username", "Username", mqtt_username, 40);
  //  WiFiManagerParameter custom_mqtt_password("Password", "Password", mqtt_password, 40);

  wifiManager.setTimeout(300);
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setShowInfoUpdate(false);
  //  wifiManager.addParameter(&custom_mqtt_client);
  //  wifiManager.addParameter(&custom_mqtt_mqtt_username);
  //  wifiManager.addParameter(&custom_mqtt_password);
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
