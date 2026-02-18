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

// ============ FAST RESPONSE ULTRASONIC VARIABLES ============
const int NUM_SAMPLES = 3;           // Only 3 samples for faster response
float distanceSamples[NUM_SAMPLES];  // Array to store samples
int sampleIndex = 0;                  // Current index
float lastValidDistance = 22.0;       // Last valid reading
int consecutiveSameValue = 0;         // Count consecutive same readings
float previousRawReading = 22.0;      // Previous raw reading for comparison
const int STABILITY_THRESHOLD = 2;    // How many same readings to confirm change
// ============================================================

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
    
    // Read with shorter timeout (10ms = about 170cm range - plenty for your tank)
    duration = pulseIn(ECHO, HIGH, 10000);
    
    if(duration > 0) {
      float tempDistance = duration * 0.034 / 2;
      
      // Check if reading is reasonable (between 2cm and 200cm)
      if(tempDistance >= 2 && tempDistance <= 200) {
        readings[validCount] = tempDistance;
        validCount++;
      }
    }
    
    // Minimal delay between readings (faster!)
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
    
    // Update running average (simple and fast)
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
  lcd.print("WATER MONITOR v3");
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
    
    // Calculate water level percentage
    waterLevelPercent = ((EMPTY_DISTANCE - distance) / TANK_HEIGHT) * 100.0;
    waterLevelPercent = constrain(waterLevelPercent, 0, 100);
    
    // AUTO FILL CONTROL LOGIC - Check every reading for fast response
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
  }

  // Read soil moisture every 500ms (doesn't need to be fast)
  static unsigned long lastSoilRead = 0;
  if(millis() - lastSoilRead >= 500) {
    soilValue = analogRead(SOIL);
    soilPercent = map(soilValue, 1023, 0, 0, 100);
    soilPercent = constrain(soilPercent, 0, 100);
    lastSoilRead = millis();
  }

  // Update LCD every 200ms (smooth but not too fast)
  if(millis() - lastDisplayUpdate >= 200) {
    updateLCD();
    lastDisplayUpdate = millis();
  }
  
  // Handle Bluetooth commands immediately
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

  // Serial debugging every 1 second
  if(millis() - lastSerialOutput > 1000) {
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
    lastSerialOutput = millis();
  }
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
  if(waterLevelPercent < 10) lcd.print(" ");
  lcd.print(waterLevelPercent, 1);
  lcd.print("%      ");
  
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
  // Removed the errorCount reference
}