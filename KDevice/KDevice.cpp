#include "KDevice.h"
String S = "";
long lastReconnectAttempt = 0;

char resetTopic[38];
char debugTopic[38];
char statusTopic[39];
char signalTopic[39];

// One time sets
const char compile_date[] = __DATE__ " " __TIME__;
unsigned long start_time;
unsigned long rssi_report_time;

String host_name;
String hostname_pref;

ESP8266WiFiMulti WiFiMulti;
WiFiClient espClient;
PubSubClient mqttClient(espClient);
f_mqtt_cb fmqtt_callback;

bool statuses_sent = false;

int reconnectCount = 0;

// Debugging
bool debugMode = true;
char debug_host[30];
int debug_port;

void debug(String s)
{
  if (!debugMode) return;
  //Serial.println(s);
  WiFiUDP udpClient;
  s = S+hostname_pref+"| "+s;
  udpClient.beginPacket(debug_host, debug_port);
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

void report_rssi(){
    String signal = S+WiFi.RSSI();
    mqttClient.publish(signalTopic, signal.c_str()); 
}


bool reconnect(char* mqtt_user, char* mqtt_pass) {
  debug("Attempting MQTT connection ["+host_name+"] ...");
  // Attempt to connect
  if (mqttClient.connect(host_name.c_str(), mqtt_user, mqtt_pass, statusTopic, 0, 1, "offline")) {
    // Once connected, resubscribe
    mqttClient.subscribe(resetTopic);
    mqttClient.subscribe(debugTopic);        
    mqttClient.subscribe(statusTopic);      
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
  
  // Reset
  if (strcmp(topic, resetTopic) == 0){
    reset();
  }
  // Debug
  else if (strcmp(topic, debugTopic) == 0){
    debugMode = (pl == "1");
  }
  // Status
  else if (strcmp(topic, statusTopic) == 0){
    if(pl != "online") mqttClient.publish(statusTopic, "online");
  }else{	  
	fmqtt_callback(topic, pl);
  }
}

void KDevice_setup(const char * room, const char * nhostname_pref, const char* mqtt_host, const char* wifi_ssid, const char* wifi_pass, const char* ndebug_host, const int ndebug_port, f_mqtt_cb cb){
  strcpy(debug_host, ndebug_host);
  debug_port = ndebug_port;
  fmqtt_callback = cb;
  
  sprintf(resetTopic,         "home/%s/reset", room);
  sprintf(debugTopic,         "home/%s/debug", room);
  sprintf(statusTopic,        "home/%s/status", room);
  sprintf(signalTopic,        "home/%s/signal", room);
  
  statuses_sent = false;
  
  reconnectCount = 0;
  rssi_report_time = 0;

  // Set Hostname.
  hostname_pref = nhostname_pref;
  host_name = hostname_pref;
  host_name += String(ESP.getChipId(), HEX);
  WiFi.hostname(host_name);
  WiFiMulti.addAP(wifi_ssid, wifi_pass);

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
  
  mqttClient.setServer(mqtt_host, 1883);
  mqttClient.setCallback(callback);
	
}

void KDevice_loop(char* mqtt_user, char* mqtt_pass, void (*connect_fn)()){
  
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
      if (reconnect(mqtt_user, mqtt_pass)) {
        lastReconnectAttempt = 0;
        reconnectCount = 0;
		connect_fn();
      }
    }
  } else {
    // Client connected
    mqttClient.loop();
  }	
}