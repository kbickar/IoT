#include <Servo.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>

#include "config.h"

#define SWITCH_PIN D1
#define SWITCH_PIN_G D2
#define BUTTON_PIN_G D3
#define BUTTON_PIN   D4
#define SERVO_PIN D7

#define UPDATE_WINDOW     5*60*1000 // 5 minute
#define RESET_TIME      7*24*60*60*1000+60*1000 // 1 week-ish

#define MAX_RECONNECTS  5
#define LONG_RECONNECT_DELAY    5*60*1000
#define RECONNECT_DELAY         5*1000
#define RSSI_REPORT_DELAY         60*1000
#define ON  1
#define OFF 0
#define LEFT 100
#define RIGHT 85

#define STATE_UNKNOWN   0
#define STATE_OPEN      1
#define STATE_CLOSED_R  2
#define STATE_CLOSED_L  3

String S = "";
const char* mqtt_server = MQTT_HOST;
long lastReconnectAttempt = 0;

char resetTopic[38];
char debugTopic[38];
char statusTopic[39];
char signalTopic[39];

char blindStateTopic[35];
char blindSetTopic[40];
char blindTiltStateTopic[40];
char blindTiltSetTopic[40];

int middleTimeLeft = 0;
int middleTimeRight = 0;
int state = STATE_UNKNOWN;

Servo myservo;

// One time sets
const char compile_date[] = __DATE__ " " __TIME__;
unsigned long start_time;
unsigned long rssi_report_time;

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

void report_rssi(){
    String signal = S+WiFi.RSSI();
    mqttClient.publish(signalTopic, signal.c_str()); 
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
  pinMode(BUTTON_PIN, INPUT);
  pinMode(SWITCH_PIN_G, OUTPUT);
  pinMode(BUTTON_PIN_G, OUTPUT);
  digitalWrite(SWITCH_PIN_G, LOW); 
  digitalWrite(BUTTON_PIN_G, LOW);  
  sprintf(resetTopic,          "home/%s/blinds/%d/reset", ROOM, NUM);
  sprintf(debugTopic,          "home/%s/blinds/%d/debug", ROOM, NUM);
  sprintf(statusTopic,         "home/%s/blinds/%d/status", ROOM, NUM);
  sprintf(signalTopic,         "home/%s/blinds/%d/signal", ROOM, NUM);
  sprintf(blindStateTopic,     "home/%s/blinds/%d/state", ROOM, NUM);
  sprintf(blindSetTopic,       "home/%s/blinds/%d/set", ROOM, NUM);
  sprintf(blindTiltStateTopic, "home/%s/blinds/%d/tilt/state", ROOM, NUM);
  sprintf(blindTiltSetTopic,   "home/%s/blinds/%d/tilt/set", ROOM, NUM);
  statuses_sent = false;
  
  reconnectCount = 0;
  rssi_report_time = 0;

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

void checkButton(){
  if (digitalRead(BUTTON_PIN) == LOW){
    if (state == STATE_OPEN) right();
    else middle();
  }
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
  middleTimeLeft =  int((cross_time)/2);  
  
  starting_time = millis();
  moveServo(RIGHT);
  freeServo(LEFT);
  cross_time = millis() - starting_time;
  debug(S+"Cross time: "+cross_time);
  debug(S+"Middle time: "+int((cross_time)/2));
  middleTimeRight =  int((cross_time)/2);  
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
  state = STATE_CLOSED_L;
}

void middle(){ 
  if (middleTimeLeft == 0){
    findMiddle();
  }
  if (state == STATE_CLOSED_L){
    moveServo(LEFT);
    freeServo(RIGHT);
    moveServo(RIGHT, middleTimeRight);
  }else if (state == STATE_CLOSED_R || state == STATE_UNKNOWN){
    moveServo(RIGHT);
    freeServo(LEFT);
    moveServo(LEFT, middleTimeLeft);
  }
  mqttClient.publish(blindStateTopic, "OPEN");
  mqttClient.publish(blindTiltStateTopic, "2");
  state = STATE_OPEN;
}

void right(){
  moveServo(RIGHT);
  freeServo(LEFT);
  mqttClient.publish(blindStateTopic, "CLOSED");
  mqttClient.publish(blindTiltStateTopic, "3");
  state = STATE_CLOSED_R;
}

void loop() {
  checkButton();
  
  // Handle OTA server.
  if  (millis() - start_time < UPDATE_WINDOW){
    ArduinoOTA.handle();
  }

  // Reset once a day
  if  (millis() - start_time > RESET_TIME){
    reset();
  }
  
  // Report signal strength once a minute
  if  (millis() - rssi_report_time > RSSI_REPORT_DELAY){
    rssi_report_time = millis();
    report_rssi();
  }
  
  
  long reconnectDelay = RECONNECT_DELAY;
  if (reconnectCount > MAX_RECONNECTS){
    reconnectDelay = LONG_RECONNECT_DELAY;
  }  
  if (!mqttClient.connected()) {
    long now = millis();
    if (now - lastReconnectAttempt > reconnectDelay) {
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
