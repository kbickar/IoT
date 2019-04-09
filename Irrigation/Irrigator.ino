 
// Wemos D1 mini Lite
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>z
#include <DHT.h>

#include "config.h"
    
// Constants
#define PRESSURE_PIN		A0
#define PRESSURE_POWER1_PIN	D1 
#define PRESSURE_POWER2_PIN	D2

#define MOIST_POWER1_PIN	D3 
#define MOIST_POWER2_PIN	D5
#define MOIST_SENSOR1_PIN	D6 
#define MOIST_SENSOR2_PIN	D0

#define VALVE1_PIN			D7
#define VALVE2_PIN			D8

#define TEMPERATURE_PIN		D4

#define UPDATE_WINDOW     	5*60*1000 // 5 minute

#define SENSOR_SCAN_DELAY  	5*60*1000 // 5 minute
#define LEVEL_SCAN_DELAY   10*60*1000 // 10 minute
#define MOISTURE_CHECK_DELAY    10*60*1000 // 10 minute
#define MOISTURE_WATERING_CHECK_DELAY 5*60*1000 // 5 minute

#define MAX_WATER_TIME		60*60*1000 // 1 hour
#define EXTRA_MOIST_DELAY	12*60*1000 // 12 minute

#define RESET_TIME      24*60*60*1000+60*1000 // 1 day-ish

#define MIN_PRESSURE1		100
#define MAX_PRESSURE1		1000
#define BARREL1_LOW			10

#define MAX_RECONNECTS  5
#define ON  1
#define OFF 0
#define OPEN  1
#define CLOSED 0

const char* mqtt_server = MQTT_HOST;
long lastReconnectAttempt = 0;
DHT dht(TEMPERATURE_PIN, DHT11);

const char* waterLevel1Topic   	= "home/garden/barrel/1/level";
const char* waterLevel2Topic   	= "home/garden/barrel/2/level";
const char* sensorTopic   		= "home/garden/temp_humidity";
const char* moist1Topic       = "home/garden/moist/1";
const char* moist2Topic       = "home/garden/moist/2";
const char* valve1Topic       = "home/garden/valve/1";
const char* valve2Topic       = "home/garden/valve/2";
const char* valve1TopicSet    = "home/garden/valve/1/set";
const char* valve2TopicSet    = "home/garden/valve/2/set";

const char* waterPlantsTopic   	= "home/garden/water";


const char* resetTopic         	= "home/garden/reset";
const char* debugTopic         	= "home/garden/debug";
const char* statusTopic        	= "home/garden/status";

// Rain barrel levels:
int water_level1 = 0;
int water_level2 = 0;
bool watering;
unsigned long watering_start = 0;
unsigned long last_level_check = 0;

// Moisture sensors:
bool moist1 = false;
bool moist2 = false;
unsigned long last_moisture_check = 0;
unsigned long moist_start = 0;

bool valve1 = CLOSED;
bool valve2 = CLOSED;

// Temp sensor
unsigned long last_sensor_scan = 0;


// One time sets
const char compile_date[] = __DATE__ " " __TIME__;
unsigned long start_time;

String host_name(HOSTNAME_PREF);

ESP8266WiFiMulti WiFiMulti;
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// State variables
bool statuses_sent = false;
int reconnectCount = 0;

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

void UpdateTemp(){
  
  float h = dht.readHumidity();
  float t = dht.readTemperature(true);
  
  if (isnan(h) || isnan(t)) return;
  last_sensor_scan = millis();
  
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  // INFO: the data must be converted into a string; a problem occurs when using floats...
  root["temperature"] = (String)t;
  root["humidity"] = (String)h;
  char data[200];
  root.printTo(data, root.measureLength() + 1);
  mqttClient.publish(sensorTopic, data, true);
}

// Power and check water level sensor
void UpdateWaterLevels(){
	last_level_check = millis();
	
	digitalWrite(PRESSURE_POWER1_PIN, HIGH);
	int pressure = analogRead(A0);
	digitalWrite(PRESSURE_POWER1_PIN, LOW);
  debug(S+"Pressure:" + pressure);
	water_level1 =  constrain(round(
	  100.0*(pressure - MIN_PRESSURE1)/(MAX_PRESSURE1 - MIN_PRESSURE1)),
    0, 100);
	String val = S+water_level1;
	mqttClient.publish(waterLevel1Topic, val.c_str());
  	
}

// Power up and check moisture sensors
void UpdateMoisture(){
	last_moisture_check = millis();
	
	digitalWrite(MOIST_POWER1_PIN, ON);
	delay(10);
	moist1 = !digitalRead(MOIST_SENSOR1_PIN);
  digitalWrite(MOIST_POWER1_PIN, OFF);
  delay(10);
  
  digitalWrite(MOIST_POWER2_PIN, ON);
  delay(10);
	moist2 = !digitalRead(MOIST_SENSOR2_PIN);
	digitalWrite(MOIST_POWER2_PIN, OFF);

	mqttClient.publish(moist1Topic, (moist1)?"MOIST":"DRY");
	mqttClient.publish(moist2Topic, (moist2)?"MOIST":"DRY");

	if (moist1) debug("moist1: moist");
	else debug("moist1: dry");
	if (moist2) debug("moist2: moist");
	else debug("moist2: dry");
}

void SetValves(){
  digitalWrite(VALVE1_PIN, valve1);
  digitalWrite(VALVE2_PIN, valve2);
  mqttClient.publish(valve1Topic, (valve1)?"OPEN":"CLOSED");
  mqttClient.publish(valve2Topic, (valve2)?"OPEN":"CLOSED");
}

// Pick source and water plants
void StartWateringPlants(){
	UpdateWaterLevels();
	if (water_level1 > BARREL1_LOW){
    valve1 = OPEN;
    valve2 = CLOSED;
	}else{
    valve1 = CLOSED;
    valve2 = OPEN;
	}
  SetValves();
	watering = true;
	watering_start = millis();
  debug("Started watering");
}

// Change to secondary water source
void ChangeWaterSource(){
  debug("changing water source");
  valve1 = CLOSED;
  valve2 = OPEN;
  SetValves();
}

// Stop watering
void StopWateringPlants(){
  valve1 = CLOSED;
  valve2 = CLOSED;
  watering = false;
  SetValves();
  debug("done watering");
}

// Returns if soil is moist
bool isMoist(){
 return moist1 && moist2;
}

// Water the plants if they need it
void WaterPlants(){
  debug("asked to water plants");
	UpdateMoisture();
	if (isMoist()) return;
	
	StartWateringPlants();
}

bool reconnect() {
  debug("Attempting MQTT connection ["+host_name+"] ...");
  // Attempt to connect
  if (mqttClient.connect(host_name.c_str(), MQTT_USER, MQTT_PASS, statusTopic, 0, 1, "offline")) {
    // Once connected, resubscribe
    mqttClient.subscribe(waterPlantsTopic);
    mqttClient.subscribe(resetTopic);
    mqttClient.subscribe(debugTopic);      
    mqttClient.subscribe(valve1TopicSet);      
    mqttClient.subscribe(valve2TopicSet);     
    mqttClient.subscribe(statusTopic);      
	  mqttClient.publish(statusTopic, "online");  
    debug("connected");
    SetValves();
    UpdateTemp();
    UpdateWaterLevels();
    UpdateMoisture();
    return mqttClient.connected();
  }
  return false;
}

// Handle mqtt events I'm subscribed to
void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  String s = S+"Message arrived ["+topic+"] " ;
  String pl = "";
  for (int i = 0; i < length; i++) {
    pl += (char)payload[i];
  }
  debug(s+pl);

  // Turn on light
  if (strcmp(topic, waterPlantsTopic) == 0){
    if(pl == "1"){  
      WaterPlants();
    }
    
  // Valve Tests
  }else if (strcmp(topic, valve1TopicSet) == 0){
    if(pl == "OPEN") valve1 = OPEN;  
    else if(pl == "CLOSED") valve1 = CLOSED;
    else return;
    valve2 = !valve1 and valve2;
    SetValves();
  }else if (strcmp(topic, valve2TopicSet) == 0){
    if(pl == "OPEN") valve2 = OPEN;  
    else if(pl == "CLOSED") valve2 = CLOSED;
    else return;
    valve1 = !valve2 and valve1;
    SetValves();
  
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
    if(pl == "offline") mqttClient.publish(statusTopic, "online");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Hello!");

  pinMode(PRESSURE_PIN, 		INPUT);
  pinMode(PRESSURE_POWER1_PIN, 	OUTPUT);
  pinMode(PRESSURE_POWER2_PIN, 	OUTPUT);
  pinMode(MOIST_SENSOR1_PIN, 	INPUT);
  pinMode(MOIST_POWER1_PIN, 	OUTPUT);
  pinMode(MOIST_SENSOR2_PIN, 	INPUT);
  pinMode(MOIST_POWER2_PIN, 	OUTPUT);
  pinMode(VALVE1_PIN, 			OUTPUT);
  pinMode(VALVE2_PIN, 			OUTPUT);
  
  valve1 = CLOSED;
  valve2 = CLOSED;
  digitalWrite(VALVE1_PIN, valve1);
  digitalWrite(VALVE2_PIN, valve2);
  
  StopWateringPlants();
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

  dht.begin();
  
  // Start OTA server.
  ArduinoOTA.setHostname((const char *)host_name.c_str());
  ArduinoOTA.begin();
  
  mqttClient.setServer(mqtt_server, 1883);
  mqttClient.setCallback(mqtt_callback);
  reconnect();
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
  
	// Check temp/humidity
	if (millis() - last_sensor_scan > SENSOR_SCAN_DELAY) 
		UpdateTemp();
	// Check rain barrel levels
	if (millis() - last_level_check > LEVEL_SCAN_DELAY)
		UpdateWaterLevels();
	// If we're watering the gardern
	if (watering){
		
		// If rain barrel empties while watering, swap to second source
		if (valve1 == OPEN and water_level1 < BARREL1_LOW){
			ChangeWaterSource();
		}
		
		// Check soil moisture levels
		if (millis() - last_moisture_check > MOISTURE_WATERING_CHECK_DELAY) {
			bool old_moist = isMoist();
			UpdateMoisture();
			if (!old_moist && isMoist()){
				moist_start = millis();
			}
		}
		
		// Finished watering
		if (isMoist() && millis() - moist_start > EXTRA_MOIST_DELAY ||
			millis() - watering_start > MAX_WATER_TIME) 
			StopWateringPlants();		
		
	}else{		
		// Check soil moisture levels
		if (millis() - last_moisture_check > MOISTURE_CHECK_DELAY) 
			UpdateMoisture();		
	}
  if ((valve1 == OPEN || valve2 == OPEN) && !watering){
    watering = true;
    watering_start = millis();
  }

}
