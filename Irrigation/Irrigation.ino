const float SensorOffset = 102.0;
// the setup routine runs once when you press reset:
void setup() {
  // initialize serial communication at 9600 bits per second:
  Serial.begin(9600);
}
 
// the loop routine runs over and over again forever:
void loop() {
  // read the input on analog pin 0:
  float sensorValue = (analogRead(A0)-SensorOffset)/100.0; //Do maths for calibration
  // print out the value you read:
  Serial.print("Air Pressure: ");  
  Serial.print(sensorValue,2);
  Serial.println(" kPa");
  
  delay(1000);        // delay in between reads for stability
}
