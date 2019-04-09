
// Wemos D1 R2 & Mini
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <Nextion.h>
#include <NextionPage.h>
#include <NextionSlider.h>
#include <NextionButton.h>
#include <NextionNumber.h>
#include <SoftwareSerial.h>

#include "config.h"
    
// Constants

#define LIGHT_PIN       D1 
#define IR_PIN          D2 
#define MOTION_PIN      D5
#define LSENSOR_PIN     D6

#define UPDATE_WINDOW     5*60*1000 // 5 minute
#define RESET_TIME      24*60*60*1000+60*1000 // 1 day-ish

#define MAX_RECONNECTS  5
#define ON  1
#define OFF 0

#define LOW_LIGHT 500
#define MIN_LAMP_OFF_TIME 20*1000
const char* mqtt_server = MQTT_HOST;
long lastReconnectAttempt = 0;

const char* lightStateTopic    = "home/keilin_office/overhead_light/state";
const char* lightSetTopic      = "home/keilin_office/overhead_light/set";
const char* lampStateTopic     = "home/keilin_office/lamp/state";
const char* lampSetTopic       = "home/keilin_office/lamp/set";
const char* lampSetLevelTopic  = "home/keilin_office/lamp/set/level";

const char* resetTopic         = "home/keilin_office/reset";
const char* debugTopic         = "home/keilin_office/debug";
const char* statusTopic        = "home/keilin_office/status";
const char* temp_command_topic = "home/keilin_office/heat_pump/temperature";
const char* temp_state_topic   = "home/keilin_office/heat_pump/temperature/state";
const char* mode_command_topic = "home/keilin_office/heat_pump/mode";
const char* mode_state_topic   = "home/keilin_office/heat_pump/mode/state";
const char* fan_command_topic  = "home/keilin_office/heat_pump/fan_mode";
const char* fan_state_topic    = "home/keilin_office/heat_pump/fan_mode/state";
SoftwareSerial nextionSerial(D7, D4); // RX, TX

// One time sets
const char compile_date[] = __DATE__ " " __TIME__;
unsigned long start_time;

String host_name(HOSTNAME_PREF);

ESP8266WiFiMulti WiFiMulti;
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// State variables
bool statuses_sent = false;

int lightOnVal = 0;
int lampOnVal = 0;
unsigned long lampOffTime = 0;

int reconnectCount = 0;

volatile unsigned long cnt = 0;
unsigned long oldcnt = 0;
unsigned long t = 0;
unsigned long last;

int lightLevel = 0;
bool debugSensors = true;

int temperature = 24;
String sMode = "False";
String sFan = "auto";

// Nextion
Nextion nex(nextionSerial);
NextionSlider lightSlider(nex, 0, 2, "h0");
NextionButton lampButton(nex, 0, 1, "b0");
NextionButton overheadButton(nex, 0, 3, "b1");

NextionButton tempUpButton(nex, 1, 3, "b0");
NextionButton tempDownButton(nex, 1, 4, "b1");
NextionNumber tempValue(nex, 1, 11, "n0");
NextionButton coolButton(nex, 1, 7, "b4");
NextionButton heatButton(nex, 1, 8, "b5");
NextionButton dryButton(nex, 1, 9, "b6");
NextionButton fanButton(nex, 1, 10, "b7");
NextionSlider fanSlider(nex, 1, 6, "h0");


// Debugging
String S = "";
bool debugMode = true;
void debug(String s)
{
  if (!debugMode) return;
  Serial.println(s);
  WiFiUDP udpClient;
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

// Returns True if button is pressed on pin
boolean buttonPressed(int pin)
{
  int reading = digitalRead(pin);
  return reading == LOW;
}


bool reconnect() {
  debug("Attempting MQTT connection ["+host_name+"] ...");
  // Attempt to connect
  if (mqttClient.connect(host_name.c_str(), MQTT_USER, MQTT_PASS, statusTopic, 0, 1, "offline")) {
    // Once connected, resubscribe
    mqttClient.subscribe(lightSetTopic);
    mqttClient.subscribe(lampStateTopic);
    mqttClient.subscribe(resetTopic);
    mqttClient.subscribe(debugTopic);    
    mqttClient.subscribe(mode_command_topic);    
    mqttClient.subscribe(fan_command_topic);    
    mqttClient.subscribe(temp_command_topic);    
    debug("connected");
    mqttClient.publish(statusTopic, "online");
    return mqttClient.connected();
  }
  return false;
}

void irq1(){
  cnt++;
}

void switchLight(int val) {
  lightOnVal = val;
  digitalWrite(LIGHT_PIN, val);
  mqttClient.publish(lightStateTopic, (lightOnVal)?"ON":"OFF");  
}

void switchLamp(int val) {
  lampOnVal = val;
  mqttClient.publish(lampSetTopic, (lampOnVal)?"ON":"OFF");  
  if (!lampOnVal) lampOffTime = millis();
}

// Handle mqtt events I'm subscribed to
void callback(char* topic, byte* payload, unsigned int length) {
  String s = S+"Message arrived ["+topic+"] " ;
  String pl = "";
  for (int i = 0; i < length; i++) {
    pl += (char)payload[i];
  }
  debug(s+pl);

  // Turn on overhead light
  if (strcmp(topic, lightSetTopic) == 0){
    if(pl == "ON"){  
      if(!lightOnVal) switchLight(ON);
      else  mqttClient.publish(lightStateTopic, (lightOnVal)?"ON":"OFF");
    }
    else if(pl =="OFF") {
      if(lightOnVal) switchLight(OFF);
      else mqttClient.publish(lightStateTopic, (lightOnVal)?"ON":"OFF");
    }
  // Turn on desk light
  }else if (strcmp(topic, lampStateTopic) == 0){
    if(pl == "on"){  
      lampButton.setNumberProperty("pic", 0);
    }
    else if(pl =="off") {
      lampButton.setNumberProperty("pic", 1);
    }
    nex.refresh();
  }else if (strcmp(topic, mode_command_topic) == 0){
    coolButton.setNumberProperty("pic", 9);
    dryButton.setNumberProperty("pic", 10);
    heatButton.setNumberProperty("pic", 11);
    fanButton.setNumberProperty("pic", 12);
    if(pl == "heat") {
      heatButton.setNumberProperty("pic", 16);
    }else if(pl == "cool") {
      coolButton.setNumberProperty("pic", 13);
    }else if(pl == "dry") {
      dryButton.setNumberProperty("pic", 14);
    }else if(pl == "fan") {
      fanButton.setNumberProperty("pic", 15);
    }
    nex.refresh();
   
  }else if (strcmp(topic, fan_command_topic) == 0){
    if(pl == "auto"){  
      fanSlider.setNumberProperty("val", 2);
    }else if(pl == "low") {
      fanSlider.setNumberProperty("val", 2);
    }else if(pl == "medium") {
      fanSlider.setNumberProperty("val", 4);
    }else if(pl == "high") {
      fanSlider.setNumberProperty("val", 6);
    }
    nex.refresh();

  }else if (strcmp(topic, temp_command_topic) == 0){
    temperature = (int) pl.toFloat(); 
    tempValue.setNumberProperty("val", temperature);
    nex.refresh();
    
  // Reset
  }else if (strcmp(topic, resetTopic) == 0){
    reset();
  }
  // Debug
  else if (strcmp(topic, debugTopic) == 0){
    debugSensors = (pl == "2");
    debugMode = (pl == "1") || debugSensors;
  }
}

void checkLightLevel(){
  if (millis() - last >= 1000)
  {
    last = millis();
    t = cnt;
    unsigned long hz = t - oldcnt;
    if (debugSensors) debug(S+"Ambient Light: "+hz);
    lightLevel = hz;
    oldcnt = t;
  }  
}

void checkMotion(){
  int motion = digitalRead(MOTION_PIN);
  if (motion){
    if (millis() - lampOffTime > MIN_LAMP_OFF_TIME){
      nex.wake();
      if (debugSensors) debug(S+"Motion detected");
      lampOffTime = millis();
      if(!lampOnVal && lightLevel < LOW_LIGHT){
        switchLamp(ON);
      }
    }
  }
}

int getLevel(NextionSlider slider){
  int level = 0;
  for (int i=0;level == 0 && i<10;i++){
    delay(100);
    level = slider.getValue();
  }  
  return level;
}

void sliderCallback(NextionEventType type, INextionTouchable *widget)
{
  char val[4];
  sprintf(val, "%d", getLevel(lightSlider));
  debug(S+"Level: "+val);
  mqttClient.publish(lampSetLevelTopic, val); 
}

void lampCallback(NextionEventType type, INextionTouchable *widget)
{
  if (type == NEX_EVENT_PUSH)
  {
    delay(200);
    int level = getLevel(lightSlider);
    debug(S+"USlider: "+level);
    if (level < 10){
      debug("Turn OFF");
      switchLamp(OFF);
    }
    else{
      debug("Turn ON");
      switchLamp(ON); 
      mqttClient.publish(lampSetLevelTopic, "255"); 
    }
  }
}

void overheadCallback(NextionEventType type, INextionTouchable *widget)
{
  if (type == NEX_EVENT_PUSH)
  {
      debug("Toggle overhead");
      switchLight(!lightOnVal);
  }
}

void tempCallback(NextionEventType type, INextionTouchable *widget)
{
  if (type == NEX_EVENT_PUSH)
  {
    delay(200);
    int temp = 72;
    for (int i =0; i<10; i++){
      temp = tempValue.getValue();
      if (temp > 0) break;
      delay(100);
    }
    char buf[3];
    sprintf(buf, "%d", temp);
    mqttClient.publish(temp_command_topic, buf); 
  }
}

void fanCallback(NextionEventType type, INextionTouchable *widget)
{
  char * mode;
  if (type == NEX_EVENT_PUSH)
  {
    delay(200);
    int level = getLevel(fanSlider);
    debug(S+"Fan: "+level);
    switch(level){
      case 1:
      case 2:
        mode = "low";
        break;
      case 3:
      case 4:
        mode = "medium";
        break;
      default:
        mode = "high";
        break;
    }
    mqttClient.publish(fan_command_topic, mode); 
  }
}

void modeCallback(NextionEventType type, INextionTouchable *widget)
{
  char * mode;
  if (type == NEX_EVENT_PUSH)
  {
    delay(200);
    if (widget->getNumberProperty("pic") < widget->getNumberProperty("pic2")){
      mode = "off";
    }else if (widget->getComponentID() == 7){
      mode = "cool"; 
    }else if (widget->getComponentID() == 8){
      mode = "heat"; 
    }else if (widget->getComponentID() == 9){
      mode = "dry"; 
    }else if (widget->getComponentID() == 10){
      mode = "fan"; 
    }else{
      mode = "off";
    }
    mqttClient.publish(mode_command_topic, mode); 
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Hello!");

  pinMode(LIGHT_PIN, OUTPUT);
  pinMode(MOTION_PIN, INPUT);
  pinMode(LSENSOR_PIN, INPUT);
  digitalWrite(LSENSOR_PIN, HIGH);
  
  lightOnVal = false;
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
  
  nextionSerial.begin(9600);
  nex.init();
  lightSlider.attachCallback(&sliderCallback);
  lampButton.attachCallback(&lampCallback);
  overheadButton.attachCallback(&overheadCallback);
  tempUpButton.attachCallback(&tempCallback);
  tempDownButton.attachCallback(&tempCallback);
  coolButton.attachCallback(&modeCallback);
  heatButton.attachCallback(&modeCallback);
  dryButton.attachCallback(&modeCallback);
  fanButton.attachCallback(&modeCallback);
  fanSlider.attachCallback(&fanCallback);
  
  mqttClient.setServer(mqtt_server, 1883);
  mqttClient.setCallback(callback);
  attachInterrupt(digitalPinToInterrupt(LSENSOR_PIN), irq1, RISING);
  switchLight(OFF);

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
  nex.poll();

  checkLightLevel();
  //checkMotion();

}
