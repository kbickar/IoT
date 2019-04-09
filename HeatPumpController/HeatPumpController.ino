#include <KDevice.h> 
#include <SoftwareSerial.h>
#include <MitsubishiHeatpumpIR.h>

#include "config.h"
    
// Constants
#define IR_PIN          D7 

char temp_command_topic[44];
char temp_state_topic[50];
char mode_command_topic[37];
char mode_state_topic[43];
char fan_command_topic[41];
char fan_state_topic[47];

IRSenderBitBang irSender(IR_PIN);   
HeatpumpIR *heatpumpIR = new MitsubishiFDHeatpumpIR();  

// State variables
int power = POWER_OFF;
int temperature = 24;
int mode = MODE_AUTO;
String sMode = "False";
int fan = FAN_AUTO;
String sFan = "auto";

// Handle mqtt events I'm subscribed to
void mqtt_callback(char* topic, String pl){
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
  }
}

void mqtt_connect(){
    mqttClient.subscribe(mode_command_topic);    
    mqttClient.subscribe(fan_command_topic);    
    mqttClient.subscribe(temp_command_topic);   
    if (sMode != "off"){
        mqttClient.publish(mode_state_topic, sMode.c_str());    
        mqttClient.publish(fan_state_topic, sFan.c_str());
        String temp = S+temperature;
        mqttClient.publish(temp_state_topic, temp.c_str());      
    }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Hello!");
  sprintf(temp_command_topic, "home/%s/heat_pump/temperature", ROOM);
  sprintf(temp_state_topic,   "home/%s/heat_pump/temperature/state", ROOM);
  sprintf(mode_command_topic, "home/%s/heat_pump/mode", ROOM);
  sprintf(mode_state_topic,   "home/%s/heat_pump/mode/state", ROOM);
  sprintf(fan_command_topic,  "home/%s/heat_pump/fan_mode", ROOM);
  sprintf(fan_state_topic,    "home/%s/heat_pump/fan_mode/state", ROOM);
  KDevice_setup(ROOM, HOSTNAME_PREF, MQTT_HOST, WIFI_SSID, WIFI_PASS, DEBUG_HOST, DEBUG_PORT, &mqtt_callback);
}

void loop() {
  KDevice_loop(MQTT_USER, MQTT_PASS, mqtt_connect);
}
