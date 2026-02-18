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

// ============ IMPROVED ULTRASONIC VARIABLES ============
const int NUM_SAMPLES = 7;           // More samples for better averaging
float distanceSamples[NUM_SAMPLES];  // Array to store samples
int sampleIndex = 0;                  // Current index
float lastValidDistance = 22.0;       // Last valid reading (start with empty)
int errorCount = 0;                    // Count consecutive errors
const int MAX_ERRORS = 5;              // Allow more errors before giving up
unsigned long lastSensorReadTime = 0;
const unsigned long SENSOR_DELAY = 100; // Minimum delay between readings
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

// ============ IMPROVED ULTRASONIC READING FUNCTION ============
float readUltrasonic() {
  unsigned long currentTime = millis();
  
  // Ensure minimum delay between readings
  if (currentTime - lastSensorReadTime < SENSOR_DELAY) {
    return lastValidDistance;
  }
  
  // Take multiple readings quickly
  float readings[5]; // Take 5 readings
  int validReadings = 0;
  
  for(int i = 0; i < 5; i++) {
    // Trigger the sensor
    digitalWrite(TRIG, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG, LOW);
    
    // Read with timeout (30ms = about 5m range)
    duration = pulseIn(ECHO, HIGH, 30000);
    
    if(duration > 0) {
      float tempDistance = duration * 0.034 / 2;
      
      // Check if reading is reasonable (between 2cm and 200cm)
      if(tempDistance >= 2 && tempDistance <= 200) {
        readings[validReadings] = tempDistance;
        validReadings++;
      }
    }
    
    // Small delay between readings
    delayMicroseconds(500);
  }
  
  lastSensorReadTime = currentTime;
  
  // If we have valid readings
  if(validReadings > 0) {
    errorCount = 0; // Reset error count
    
    // Calculate average of valid readings
    float sum = 0;
    for(int i = 0; i < validReadings; i++) {
      sum += readings[i];
    }
    float avgReading = sum / validReadings;
    
    // Add to running average
    distanceSamples[sampleIndex] = avgReading;
    sampleIndex = (sampleIndex + 1) % NUM_SAMPLES;
    
    // Calculate smooth average
    float smoothSum = 0;
    for(int i = 0; i < NUM_SAMPLES; i++) {
      smoothSum += distanceSamples[i];
    }
    float smoothReading = smoothSum / NUM_SAMPLES;
    
    // Only accept if change is reasonable (less than 10cm jump)
    if(abs(smoothReading - lastValidDistance) < 10.0 || lastValidDistance == 22.0) {
      lastValidDistance = smoothReading;
      return smoothReading;
    } else {
      // Gradual change - move 20% toward new reading
      lastValidDistance = lastValidDistance * 0.8 + smoothReading * 0.2;
      return lastValidDistance;
    }
  } else {
    // No valid readings
    errorCount++;
    
    if(errorCount > MAX_ERRORS) {
      Serial.println("Warning: Ultrasonic sensor not responding!");
      // Return last valid distance but maybe indicate problem
      return lastValidDistance;
    }
    
    return lastValidDistance; // Return last valid reading
  }
}
// ============================================================

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

  // ============ GET ULTRASONIC READING ============
  distance = readUltrasonic();
  // =================================================
  
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
  if(millis() - lastSerial > 2000) {
    Serial.println("=== SYSTEM STATUS ===");
    Serial.print("Raw Distance: ");
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
    Serial.print("Sensor errors: ");
    Serial.println(errorCount);
    lastSerial = millis();
  }

  delay(300);
}

// ============ UPDATE LCD FUNCTION ============
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