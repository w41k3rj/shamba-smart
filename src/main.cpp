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
const float TANK_PHYSICAL_HEIGHT = 23.0;     // Actual tank height in cm
const float MAX_WATER_HEIGHT = 20.0;          // Maximum water height (to avoid sensor)
const float SENSOR_OFFSET = 3.0;               // Distance from sensor to MAX water level
const float EMPTY_DISTANCE = TANK_PHYSICAL_HEIGHT;  // 23cm when empty (sensor to bottom)
const float FULL_DISTANCE = SENSOR_OFFSET;           // 3cm when at max water level

// Thresholds
const int PUMP1_ON_THRESHOLD = 50;     // Pump1 ON when below 50%
const float PUMP1_OFF_HEIGHT = 15.0;    // Pump1 OFF when water reaches 15cm height

// ============ FAST RESPONSE ULTRASONIC VARIABLES ============
const int NUM_SAMPLES = 3;           // Only 3 samples for faster response
float distanceSamples[NUM_SAMPLES];  // Array to store samples
int sampleIndex = 0;                  // Current index
float lastValidDistance = 23.0;       // Last valid reading (start with empty)
int consecutiveSameValue = 0;         // Count consecutive same readings
float previousRawReading = 23.0;      // Previous raw reading for comparison
const int STABILITY_THRESHOLD = 2;    // How many same readings to confirm change
// ============================================================

long duration;
float distance;
int soilValue;
int soilPercent;
float waterLevelPercent;
float waterHeight;  // Actual water height in cm
bool btReady = false;
bool pump1State = HIGH;  // Pump1 - HIGH = OFF
bool pump2State = HIGH;  // Pump2 - HIGH = OFF

// Function declarations
void updateLCD();

// ============ FAST RESPONSE ULTRASONIC READING ============
float readUltrasonic() {
  // Take 3 readings and use median (faster than averaging)
  float readings[3];
  int validCount = 0;
  
  for(int i = 0; i < 3; i++) {
    // Trigger the sensor
    digitalWrite(TRIG, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG, LOW);
    
    // Read with shorter timeout (10ms = about 170cm range)
    duration = pulseIn(ECHO, HIGH, 10000);
    
    if(duration > 0) {
      float tempDistance = duration * 0.034 / 2;
      
      // Check if reading is reasonable (between 2cm and 200cm)
      if(tempDistance >= 2 && tempDistance <= 200) {
        readings[validCount] = tempDistance;
        validCount++;
      }
    }
    
    // Minimal delay between readings
    delayMicroseconds(200);
  }
  
  // If we have at least 2 valid readings
  if(validCount >= 2) {
    // Sort the valid readings
    for(int i = 0; i < validCount-1; i++) {
      for(int j = i+1; j < validCount; j++) {
        if(readings[i] > readings[j]) {
          float temp = readings[i];
          readings[i] = readings[j];
          readings[j] = temp;
        }
      }
    }
    
    // Use median (middle value) or average of middle two if even count
    float currentReading;
    if(validCount == 3) {
      currentReading = readings[1];  // Median of 3
    } else {
      currentReading = (readings[0] + readings[1]) / 2.0;  // Average of 2
    }
    
    // Check if reading is stable (same as previous)
    if(abs(currentReading - previousRawReading) < 0.5) {
      consecutiveSameValue++;
    } else {
      consecutiveSameValue = 0;
    }
    
    previousRawReading = currentReading;
    
    // Update running average
    distanceSamples[sampleIndex] = currentReading;
    sampleIndex = (sampleIndex + 1) % NUM_SAMPLES;
    
    // Calculate current average
    float sum = 0;
    for(int i = 0; i < NUM_SAMPLES; i++) {
      sum += distanceSamples[i];
    }
    float avgReading = sum / NUM_SAMPLES;
    
    // If reading is stable or change is significant, update immediately
    if(consecutiveSameValue >= STABILITY_THRESHOLD || abs(avgReading - lastValidDistance) > 2.0) {
      lastValidDistance = avgReading;
    }
    
    return lastValidDistance;
  }
  
  // If not enough valid readings, return last valid
  return lastValidDistance;
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

  // Initialize sample array
  for(int i = 0; i < NUM_SAMPLES; i++) {
    distanceSamples[i] = EMPTY_DISTANCE;
  }

  lcd.setCursor(0,0);
  lcd.print("WATER MONITOR v4");
  lcd.setCursor(0,1);
  lcd.print("Tank: 23/20cm");
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
  static unsigned long lastSensorRead = 0;
  static unsigned long lastDisplayUpdate = 0;
  static unsigned long lastSerialOutput = 0;
  
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

  // Read ultrasonic sensor every 100ms for fast response
  if(millis() - lastSensorRead >= 100) {
    distance = readUltrasonic();
    lastSensorRead = millis();
    
    // Constrain distance to valid range
    if(distance < FULL_DISTANCE) distance = FULL_DISTANCE;
    if(distance > EMPTY_DISTANCE) distance = EMPTY_DISTANCE;
    
    // Calculate water height in cm (actual water level)
    waterHeight = EMPTY_DISTANCE - distance;
    if(waterHeight < 0) waterHeight = 0;
    if(waterHeight > MAX_WATER_HEIGHT) waterHeight = MAX_WATER_HEIGHT;
    
    // Calculate water level percentage (for display)
    waterLevelPercent = (waterHeight / MAX_WATER_HEIGHT) * 100.0;
    waterLevelPercent = constrain(waterLevelPercent, 0, 100);
    
    // ============ PUMP1 CONTROL LOGIC ============
    // Pump1 turns ON when below 50%, turns OFF when reaches 15cm height
    
    // Debug output to see values
    Serial.print("Water Height: ");
    Serial.print(waterHeight);
    Serial.print("cm, Pump State: ");
    Serial.println(pump1State == LOW ? "ON" : "OFF");
    
    if(waterHeight < (MAX_WATER_HEIGHT * PUMP1_ON_THRESHOLD / 100.0)) // Below 50% (10cm)
    {
      // Below 10cm - turn pump ON if it's OFF
      if(pump1State == HIGH) {
        digitalWrite(PUMP1, LOW);  // Turn ON
        digitalWrite(LAMP1, HIGH);
        pump1State = LOW;
        Serial.println("PUMP1 ON - Water below 10cm");
        BT.println("PUMP1 ON - Filling tank");
      }
    }
    else if(waterHeight >= PUMP1_OFF_HEIGHT) // At or above 15cm
    {
      // At or above 15cm - turn pump OFF if it's ON
      if(pump1State == LOW) {
        digitalWrite(PUMP1, HIGH);  // Turn OFF
        digitalWrite(LAMP1, LOW);
        pump1State = HIGH;
        Serial.println("PUMP1 OFF - Water reached 15cm");
        BT.println("PUMP1 OFF - Tank full at 15cm");
      }
    }
    
    // Safety timeout (10 minutes)
    static unsigned long pumpStartTime = 0;
    static bool pumpTimerStarted = false;
    
    if(pump1State == LOW && !pumpTimerStarted) {
      pumpStartTime = millis();
      pumpTimerStarted = true;
    }
    
    if(pump1State == HIGH) {
      pumpTimerStarted = false;
    }
    
    // If pump runs for more than 10 minutes without reaching 15cm, force stop
    if(pump1State == LOW && (millis() - pumpStartTime > 600000)) { // 10 minutes
      if(waterHeight > 14) { // If close to 15cm, consider it done
        digitalWrite(PUMP1, HIGH);
        digitalWrite(LAMP1, LOW);
        pump1State = HIGH;
        pumpTimerStarted = false;
        Serial.println("PUMP1 FORCE OFF - Timeout reached");
        BT.println("PUMP1 FORCE OFF - Timeout");
      }
    }
  }

  // Read soil moisture every 500ms
  static unsigned long lastSoilRead = 0;
  if(millis() - lastSoilRead >= 500) {
    soilValue = analogRead(SOIL);
    soilPercent = map(soilValue, 1023, 0, 0, 100);
    soilPercent = constrain(soilPercent, 0, 100);
    lastSoilRead = millis();
  }

  // Update LCD every 200ms
  if(millis() - lastDisplayUpdate >= 200) {
    updateLCD();
    lastDisplayUpdate = millis();
  }
  
  // Handle Bluetooth commands
  if(BT.available())
  {
    char cmd = BT.read();
    Serial.print("BT Command: ");
    Serial.println(cmd);

    if(cmd == '1')  // Manual ON for Pump2
    {
      digitalWrite(PUMP2, LOW);
      digitalWrite(LAMP2, HIGH);
      pump2State = LOW;
      BT.println("PUMP2 MANUAL ON");
    }

    if(cmd == '0')  // Manual OFF for Pump2
    {
      digitalWrite(PUMP2, HIGH);
      digitalWrite(LAMP2, LOW);
      pump2State = HIGH;
      BT.println("PUMP2 MANUAL OFF");
    }
    
    if(cmd == 's')  // Status request
    {
      BT.print("H:");
      BT.print(waterHeight, 1);
      BT.print("cm (");
      BT.print(waterLevelPercent, 1);
      BT.print("%), SOIL:");
      BT.print(soilPercent);
      BT.print("%, P1:");
      BT.print(pump1State == LOW ? "ON" : "OFF");
      BT.print(", P2:");
      BT.println(pump2State == LOW ? "ON" : "OFF");
    }
  }

  // Serial debugging every 1 second
  if(millis() - lastSerialOutput > 1000) {
    Serial.println("=== SYSTEM STATUS ===");
    Serial.print("Distance: ");
    Serial.print(distance);
    Serial.print(" cm, Water Height: ");
    Serial.print(waterHeight, 1);
    Serial.print(" cm (");
    Serial.print(waterLevelPercent, 1);
    Serial.print("%), Soil: ");
    Serial.print(soilPercent);
    Serial.println("%");
    Serial.print("Pump1: ");
    Serial.print(pump1State == LOW ? "ON" : "OFF");
    Serial.print(", Pump2: ");
    Serial.println(pump2State == LOW ? "ON" : "OFF");
    lastSerialOutput = millis();
  }
}

// ============ UPDATE LCD FUNCTION ============
void updateLCD() {
  // Line 0: Tank status
  lcd.setCursor(0,0);
  lcd.print("TANK: ");
  if(waterHeight < 5)
    lcd.print("CRITICAL ");
  else if(waterHeight < 10)
    lcd.print("LOW      ");
  else if(waterHeight >= 14.5)
    lcd.print("FULL     ");
  else
    lcd.print("NORMAL   ");
  
  // Line 1: Water height and percentage
  lcd.setCursor(0,1);
  lcd.print("H: ");
  if(waterHeight < 10) lcd.print(" ");
  lcd.print(waterHeight, 1);
  lcd.print("cm ");
  lcd.print(waterLevelPercent, 0);
  lcd.print("%   ");
  
  // Line 2: Soil moisture
  lcd.setCursor(0,2);
  lcd.print("SOIL: ");
  if(soilPercent < 10) lcd.print(" ");
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
  
  // Show target on LCD
  lcd.setCursor(12,3);
  lcd.print("T:15cm");
}