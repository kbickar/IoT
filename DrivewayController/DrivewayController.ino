#include <KDevice.h>
#include <ArduinoJson.h>   
#define FASTLED_ALLOW_INTERRUPTS 0
#include <WS2812FX.h>

#include "config.h"
    
// Constants
#define LAMP_PIN          D3 
#define ON  1
#define OFF 0
#define LED_COUNT 50
#define LED_PIN  3

#define TIMER_MS 5000

const char* lampStateTopic     = "home/driveway/lamp/state";
const char* lampSetTopic       = "home/driveway/lamp/set";
const char* fenceStateTopic    = "home/garden/fence/state";
const char* fenceSetTopic      = "home/garden/fence/set";
const char* ledSetTopic      = "home/garden/led/set";

// State variables
int lampOnVal = 0;
bool updateBr = false;
bool updateRGB = false;
bool updateMode = false;
byte brightness = 0;
byte r = 0;
byte g = 0;
byte b = 0;
byte effect = 0;

unsigned long last_change = 0;
unsigned long now = 0;

void switchLight(int val) {
  lampOnVal = val;
  digitalWrite(LAMP_PIN, val);
  mqttClient.publish(lampStateTopic, (lampOnVal)?"ON":"OFF");  
}

void sendCmds() {  
  int x = 0;
    while (Serial.available() > 0) {
      x = Serial.read();
    }
    if (x == 1){
      if (!updateBr && !updateRGB && !updateMode){
        Serial.write(0);
        
      }else{
        Serial.write(126);
        if (updateBr){
          Serial.write(1);  
          Serial.write(brightness);        
        }else{ 
          Serial.write(0);  
        }
        if (updateRGB){
          Serial.write(1);  
          Serial.write(r);    
          Serial.write(g);    
          Serial.write(b);        
        }else{
          Serial.write(0);   
        }
        if (updateMode){
          Serial.write(1);  
          Serial.write(effect);        
        }else{
          Serial.write(0);  
        }
        Serial.flush();
      }
    }
    updateBr = false;
    updateRGB = false;
    updateMode = false;  
       delay(100);
}


void mqtt_callback(char* topic, String pl){
  // Turn on lamp
  if (strcmp(topic, lampSetTopic) == 0){
    if(pl == "ON"){  
      if(!lampOnVal) switchLight(ON);
      else  mqttClient.publish(lampStateTopic, (lampOnVal)?"ON":"OFF");
    }
    else if(pl =="OFF") {
      if(lampOnVal) switchLight(OFF);
      else mqttClient.publish(lampStateTopic, (lampOnVal)?"ON":"OFF");
    }
  }  
  else if (strcmp(topic, fenceSetTopic) == 0){    
    DynamicJsonBuffer dynamicJsonBuffer;
    JsonObject& root = dynamicJsonBuffer.parseObject(pl);
     if (!root.success()) {
      debug("parsing error");
      return;
    }

    if (root.containsKey("state")) {
      const char* state = root["state"];
      if (strcmp(state, "ON") == 0) {
        if (!root.containsKey("color") &&
            !root.containsKey("effect") &&
            !root.containsKey("brightness")) {
          debug(S+"Set state: "+state);
          effect = FX_MODE_STATIC;
          r = 255; g = 255; b = 255;
          brightness = 255;
          updateBr = true;
          updateRGB = true;
          updateMode = true;
        }
      } else if (strcmp(state, "OFF") == 0) {
        debug(S+"Set state: "+state);
        effect = FX_MODE_STATIC;
        brightness = 0;
        updateBr = true;
        updateMode = true;
      }
    }
     if (root.containsKey("color")) {
      // stops the possible current effect
      effect = FX_MODE_STATIC;
      
      r = root["color"]["r"];
      g = root["color"]["g"];
      b = root["color"]["b"];
      updateRGB = true;
      updateMode = true;
    }

    if (root.containsKey("brightness")) {
      brightness = root["brightness"];
      updateBr = true;
    }
    
    if (root.containsKey("effect")) {
      const char* effectname = root["effect"];
      for (int m = 0; m < 60; m++){
        if (strcmp(effectname, (char*)_names[m]) == 0 ){
          debug(S+"Set effect: "+effectname+" = "+m);
          effect = m;
          updateMode = true;
          break;
        }
      }
    }
  }
}

void mqtt_connect(){
    mqttClient.subscribe(lampSetTopic);   
    mqttClient.subscribe(fenceSetTopic);    
    mqttClient.subscribe(ledSetTopic);   
    mqttClient.publish(lampStateTopic, (lampOnVal)?"ON":"OFF"); 
}

void setup() {
  Serial.begin(115200);
  Serial.println("Hello!");
  pinMode(LAMP_PIN, OUTPUT);
  KDevice_setup(ROOM, HOSTNAME_PREF, MQTT_HOST, WIFI_SSID, WIFI_PASS, DEBUG_HOST, DEBUG_PORT, &mqtt_callback);
  switchLight(OFF);
}

void loop() {
  KDevice_loop(MQTT_USER, MQTT_PASS, mqtt_connect);
  now = millis();

  if (Serial.available() > 0) {
    sendCmds();
  }
}
