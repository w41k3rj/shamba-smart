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

long duration;
int distance;
int soilValue;
int soilPercent;
bool btReady = false;

void setup()
{
  pinMode(TRIG,OUTPUT);
  pinMode(ECHO,INPUT);
  pinMode(PUMP1,OUTPUT);
  pinMode(PUMP2,OUTPUT);
  pinMode(LAMP1,OUTPUT);
  pinMode(LAMP2,OUTPUT);
  pinMode(LAMP3,OUTPUT);

  digitalWrite(PUMP1,HIGH);
  digitalWrite(PUMP2,HIGH);
  digitalWrite(LAMP1,LOW);
  digitalWrite(LAMP2,LOW);
  digitalWrite(LAMP3,HIGH);

  lcd.init();
  lcd.backlight();

  BT.begin(9600);
  Serial.begin(9600);

  lcd.setCursor(0,0);
  lcd.print("WAIT BLUETOOTH");
}

void loop()
{
  if(btReady == false)
  {
    if(BT.available())
    {
      btReady = true;
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("BT CONNECTED");
      delay(1000);
    }
    return;
  }

  digitalWrite(TRIG,LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG,HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG,LOW);

  duration = pulseIn(ECHO,HIGH,30000);
  distance = duration * 0.034 / 2;

  if(distance > 10)
  {
    digitalWrite(PUMP1,LOW);
    digitalWrite(LAMP1,HIGH);
    lcd.setCursor(0,0);
    lcd.print("TANK: EMPTY     ");
    lcd.setCursor(0,1);
    lcd.print("PUMP1: AUTO ON ");
  }
  else
  {
    digitalWrite(PUMP1,HIGH);
    digitalWrite(LAMP1,LOW);
    lcd.setCursor(0,0);
    lcd.print("TANK: FULL      ");
    lcd.setCursor(0,1);
    lcd.print("PUMP1: AUTO OFF");
  }

  soilValue = analogRead(SOIL);
  soilPercent = map(soilValue,1023,0,0,100);
  soilPercent = constrain(soilPercent,0,100);

  lcd.setCursor(0,2);
  lcd.print("SOIL: ");
  lcd.print(soilPercent);
  lcd.print("%   ");

  lcd.setCursor(0,3);
  if(soilPercent < 30)
    lcd.print("STATUS: DRY     ");
  else if(soilPercent < 70)
    lcd.print("STATUS: NORMAL  ");
  else
    lcd.print("STATUS: WET     ");

  if(BT.available())
  {
    char cmd = BT.read();
    Serial.println(cmd);

    if(cmd == '1')
    {
      digitalWrite(PUMP2,LOW);
      digitalWrite(LAMP2,HIGH);
    }

    if(cmd == '0')
    {
      digitalWrite(PUMP2,HIGH);
      digitalWrite(LAMP2,LOW);
    }
  }

  delay(500);
}
