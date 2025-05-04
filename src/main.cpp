#include <Arduino.h>

#include <Adafruit_ADS1X15.h>
#include "SPIFFS.h"
#include <LovyanGFX.hpp>
#include "lovyangfx_mysetup.h"

#define LGFX_USE_V1
#define TOUCH_DELAY 500
#define BUTTON_W 220
#define BUTTON_H 50
#define BUTTON_MENU_CX 120
#define BUTTON_MENU_CY 300
#define BUTTON_LOCK_CX 120
#define BUTTON_LOCK_CY 240
#define BUTTON_AIRCAL_CX 120
#define BUTTON_AIRCAL_CY 40
#define BUTTON_SETALT_CX 120
#define BUTTON_SETALT_CY 40
#define BUTTON_O2CAL_CX 120
#define BUTTON_O2CAL_CY 100
#define BUTTON_CHECKLIN_CX 120
#define BUTTON_CHECKLIN_CY 100
#define BUTTON_HECAL_CX 120
#define BUTTON_HECAL_CY 160
#define BUTTON_DBG_CX 120
#define BUTTON_DBG_CY 160
#define BUTTON_NEXT_CX 120
#define BUTTON_NEXT_CY 220
#define BUTTON_TCAL_CX 120
#define BUTTON_TCAL_CY 220
#define BUTTON_EXIT_CX 120
#define BUTTON_EXIT_CY 280

#define RA_SIZE 20

#define BATTPIN  4
#define SDAPIN  42
#define SCLPIN  44

#define PRESSURE_CALC_CONSTANT 0.00011464

// TFT
LGFX tft;
lgfx::touch_point_t tp;
LGFX_Sprite tSpr(&tft);
LGFX_Sprite modSpr(&tft);
int16_t h;
int16_t w;
float tx, ty; // touch x,y coordinates

// Touch Calibration
float ax = 0; // (expectedMax -expectedMin) / (xmax-xmin)
float bx = 0; //  expectedMin - xmin * ax
float ay = 0;
float by = 0;

// Sensor Calibration
float calibO2InAir  = 0;
float calibHeInAir  = 0;
float calibO2InO2   = 0;
float calibHeInO2   = 0;
float calibO2InHe   = 0;
float calibHeInHe   = 0;

// Altitude
uint16_t altitude   = 0;

// Sensor Curves
float a1O2;
float b1O2;
float a2O2;
float b2O2;
float bHe;
float cHe;
float xSens;

// Data values
float o2Val = 5.567;
float heVal = 25.321;
int32_t o2SensorReading[RA_SIZE];
int32_t heSensorReading[RA_SIZE];
float o2SensorReadingAvg;
float heSensorReadingAvg;
int avgCounter = 0;
float battScale = 1.0;

// Other
uint32_t tmpTimer;
Adafruit_ADS1115 ads;

void dataStoredScreen(){
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN);
  tft.setTextDatum(CC_DATUM);
  tft.setFont(&fonts::FreeSans18pt7b);
  tft.drawString("New Data", w/2, h/2 - 50);
  tft.drawString("Stored", w/2, h/2);
  tft.drawString("Successfully!", w/2, h/2 + 50);

  delay(3000);
}

float pressureRatio(){
  return exp(PRESSURE_CALC_CONSTANT * altitude);
}

void updateSpriteO2(bool clearSpr, uint8_t position) {
  if (clearSpr) tSpr.clearDisplay(TFT_BLACK);
  tSpr.setFont(&fonts::FreeSans24pt7b);
  tSpr.setTextDatum(TL_DATUM);
  tSpr.setTextColor(TFT_GREEN);
  if (position == 0) tSpr.setCursor(0, 20);
  else tSpr.setCursor(0, 80);
  if (o2Val == 100.0) tSpr.printf("%.0f %%", o2Val);
  else if (o2Val > 10.0) tSpr.printf("%.1f %%", o2Val);
  else tSpr.printf("0%.1f %%", o2Val);
}

void updateSpriteHe(bool clearSpr, uint8_t position) {
  if (clearSpr) tSpr.clearDisplay(TFT_BLACK);
  tSpr.setFont(&fonts::FreeSans24pt7b);
  tSpr.setTextDatum(TL_DATUM);
  tSpr.setTextColor(TFT_GREEN);
  if (position == 0) tSpr.setCursor(0, 20);
  else tSpr.setCursor(0, 80);
  if (heVal == 100.0) tSpr.printf("%.0f %%", heVal);
  else if (heVal > 10.0) tSpr.printf("%.1f %%", heVal);
  else tSpr.printf("0%.1f %%", heVal);
}

void updateSprite() {
  updateSpriteO2(true, 0);
  updateSpriteHe(false, 1);
  tSpr.pushSprite(80, 0);

  modSpr.clearDisplay(TFT_BLACK);
  modSpr.setFont(&fonts::FreeSans9pt7b);
  modSpr.setTextDatum(TL_DATUM);
  modSpr.setTextColor(TFT_YELLOW);
  float mod6 = (160/o2Val - 1) * 10;
  float mod4 = (140/o2Val - 1) * 10;
  float mod3 = (130/o2Val - 1) * 10;
  modSpr.setCursor(5, 15);
  modSpr.printf("MOD:"); 
  modSpr.setCursor(60, 0);
  modSpr.printf("(1.6)   %.0fm", mod6);
  modSpr.setCursor(60, 15);
  modSpr.printf("(1.4)   %.0fm", mod4);
  modSpr.setCursor(60, 30);
  modSpr.printf("(1.3)   %.0fm", mod3);

  modSpr.setTextColor(TFT_MAGENTA);
  modSpr.setCursor(170, 6);
  modSpr.printf("Altitude:"); 
  modSpr.setCursor(170, 24);
  modSpr.printf("%dm", altitude);
  modSpr.pushSprite(0, 140);
}

float getBattPercent() {
  float battVal = analogRead(BATTPIN) * battScale;
  if (battVal > 100.0) return 100.0;
  if (battVal < 0.0) return 0.0;
  return battVal;
}

float calcO2(float x){
  if (x < calibO2InAir)
    return (a1O2 * x + b1O2) * pressureRatio();
  else
    return (a2O2 * x + b2O2) * pressureRatio();
}

float calcHe(float x){
  return (x * bHe + cHe - o2Val * xSens) * pressureRatio(); 
}

void read_sensors() {
  int32_t x;
  // Measure O2
  x = ads.readADC_Differential_0_1();
  o2SensorReading[avgCounter] = x;
  // Measure He
  x = ads.readADC_Differential_2_3();
  heSensorReading[avgCounter] = x;

  avgCounter++;
  if (avgCounter == RA_SIZE) avgCounter = 0;

  o2SensorReadingAvg = 0;
  heSensorReadingAvg = 0;
  for(int ii = 0; ii < RA_SIZE; ii++) {
    o2SensorReadingAvg += o2SensorReading[ii];
    heSensorReadingAvg += heSensorReading[ii];
  }
  o2SensorReadingAvg /= RA_SIZE;
  heSensorReadingAvg /= RA_SIZE;

  o2Val = calcO2(o2SensorReadingAvg);
  if (o2Val < 0.0) o2Val = 0.0;
  if (o2Val > 100.0) o2Val = 100.0;
  heVal = calcHe(heSensorReadingAvg);
  if (heVal < 0.0) heVal = 0.0;
  if (heVal > 100.0) heVal = 100.0;
  if (o2Val + heVal > 100.0) heVal = 100.0 - o2Val;
}

void updateTouchCoordinates() {
  tx = float(tp.x)*ax+bx;
  ty = float(tp.y)*ay+by;
}

void mainScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN);
  tft.setFont(&fonts::FreeSans24pt7b);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("O2:", 5, 20);
  tft.drawString("He:", 5, 80);
  tft.setTextDatum(CC_DATUM);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Lock", BUTTON_LOCK_CX, BUTTON_LOCK_CY);
  tft.drawRect(BUTTON_LOCK_CX - BUTTON_W/2, BUTTON_LOCK_CY - BUTTON_H/2 - 5, BUTTON_W, BUTTON_H, TFT_WHITE);
  tft.drawString("Menu", BUTTON_MENU_CX, BUTTON_MENU_CY);
  tft.drawRect(BUTTON_MENU_CX - BUTTON_W/2, BUTTON_MENU_CY - BUTTON_H/2 - 5, BUTTON_W, BUTTON_H, TFT_WHITE);
  updateSprite();
  delay(TOUCH_DELAY);
}

void getSensorData() {
  File file = SPIFFS.open("/sensor_calib.txt", "r");
  if(!file){
    Serial.println("Failed to open file for reading");
    return;
  }

  calibO2InAir = file.readStringUntil('\n').toFloat();
  calibHeInAir = file.readStringUntil('\n').toFloat();
  calibO2InO2  = file.readStringUntil('\n').toFloat();
  calibHeInO2  = file.readStringUntil('\n').toFloat();
  calibO2InHe  = file.readStringUntil('\n').toFloat();
  calibHeInHe  = file.readStringUntil('\n').toFloat();
  
  file.close();

  Serial.printf("%d\n",calibO2InAir);
  Serial.printf("%d\n",calibHeInAir);
  Serial.printf("%d\n",calibO2InO2);
  Serial.printf("%d\n",calibHeInO2);
  Serial.printf("%d\n",calibO2InHe);
  Serial.printf("%d\n",calibHeInHe);
}

void writeSensorData() {
  Serial.println("Writing data to file...");

  // Open file in WRITE mode (this will overwrite the file)
  File file = SPIFFS.open("/sensor_calib.txt", "w");
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }

  file.println(calibO2InAir);
  file.println(calibHeInAir);
  file.println(calibO2InO2);
  file.println(calibHeInO2);
  file.println(calibO2InHe);
  file.println(calibHeInHe);

  file.close();
  Serial.println("Data written successfully!");
}

void getAltData() {
  File file = SPIFFS.open("/altitude.txt", "r");
  if(!file){
    Serial.println("Failed to open file for reading");
    return;
  }

  altitude = file.readStringUntil('\n').toInt();
  file.close();

  Serial.printf("%d\n", altitude);
}

void writeAltData() {
  Serial.println("Writing data to file...");

  // Open file in WRITE mode (this will overwrite the file)
  File file = SPIFFS.open("/altitude.txt", "w");
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }

  file.println(altitude);

  file.close();
  Serial.println("Data written successfully!");
}

void setAltData() {
  uint16_t doneFlag = 0;
  uint16_t tmpAltitude = altitude;
  do {
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(CC_DATUM);
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(w/2 - 40, 30);
    tft.printf("%3d", tmpAltitude);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("+", BUTTON_CHECKLIN_CX, BUTTON_CHECKLIN_CY);
    tft.drawRect(BUTTON_CHECKLIN_CX - BUTTON_W/2, BUTTON_CHECKLIN_CY - BUTTON_H/2 - 5, BUTTON_W, BUTTON_H, TFT_WHITE);
    tft.drawString("-", BUTTON_DBG_CX, BUTTON_DBG_CY);
    tft.drawRect(BUTTON_DBG_CX - BUTTON_W/2, BUTTON_DBG_CY - BUTTON_H/2 - 5, BUTTON_W, BUTTON_H, TFT_WHITE);
    tft.drawString("Set", BUTTON_NEXT_CX, BUTTON_NEXT_CY);
    tft.drawRect(BUTTON_NEXT_CX - BUTTON_W/2, BUTTON_NEXT_CY - BUTTON_H/2 - 5, BUTTON_W, BUTTON_H, TFT_WHITE);
    tft.drawString("Exit", BUTTON_EXIT_CX, BUTTON_EXIT_CY);
    tft.drawRect(BUTTON_EXIT_CX - BUTTON_W/2, BUTTON_EXIT_CY - BUTTON_H/2 - 5, BUTTON_W, BUTTON_H, TFT_WHITE);
    delay(TOUCH_DELAY);
    while (!tft.getTouch(&tp)) delay(10);
    updateTouchCoordinates();
    Serial.printf("Touched! %f, %f\n", tx, ty);
    if      (tx < BUTTON_CHECKLIN_CX + BUTTON_W/2 && 
             tx > BUTTON_CHECKLIN_CX - BUTTON_W/2 && 
             ty < BUTTON_CHECKLIN_CY + BUTTON_H/2 && 
             ty > BUTTON_CHECKLIN_CY - BUTTON_H/2) {tmpAltitude += 100;}
    else if (tx < BUTTON_DBG_CX + BUTTON_W/2 && 
             tx > BUTTON_DBG_CX - BUTTON_W/2 && 
             ty < BUTTON_DBG_CY + BUTTON_H/2 && 
             ty > BUTTON_DBG_CY - BUTTON_H/2) {if (tmpAltitude > 0) tmpAltitude -= 100;}
    else if (tx < BUTTON_NEXT_CX + BUTTON_W/2 && 
             tx > BUTTON_NEXT_CX - BUTTON_W/2 && 
             ty < BUTTON_NEXT_CY + BUTTON_H/2 && 
             ty > BUTTON_NEXT_CY - BUTTON_H/2) {doneFlag = 1;}
    else if (tx < BUTTON_EXIT_CX + BUTTON_W/2 && 
             tx > BUTTON_EXIT_CX - BUTTON_W/2 && 
             ty < BUTTON_EXIT_CY + BUTTON_H/2 && 
             ty > BUTTON_EXIT_CY - BUTTON_H/2) {return;}
  } while(!doneFlag);
  altitude = tmpAltitude;
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(10, 30);
  writeAltData();
  dataStoredScreen();
}

void getTouchData() {
  File file = SPIFFS.open("/touch_calib.txt", "r");
  if(!file){
    Serial.println("Failed to open file for reading");
    return;
  }

  ax = file.readStringUntil('\n').toFloat();
  bx = file.readStringUntil('\n').toFloat();
  ay = file.readStringUntil('\n').toFloat();
  by = file.readStringUntil('\n').toFloat();
  file.close();

  Serial.printf("%f\n",ax);
  Serial.printf("%f\n",bx);
  Serial.printf("%f\n",ay);
  Serial.printf("%f\n",by);
}

void storeTouch(float axNew, float ayNew, float bxNew, float byNew) {
  Serial.println("Writing data to file...");

  // Open file in WRITE mode (this will overwrite the file)
  File file = SPIFFS.open("/touch_calib.txt", "w");
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }

  file.println(axNew);
  file.println(bxNew);
  file.println(ayNew);
  file.println(byNew);

  file.close();
  Serial.println("Data written successfully!");
}

void setO2Vars() {
  a1O2 = 20.9/(calibO2InAir - calibO2InHe);
  b1O2 = -1 * a1O2 * calibO2InHe;
  a2O2 = 77.1/(calibO2InO2 - calibO2InAir);
  b2O2 = 98.0 - a2O2 * calibO2InO2;
}

void setHeVars() {
  float alpha = 98.0/77.1 * calibHeInAir + (1 - 98.0/77.1) * calibHeInO2 - calibHeInHe;
  bHe = -1 * 98.0 / alpha;
  cHe = 98.0 - calibHeInHe * bHe;
  xSens = (calibHeInO2 * bHe + cHe) / 98.0;
}

void calibrateAir(){
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(CC_DATUM);
  tft.setFont(&fonts::FreeSans24pt7b);
  tft.setTextColor(TFT_WHITE);

  tft.drawString("Calibrate", BUTTON_NEXT_CX, BUTTON_NEXT_CY);
  tft.drawRect(BUTTON_NEXT_CX - BUTTON_W/2, BUTTON_NEXT_CY - BUTTON_H/2 - 5, BUTTON_W, BUTTON_H, TFT_WHITE);
  tft.drawString("Exit", BUTTON_EXIT_CX, BUTTON_EXIT_CY);
  tft.drawRect(BUTTON_EXIT_CX - BUTTON_W/2, BUTTON_EXIT_CY - BUTTON_H/2 - 5, BUTTON_W, BUTTON_H, TFT_WHITE);

  tft.setTextColor(TFT_CYAN);
  tft.setFont(&fonts::FreeSans24pt7b);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("O2:", 5, 20);

  updateSpriteO2(true, 0);
  tSpr.pushSprite(80, 0);


  delay(TOUCH_DELAY);

  while (1) {
    while (!tft.getTouch(&tp)) {
      delay(10);
      read_sensors();
      updateSpriteO2(true, 0);
      tSpr.pushSprite(80, 0);
    }
    updateTouchCoordinates();
    Serial.printf("Touched! %f, %f\n", tx, ty);
    if      (tx < BUTTON_NEXT_CX + BUTTON_W/2 && 
             tx > BUTTON_NEXT_CX - BUTTON_W/2 && 
             ty < BUTTON_NEXT_CY + BUTTON_H/2 && 
             ty > BUTTON_NEXT_CY - BUTTON_H/2) {
      Serial.printf("CAL!"); 
      calibO2InAir = o2SensorReadingAvg; 
      calibHeInAir = heSensorReadingAvg;
      writeSensorData();
      setO2Vars();
      setHeVars();
      dataStoredScreen();
      return;
    }
    else if (tx < BUTTON_EXIT_CX + BUTTON_W/2 && 
             tx > BUTTON_EXIT_CX - BUTTON_W/2 && 
             ty < BUTTON_EXIT_CY + BUTTON_H/2 && 
             ty > BUTTON_EXIT_CY - BUTTON_H/2) {return;}
  }
}

void calibrateO2(){
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(CC_DATUM);
  tft.setFont(&fonts::FreeSans24pt7b);
  tft.setTextColor(TFT_WHITE);

  tft.drawString("Calibrate", BUTTON_NEXT_CX, BUTTON_NEXT_CY);
  tft.drawRect(BUTTON_NEXT_CX - BUTTON_W/2, BUTTON_NEXT_CY - BUTTON_H/2 - 5, BUTTON_W, BUTTON_H, TFT_WHITE);
  tft.drawString("Exit", BUTTON_EXIT_CX, BUTTON_EXIT_CY);
  tft.drawRect(BUTTON_EXIT_CX - BUTTON_W/2, BUTTON_EXIT_CY - BUTTON_H/2 - 5, BUTTON_W, BUTTON_H, TFT_WHITE);

  tft.setTextColor(TFT_CYAN);
  tft.setFont(&fonts::FreeSans24pt7b);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("O2:", 5, 20);

  updateSpriteO2(true, 0);
  tSpr.pushSprite(80, 0);


  delay(TOUCH_DELAY);

  while (1) {
    while (!tft.getTouch(&tp)) {
      delay(10);
      read_sensors();
      updateSpriteO2(true, 0);
      tSpr.pushSprite(80, 0);
    }
    updateTouchCoordinates();
    Serial.printf("Touched! %f, %f\n", tx, ty);
    if      (tx < BUTTON_NEXT_CX + BUTTON_W/2 && 
             tx > BUTTON_NEXT_CX - BUTTON_W/2 && 
             ty < BUTTON_NEXT_CY + BUTTON_H/2 && 
             ty > BUTTON_NEXT_CY - BUTTON_H/2) {
      Serial.printf("CAL!"); 
      calibO2InO2 = o2SensorReadingAvg; 
      calibHeInO2 = heSensorReadingAvg;
      writeSensorData();
      setO2Vars();
      setHeVars();
      dataStoredScreen();
      return;
    }
    else if (tx < BUTTON_EXIT_CX + BUTTON_W/2 && 
             tx > BUTTON_EXIT_CX - BUTTON_W/2 && 
             ty < BUTTON_EXIT_CY + BUTTON_H/2 && 
             ty > BUTTON_EXIT_CY - BUTTON_H/2) {return;}
  }
}

void calibrateHe(){
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(CC_DATUM);
  tft.setFont(&fonts::FreeSans24pt7b);
  tft.setTextColor(TFT_WHITE);

  tft.drawString("Calibrate", BUTTON_NEXT_CX, BUTTON_NEXT_CY);
  tft.drawRect(BUTTON_NEXT_CX - BUTTON_W/2, BUTTON_NEXT_CY - BUTTON_H/2 - 5, BUTTON_W, BUTTON_H, TFT_WHITE);
  tft.drawString("Exit", BUTTON_EXIT_CX, BUTTON_EXIT_CY);
  tft.drawRect(BUTTON_EXIT_CX - BUTTON_W/2, BUTTON_EXIT_CY - BUTTON_H/2 - 5, BUTTON_W, BUTTON_H, TFT_WHITE);

  tft.setTextColor(TFT_CYAN);
  tft.setFont(&fonts::FreeSans24pt7b);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("He:", 5, 20);

  updateSpriteHe(true, 0);
  tSpr.pushSprite(80, 0);


  delay(TOUCH_DELAY);

  while (1) {
    while (!tft.getTouch(&tp)) {
      delay(10);
      read_sensors();
      updateSpriteHe(true, 0);
      tSpr.pushSprite(80, 0);
    }
    updateTouchCoordinates();
    Serial.printf("Touched! %f, %f\n", tx, ty);
    if      (tx < BUTTON_NEXT_CX + BUTTON_W/2 && 
             tx > BUTTON_NEXT_CX - BUTTON_W/2 && 
             ty < BUTTON_NEXT_CY + BUTTON_H/2 && 
             ty > BUTTON_NEXT_CY - BUTTON_H/2) {
      Serial.printf("CAL!"); 
      calibO2InHe = o2SensorReadingAvg; 
      calibHeInHe = heSensorReadingAvg;
      writeSensorData();
      setO2Vars();
      setHeVars();
      dataStoredScreen();
      return;
    }
    else if (tx < BUTTON_EXIT_CX + BUTTON_W/2 && 
             tx > BUTTON_EXIT_CX - BUTTON_W/2 && 
             ty < BUTTON_EXIT_CY + BUTTON_H/2 && 
             ty > BUTTON_EXIT_CY - BUTTON_H/2) {return;}
  }
}

void calibrateTouch() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(CC_DATUM);
  tft.drawString("Press",w/2, h/2 - 30);
  tft.drawString("Circle",w/2, h/2 + 30);
  tft.fillCircle(20,20,10,TFT_RED);
  delay(TOUCH_DELAY);
  while (!tft.getTouch(&tp)) delay(10);
  float x1 = tp.x;
  float y1 = tp.y;
  tft.fillScreen(TFT_BLACK);
  tft.drawString("Press",w/2, h/2 - 30);
  tft.drawString("Circle",w/2, h/2 + 30);
  tft.fillCircle(220,20,10,TFT_RED);
  delay(TOUCH_DELAY);
  while (!tft.getTouch(&tp)) delay(10);
  float x2 = tp.x;
  float y2 = tp.y;
  tft.fillScreen(TFT_BLACK);
  tft.drawString("Press",w/2, h/2 - 30);
  tft.drawString("Circle",w/2, h/2 + 30);
  tft.fillCircle(20,300,10,TFT_RED);
  delay(TOUCH_DELAY);
  while (!tft.getTouch(&tp)) delay(10);
  float x3 = tp.x;
  float y3 = tp.y;
  tft.fillScreen(TFT_BLACK);
  tft.drawString("Press",w/2, h/2 - 30);
  tft.drawString("Circle",w/2, h/2 + 30);
  tft.fillCircle(220,300,10,TFT_RED);
  delay(TOUCH_DELAY);
  while (!tft.getTouch(&tp)) delay(10);
  float x4 = tp.x;
  float y4 = tp.y;

  float xMinAvg = (x1 + x3) / 2;
  float xMaxAvg = (x2 + x4) / 2;
  float yMinAvg = (y1 + y2) / 2;
  float yMaxAvg = (y3 + y4) / 2;
  float axNew = (220 - 20) / (xMaxAvg - xMinAvg);
  float ayNew = (300 - 20) / (yMaxAvg - yMinAvg);
  float bxNew = 20 - axNew * xMinAvg;
  float byNew = 20 - ayNew * yMinAvg;

  storeTouch(axNew, ayNew, bxNew, byNew);
  dataStoredScreen();
  getTouchData();
}

void debugScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(ML_DATUM);
  tft.setFont(&fonts::FreeSans12pt7b);
  tft.setTextColor(TFT_WHITE);

  delay(TOUCH_DELAY);

  while(!tft.getTouch(&tp)) {
    tft.fillScreen(TFT_BLACK);
    float battVal = analogRead(BATTPIN);
    float battScale = 0.111982;
    float battOffset = -2857;
    float battPercent = (battVal + battOffset) * battScale;
    tft.setCursor(0, 10);
    tft.printf("battPercent = %.3f", battPercent);

    int32_t adcRead = ads.readADC_Differential_0_1();
    tft.setCursor(0, 30);
    tft.printf("o2Adc = %d", adcRead);
    float tmp = calcO2(adcRead);
    tft.setCursor(0, 50);
    tft.printf("o2Val = %.3f", tmp);
    adcRead = ads.readADC_Differential_2_3();
    tft.setCursor(0, 70);
    tft.printf("heAdc = %d", adcRead);
    tmp = calcHe(adcRead);
    tft.setCursor(0, 90);
    tft.printf("heVal = %.3f", tmp);

    delay(1000);
  }
  return;
}

void linearityCheck() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(CC_DATUM);
  tft.setFont(&fonts::FreeSans24pt7b);
  tft.setTextColor(TFT_WHITE);

  tft.drawString("Exit", BUTTON_EXIT_CX, BUTTON_EXIT_CY);
  tft.drawRect(BUTTON_EXIT_CX - BUTTON_W/2, BUTTON_EXIT_CY - BUTTON_H/2 - 5, BUTTON_W, BUTTON_H, TFT_WHITE);

  tft.setTextColor(TFT_CYAN);
  tft.setFont(&fonts::FreeSans24pt7b);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("Current O2", 5, 20);
  tft.drawString("sensor", 5, 60);
  tft.drawString("linearity:", 5, 100);

  tft.setTextColor(TFT_GREEN);
  tft.setCursor(30, 180);
  tft.printf("%.1f %%", calibO2InO2 / (calibO2InAir/20.9*98.0) * 100.0);

  delay(TOUCH_DELAY);

  while (1) {
    while (!tft.getTouch(&tp)) {
      delay(10);
    }
    updateTouchCoordinates();
    if (tx < BUTTON_EXIT_CX + BUTTON_W/2 && 
        tx > BUTTON_EXIT_CX - BUTTON_W/2 && 
        ty < BUTTON_EXIT_CY + BUTTON_H/2 && 
        ty > BUTTON_EXIT_CY - BUTTON_H/2) {return;}
  }
}

void menuScreen2() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(CC_DATUM);
  tft.setFont(&fonts::FreeSans24pt7b);
  tft.setTextColor(TFT_WHITE);

  tft.drawString("Set Alt", BUTTON_SETALT_CX, BUTTON_SETALT_CY);
  tft.drawRect(BUTTON_SETALT_CX - BUTTON_W/2, BUTTON_SETALT_CY - BUTTON_H/2 - 5, BUTTON_W, BUTTON_H, TFT_WHITE);
  tft.drawString("Check Lin", BUTTON_CHECKLIN_CX, BUTTON_CHECKLIN_CY);
  tft.drawRect(BUTTON_CHECKLIN_CX - BUTTON_W/2, BUTTON_CHECKLIN_CY - BUTTON_H/2 - 5, BUTTON_W, BUTTON_H, TFT_WHITE);
  tft.drawString("Debug", BUTTON_DBG_CX, BUTTON_DBG_CY);
  tft.drawRect(BUTTON_DBG_CX - BUTTON_W/2, BUTTON_DBG_CY - BUTTON_H/2 - 5, BUTTON_W, BUTTON_H, TFT_WHITE);
  tft.drawString("Cal Touch", BUTTON_TCAL_CX, BUTTON_TCAL_CY);
  tft.drawRect(BUTTON_TCAL_CX - BUTTON_W/2, BUTTON_TCAL_CY - BUTTON_H/2 - 5, BUTTON_W, BUTTON_H, TFT_WHITE);
  tft.drawString("Exit", BUTTON_EXIT_CX, BUTTON_EXIT_CY);
  tft.drawRect(BUTTON_EXIT_CX - BUTTON_W/2, BUTTON_EXIT_CY - BUTTON_H/2 - 5, BUTTON_W, BUTTON_H, TFT_WHITE);

  delay(TOUCH_DELAY);

  while(1) {
    while (!tft.getTouch(&tp)) delay(10);
    updateTouchCoordinates();
    Serial.printf("Touched! %f, %f\n", tx, ty);
    if      (tx < BUTTON_SETALT_CX + BUTTON_W/2 && 
             tx > BUTTON_SETALT_CX - BUTTON_W/2 && 
             ty < BUTTON_SETALT_CY + BUTTON_H/2 && 
             ty > BUTTON_SETALT_CY - BUTTON_H/2) {setAltData(); return;}
    if      (tx < BUTTON_TCAL_CX + BUTTON_W/2 && 
             tx > BUTTON_TCAL_CX - BUTTON_W/2 && 
             ty < BUTTON_TCAL_CY + BUTTON_H/2 && 
             ty > BUTTON_TCAL_CY - BUTTON_H/2) {calibrateTouch(); return;}
    if      (tx < BUTTON_DBG_CX + BUTTON_W/2 && 
             tx > BUTTON_DBG_CX - BUTTON_W/2 && 
             ty < BUTTON_DBG_CY + BUTTON_H/2 && 
             ty > BUTTON_DBG_CY - BUTTON_H/2) {debugScreen(); return;}
    if      (tx < BUTTON_CHECKLIN_CX + BUTTON_W/2 && 
             tx > BUTTON_CHECKLIN_CX - BUTTON_W/2 && 
             ty < BUTTON_CHECKLIN_CY + BUTTON_H/2 && 
             ty > BUTTON_CHECKLIN_CY - BUTTON_H/2) {linearityCheck(); return;}
    else if (tx < BUTTON_EXIT_CX + BUTTON_W/2 && 
             tx > BUTTON_EXIT_CX - BUTTON_W/2 && 
             ty < BUTTON_EXIT_CY + BUTTON_H/2 && 
             ty > BUTTON_EXIT_CY - BUTTON_H/2) {return;}
  }
}

void menuScreen1() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(CC_DATUM);
  tft.setFont(&fonts::FreeSans24pt7b);
  tft.setTextColor(TFT_WHITE);

  tft.drawString("Cal Air", BUTTON_AIRCAL_CX, BUTTON_AIRCAL_CY);
  tft.drawRect(BUTTON_AIRCAL_CX - BUTTON_W/2, BUTTON_AIRCAL_CY - BUTTON_H/2 - 5, BUTTON_W, BUTTON_H, TFT_WHITE);
  tft.drawString("Cal O2", BUTTON_O2CAL_CX, BUTTON_O2CAL_CY);
  tft.drawRect(BUTTON_O2CAL_CX - BUTTON_W/2, BUTTON_O2CAL_CY - BUTTON_H/2 - 5, BUTTON_W, BUTTON_H, TFT_WHITE);
  tft.drawString("Cal He", BUTTON_HECAL_CX, BUTTON_HECAL_CY);
  tft.drawRect(BUTTON_HECAL_CX - BUTTON_W/2, BUTTON_HECAL_CY - BUTTON_H/2 - 5, BUTTON_W, BUTTON_H, TFT_WHITE);
  tft.drawString("Next", BUTTON_NEXT_CX, BUTTON_NEXT_CY);
  tft.drawRect(BUTTON_NEXT_CX - BUTTON_W/2, BUTTON_NEXT_CY - BUTTON_H/2 - 5, BUTTON_W, BUTTON_H, TFT_WHITE);
  tft.drawString("Exit", BUTTON_EXIT_CX, BUTTON_EXIT_CY);
  tft.drawRect(BUTTON_EXIT_CX - BUTTON_W/2, BUTTON_EXIT_CY - BUTTON_H/2 - 5, BUTTON_W, BUTTON_H, TFT_WHITE);

  delay(TOUCH_DELAY);

  while(1) {
    while (!tft.getTouch(&tp)) delay(10);
    updateTouchCoordinates();
    Serial.printf("Touched! %f, %f\n", tx, ty);
    if      (tx < BUTTON_NEXT_CX + BUTTON_W/2 && 
             tx > BUTTON_NEXT_CX - BUTTON_W/2 && 
             ty < BUTTON_NEXT_CY + BUTTON_H/2 && 
             ty > BUTTON_NEXT_CY - BUTTON_H/2) {menuScreen2(); mainScreen(); return;}
    else if (tx < BUTTON_AIRCAL_CX + BUTTON_W/2 && 
             tx > BUTTON_AIRCAL_CX - BUTTON_W/2 && 
             ty < BUTTON_AIRCAL_CY + BUTTON_H/2 && 
             ty > BUTTON_AIRCAL_CY - BUTTON_H/2) {calibrateAir();  mainScreen(); return;}
    else if (tx < BUTTON_O2CAL_CX + BUTTON_W/2 && 
             tx > BUTTON_O2CAL_CX - BUTTON_W/2 && 
             ty < BUTTON_O2CAL_CY + BUTTON_H/2 && 
             ty > BUTTON_O2CAL_CY - BUTTON_H/2) {calibrateO2();  mainScreen(); return;}
    else if (tx < BUTTON_HECAL_CX + BUTTON_W/2 && 
             tx > BUTTON_HECAL_CX - BUTTON_W/2 && 
             ty < BUTTON_HECAL_CY + BUTTON_H/2 && 
             ty > BUTTON_HECAL_CY - BUTTON_H/2) {calibrateHe(); mainScreen(); return;}
    else if (tx < BUTTON_EXIT_CX + BUTTON_W/2 && 
             tx > BUTTON_EXIT_CX - BUTTON_W/2 && 
             ty < BUTTON_EXIT_CY + BUTTON_H/2 && 
             ty > BUTTON_EXIT_CY - BUTTON_H/2) {mainScreen(); return;}
  }
}

void initSprites(){
  tSpr.setColorDepth(8);
  tSpr.createSprite(160, 120);
  tSpr.setSwapBytes(true);
  modSpr.setColorDepth(8);
  modSpr.createSprite(w, 60);
  modSpr.setSwapBytes(true);
}

void resetScreen(){
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(CC_DATUM);
  tft.setTextColor(TFT_CYAN);
  tft.setFont(&fonts::FreeSans24pt7b);
  tft.drawString("Factory", w/2, h/2-30);
  tft.drawString("Reset", w/2, h/2+30);
  
  ax = 0.06367399;
  bx = 235;
  ay = 0.08908686;
  by = -30;

  storeTouch(ax, ay, bx, by);

  delay(2000);
}

void startupScreen(){
  tft.fillScreen(TFT_RED);
  tft.setTextDatum(CC_DATUM);
  tft.setTextColor(TFT_BLACK);
  tft.setFont(&fonts::FreeSansBold24pt7b);
  tft.drawString("Trimix", w/2, h/2-30);
  tft.drawString("Analyzer", w/2, h/2+30);

  tft.fillRect(10, h-20, 20, 16, TFT_BLACK);
  tft.fillRect(10 + 20, h-20 + 8 - 3, 5, 6, TFT_BLACK);

  float battPercent = getBattPercent();
  tft.setCursor(40, h-12);
  tft.setFont(&fonts::FreeSans12pt7b);
  tft.printf("%.0f%%", battPercent);

  tmpTimer = millis();
  while (millis() - tmpTimer < 2000) {
    delay(10);
    if (tft.getTouch(&tp)) calibrateTouch();
  }
}

void setup() {
  pinMode(BATTPIN, INPUT);
  Serial.begin(115200);
  delay(2000);
  Serial.println("1");

  if(!SPIFFS.begin(true)){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  // TFT
  tft.init();
  tft.setRotation(2);
  tft.setColorDepth(8);
  h = tft.height();
  w = tft.width();
  initSprites();
  tft.setBrightness(255); // out of 255
  startupScreen();
  Serial.println("2");

  // Read data from FLASH
  getTouchData();
  getSensorData();
  getAltData();
  Serial.println("3");
  
  // Calculate fitting variables
  getSensorData();
  setO2Vars();
  setHeVars();
  Serial.println("4");
  
  Wire.setPins(SDAPIN, SCLPIN);
  Wire.begin();
  ads.setGain(GAIN_FOUR); // 4x gain 1 bit = 0.03125mV
  if (!ads.begin()) {
    while (1) Serial.println("Could not find ADS1X15, check address!");
  }

  for(int ii=0; ii <= RA_SIZE; ii++) {
    read_sensors();
  }
  Serial.println("5");
  // Load main screen before entering loop
  mainScreen();
  Serial.println("6");
}

void updateScreen() {
  updateSprite();
}

void screenLock() {
  tft.fillRect(0, 200, w, h-200, TFT_BLACK);
  tft.setTextDatum(CC_DATUM);
  tft.setTextColor(TFT_RED);
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.drawString("LOCKED", w/2, 260);
  tft.drawString("Press anywhere to unlock", w/2, 260+20);
  delay(TOUCH_DELAY);
  while (!tft.getTouch(&tp));
  mainScreen();
}

void loop() {
  Serial.println("7");
  delay(TOUCH_DELAY);
  while (!tft.getTouch(&tp)) {
    delay(30);
    read_sensors();
    updateScreen();
  }
  updateTouchCoordinates();
  Serial.printf("Touched! %f, %f\n", tx, ty);
  if      (tx < BUTTON_MENU_CX + BUTTON_W/2 && 
           tx > BUTTON_MENU_CX - BUTTON_W/2 && 
           ty < BUTTON_MENU_CY + BUTTON_H/2 && 
           ty > BUTTON_MENU_CY - BUTTON_H/2) menuScreen1();
  else if (tx < BUTTON_LOCK_CX + BUTTON_W/2 && 
           tx > BUTTON_LOCK_CX - BUTTON_W/2 && 
           ty < BUTTON_LOCK_CY + BUTTON_H/2 && 
           ty > BUTTON_LOCK_CY - BUTTON_H/2) screenLock();
}