#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>
#include <Arduino.h>

LiquidCrystal_I2C lcd(0x27,20,4);
SoftwareSerial BT(10,11);

#define TRIG 2
#define ECHO 3
#define PUMP1 4
#define PUMP2 5
#define LAMP1 6
#define LAMP2 7
#define LAMP3 8
#define SOIL A0

// Tank specifications
const float TANK_HEIGHT = 20.0;        // Your tank height in cm
const float SENSOR_OFFSET = 2.0;        // Sensor mounted above tank
const float EMPTY_DISTANCE = TANK_HEIGHT + SENSOR_OFFSET;  // 22cm when empty
const float FULL_DISTANCE = SENSOR_OFFSET;                  // 2cm when full

// Thresholds
const int EMPTY_THRESHOLD = 20;    // Pump ON when below 20%
const int FULL_THRESHOLD = 90;      // Pump OFF when above 90%

// ============ ADDED FOR SMOOTH ULTRASONIC ============
const int NUM_SAMPLES = 5;           // Number of samples for averaging
float distanceSamples[NUM_SAMPLES];  // Array to store samples
int sampleIndex = 0;                  // Current index
float lastValidDistance = 22.0;       // Last valid reading (start with empty)
int errorCount = 0;                    // Count consecutive errors
const int MAX_ERRORS = 3;              // Max errors before using last value
// =====================================================

long duration;
float distance;
int soilValue;
int soilPercent;
float waterLevelPercent;
bool btReady = false;
bool pump1State = HIGH;
bool pump2State = HIGH;

// Function declarations
void updateLCD();

// ============ SETUP FUNCTION ============
void setup()
{
  pinMode(TRIG,OUTPUT);
  pinMode(ECHO,INPUT);
  pinMode(PUMP1,OUTPUT);
  pinMode(PUMP2,OUTPUT);
  pinMode(LAMP1,OUTPUT);
  pinMode(LAMP2,OUTPUT);
  pinMode(LAMP3,OUTPUT);

  digitalWrite(PUMP1, HIGH);   // Pump1 OFF
  digitalWrite(PUMP2, HIGH);   // Pump2 OFF
  digitalWrite(LAMP1, LOW);    // Lamp1 OFF
  digitalWrite(LAMP2, LOW);    // Lamp2 OFF
  digitalWrite(LAMP3, HIGH);   // Lamp3 ON

  lcd.init();
  lcd.backlight();
  
  BT.begin(9600);
  Serial.begin(9600);

  // ============ INITIALIZE SAMPLE ARRAY ============
  for(int i = 0; i < NUM_SAMPLES; i++) {
    distanceSamples[i] = EMPTY_DISTANCE;
  }
  // =================================================

  lcd.setCursor(0,0);
  lcd.print("WATER MONITOR v2");
  lcd.setCursor(0,1);
  lcd.print("Tank: 20cm");
  lcd.setCursor(0,2);
  lcd.print("Wait Bluetooth");
  
  delay(2000);
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("WAIT BLUETOOTH");
}

// ============ NEW FUNCTION FOR SMOOTH ULTRASONIC ============
float readSmoothUltrasonic() {
  float rawReadings[3];  // Take 3 quick samples
  
  // Take 3 samples in quick succession
  for(int i = 0; i < 3; i++) {
    digitalWrite(TRIG, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG, LOW);
    
    duration = pulseIn(ECHO, HIGH, 30000);
    
    if(duration == 0) {
      rawReadings[i] = lastValidDistance;  // Use last valid on error
      errorCount++;
    } else {
      rawReadings[i] = duration * 0.034 / 2;
      errorCount = 0;
    }
    delayMicroseconds(500);  // Small delay between samples
  }
  
  // If too many errors, return last valid
  if(errorCount > MAX_ERRORS) {
    Serial.println("Sensor unstable - using last valid");
    return lastValidDistance;
  }
  
  // Sort the 3 readings to find median
  float temp;
  for(int i = 0; i < 2; i++) {
    for(int j = i+1; j < 3; j++) {
      if(rawReadings[i] > rawReadings[j]) {
        temp = rawReadings[i];
        rawReadings[i] = rawReadings[j];
        rawReadings[j] = temp;
      }
    }
  }
  
  float medianReading = rawReadings[1];  // Middle value
  
  // Add to running average
  distanceSamples[sampleIndex] = medianReading;
  sampleIndex = (sampleIndex + 1) % NUM_SAMPLES;
  
  // Calculate average
  float sum = 0;
  for(int i = 0; i < NUM_SAMPLES; i++) {
    sum += distanceSamples[i];
  }
  float smoothReading = sum / NUM_SAMPLES;
  
  // Only accept readings that make sense (not jumping more than 5cm)
  if(abs(smoothReading - lastValidDistance) < 5.0 || lastValidDistance == 22.0) {
    lastValidDistance = smoothReading;
    return smoothReading;
  } else {
    // If jump is too big, use last valid
    Serial.println("Large jump detected - filtered");
    return lastValidDistance;
  }
}
// ============================================================

// ============ LOOP FUNCTION ============
void loop()
{
  // Check Bluetooth connection
  if(btReady == false)
  {
    if(BT.available())
    {
      btReady = true;
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("BT CONNECTED");
      delay(1000);
      lcd.clear();
    }
    return;
  }

  // ============ REPLACED WITH SMOOTH READING ============
  distance = readSmoothUltrasonic();
  // ======================================================
  
  // Constrain distance to valid range
  if(distance < FULL_DISTANCE) distance = FULL_DISTANCE;
  if(distance > EMPTY_DISTANCE) distance = EMPTY_DISTANCE;
  
  // Calculate water level percentage
  waterLevelPercent = ((EMPTY_DISTANCE - distance) / TANK_HEIGHT) * 100.0;
  waterLevelPercent = constrain(waterLevelPercent, 0, 100);
  
  // AUTO FILL CONTROL LOGIC
  if(waterLevelPercent < EMPTY_THRESHOLD)
  {
    if(pump1State == HIGH) {
      digitalWrite(PUMP1, LOW);
      digitalWrite(LAMP1, HIGH);
      pump1State = LOW;
      Serial.println("PUMP1 AUTO ON - Tank low");
    }
  }
  else if(waterLevelPercent > FULL_THRESHOLD)
  {
    if(pump1State == LOW) {
      digitalWrite(PUMP1, HIGH);
      digitalWrite(LAMP1, LOW);
      pump1State = HIGH;
      Serial.println("PUMP1 AUTO OFF - Tank full");
      BT.println("TANK FULL");
    }
  }

  // Read soil moisture
  soilValue = analogRead(SOIL);
  soilPercent = map(soilValue, 1023, 0, 0, 100);
  soilPercent = constrain(soilPercent, 0, 100);

  // Update LCD Display
  updateLCD();
  
  // Handle Bluetooth commands
  if(BT.available())
  {
    char cmd = BT.read();
    Serial.print("BT Command: ");
    Serial.println(cmd);

    if(cmd == '1')
    {
      digitalWrite(PUMP2, LOW);
      digitalWrite(LAMP2, HIGH);
      pump2State = LOW;
      BT.println("PUMP2 ON");
    }

    if(cmd == '0')
    {
      digitalWrite(PUMP2, HIGH);
      digitalWrite(LAMP2, LOW);
      pump2State = HIGH;
      BT.println("PUMP2 OFF");
    }
    
    if(cmd == 's')
    {
      BT.print("LVL:");
      BT.print(waterLevelPercent, 1);
      BT.print("%, SOIL:");
      BT.print(soilPercent);
      BT.print("%, P1:");
      BT.print(pump1State == LOW ? "ON" : "OFF");
      BT.print(", P2:");
      BT.println(pump2State == LOW ? "ON" : "OFF");
    }
  }

  // Serial debugging
  static unsigned long lastSerial = 0;
  if(millis() - lastSerial > 1000) {  // Changed to 1 second for more frequent debug
    Serial.println("=== SYSTEM STATUS ===");
    Serial.print("Distance: ");
    Serial.print(distance);
    Serial.print(" cm, Water: ");
    Serial.print(waterLevelPercent, 1);
    Serial.print("%, Soil: ");
    Serial.print(soilPercent);
    Serial.println("%");
    Serial.print("Pump1: ");
    Serial.print(pump1State == LOW ? "ON" : "OFF");
    Serial.print(", Pump2: ");
    Serial.println(pump2State == LOW ? "ON" : "OFF");
    lastSerial = millis();
  }

  delay(300);  // Reduced from 500ms to 300ms for smoother updates
}

// ============ UPDATE LCD FUNCTION (YOUR ORIGINAL CODE) ============
void updateLCD() {
  // Line 0: Tank status
  lcd.setCursor(0,0);
  lcd.print("TANK: ");
  if(waterLevelPercent < 10)
    lcd.print("CRITICAL ");
  else if(waterLevelPercent < EMPTY_THRESHOLD)
    lcd.print("LOW      ");
  else if(waterLevelPercent > FULL_THRESHOLD)
    lcd.print("FULL     ");
  else
    lcd.print("NORMAL   ");
  
  // Line 1: Water level percentage
  lcd.setCursor(0,1);
  lcd.print("LEVEL: ");
  lcd.print(waterLevelPercent, 1);
  lcd.print("%      ");
  
  // Line 2: Soil moisture
  lcd.setCursor(0,2);
  lcd.print("SOIL: ");
  lcd.print(soilPercent);
  lcd.print("% ");
  if(soilPercent < 30)
    lcd.print("DRY  ");
  else if(soilPercent < 70)
    lcd.print("NORMAL");
  else
    lcd.print("WET  ");
  
  // Line 3: Pump status
  lcd.setCursor(0,3);
  lcd.print("P1:");
  lcd.print(pump1State == LOW ? "ON " : "OFF");
  lcd.print(" P2:");
  lcd.print(pump2State == LOW ? "ON " : "OFF");
  lcd.print(" ");
}