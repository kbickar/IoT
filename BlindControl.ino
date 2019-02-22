#include <Servo.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>

#include "config.h"

#define SWITCH_PIN D1
#define SWITCH_PIN_G D2
#define SERVO_PIN D7

#define UPDATE_WINDOW     5*60*1000 // 5 minute
#define RESET_TIME      24*60*60*1000+60*1000 // 1 day-ish

#define MAX_RECONNECTS  5
#define ON  1
#define OFF 0
#define LEFT 100
#define RIGHT 85
String S = "";
const char* mqtt_server = MQTT_HOST;
long lastReconnectAttempt = 0;

char resetTopic[38];
char debugTopic[38];
char statusTopic[39];

char blindStateTopic[35];
char blindSetTopic[40];
char blindTiltStateTopic[40];
char blindTiltSetTopic[40];

int middleTime = 0;

Servo myservo;

// One time sets
const char compile_date[] = __DATE__ " " __TIME__;
unsigned long start_time;

String host_name(HOSTNAME_PREF);

ESP8266WiFiMulti WiFiMulti;
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// State variables
bool statuses_sent = false;
int lampOnVal = 0;

int reconnectCount = 0;

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
    mqttClient.subscribe(statusTopic);  
    mqttClient.subscribe(blindSetTopic);
    mqttClient.subscribe(blindTiltSetTopic); 
    mqttClient.publish(statusTopic, "online");  
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
  
  // Set open/closed
  if (strcmp(topic, blindSetTopic) == 0){
    if(pl == "OPEN"){  
      middle();
    }
    else if(pl =="CLOSE") {
      right();
    }
  // Set Tilt
  }else if (strcmp(topic, blindTiltSetTopic) == 0){
    if(pl == "1"){  
      left();
    }
    else if(pl =="2") {
      middle();
    }
    else if(pl =="3") {
      right();
    }
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
  pinMode(SWITCH_PIN, INPUT);
  pinMode(SWITCH_PIN_G, OUTPUT);
  digitalWrite(SWITCH_PIN_G, LOW);  
  sprintf(resetTopic,          "home/%s/blinds/reset", ROOM);
  sprintf(debugTopic,          "home/%s/blinds/debug", ROOM);
  sprintf(statusTopic,         "home/%s/blinds/status", ROOM);
  sprintf(blindStateTopic,     "home/%s/blinds/state", ROOM);
  sprintf(blindSetTopic,       "home/%s/blinds/set", ROOM);
  sprintf(blindTiltStateTopic, "home/%s/blinds/tilt/state", ROOM);
  sprintf(blindTiltSetTopic,   "home/%s/blinds/tilt/set", ROOM);
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
  myservo.attach(SERVO_PIN); 
  myservo.write(90);
  
}

int findMiddle(){
  debug(S+"Finding middle...");
  moveServo(RIGHT);
  freeServo(LEFT);
  long starting_time = millis();
  moveServo(LEFT);
  freeServo(RIGHT); 
  long cross_time = millis() - starting_time;
  debug(S+"Cross time: "+cross_time);
  debug(S+"Middle time: "+int((cross_time)/2));
  return int((cross_time)/2);  
}

void freeServo(int dir){
  myservo.write(dir);
  debug(S+"Free "+dir);
  for(int i = 0; i < 400; i++){
    if(digitalRead(SWITCH_PIN) == HIGH)break;
    delay(1);
  }  
  myservo.write(90); 
}

void moveServo(int dir){  
  moveServo(dir, 5000);
}

void moveServo(int dir, int time){  
  debug(S+"Move "+dir+", "+time);
  myservo.write(dir);
  for(int i = 0; i < time; i++){
    if(digitalRead(SWITCH_PIN) == LOW)break;
    delay(1);
  }  
  debug(S+"Move Done "+dir);
  myservo.write(90);  
}

void left(){ 
  moveServo(LEFT); 
  freeServo(RIGHT);
  mqttClient.publish(blindStateTopic, "CLOSED");
  mqttClient.publish(blindTiltStateTopic, "1");
}

void middle(){ 
  if (middleTime == 0){
    middleTime = findMiddle();
  }
  moveServo(RIGHT);
  freeServo(LEFT);
  moveServo(LEFT, middleTime);
  mqttClient.publish(blindStateTopic, "OPEN");
  mqttClient.publish(blindTiltStateTopic, "2");
}

void right(){
  moveServo(RIGHT);
  freeServo(LEFT);
  mqttClient.publish(blindStateTopic, "CLOSED");
  mqttClient.publish(blindTiltStateTopic, "3");
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
