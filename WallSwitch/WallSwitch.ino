
// Wemos D1 R2 & Mini
#include <KDevice.h>
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

#define ON  1
#define OFF 0

#define LOW_LIGHT 500
#define MIN_LAMP_OFF_TIME 20*1000

const char* lightStateTopic    = "home/keilin_office/overhead_light/state";
const char* lightSetTopic      = "home/keilin_office/overhead_light/set";
const char* lampStateTopic     = "home/keilin_office/lamp/state";
const char* lampSetTopic       = "home/keilin_office/lamp/set";
const char* lampSetLevelTopic  = "home/keilin_office/lamp/set/level";

const char* temp_command_topic = "home/keilin_office/heat_pump/temperature";
const char* temp_state_topic   = "home/keilin_office/heat_pump/temperature/state";
const char* mode_command_topic = "home/keilin_office/heat_pump/mode";
const char* mode_state_topic   = "home/keilin_office/heat_pump/mode/state";
const char* fan_command_topic  = "home/keilin_office/heat_pump/fan_mode";
const char* fan_state_topic    = "home/keilin_office/heat_pump/fan_mode/state";
SoftwareSerial nextionSerial(D7, D4); // RX, TX

// State variables
int lightOnVal = 0;
int lampOnVal = 0;
unsigned long lampOffTime = 0;

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

// Returns True if button is pressed on pin
boolean buttonPressed(int pin)
{
  int reading = digitalRead(pin);
  return reading == LOW;
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


void mqtt_callback(char* topic, String pl){
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
  }  
}

void mqtt_connect(){
    mqttClient.subscribe(lightSetTopic);
    mqttClient.subscribe(lampStateTopic); 
    mqttClient.subscribe(mode_command_topic);    
    mqttClient.subscribe(fan_command_topic);    
    mqttClient.subscribe(temp_command_topic); 
}

void setup() {
  Serial.begin(115200);
  Serial.println("Hello!");

  pinMode(LIGHT_PIN, OUTPUT);
  pinMode(MOTION_PIN, INPUT);
  pinMode(LSENSOR_PIN, INPUT);
  digitalWrite(LSENSOR_PIN, HIGH);
  
  lightOnVal = false;
  
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
  
  KDevice_setup(ROOM, HOSTNAME_PREF, MQTT_HOST, WIFI_SSID, WIFI_PASS, DEBUG_HOST, DEBUG_PORT, &mqtt_callback);
  attachInterrupt(digitalPinToInterrupt(LSENSOR_PIN), irq1, RISING);
  switchLight(OFF);

}

void loop() {
  KDevice_loop(MQTT_USER, MQTT_PASS, mqtt_connect);
  nex.poll();

  checkLightLevel();
  //checkMotion();

}
