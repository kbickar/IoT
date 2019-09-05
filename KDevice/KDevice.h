#ifndef KLIB_H
#define KLIB_H

#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#define UPDATE_WINDOW     5*60*1000 // 5 minute
#define RESET_TIME      24*60*60*1000+60*1000 // 1 day-ish

#define MAX_RECONNECTS  5
#define LONG_RECONNECT_DELAY    5*60*1000
#define RECONNECT_DELAY         5*1000
#define RSSI_REPORT_DELAY         60*1000

using f_mqtt_cb = void (*)(char*, String);
extern String S;
extern PubSubClient mqttClient;

// Debugging
extern bool debugMode;

void debug(String s);

void reset();

void report_rssi();

bool reconnect(char* mqtt_user, char* mqtt_pass);

void callback(char* topic, byte* payload, unsigned int length);

void KDevice_setup(const char * room, const char * nhostname_pref, const char* mqtt_host, const char* wifi_ssid, const char* wifi_pass, const char* ndebug_host, const int ndebug_port, f_mqtt_cb cb);

void KDevice_loop( char* mqtt_user, char* mqtt_pass, void (*connect_fn)());

#endif