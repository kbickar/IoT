
#include <WS2812FX.h>
#define LED_COUNT 300
#define LED_PIN  D6

WS2812FX ws2812fx = WS2812FX(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// Start Bright? Brightness RGB?   R       G       B      Mode? Mode
// [126] [0|1]   [0-255]    [0|1] [0-255] [0-255] [0-255] [0|1] [0-50]
void readData(){
  if (Serial.available() == 0) return;
  int start= Serial.read();
  while (start != 126 && Serial.available() > 0) start= Serial.read();
  if (start != 126) return;
  /*Serial.println("");
  Serial.print("start: ");
  Serial.print(start);*/

  int br = Serial.read();
  /*Serial.println("");
  Serial.print("br: ");
  Serial.print(br);*/
  if (br != 0 && br != 1) return;
  if (br == 1){
      br = Serial.read();
  /*Serial.print(" = ");
  Serial.print(br);*/
      ws2812fx.setBrightness(br);    
  }

  int rgb = Serial.read();
 /* Serial.println("");
  Serial.print("rgb: ");
  Serial.print(rgb);*/
  if (rgb != 0 && rgb != 1) return;
  if (rgb == 1){
      byte r = Serial.read();
      byte g = Serial.read();
      byte b = Serial.read();
  /*Serial.print(" = ");
  Serial.print(r);
  Serial.print(", ");
  Serial.print(g);
  Serial.print(", ");
  Serial.print(b);*/
      ws2812fx.setColor(r, g, b);  
  }

  int e = Serial.read();
  /*Serial.println("");
  Serial.print("mode: ");
  Serial.print(e);*/
  if (e != 0 && e != 1) return;
  if (e == 1){
      e = Serial.read();
 /* Serial.print(" = ");
  Serial.print(e);*/
      ws2812fx.setMode(e);   
  }
  /*Serial.println("");*/

}

void setup() { 
  Serial.begin(115200);
  Serial.println("Hello!");
  ws2812fx.init();
  ws2812fx.setBrightness(100);
  ws2812fx.setSpeed(200);
  ws2812fx.setMode(FX_MODE_RAINBOW_CYCLE);
  ws2812fx.start();
  while (Serial.available() > 0) Serial.read();
}

void loop(){ 
  while (Serial.available() > 0) Serial.read();
  ws2812fx.service();
     
  Serial.write(1);
  for ( int i = 0; i<200 && Serial.available() == 0; i++) delay(1);
  readData();
}


