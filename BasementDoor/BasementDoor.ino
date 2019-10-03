
#include <PN532_HSU.h>
#include <PN532.h>
#include <DHT.h>
#include <KDevice.h>
#include "config.h"
    
// Constants
#define RedPin          D7  
#define GreenPin        D8  
#define BluePin         D6  
#define IndicatorPin    D0


#define DoorSensorPin   D3  
#define LockSensorPin   D4  
#define LockStrikePin   D2  
#define DHTPin          D5  

#define LOCKED          180
#define UNLOCKED        0

#define CODE_SCAN_DELAY   1000
#define SENSOR_SCAN_DELAY   5*60*1000 // 5 minute
#define MAIL_DETECT_DELAY 10000
#define SCAN_DELAY        1000
#define SCAN_FAIL_DELAY   2000

#define UNLOCK_TIME      10*1000


unsigned long lastCloseTime = 0;

const char* doorOpenTopic = "home/basement_door/state";
const char* doorLockedTopic = "home/basement_door/deadbolt/state";
const char* lockStateTopic = "home/basement_door/latch/state";
const char* lockDoorTopic = "home/basement_door/latch/set";
const char* lightStateTopic = "home/basement_door/door_bell/light/state";
const char* lightSetTopic = "home/basement_door/door_bell/light/set";
const char* lightRGBTopic = "home/basement_door/door_bell/light/rgb";
const char* lightRGBSetTopic = "home/basement_door/door_bell/light/rgb/set";
const char* scanTopic = "home/basement_door/scan";
const char* sensorTopic = "home/basement_door/sensor";

// One time sets

PN532_HSU pn532hsu(Serial);
PN532 nfc(pn532hsu);

DHT dht(DHTPin, DHT11);

// State variables
int RVal = LOW;
int GVal = LOW;
int BVal = LOW;

bool DoorClosedVal = false;
bool DoorLockedVal = false;

bool lightOnVal = false;

bool DoorLocked = false;
unsigned long lastCodeScan = 0;
unsigned long lastSensorScan = 0;


// Returns True if button is pressed on pin
boolean buttonPressed(int pin)
{
  int reading = digitalRead(pin);
  return reading == LOW;
}

void LockDetection(bool doorLocked ){
    if (doorLocked != DoorLockedVal){ 
      mqttClient.publish(doorLockedTopic, (doorLocked)?"OFF":"ON");
      debug((doorLocked)?"Door Locked: yes":"Door Locked: no");
      DoorLockedVal = doorLocked;  
      if (doorLocked) DoorBellColor(1,0,0);
      else DoorBellColor(0,0,1);
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

void UpdateTemp(){
  if (millis() - lastSensorScan < SENSOR_SCAN_DELAY) return;
  lastSensorScan = millis();
  float h = dht.readHumidity();
  float t = dht.readTemperature(true);
  
  if (isnan(h) || isnan(t)) return;
  
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  // INFO: the data must be converted into a string; a problem occurs when using floats...
  root["temperature"] = (String)t;
  root["humidity"] = (String)h;
  char data[200];
  root.printTo(data, root.measureLength() + 1);
  mqttClient.publish(sensorTopic, data, true);
}

bool validate_code(uint32_t code){
    debug(S+"Code scanned: "+code);
    DoorBellColor(1,0,1);
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
    digitalWrite(RedPin, !r);
    digitalWrite(GreenPin, !g);
    digitalWrite(BluePin, !b);  
    RVal = r;
    GVal = g;
    BVal = b;
    debug(S+"Button Color = "+r+g+b);
    mqttClient.publish(lightStateTopic, (r||g||b)?"ON":"OFF");
    String rgb = "";
    rgb += (r)?"255":"0";
    rgb += (g)?"255":"0";
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


// Unlocks door, button is green while it is unlocking
void UnLockDoor(){
    debug("Unlocking door...");
    mqttClient.publish(lockStateTopic, "UNLOCK");
    digitalWrite(IndicatorPin, HIGH);
    digitalWrite(LockStrikePin, HIGH);  
    lastCodeScan = 0;
    DoorBellColor(0,1,0);
    delay(UNLOCK_TIME);
    digitalWrite(LockStrikePin, LOW);  
    digitalWrite(IndicatorPin, LOW);
    DoorBellColor(0,0,1);
    mqttClient.publish(lockStateTopic, "LOCK");
}

void mqtt_callback(char* topic, String pl){
  // Toggle Lock
  if (strcmp(topic, lockDoorTopic) == 0){
    if(pl =="UNLOCK") UnLockDoor();
    else mqttClient.publish(lockStateTopic, "LOCK");
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

void setup() {
  Serial.begin(115200);
  Serial.println("Hello!");
  pinMode(LockSensorPin, INPUT_PULLUP);
  pinMode(DoorSensorPin, INPUT);

  pinMode(RedPin, OUTPUT);
  pinMode(GreenPin, OUTPUT);
  pinMode(BluePin, OUTPUT);
  pinMode(IndicatorPin, OUTPUT);
  pinMode(LockStrikePin, OUTPUT);

  digitalWrite(IndicatorPin, LOW);
  delay(500);
  digitalWrite(IndicatorPin, HIGH);
  DoorBellColor(0,0,0);
  
  RVal = LOW;
  GVal = LOW;
  BVal = LOW;
  
  DoorClosedVal = false;
  DoorLockedVal = false;
  
  lightOnVal = false;
  
  lastCodeScan = 0;
  lastSensorScan = 0;

  KDevice_setup(ROOM, HOSTNAME_PREF, MQTT_HOST, WIFI_SSID, WIFI_PASS, DEBUG_HOST, DEBUG_PORT, &mqtt_callback);

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

  dht.begin();

  delay(20);
  DoorBellColor(1,0,0);
  delay(1000);
  DoorBellColor(0,0,0);
  delay(100);
  DoorBellColor(0,1,0);
  delay(1000);
  DoorBellColor(0,0,0);
  delay(100);
  DoorBellColor(0,0,1);
  digitalWrite(IndicatorPin, LOW);

}

uint32_t bytesToInt(uint8_t bytes[]){
  uint32_t ret = 0;
  for(int i=0;i<4;i++){
    ret += bytes[i] << (8*i);
  }
  return ret;
}

void mqtt_connect(){
    mqttClient.subscribe(lockDoorTopic);
    mqttClient.subscribe(lightSetTopic);
    mqttClient.subscribe(lightRGBSetTopic);
    // Send states on connection
    mqttClient.publish(lockStateTopic, "LOCK");
    DoorLockedVal = !DoorLockedVal;
    DoorClosedVal = !DoorClosedVal;
    lastSensorScan = 0;   
}

void loop() {

  LockDetection( buttonPressed(LockSensorPin) );
  DoorDetection( buttonPressed(DoorSensorPin) );
  UpdateTemp();
  
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };  // Buffer to store the returned UID
  uint8_t uidLength;                        // Length of the UID (4 or 7 bytes depending on ISO14443A card type)
  if (!DoorLockedVal &&
      millis() - lastCodeScan > SCAN_DELAY && 
      nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 10)){
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
