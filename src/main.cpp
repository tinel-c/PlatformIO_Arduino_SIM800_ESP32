/*

Enclosure conditions test via TCALL SIM800 & BME280

 */

#define TINY_GSM_MODEM_SIM800
#define TINY_GSM_RX_BUFFER 1024 // Set RX buffer to 1Kb
//#define DUMP_AT_COMMANDS

#include <WiFi.h>
#include "TinyGsmClient.h"
#include "ThingsBoard.h"
#include "SparkFunBME280.h"
#include "utilities.h"
#include "driver/adc.h"
#include <esp_wifi.h>
#include <time.h>
#include "passwords.h"

#define uS_TO_S_FACTOR 1000000 /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP 40       /* Time ESP32 will go to sleep (in seconds) */
RTC_DATA_ATTR int bootCount = 0;

#define PIN_TX 25
#define PIN_RX 26

HardwareSerial serialGsm(1);
#define SerialAT serialGsm
#define TINY_GSM_USE_GPRS true
#define TINY_GSM_USE_WIFI false

// Your GPRS credentials
// Leave empty, if missing user or pass
const char apn[] = "net";
const char user[] = "";
const char pass[] = "";

// TTGO T-Call pin definitions
#define MODEM_RST 5       // SIM800 RESET but also IP5306 IRQ: use IRQ Analyzing signals IP5306 It is in working condition or in standby mode: IRQ = 1 Work, IRQ = 0 When in standby
#define MODEM_PWKEY 4     // PWRKEY SIM800
#define MODEM_POWER_ON 23 // EN SY8089 4v4 regulator for SIM800
#define MODEM_TX 27
#define MODEM_RX 26
#define I2C_SDA 21
#define I2C_SCL 22

#define ADC_BAT 35 // TCALL 35
int bat_mv = 0;

BME280 mySensor;
bool isEnvSensor = true;
void shutdown();
void getBatteryFromADC();

#ifdef DUMP_AT_COMMANDS
#include "StreamDebugger.h"
StreamDebugger debugger(serialGsm, Serial);
TinyGsm modem(debugger);
#else
TinyGsm modem(serialGsm);
#endif

// See https://thingsboard.io/docs/getting-started-guides/helloworld/
// to understand how to obtain an access token

#define TOKEN "BQhPIpAfNt9IN5eYGJP5" // Thingsboard token
 
#define THINGSBOARD_SERVER "mqtt.thingsboard.cloud" // Thingsboard server

// Baud rate for debug serial
#define SERIAL_DEBUG_BAUD 115200

// Initialize GSM client
TinyGsmClient client(modem);

// Initialize ThingsBoard instance
ThingsBoard tb(client);

// Set to true, if modem is connected
bool modemConnected = false;

//SMS Intitalization
String smsText = "Init";
bool smsReceived = 0;
bool smsSent = 1;

// NTP client for date and time
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = 3600;


WiFiClient espClient;
PubSubClient pubClient(espClient);
long lastMsg = 0;
char msg[50];
int value = 0;

void GSM_ON(uint32_t time_delay)
{
  // Set-up modem reset, enable, power pins
  pinMode(MODEM_PWKEY, OUTPUT);
  pinMode(MODEM_POWER_ON, OUTPUT);
  pinMode(MODEM_RST, OUTPUT);

  Serial.println("MODEM_RST & IP5306 IRQ: HIGH"); // IP5306 HIGH
  digitalWrite(MODEM_RST, HIGH);
  delay(time_delay);

  Serial.println("MODEM_PWKEY: HIGH");
  digitalWrite(MODEM_PWKEY, HIGH); // turning modem OFF
  delay(time_delay);

  Serial.println("MODEM_POWER_ON: HIGH");
  digitalWrite(MODEM_POWER_ON, HIGH); //Enabling SY8089 4V4 for SIM800 (crashing when in battery)
  delay(time_delay);

  Serial.println("MODEM_PWKEY: LOW");
  digitalWrite(MODEM_PWKEY, LOW); // turning modem ON
  delay(time_delay);
}

void GSM_OFF()
{
  pinMode(MODEM_PWKEY, OUTPUT);
  pinMode(MODEM_POWER_ON, OUTPUT);
  pinMode(MODEM_RST, OUTPUT);

  digitalWrite(MODEM_PWKEY, HIGH);   // turn of modem in case its ON from previous state
  digitalWrite(MODEM_POWER_ON, LOW); // turn of modem psu in case its from previous state
  digitalWrite(MODEM_RST, HIGH);     // Keep IRQ high ? (or not to save power?)
}

void printLocalTime(){
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  Serial.print("Day of week: ");
  Serial.println(&timeinfo, "%A");
  Serial.print("Month: ");
  Serial.println(&timeinfo, "%B");
  Serial.print("Day of Month: ");
  Serial.println(&timeinfo, "%d");
  Serial.print("Year: ");
  Serial.println(&timeinfo, "%Y");
  Serial.print("Hour: ");
  Serial.println(&timeinfo, "%H");
  Serial.print("Hour (12 hour format): ");
  Serial.println(&timeinfo, "%I");
  Serial.print("Minute: ");
  Serial.println(&timeinfo, "%M");
  Serial.print("Second: ");
  Serial.println(&timeinfo, "%S");

  Serial.println("Time variables");
  char timeHour[3];
  strftime(timeHour,3, "%H", &timeinfo);
  Serial.println(timeHour);
  char timeWeekDay[10];
  strftime(timeWeekDay,10, "%A", &timeinfo);
  Serial.println(timeWeekDay);
  Serial.println();
  char currentTime[50];
  strftime(currentTime,50, "%A, %B %d %Y %H:%M:%S", &timeinfo);
  pubClient.publish("esp32SMS/Status", currentTime);
  if(smsReceived == 1) pubClient.publish("esp32SMS/smsReceive/timestamp", currentTime);
  if(smsSent == 1) pubClient.publish("esp32SMS/smsSend/timestamp", currentTime);
  smsReceived = 0;
  smsSent = 0;
}



void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void mqttCallback(char* topic, byte* message, unsigned int length) {
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");
  String messageTemp;
  String topicSmsNo = "esp32SMS/smsSend/to";
  String topicSmsText = "esp32SMS/smsSend/text";

  for (int i = 0; i < length; i++) {
    Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }
  Serial.println();
  if(topicSmsNo.compareTo(topic) == 0)
  {
    Serial.println("New number received on MQTT: " + messageTemp);
    smsNumber = messageTemp;
    smsSent = 1;
    printLocalTime();
  }

  if(topicSmsText.compareTo(topic) == 0)
  {
    Serial.println("New text received on MQTT: " + messageTemp);
    Serial.println(modem.sendSMS(String(smsNumber),messageTemp));
    smsSent = 1;
    printLocalTime();
  }
  // Feel free to add more if statements to control more GPIOs with MQTT
  

}

void reconnect() {
  // Loop until we're reconnected
  while (!pubClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (pubClient.connect("ESP32Client")) {
      Serial.println("connected");
      // Subscribe
      pubClient.subscribe("esp32SMS/smsSend/to");
      Serial.println("Subscribed to: esp32SMS/smsSend/to");
      pubClient.subscribe("esp32SMS/smsSend/text");
      Serial.println("Subscribed to: esp32SMS/smsSend/text");
    } else {
      Serial.print("failed, rc=");
      Serial.print(pubClient.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}


void setup()
{

  Serial.begin(115200);

  setup_wifi();
  pubClient.setServer(mqtt_server, 1883);
  pubClient.setCallback(mqttCallback);
  // Loop until we're reconnected
  while (!pubClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (pubClient.connect("ESP32Client")) {
      Serial.println("connected");
      // Subscribe
      pubClient.subscribe("esp32SMS/smsSend/to");
      Serial.println("Subscribed to: esp32SMS/smsSend/to");
      pubClient.subscribe("esp32SMS/smsSend/text");
      Serial.println("Subscribed to: esp32SMS/smsSend/text");
    } else {
      Serial.print("failed, rc=");
      Serial.print(pubClient.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
  Serial.println("WiFi connected!");

  GSM_ON(1000);

  // Set GSM module baud rate and UART pins
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);

  // Init and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  printLocalTime();
  // Empty sms buffer
  while(modem.emptySMSBuffer() == 0)
  {
    Serial.println("Wait for the SMS buffer to empty!");
    delay(1000);
  }
   
}

void loop()
{
  if (!pubClient.connected()) {
    reconnect();
    Serial.println("WiFi connected!");
  }
  char bootCountString[8];
  bootCount++;
 
  pubClient.loop();
  int index=modem.newMessageIndex(0);
  if(index>0){
    String SMS=modem.readSMS(index);
    String ID=modem.getSenderID(index);
    Serial.println("new message arrived from :");
    pubClient.publish("esp32SMS/smsReceive/from", ID.c_str());
    Serial.println(ID);
    Serial.println("Says");
    Serial.println(SMS);
    pubClient.publish("esp32SMS/smsReceive/text", SMS.c_str());
    smsReceived = 1;
    printLocalTime();
    while(modem.emptySMSBuffer() == 0)
    {
      Serial.println("Wait for the SMS buffer to empty!");
      delay(1000);
    }
  }

 /*  if (!modemConnected)
  {
    Serial.print(F("Waiting for network..."));
    if (!modem.waitForNetwork(2400L))
    {
      Serial.println(" fail");
      shutdown();
    }
    Serial.println(" OK");

    Serial.print("Signal quality:");
    Serial.println(modem.getSignalQuality());

    Serial.print(F("Connecting to "));
    Serial.print(apn);
    if (!modem.gprsConnect(apn, user, pass))
    {
      Serial.println(" fail");
      shutdown();
    }

    

    modemConnected = true;
    Serial.println(" OK");
  } */
}

void shutdown()
{
  modemConnected = false;
  Serial.println(F("GPRS disconnect"));
  modem.gprsDisconnect();

  Serial.println("Radio off");
  modem.radioOff();

  Serial.println("GSM power off");
  GSM_OFF();
}

