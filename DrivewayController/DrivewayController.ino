#include <KDevice.h>
#include "config.h"
    
// Constants
#define LAMP_PIN          D1 
#define ON  1
#define OFF 0
const char* lampStateTopic     = "home/driveway/lamp/state";
const char* lampSetTopic       = "home/driveway/lamp/set";

// State variables
int lampOnVal = 0;

void switchLight(int val) {
  lampOnVal = val;
  digitalWrite(LAMP_PIN, val);
  mqttClient.publish(lampStateTopic, (lampOnVal)?"ON":"OFF");  
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
}
void mqtt_connect(){
    mqttClient.subscribe(lampSetTopic);   
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
}
