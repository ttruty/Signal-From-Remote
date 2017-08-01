

// the setup routine runs once when you press reset:
void setup() {                
  // initialize the digital pin as an output.
  pinMode(1, OUTPUT); //LED on Model A  or Pro
  pinMode(0, OUTPUT); //LED on Model B
  pinMode(4, INPUT); //

  digitalWrite(0, LOW);
  digitalWrite(1, LOW);
}

// the loop routine runs over and over again forever:
void loop() {
  int sensorValue = analogRead(2); //Read P4
  delay(1);
  digitalWrite(0, LOW); // sound
  digitalWrite(1, LOW); // light
  if ((sensorValue  > 100) && (sensorValue < 200)) {
     // tone(0, 2000, 500);
     digitalWrite(1, HIGH);
     delay(1000);
     digitalWrite(1, LOW);
  } 
}
