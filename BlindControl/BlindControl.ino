#include <KDevice.h>
#include "config.h"

#define SWITCH_PIN D1
#define SWITCH_PIN_G D2
#define BUTTON_PIN_G D3
#define BUTTON_PIN   D4
#define SERVO_PIN D7

#define ON  1
#define OFF 0
#define LEFT 100
#define RIGHT 85

#define STATE_UNKNOWN   0
#define STATE_OPEN      1
#define STATE_CLOSED_R  2
#define STATE_CLOSED_L  3

char blindStateTopic[35];
char blindSetTopic[40];
char blindTiltStateTopic[40];
char blindTiltSetTopic[40];

int middleTimeLeft = 0;
int middleTimeRight = 0;
int state = STATE_UNKNOWN;

Servo tiltServo;

// State variables
int lampOnVal = 0;

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
  tiltServo.write(dir);
  debug(S+"Free "+dir);
  for(int i = 0; i < 400; i++){
    if(digitalRead(SWITCH_PIN) == HIGH)break;
    delay(1);
  }  
  tiltServo.write(90); 
}

void moveServo(int dir){  
  moveServo(dir, 5000);
}

void moveServo(int dir, int time){  
  debug(S+"Move "+dir+", "+time);
  tiltServo.write(dir);
  for(int i = 0; i < time; i++){
    if(digitalRead(SWITCH_PIN) == LOW)break;
    delay(1);
  }  
  debug(S+"Move Done "+dir);
  tiltServo.write(90);  
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

void mqtt_callback(char* topic, String pl){  
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
  }    
}

void mqtt_connect(){
    mqttClient.subscribe(blindSetTopic);
    mqttClient.subscribe(blindTiltSetTopic); 
}

void setup() {
  Serial.begin(115200);
  Serial.println("Hello!");
  
  pinMode(SWITCH_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT);
  pinMode(SWITCH_PIN_G, OUTPUT);
  pinMode(BUTTON_PIN_G, OUTPUT);
  digitalWrite(SWITCH_PIN_G, LOW); 
  digitalWrite(BUTTON_PIN_G, LOW);  
  
  sprintf(blindStateTopic,     "home/%s/blinds/%d/state", ROOM, NUM);
  sprintf(blindSetTopic,       "home/%s/blinds/%d/set", ROOM, NUM);
  sprintf(blindTiltStateTopic, "home/%s/blinds/%d/tilt/state", ROOM, NUM);
  sprintf(blindTiltSetTopic,   "home/%s/blinds/%d/tilt/set", ROOM, NUM);
  
  tiltServo.attach(SERVO_PIN); 
  tiltServo.write(90);
  KDevice_setup(ROOM, HOSTNAME_PREF, MQTT_HOST, WIFI_SSID, WIFI_PASS, DEBUG_HOST, DEBUG_PORT, &mqtt_callback); 
  sprintf(resetTopic,          "home/%s/blinds/%d/reset", ROOM, NUM);
  sprintf(debugTopic,          "home/%s/blinds/%d/debug", ROOM, NUM);
  sprintf(statusTopic,         "home/%s/blinds/%d/status", ROOM, NUM);
  sprintf(signalTopic,         "home/%s/blinds/%d/signal", ROOM, NUM);
  
}

void loop() {
  checkButton();
  KDevice_loop(MQTT_USER, MQTT_PASS, mqtt_connect);
}
