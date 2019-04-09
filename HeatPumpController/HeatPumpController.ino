
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <SoftwareSerial.h>
#include <MitsubishiHeatpumpIR.h>

#include "config.h"
    
// Constants

#define IR_PIN          D7 

#define UPDATE_WINDOW     5*60*1000 // 5 minute
#define RESET_TIME      24*60*60*1000+60*1000 // 1 day-ish

#define MAX_RECONNECTS  5
#define ON  1
#define OFF 0
String S = "";
const char* mqtt_server = MQTT_HOST;
long lastReconnectAttempt = 0;

char resetTopic[38];
char debugTopic[38];
char statusTopic[39];
char temp_command_topic[44];
char temp_state_topic[50];
char mode_command_topic[37];
char mode_state_topic[43];
char fan_command_topic[41];
char fan_state_topic[47];

IRSenderBitBang irSender(IR_PIN);   
HeatpumpIR *heatpumpIR = new MitsubishiFDHeatpumpIR();  

// One time sets
const char compile_date[] = __DATE__ " " __TIME__;
unsigned long start_time;

String host_name(HOSTNAME_PREF);

ESP8266WiFiMulti WiFiMulti;
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// State variables
bool statuses_sent = false;

int reconnectCount = 0;

int power = POWER_OFF;
int temperature = 24;
int mode = MODE_AUTO;
String sMode = "False";
int fan = FAN_AUTO;
String sFan = "auto";

// Debugging
bool debugMode = true;
void debug(String s)
{
  if (!debugMode) return;
  Serial.println(s);
  WiFiUDP udpClient;
  s = S+HOSTNAME_PREF+" "+s;
  udpClient.beginPacket(DEBUG_HOST, DEBUG_PORT);
  udpClient.write(s.c_str(), s.length() + 1);
  udpClient.endPacket();
  yield();
}

void reset()
{
  debug(S+"Restarting... (Heap="+ESP.getFreeHeap()+", Cycles="+ESP.getCycleCount()+")");
  delay(2);
  ESP.restart();
}

bool reconnect() {
  debug("Attempting MQTT connection ["+host_name+"] ...");
  // Attempt to connect
  if (mqttClient.connect(host_name.c_str(), MQTT_USER, MQTT_PASS, statusTopic, 0, 1, "offline")) {
    // Once connected, resubscribe
    mqttClient.subscribe(resetTopic);
    mqttClient.subscribe(debugTopic);    
    mqttClient.subscribe(mode_command_topic);    
    mqttClient.subscribe(fan_command_topic);    
    mqttClient.subscribe(temp_command_topic);    
    mqttClient.subscribe(statusTopic);      
    mqttClient.publish(statusTopic, "online");  
    if (sMode != "off"){
      mqttClient.publish(mode_state_topic, sMode.c_str());    
      mqttClient.publish(fan_state_topic, sFan.c_str());
      String temp = S+temperature;
      mqttClient.publish(temp_state_topic, temp.c_str());      
    }
    debug("connected");
    return mqttClient.connected();
  }
  return false;
}


// Handle mqtt events I'm subscribed to
void callback(char* topic, byte* payload, unsigned int length) {
  String s = S+"Message arrived ["+topic+"] " ;
  String pl = "";
  for (int i = 0; i < length; i++) {
    pl += (char)payload[i];
  }
  debug(s+pl);

  if (strcmp(topic, mode_command_topic) == 0){
    power = POWER_ON;
    if(pl == "False" || pl == "off"){  
      power = POWER_OFF;
      pl = "off";
    }else if(pl == "auto") {
      mode = MODE_AUTO;
    }else if(pl == "heat") {
      mode = MODE_HEAT;
    }else if(pl == "cool") {
      mode = MODE_COOL;
    }else if(pl == "dry") {
      mode = MODE_DRY;
    }else if(pl == "fan") {
      mode = MODE_FAN;
    }
    sMode = pl;
    heatpumpIR->send(irSender, power, mode, fan, temperature, VDIR_AUTO, HDIR_AUTO);  
    mqttClient.publish(mode_state_topic, sMode.c_str());    
  }else if (strcmp(topic, fan_command_topic) == 0){
    if(pl == "auto"){  
      fan = FAN_AUTO;
    }else if(pl == "low") {
      fan = FAN_1;
    }else if(pl == "medium") {
      fan = FAN_2;
    }else if(pl == "high") {
      fan = FAN_4;
    }
    sFan = pl;
    heatpumpIR->send(irSender, power, mode, fan, temperature, VDIR_AUTO, HDIR_AUTO);  
    mqttClient.publish(fan_state_topic, sFan.c_str());
  }else if (strcmp(topic, temp_command_topic) == 0){
    temperature = (int) pl.toFloat(); 
    heatpumpIR->send(irSender, power, mode, fan, temperature, VDIR_AUTO, HDIR_AUTO);  
    String temp = S+temperature;
    mqttClient.publish(temp_state_topic, temp.c_str());
  // Reset
  }else if (strcmp(topic, resetTopic) == 0){
    reset();
  }
  // Debug
  else if (strcmp(topic, debugTopic) == 0){
    debugMode = (pl == "1");
  }
  // Status
  else if (strcmp(topic, statusTopic) == 0){
    if(pl != "online") mqttClient.publish(statusTopic, "online");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Hello!");
  
  sprintf(resetTopic,         "home/%s/heat_pump/reset", ROOM);
  sprintf(debugTopic,         "home/%s/heat_pump/debug", ROOM);
  sprintf(statusTopic,        "home/%s/heat_pump/status", ROOM);
  sprintf(temp_command_topic, "home/%s/heat_pump/temperature", ROOM);
  sprintf(temp_state_topic,   "home/%s/heat_pump/temperature/state", ROOM);
  sprintf(mode_command_topic, "home/%s/heat_pump/mode", ROOM);
  sprintf(mode_state_topic,   "home/%s/heat_pump/mode/state", ROOM);
  sprintf(fan_command_topic,  "home/%s/heat_pump/fan_mode", ROOM);
  sprintf(fan_state_topic,    "home/%s/heat_pump/fan_mode/state", ROOM);
  
  statuses_sent = false;
  
  reconnectCount = 0;

  // Set Hostname.
  host_name += String(ESP.getChipId(), HEX);
  WiFi.hostname(host_name);
  WiFiMulti.addAP(WIFI_SSID, WIFI_PASS);

  Serial.print("Wait for WiFi...");

  while (WiFiMulti.run() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  debug("");
  Serial.println("WiFi connected");
  debug("Compile Date:");
  debug(compile_date);
  debug(S + "Chip ID: 0x" + String(ESP.getChipId(), HEX));
  debug("Hostname: " + host_name);
  debug("IP address: " + WiFi.localIP().toString());

  // Start OTA server.
  ArduinoOTA.setHostname((const char *)host_name.c_str());
  ArduinoOTA.begin();
  
  mqttClient.setServer(mqtt_server, 1883);
  mqttClient.setCallback(callback);
}

void loop() {
  
  // Handle OTA server.
  if  (millis() - start_time < UPDATE_WINDOW){
    ArduinoOTA.handle();
  }

  // Reset once a day
  if  (millis() - start_time > RESET_TIME){
    reset();
  }
  
  if (!mqttClient.connected()) {
    long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      reconnectCount++;
      // Attempt to reconnect
      if (reconnect()) {
        lastReconnectAttempt = 0;
        reconnectCount = 0;
      }
    }
  } else {
    // Client connected
    mqttClient.loop();
  }
}
