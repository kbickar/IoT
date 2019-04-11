#include <Servo.h>
#include <KDevice.h>
#include <PN532_HSU.h>
#include <PN532.h>

#include "config.h"
    
// Constants
#define RedPin          D0  
#define GreenPin        D5  
#define BluePin         D6  
#define IndicatorPin    D8

#define DBButtonPin     D4  
#define DBRelayPin      D7  

#define DoorSensorPin   D2  
#define LockSensorPin   D3  
#define LockServoPin    D1  

#define MailSensorPin   A0  

#define LOCKED          180
#define HALF_LOCKED     90
#define UNLOCKED        0

#define CODE_SCAN_DELAY   1000
#define MAIL_DETECT_DELAY 10000
#define SCAN_DELAY        1000
#define SCAN_FAIL_DELAY   2000

#define MAX_LOCK_TIME     1500

unsigned long mailDetectTime = 0;
unsigned long lastCloseTime = 0;

const char* pressButtonTopic = "home/front_door/door_bell/state";
const char* ringBellTopic = "home/front_door/door_bell/ring";
const char* doorOpenTopic = "home/front_door/state";
const char* doorLockedTopic = "home/front_door/deadbolt/state";
const char* lockDoorTopic = "home/front_door/deadbolt/set";
const char* mailTopic = "home/front_door/mail/state";
const char* lightStateTopic = "home/front_door/door_bell/light/state";
const char* lightSetTopic = "home/front_door/door_bell/light/set";
const char* lightRGBTopic = "home/front_door/door_bell/light/rgb";
const char* lightRGBSetTopic = "home/front_door/door_bell/light/rgb/set";
const char* scanTopic = "home/front_door/scan";
const char* buttonLockTopic = "home/front_door/exit_lock";

PN532_HSU pn532hsu(Serial);
PN532 nfc(pn532hsu);

// State variables
int RVal = LOW;
int GVal = LOW;
int BVal = LOW;

bool DBButtonVal = false;
bool DBRelayVal = false;
bool DoorClosedVal = false;
bool DoorLockedVal = false;
bool mailDetectedVal = false;

bool lightOnVal = false;

bool DoorLocked = false;

unsigned long lastCodeScan = 0;

// Returns True if button is pressed on pin
boolean buttonPressed(int pin)
{
  int reading = digitalRead(pin);
  return reading == LOW;
}

// Sets relay value on pin to open/closed
void BellOn(int val){
    if (val != DBRelayVal){
        DBRelayVal = val;
        if (val){
           digitalWrite(DBRelayPin, LOW);
           DoorBellColor(0,0,1);
        }else{
           digitalWrite(DBRelayPin, HIGH);
           DoorBellColor(1,1,1);
        }
        debug(S+"Doorbell Relay = "+DBRelayVal);
        if (val != DBButtonVal) mqttClient.publish(pressButtonTopic, (val)?"ON":"OFF");
        DBButtonVal = val;
    }
}

void MailDetection(){
  if (millis() - mailDetectTime > MAIL_DETECT_DELAY){
    int irLight = analogRead(MailSensorPin);
    if (debugSensors) debug(S+"IR Light: "+irLight);
    bool mailDetected = irLight > 650;
    if (mailDetected != mailDetectedVal) mqttClient.publish(mailTopic, (mailDetected)?"ON":"OFF");
    if (mailDetected != mailDetectedVal) debug((mailDetected)?"mail: yes":"mail: no");
    mailDetectedVal = mailDetected; 
    mailDetectTime = millis();
    digitalWrite(IndicatorPin, mailDetected);
  }
}

void LockDetection(bool doorLocked ){
    if (doorLocked != DoorLockedVal){ 
      mqttClient.publish(doorLockedTopic, (doorLocked)?"LOCK":"UNLOCK");
      debug((doorLocked)?"Door Locked: yes":"Door Locked: no");
      DoorLockedVal = doorLocked;  
    }
}
void DoorDetection(bool doorClosed ){
    if (doorClosed != DoorClosedVal) {
      mqttClient.publish(doorOpenTopic, (!doorClosed)?"ON":"OFF");
      debug((!doorClosed)?"Door Open: yes":"Door Open: no");
      if(doorClosed) lastCloseTime = millis();
      DoorClosedVal = doorClosed;  
    }
}

// Sets servo on pin to degrees
void setServo(int pin, int degree)
{
    Servo myservo;
    myservo.attach(pin); 
    myservo.write(degree); 
    unsigned long lockingTime = millis();
    debug(S+"Lock Servo = "+degree);
    if (degree == LOCKED){
      while(!buttonPressed(LockSensorPin) && (millis() - lockingTime < MAX_LOCK_TIME)){
        delay(100);        
      }
    }else{        
      delay(1000);
    }
    myservo.detach(); 
}

bool validate_code(uint32_t code){
    debug(S+"Code scanned: "+code);
    DoorBellColor(1,1,0);
    for (int i = 0; i < (sizeof(CARDS)/sizeof(uint32_t)); i++) {
      if (code == CARDS[i]){
        UnLockDoor();
        break;
      }
    }
    String data = S+code;
    mqttClient.publish(scanTopic, data.c_str());
    return false;
}

void DoorBellColor(bool r, bool g, bool b){
    digitalWrite(RedPin, r);
    digitalWrite(GreenPin, g);
    digitalWrite(BluePin, b);  
    RVal = r;
    GVal = g;
    BVal = b;
    debug(S+"Button Color = "+r+g+b);
    mqttClient.publish(lightStateTopic, (r||g||b)?"ON":"OFF");
    String rgb = "";
    rgb += (r)?"255,":"0,";
    rgb += (g)?"255,":"0,";
    rgb += (b)?"255":"0";
    mqttClient.publish(lightRGBTopic, rgb.c_str());
    
    lightOnVal = r||g||b;
}

void BlinkDoorBell(int blinks, int r, int g, int b){
    for (int i=0; i < blinks; i++){
        DoorBellColor(r,g,b);
        delay(300);
        DoorBellColor(0,0,0);
        delay(300);
    }
      DoorBellColor(1,1,1);
}

// Locks door, button is red while it is locking
void LockDoor(){
    debug("Locking door...");
    if (!buttonPressed(DoorSensorPin)){
      debug("Door not closed, cannot lock!");
      BlinkDoorBell(4,1,0,0);
      return;
    }
    DoorBellColor(1,0,0);
    setServo(LockServoPin, LOCKED);
    DoorBellColor(1,1,1);
    if ( buttonPressed(LockSensorPin) ){
      DoorLocked = true;
      debug("Door locked");
    }
}

// Unlocks door, button is green while it is unlocking
void UnLockDoor(){
    debug("Unlocking door...");
    lastCodeScan = 0;
    DoorBellColor(0,1,0);
    setServo(LockServoPin, UNLOCKED);
    DoorBellColor(1,1,1);;
    if ( !buttonPressed(LockSensorPin) ){
      DoorLocked = false;
      debug("Door unlocked");
    }else{
      debug("Unlock failed");
      reset();
    }
}


void mqtt_callback(char* topic, String pl){
  // Ring doorbell
  if (strcmp(topic, ringBellTopic) == 0 && pl == "ON") {
    debug("ding dong!");
    BellOn(true);
    delay(500);
    BellOn(false);
  }
  // Toggle Lock
  else if (strcmp(topic, lockDoorTopic) == 0){
    if(pl == "LOCK") LockDoor();
    else if(pl =="UNLOCK") UnLockDoor();
  }
  // Turn on light
  else if (strcmp(topic, lightSetTopic) == 0){
    if(pl == "ON"){  
      if(!lightOnVal) DoorBellColor(1,1,1);
      else  mqttClient.publish(lightStateTopic, (lightOnVal)?"ON":"OFF");
    }
    else if(pl =="OFF") {
      if(lightOnVal) DoorBellColor(0,0,0);
      else mqttClient.publish(lightStateTopic, (lightOnVal)?"ON":"OFF");
    }
  }
  // Set light colors
  else if (strcmp(topic, lightRGBSetTopic) == 0){
    int commaIndex = pl.indexOf(',');
    int secondCommaIndex = pl.indexOf(',', commaIndex + 1);
    bool r = 64 < pl.substring(0, commaIndex).toInt();
    bool g = 64 < pl.substring(commaIndex + 1, secondCommaIndex).toInt();
    bool b = 64 < pl.substring(secondCommaIndex + 1).toInt();
    DoorBellColor(r,g,b);
  } 
}

void mqtt_connect(){
    mqttClient.subscribe(ringBellTopic);
    mqttClient.subscribe(lockDoorTopic);
    mqttClient.subscribe(lightSetTopic);
    mqttClient.subscribe(lightRGBSetTopic);
    DoorLockedVal = !DoorLockedVal;
    DoorClosedVal = !DoorClosedVal;
    mailDetectedVal = !mailDetectedVal;
    mqttClient.publish(mailTopic, (mailDetectedVal)?"ON":"OFF");
    mqttClient.publish(doorLockedTopic, (DoorLockedVal)?"ON":"OFF");
    mqttClient.publish(doorOpenTopic, (!DoorClosedVal)?"ON":"OFF"); 
}

void setup() {
  Serial.begin(115200);
  Serial.println("Hello!");
  pinMode(DBButtonPin, INPUT_PULLUP);
  pinMode(LockSensorPin, INPUT_PULLUP);
  pinMode(DoorSensorPin, INPUT);

  pinMode(RedPin, OUTPUT);
  pinMode(GreenPin, OUTPUT);
  pinMode(BluePin, OUTPUT);
  pinMode(IndicatorPin, OUTPUT);

  pinMode(DBRelayPin, OUTPUT_OPEN_DRAIN);
  digitalWrite(DBRelayPin, HIGH);
  digitalWrite(IndicatorPin, HIGH);
  
  RVal = LOW;
  GVal = LOW;
  BVal = LOW;
  
  DBButtonVal = false;
  DBRelayVal = false;
  DoorClosedVal = false;
  DoorLockedVal = false;
  mailDetectedVal = false;
  
  lightOnVal = false;
  
  DoorLocked = false;
  
  lastCodeScan = 0;

  nfc.begin();

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (! versiondata) {
    debug("Didn't find NFC PN53x board");
  }else{
    // Got ok data, print it out!
    char buf[1];
    sprintf(buf, "%01x", ((versiondata>>24) & 0xFF));
    debug(S+"Found chip PN5"+ buf+" ver. "+ ((versiondata>>16) & 0xFF)+'.'+((versiondata>>8) & 0xFF));
  
    // configure board to read RFID tags
    //nfc.setPassiveActivationRetries(0xFF); 
    nfc.SAMConfig();
  }

  KDevice_setup(ROOM, HOSTNAME_PREF, MQTT_HOST, WIFI_SSID, WIFI_PASS, DEBUG_HOST, DEBUG_PORT, &mqtt_callback);

  delay(20);
  BellOn(false);
  LockDoor();
  DoorBellColor(1,1,1);
  
  digitalWrite(IndicatorPin, LOW);

}

uint32_t bytesToInt(uint8_t bytes[]){
  uint32_t ret = 0;
  for(int i=0;i<4;i++){
    ret += bytes[i] << (8*i);
  }
  return ret;
}

void loop() {

  MailDetection();
  LockDetection( buttonPressed(LockSensorPin) );
  DoorDetection( buttonPressed(DoorSensorPin) );

  // Pressing the doorbell button:
  if (buttonPressed(DBButtonPin) ){
    // If door was closed under 10 seconds ago, lock instead of ring
    if(millis() - lastCloseTime < 10000){
       debug("Exit lock triggered by doorbell");
       LockDoor();
       mqttClient.publish(buttonLockTopic, "");
    }else{
      BellOn( true );// Ring bell
    }
  }else{    
    BellOn( false );
  }
  
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };  // Buffer to store the returned UID
  uint8_t uidLength;                        // Length of the UID (4 or 7 bytes depending on ISO14443A card type)
  if (millis() - lastCodeScan > SCAN_DELAY && nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 10)){
    uint32_t code = bytesToInt(uid);
    lastCodeScan = millis();
    validate_code(code);
  }

  if (lastCodeScan > 0 && millis() - lastCodeScan > SCAN_FAIL_DELAY){
      // Not valid code!
      lastCodeScan = 0;
      debug(S+"Lookup timed out");
      BlinkDoorBell(3, 1,0,0);
  }
  
  KDevice_loop(MQTT_USER, MQTT_PASS, mqtt_connect);
}
