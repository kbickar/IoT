
#include <ESP8266HTTPClient.h>
#include <ArduinoOTA.h>
#include <Adafruit_VC0706.h>
#include <JPEGDecoder.h>
#include <KDevice.h>
#include "config.h"

#define ON          1
#define OFF         0
IPAddress server(192,168,1,57); 

#define LIGHT_PIN           D8

#define WM_RESET_TIME       2*60*1000
#define TAKE_PHOTO_DELAY    60*1000
#define LIGHT_UP_DELAY      10*1000

#define NUM_DIGITS   9
#define DIGIT_HEIGHT 16
#define DIGIT_WIDTH  8
#define DIGIT_SPACE  2

#define SAVE_FILES 1

const char* waterQueryTopic = "home/basement/water_meter/query";
const char* waterMeterTopic = "home/basement/water_meter/value";

Adafruit_VC0706 cam = Adafruit_VC0706(&Serial);

uint8_t jpg_data[32767];
char jbuff[64];
int jpg_arr_i = 0;
uint16_t jpglen = 0;
uint16_t frameptr;

boolean photo_ready = false;
boolean lights_on = false;
unsigned long last_photo = 0;
unsigned long lights_time = 0;
unsigned long wm_start_time;

byte pixel_to_green(uint16_t pix){
  int r = (pix & 0xF800) >> 8;
  int g = (pix & 0x7E0) >> 3;
  int b = (pix & 0x1F) << 3;
  return g;
}

void sendCommand(uint8_t cmd, uint8_t args[] = 0, uint8_t argn = 0) {
  String s = "> ";
  s += String(0x56, HEX)+" ";
  Serial.write(0x56);
  s += String(0x0, HEX)+" ";
  Serial.write(0x0);
  s += String(cmd, HEX)+" ";
  Serial.write(cmd);

  for (uint8_t i=0; i<argn; i++) {
  s += String(args[i], HEX)+" ";
    Serial.write(args[i]);
  }
}

void readJpg(uint8_t numbytes) {
  uint8_t args[] = {0x0C, 0x0, 0x0A, 
                    0, 0, frameptr >> 8, frameptr & 0xFF, 
                    0, 0, 0, numbytes, 
                    CAMERADELAY >> 8, CAMERADELAY & 0xFF};

  sendCommand(VC0706_READ_FBUF, args, sizeof(args));
  frameptr += numbytes;
  numbytes += 10;

  uint8_t counter = 0;
  uint8_t bufferLen = 0;
  int bi = 0;
  
  while ((CAMERADELAY != counter) && (bufferLen != numbytes)){
    if (Serial.available() <= 0) {
      delay(1);
      counter++;
      continue;
    }
    counter = 0;
    bufferLen++;
    uint8_t b = Serial.read();

    if (bufferLen > 5 && bi < numbytes-10){
      jpg_data[jpg_arr_i++] = b;
      if (SAVE_FILES) jbuff[bi++] = b;
    }
  }
}

void take_photo(){
  WiFiClient client;
  if (SAVE_FILES) {
    client.connect(server, 3347);
  }

  uint8_t args4[] = {0x1, VC0706_STOPCURRENTFRAME};
  sendCommand(VC0706_FBUF_CTRL, args4, sizeof(args4));
  
  // Get the size of the image (frame) taken  
  jpglen = cam.frameLength();
  uint16_t remaining = jpglen;
  debug(S+"Storing "+jpglen+" byte image.");

  // Read all the data up to # bytes!
  frameptr  = 0;
  byte wCount = 0; // For counting # of writes
  jpg_arr_i = 0;
  while (remaining > 0) {
    // read 64 bytes at a time;
    uint8_t bytesToRead = min(64, int(remaining)); 
    readJpg(bytesToRead);
    if(++wCount >= 64) { // Every 2K, give a little feedback so it doesn't appear locked up
      debug(".");
      ESP.wdtFeed();
      wCount = 0;
    }
    if (SAVE_FILES) client.write(jbuff, 64);
    remaining -= bytesToRead;
  } 
  if (SAVE_FILES) client.stop();
  if (jpglen > 0){
    photo_ready = true;
  }else{    
    ESP.restart();
  }
}

void process_photo(){
  boolean decoded = JpegDec.decodeArray(jpg_data, jpglen);
  if (decoded) {
    //jpegInfo();
    jpegRender();
  }else{
    debug("didn't decode though");
  }   
}

void jpegInfo() {
  debug(F("==============="));
  debug(F("JPEG image info"));
  debug(F("==============="));
  debug(S+  "Width      : "+JpegDec.width);
  debug(S+  "Height     : "+JpegDec.height);
  debug(S+  "Components : "+JpegDec.comps);
  debug(S+  "MCU / row  : "+JpegDec.MCUSPerRow);
  debug(S+  "MCU / col  : "+JpegDec.MCUSPerCol);
  debug(S+  "Scan type  : "+JpegDec.scanType);
  debug(S+  "MCU width  : "+JpegDec.MCUWidth);
  debug(S+  "MCU height : "+JpegDec.MCUHeight);
  debug(F("==============="));
}

int get_digit(byte buffer[][DIGIT_WIDTH]){
  for(int y = 0; y < DIGIT_HEIGHT;y++){
    // Find middle brightness of row:
    byte rmin = 255;
    byte rmax = 0;
    for(int x = 0; x < DIGIT_WIDTH; x++){
      if (buffer[y][x] > rmax) rmax = buffer[y][x];
      if (buffer[y][x] < rmin) rmin = buffer[y][x];
    }
    String row = "";
    byte row_mid = (rmax - rmin)/2 + rmin;
    // Change colors to binary brighter/darker than mid
    for(int x = 0; x < DIGIT_WIDTH; x++){
      if (buffer[y][x] > row_mid) buffer[y][x]  = 0;
      else buffer[y][x]  = 1;  
      
    }
  }
  /*
  // Check each segment:
  byte bin_val = 0;
  // top left
  if (buffer[3][0] + buffer[3][1] + buffer[4][0] + buffer[4][1] > 2 ||
      buffer[3][1] + buffer[3][2] + buffer[4][1] + buffer[4][2] > 2 ||
      buffer[3][0] + buffer[4][0] + buffer[5][0] > 2 ||
      buffer[3][1] + buffer[4][1] + buffer[5][1] > 2 )
      bin_val |= 1; // 1
  // bottom left
  if (buffer[10][0] + buffer[10][1] + buffer[11][0] + buffer[11][1] > 2 ||
      buffer[10][1] + buffer[10][2] + buffer[11][1] + buffer[11][2] > 2 ||
      buffer[9][0] + buffer[10][0] + buffer[11][0] > 2 ||
      buffer[9][1] + buffer[10][1] + buffer[11][1] > 2) 
      bin_val |= 2; // 2
  // top right
  if (buffer[3][4] + buffer[3][5] + buffer[4][4] + buffer[4][5] > 2 ||
      buffer[3][5] + buffer[3][6] + buffer[4][5] + buffer[4][6] > 2 ||
      buffer[4][5] + buffer[5][5] + buffer[6][5] > 2 ||
      buffer[4][6] + buffer[5][6] + buffer[6][6] > 2 ) 
      bin_val |= 4; // 3
  // bottom right
  if (buffer[10][4] + buffer[10][5] + buffer[11][4] + buffer[11][5] > 2 ||
      buffer[10][5] + buffer[10][6] + buffer[11][5] + buffer[11][6] > 2 ||
      buffer[9][5] + buffer[10][5] + buffer[11][5] + buffer[12][5] > 2 ||
      buffer[9][6] + buffer[10][6] + buffer[11][6] + buffer[12][6] > 2) 
      bin_val |= 8; // 4
  // top
  if (buffer[0][2] + buffer[0][3] + buffer[1][2] + buffer[1][3] > 3 ||
      buffer[1][2] + buffer[1][3] + buffer[2][2] + buffer[2][3] > 3)
      bin_val |= 16; // 5
  // middle
  if (buffer[7][2] + buffer[7][3] + buffer[8][2] + buffer[8][3] > 2 ||
      buffer[8][2] + buffer[8][3] + buffer[9][2] + buffer[9][3] > 2 || 
      buffer[6][2] + buffer[7][2] + buffer[8][2] + buffer[9][2] > 0 &&
      buffer[6][3] + buffer[7][3] + buffer[8][3] + buffer[9][3] > 0 &&
      buffer[6][4] + buffer[7][4] + buffer[8][4] + buffer[9][4] > 0 )
      bin_val |= 32; // 6
  // bottom
  if (buffer[13][3] + buffer[13][4] + buffer[14][3] + buffer[14][4] > 3 ||
      buffer[14][3] + buffer[14][4] + buffer[15][3] + buffer[15][4] > 3 )
      bin_val |= 64; // 7

  byte vals[]= {95, 3, 118, 115, 43, 121, 125, 67, 127, 107};
  for(int i=0; i<10;i++){
    if (bin_val == vals[i]) return i;
  }
  debug(S+"Invalid: "+bin_val);
  return -1;
  */
  if (buffer[1][3]){
        if (buffer[8][3]){
            if (buffer[3][5]){
                if (buffer[11][5]){
                    if (buffer[11][0]){
                        if (buffer[8][2]){
                            return 8;
                        }else{
                            return 0;
                        }
                    }else{
                        if (buffer[12][1]){
                            return 8;
                        }else{
                            return 6;
                        }
                    }
                }else{
                    if (buffer[6][1]){
                        return 8;
                    }else{
                        return 2;
                    }
                }
            }else{
                if (buffer[12][6]){
                    if (buffer[10][1]){
                        if (buffer[15][2]){
                            if (buffer[6][6]){
                                return 8;
                            }else{
                                return 9;
                            }
                        }else{
                            return 4;
                        }
                    }else{
                        if (buffer[4][6]){
                            return 6;
                        }else{
                            return 5;
                        }
                    }
                }else{
                    if (buffer[11][5]){
                        return 0;
                    }else{
                        return 3;
                    }
                }
            }
        }else{
            if (buffer[7][0]){
                if (buffer[1][2]){
                    if (buffer[0][2]){
                        return 0;
                    }else{
                        if (buffer[0][1]){
                            if (buffer[15][1]){
                                return 7;
                            }else{
                                return 1;
                            }
                        }else{
                            if (buffer[3][6]){
                                return 0;
                            }else{
                                return 4;
                            }
                        }
                    }
                }else{
                    if (buffer[14][3]){
                        return 7;
                    }else{
                        return 1;
                    }
                }
            }else{
                if (buffer[8][5]){
                    if (buffer[9][1]){
                        return 8;
                    }else{
                        return 6;
                    }
                }else{
                    if (buffer[13][6]){
                        return 0;
                    }else{
                        return 7;
                    }
                }
            }
        }
    }else{
        if (buffer[11][6]){
            if (buffer[14][2]){
                if (buffer[14][4]){
                    if (buffer[4][5]){
                        return 0;
                    }else{
                        return 9;
                    }
                }else{
                    return 4;
                }
            }else{
                if (buffer[2][0]){
                    return 4;
                }else{
                    return 8;
                }
            }
        }else{
            if (buffer[14][3]){
                if (buffer[11][5]){
                    if (buffer[0][1]){
                        return 0;
                    }else{
                        return 9;
                    }
                }else{
                    if (buffer[14][2]){
                        return 7;
                    }else{
                        return 1;
                    }
                }
            }else{
                return 1;
            }
        }
    }
}

void jpegRender() {
  
    int origin_x = -1;
    int origin_y = -1;
    int side_finds = 0;
  
    // retrieve infomration about the image
    uint16_t  *pImg;
    uint16_t mcu_w = JpegDec.MCUWidth;
    uint16_t mcu_h = JpegDec.MCUHeight;
    uint32_t max_x = JpegDec.width;
    uint32_t max_y = JpegDec.height;

    byte buffer[mcu_h][max_x];
    byte digits[NUM_DIGITS][DIGIT_HEIGHT][DIGIT_WIDTH];
    byte dot_col[8];
    int col = 0;
  
    // Skip first 40 lines or 50 blocks
    for (int i =0;i<50;i++)JpegDec.read();
    
    // Read 40 lines or 50 blocks
    int mcu_row = 0;
    for (int block =0;block<50;block++){
        for (int mcu_col = 0; mcu_col < 10; mcu_col++){
            JpegDec.read();
        
            // save a pointer to the image block
            pImg = JpegDec.pImage;
        
            for (int y = 0; y < mcu_h; y++){      
              for (int x = 0; x < mcu_w; x++){
                 buffer[y][mcu_col*mcu_w+x] = pixel_to_green(*pImg++);
              } 
            }
        }
        for(int y =0; y < mcu_h; y++){
            if (mcu_row*8+y < 8)
                dot_col[y] = buffer[y][69];
            // Find corner/origin of LCD- it's a dark area after a light area
            // Find it 3 times to be sure it's not a light corner
            if (origin_x < 0){
                bool light = false;
                for (int xa = 30; xa < 44; xa++){
                  if (light && buffer[y][xa] < 200){
                    side_finds++;

                    if (side_finds > 2){
                      origin_x = xa;
                      origin_y = y+mcu_row*8;
                      // Set origin at corner of digit
                      origin_x += 3;
                      origin_y += 3;
                    }
                    break;
                  }
                  if (buffer[y][xa] > 200) light = true;
                }
            }else{                
                if (mcu_row*8+y == origin_y-1){
                    // find dot to fine tune origin alignment
                    byte dot_row[10];
                    byte min_row = 255;
                    byte max_row = 0;
                    for (int xa = 0; xa < 10; xa++){
                        dot_row[xa] = buffer[y][origin_x + 25 + xa];
                        if (dot_row[xa]< min_row) min_row = dot_row[xa];
                        if (dot_row[xa]> max_row) max_row = dot_row[xa];
                    }
                    byte mid = (max_row - min_row)/2 + min_row;
                    for (int xa = 2; xa < 10; xa++){
                        if (dot_row[xa] > mid) origin_x += 1;
                        if (dot_row[xa] < mid) break;
                    }
                    
                    // now fine tune y axis -make sure dot isn't too close to top
                    byte min_col = 255;
                    byte max_col = 0;
                    for (int i = origin_y-4; i < 8; i++){
                        if (dot_col[i] < min_col) min_col = dot_col[i];
                        if (dot_col[i] > max_col) max_col = dot_col[i];
                    }
                    byte cmid = (max_col - min_col)/2 + min_col;
                    if (dot_col[origin_y-3] < cmid){
                        origin_y+=1;
                        continue;
                    }
                    
                }
                // Store digits
                if (mcu_row*8+y >= origin_y && mcu_row*8+y < origin_y+16){   
                    int yd = mcu_row*8+y - origin_y;
                    for(int d = 0; d < NUM_DIGITS; d++){
                        for (int w = 0; w< DIGIT_WIDTH; w++){
                            int x = origin_x + d*(DIGIT_WIDTH + DIGIT_SPACE) + w;
                            digits[d][yd][w] = buffer[y][x];
                        }                                 
                    }
                }
                if  (mcu_row*8+y > origin_y+16) break;
            }
          ESP.wdtFeed();
        } 
        mcu_row++; 
        if  (origin_y > 0 && mcu_row*8 > origin_y+16) break;
    }
    debug(S+"Origin: "+origin_x+", "+origin_y);
    delay(50);
    debug("Stored digit data, identifying digits...");
    ESP.wdtFeed();
    String reading = "";
    for(int d = 0; d < NUM_DIGITS; d++){
        int x = get_digit(digits[d]);
        if (x >=0){
            reading = x + reading;
        }
        ESP.wdtFeed();
    }

    reading[0] = '0';
    if (reading.length() == NUM_DIGITS && reading[0] != '8' && reading[1] != '8'){
      reading = reading.substring(0, 6) + "." + reading.substring(6);
      while(reading[0] == '0') reading.remove(0,1);
      mqttClient.publish(waterMeterTopic, reading.c_str());
      debug("Meter reading: "+reading);
    }else{
      debug("***Invalid Reading:"+reading);
    }
}

void set_lights(bool val){
  digitalWrite(LIGHT_PIN, val);
  lights_on = val;
  lights_time = millis();
}

void mqtt_callback(char* topic, String pl){
  // Toggle Lock
  if (strcmp(topic, waterQueryTopic) == 0){
    debug("Ready to take photo - activating lights");
    set_lights(ON);
  }
}

void mqtt_connect(){
    mqttClient.subscribe(waterQueryTopic);;   
}

void setup() {

  KDevice_setup(ROOM, HOSTNAME_PREF, MQTT_HOST, WIFI_SSID, WIFI_PASS, DEBUG_HOST, DEBUG_PORT, &mqtt_callback);
  Serial.begin(38400);
  pinMode(LIGHT_PIN, OUTPUT); 

  
  // Try to locate the camera
  if (cam.begin()) {
    debug("Camera Found:");
  } else {
    debug("No camera found?");
    return;
  }
  // Print out the camera version information (optional)
  uint8_t args2[] = {0x01};  
  sendCommand(VC0706_GEN_VERSION, args2, 1);
  char *reply = cam.getVersion();
  if (reply == 0) {
    debug("Failed to get version");
  } else {
    debug("-----------------");
    debug(reply);
    debug("-----------------");
  }
  
  uint8_t args3[] = {0x1, 0x22};
  sendCommand(VC0706_DOWNSIZE_CTRL,args3, sizeof(args3));
  delay(200);  
  last_photo = 0;
  wm_start_time = millis();
}


void loop() {
  if (photo_ready){
    photo_ready = false;
    debug("Processing photo");
    process_photo();
    debug("Processing Complete - restarting");
    ESP.restart();
  }
  if (lights_on && millis() - lights_time > LIGHT_UP_DELAY){
    debug("Taking photo");
    take_photo();  
    set_lights(OFF);
    // Continue loop before processing to maintain wifi
  }
  // Reset once a day
  if  (millis() - wm_start_time > WM_RESET_TIME){
    reset();
  }
  KDevice_loop(MQTT_USER, MQTT_PASS, mqtt_connect);
}

