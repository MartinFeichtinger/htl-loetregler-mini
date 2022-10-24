#include <stdlib.h>
#include <time.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>
#include "button.h"


// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);


#define PIN_Selbsthaltung   2
#define PIN_Heizelement     3
// Taster, siehe button.cpp
#define PIN_ComTreiber_Enable   9


// Externe Referenzspannung 2.51V +/-0.5%
#define PIN_AIN_UBatt       A0
#define PIN_AIN_TempSensor  A1
#define PIN_AIN_Strom       A2
// A3: unused
// A4: SDA (OLED und PCB-Temp-Sensor)
// A5: SCL (OLED und PCB-Temp-Sensor)
#define PIN_AIN_Staender    A6

#define DIVISOR 2
#define SPANNUNG_VOLL (40.0/DIVISOR)
#define SPANNUNG_LEER (32.0/DIVISOR)

#define MIT_NAMEN
#define VORNAME  "OTELO"
#define NACHNAME "Gmunden"


char str[20] = {0};
uint16_t tempSoll = 100;
uint16_t tempSpitze = 999;
bool standby = false;
uint32_t timeLastTempIncrease = 0;
bool loetkolbenVerbunden=false;
float leistung;

enum Setting {STROMVERSORGUNG=0, MAX_STROM=1, NENNSPANNUNG=2, AUTO_STANDBY=3, STANDBY_TEMP=4, STANDBY_DELAY=5} activeSetting=STROMVERSORGUNG;
String settingName[6]={"Strom-\nversorgung", "Strom-\nbegrenzung", "Nenn-\nspannung", "Auto-\nStandby", "Standby\nTemperatur", "Standby-\nDelay"}; 
enum Stromversorgung {AKKU=0, NETZTEIL=1};

struct Settings {
  enum Stromversorgung stromversorgung;
  uint8_t maxStrom;     // Wert 15 entspricht 1.5A
  uint8_t nennspannung;
  uint8_t standbyTemp;
  bool autoStandby;
  uint8_t standbyDelay;
} setting;
  
bool modifySetting=false;

enum Screen {BootScreen, MainScreen, SettingsScreen} activeScreen = BootScreen;
enum Screen oldScreen;

void setup() {
  pinMode(PIN_Selbsthaltung, OUTPUT);
  selbsthaltung();

  // put your setup code here, to run once:
  analogReference(EXTERNAL); // 2.51V

  Serial.begin(115200);
  delay(100);
  Serial.println("HTL LOETREGLER MINI");
  Wire.begin();

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3c, false, false)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;); // Don't proceed, loop forever
  }
  
  if(EEPROM.read(1023) == 'T') {
    EEPROM.get(0, setting);
  }
  else {
    setting.stromversorgung=AKKU;
    setting.maxStrom=30;  // entspricht 3A
    setting.nennspannung=48;
    setting.standbyTemp=150;
    setting.autoStandby=true;
    setting.standbyDelay=60;

    EEPROM.write(1023, 'T');
  }

  TCCR2B = TCCR2B & B11111000 | B00000001;  // set pwm-frequency 31372.55 Hz

  display.clearDisplay();
  display.setCursor(10,0);
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.print("HTL STEYR");
  display.setCursor(0,31-7);
  display.setTextSize(1);
  display.print("LOETREGLER MINI V0.1");
  display.display();

  delay(1000);

#ifdef MIT_NAMEN
  nameAnzeigen();
#endif

  pinMode(PIN_Heizelement, OUTPUT);
  analogWrite(PIN_Heizelement, 3);
  oldScreen=activeScreen;
  activeScreen=MainScreen;
}

void nameAnzeigen(){
  display.clearDisplay();
  display.setCursor(0,0);
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.print(VORNAME);
  display.setCursor(0,31-14);
  display.setTextSize(2);
  display.print(NACHNAME);
  display.display();

  delay(1000);
}

void mainScreen() {
  static const int8_t UpdatePeriod = 3;
  static int8_t updateCounter = UpdatePeriod;
  // paint static objects

  if (oldScreen != MainScreen) {
    updateCounter = 0;
    activeScreen = MainScreen;
    display.clearDisplay();

    // Grad Celsius groß
    display.setTextSize(3);
    display.setCursor(60, 1);
    display.print("C");
    display.drawCircle(57, 3, 2, WHITE);
    display.drawCircle(57, 3, 3, WHITE);


    // Watt
    display.setTextSize(1);
    display.setCursor(2 + 3 * 6, 24);
    display.print("W");

    // Grad Celsius klein
    display.setTextSize(1);
    display.setCursor(59, 24);
    display.print("C");
    display.drawCircle(56, 25, 1, WHITE);

    if(setting.stromversorgung == AKKU) {
      // Batterieumrandung
      display.drawRect(96, 3, 27, 11, WHITE);
      display.drawRect(122, 6, 3, 5, WHITE);
    
      // Volt
      display.setTextSize(1);
      display.setCursor(96 + 4 * 6, 18);
      display.print("V");
    }
  }

  // CLEAR Dynamic Objects
  {
    updateCounter--;
    if (updateCounter <= 0) {
      display.fillRect(2, 1, 3 * 18 - 3, 21, BLACK); // Ist-Temp.
    }
    display.fillRect(2, 24, 3 * 6 - 1, 7, BLACK); // Leistung
    display.fillRect(36, 24, 3 * 6 - 1, 7, BLACK); // Soll Temperatur
    display.fillRect(80, 24, 7 * 6 - 1, 7, BLACK); // Standby-Anzeige
    if(setting.stromversorgung == AKKU) {
      display.fillRect(96, 18, 4 * 6 - 1, 7, BLACK); // Batterie Spannung
      display.fillRect(98, 5, 2 * 12 - 1, 7, BLACK); // Batterie Zustand
    }
    else {
      display.fillRect(120-5*6, 1, 120, 10, BLACK);
    }
  }


  // Draw/Print Dynamic Objects:
  {
    // Ist Temperatur
    // tempSpitze = temperaturSpitze(); 
    // tempSpitze wird aus regler() übernommen
    if(loetkolbenVerbunden) {
      static int32_t sumTemp = 0;
      sumTemp += tempSpitze;
      if (updateCounter <= 0) {
        display.setCursor(2, 1);
        display.setTextSize(3);
        sprintf(str, "%d", uint16_t(sumTemp / UpdatePeriod));
        //sprintf(str, "%d", uint16_t(tempSpitze));
        display.print(str);
        sumTemp = 0;
      }
    }
    else {
      display.setCursor(2, 1);
      display.setTextSize(3);
      display.print("---");
    }

    // Soll Temperatur
    display.setCursor(36, 24);
    display.setTextSize(1);
    sprintf(str, "%d", uint16_t(tempSoll));
    display.print(str);


    // Leistung
    if(leistung > 100) display.setCursor(2, 24);
    else if(leistung > 10) display.setCursor(2+6, 24);
    else display.setCursor(2+12, 24);
    display.setTextSize(1);
    sprintf(str, "%d", uint16_t(leistung));
    display.print(str);

    // Standby-Anzeige
    if(standby){
      display.setCursor(80, 24);
      display.setTextSize(1);
      display.print("STANDBY");
    }

    if(setting.stromversorgung == AKKU) {
      // Batteriespannung
      float uBatt = spannungBatterie();
      display.setTextSize(1);
      sprintf(str, "%3d", uint16_t(uBatt * 10));
      display.setCursor(96, 18);
      display.print(str[0]);
      display.setCursor(96 + 6, 18);
      display.print(str[1]);
      display.setCursor(96 + 12, 18);
      display.print(".");
      display.setCursor(96 + 18, 18);
      display.print(str[2]);

      uint8_t z = (batterieZustand() * 12 + 50) / 100; // umrechnen auf 12 Skalenstriche
      for (int x = 0; x < z; x++) {
        display.drawLine(x * 2 + 98, 5, x * 2 + 98, 11, WHITE);
      }
    }
    else if(setting.stromversorgung == NETZTEIL) {
      // Netzteilspannung
      float uNetz = spannungBatterie();
      display.setTextSize(1);
      sprintf(str, "%d.%dV", uint16_t(uNetz), uint8_t(uNetz*10)%10);
      if(uNetz < 10) {
        display.setCursor(120-4*6, 1);
      }
      else {
        display.setCursor(120-5*6, 1);
      }
      display.print(str);

      sprintf(str, "%d.%dA", setting.maxStrom/10, setting.maxStrom%10);
      if(setting.maxStrom < 100) {
        display.setCursor(120-4*6, 11);
      }
      else {
        display.setCursor(120-5*6, 11);
      }
      display.print(str);
    }
  }

  if (updateCounter <= 0) {
    updateCounter = UpdatePeriod;
  }

  oldScreen=MainScreen;
  display.display();
}

// geht noch nicht
void settingsScreen(){
  display.clearDisplay();
  display.setCursor(0,0);
  display.setTextSize(2);
  display.setTextColor(WHITE);

  if(!modifySetting) {
    display.print(settingName[activeSetting]);
  }
  else {
    switch(activeSetting) {
      case STROMVERSORGUNG: {
        if(setting.stromversorgung == AKKU) {
          display.print("Akku");
        }
        else if(setting.stromversorgung == NETZTEIL) {
          display.print("Netzteil");
        }
        break;
      }
      case MAX_STROM: {
        sprintf(str, "%d.%dA", setting.maxStrom/10, setting.maxStrom%10);
        display.print(str);
        break;
      }
      case NENNSPANNUNG: {
        sprintf(str, "%dV", setting.nennspannung);
        display.print(str);
        break;
      }
      case AUTO_STANDBY: {
        if(setting.autoStandby == true) {
          display.print("Aktiviert");
        }
        else if(setting.autoStandby == false) {
          display.print("Deaktiviert");
        }
        break;
      }
      case STANDBY_TEMP: {
        sprintf(str, "%d C", setting.standbyTemp);
        display.print(str);
        break;
      }
      case STANDBY_DELAY: {
        sprintf(str, "%ds", setting.standbyDelay);
        display.print(str);
        break;
      }
    }
  }
 
  oldScreen=SettingsScreen;
  display.display();
}

// 0...100%
uint8_t batterieZustand() {
  int32_t ret = constrain(
                  100.0/(SPANNUNG_VOLL-SPANNUNG_LEER)*(spannungBatterie()-SPANNUNG_LEER), 
                  0, 100);
  //Serial.println(ret);
  return ret;
}

void regler(){
  float stromMesswert;
  static float stromMittelwert = float(setting.maxStrom)/10;
  static uint8_t dutyCycle=20;

  tempSpitze = temperaturSpitze();
  if ((!standby && (tempSpitze < tempSoll)) || (standby && (tempSpitze < setting.standbyTemp))) {
    analogWrite(PIN_Heizelement, dutyCycle); // nicht 100% PWM (=255), damit der Bootstrap-Kondensator nachladen kann!
  }
  if(tempSpitze+80 < tempSoll){
    // beschleunigtes Heizen
    delay(100);
  }else{
    delay(10); // max. 10ms durchgehend Heizen
  }

  stromMesswert = strom();
  analogWrite(PIN_Heizelement, 0);
  Serial.print(stromMesswert, 2);
  Serial.print(", ");

  // Regelung des Stroms mittels dem DutyCycle
  if(stromMesswert > 0){
    if(stromMesswert > (float(setting.maxStrom) / 10) - 0.1){       // setting.maxStrom ist ein uint8_t. der wert 30 entspricht 3.0 Amper. -0.1 damit der regler nicht über die grenze geht
      if(dutyCycle > 0) dutyCycle--;
    }
    else{
      if(dutyCycle < 250) dutyCycle++;
    }
  }
  Serial.print(dutyCycle);

  // Berechnung der Leistung
  stromMittelwert = 0.95 * stromMittelwert + 0.05 * stromMesswert;
  leistung = stromMittelwert * spannungBatterie();
  Serial.print(", ");
  Serial.println(leistung);
}

void tasterAuswertung(){
  buttons.readAll();
  if (buttons.up->getEvent() == Button::PressedEvent) {

    switch(activeScreen) {
      case MainScreen: {
        tempSoll += 10;
        timeLastTempIncrease = millis();        
        break;
      }
      case SettingsScreen: {
        if(modifySetting) {
          switch(activeSetting) {
            case STROMVERSORGUNG: setting.stromversorgung = (Stromversorgung)((int)setting.stromversorgung -1); break;
            case MAX_STROM: setting.maxStrom+=2; break;
            case NENNSPANNUNG: setting.nennspannung+=1; break;
            case AUTO_STANDBY: setting.autoStandby=true; break;
            case STANDBY_TEMP: setting.standbyTemp+=10; break;
            case STANDBY_DELAY: setting.standbyDelay+=30; break;
          }
        }
        else {
          activeSetting = (Setting)((int)activeSetting - 1);  // muss hier so umständlich gechastet werden da enums in c++ anders funktionieren
        }
        break;
      }
    }
  }
  if (buttons.down->getEvent() == Button::PressedEvent) {
    switch(activeScreen) {
      case MainScreen: tempSoll -= 10; break;
      case SettingsScreen: {
        if(modifySetting) {
          switch(activeSetting) {
            case STROMVERSORGUNG: setting.stromversorgung = (Stromversorgung)((int)setting.stromversorgung +1); break;
            case MAX_STROM: setting.maxStrom-=2; break;
            case NENNSPANNUNG: setting.nennspannung-=1; break;
            case AUTO_STANDBY: setting.autoStandby=false; break;
            case STANDBY_TEMP: setting.standbyTemp-=10; break;
            case STANDBY_DELAY: setting.standbyDelay-=30; break;
          }       
        }
        else {
          activeSetting = (Setting)((int)activeSetting + 1);  // scheiß c++
        }
        break;
      }
    }
  }
  if (buttons.back->getEvent() == Button::PressedEvent) {
    if(modifySetting) {
      modifySetting=false;
      setting=EEPROM.get(0, setting);
    }
    else {
      activeScreen=MainScreen; 
    }
  }
  if (buttons.enter->getEvent() == Button::PressedEvent) {
    if(activeScreen == MainScreen) {
      activeScreen=SettingsScreen;
      // vieleicht in standby gehen
    }
    else {
      if(modifySetting) {
        modifySetting=false;  // Der Wert wir gespeichert und man kommt zurück in MENU
        EEPROM.put(0, setting);
      }
      else {
        modifySetting=true;
      }
    }
  }
  if ((buttons.power->getEvent() == Button::ReleasedEvent) && millis() > 1000) {
    if((buttons.power->tLastReleased-buttons.power->tLastPressed) > 1000) {
      abschalten();
    }
    else {
      standby =! standby;
    }
  }
  tempSoll = constrain(tempSoll, 50, 450);
  activeSetting = constrain(activeSetting, 0, 5);
  setting.stromversorgung = constrain(setting.stromversorgung, 0, 1);
  setting.maxStrom = constrain(setting.maxStrom, 1, 100);    // 1-10A
  setting.nennspannung = constrain(setting.nennspannung, 18, 48); 
  setting.standbyTemp = constrain(setting.standbyTemp, 50, 250);
  setting.standbyDelay = constrain(setting.standbyDelay, 30, 180);
}

void loop() {
  switch(activeScreen) {
    case MainScreen: mainScreen(); break;
    case SettingsScreen: settingsScreen(); break;
  }
    
  regler();
  tasterAuswertung();
  selbsthaltung();
  zeitabschaltung();
  uebertemperaturwaechter();
}

// Temperaturen über 330°C werden nur 1 Minute zugelassen
// Dient dem Schutz des 3D-gedruckten Griffstücks
void uebertemperaturwaechter()
{
  if(millis() > timeLastTempIncrease+60000UL && tempSoll > 330){
    tempSoll = 330;
  }
}

float spannungMessen(uint8_t pin) {
  float u = analogRead(pin);
  u = u * 2.51 / 1024;
  return u;
}

// muss periodisch aufgerufen werden!
void selbsthaltung() {
  if (millis() < 10000 || spannungBatterie() > SPANNUNG_LEER) { // für 40V-Akku: 30V, für 20V-Akku: 15V
    digitalWrite(PIN_Selbsthaltung, HIGH);
  } else {
    Serial.println("Unterspannung");
    abschalten();
  }
}

float temperaturSpitze() {
  uint16_t tempSpitze=spannungMessen(PIN_AIN_TempSensor) / 221 / 24e-6 + 30; // in °C
  if(tempSpitze > 500) {
    loetkolbenVerbunden=false;
  }
  else {
    loetkolbenVerbunden=true;
  }
  return tempSpitze;
}

float spannungBatterie() {
  return spannungMessen(PIN_AIN_UBatt) * 23;
}

float strom() {
  return spannungMessen(PIN_AIN_Strom) / 221 / 0.001;
}

// muss periodisch aufgerufen werden
void zeitabschaltung() {
  if (millis() / 1000 / 60 > 10) {
    Serial.println("Zeitabschaltung");
    abschalten();
  }
}

// doesn't return any more, except, if powered by USB
void abschalten() {
  if(digitalRead(10) == HIGH){
    digitalWrite(PIN_Selbsthaltung, LOW);
    return;
  }
  display.fillRect(0, 0, 128, 32, BLACK);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("ACHTUNG HEISS");
  display.setCursor(0, 10);
  display.print("AUSKUEHLEN...");
  display.display();
  
  while (temperaturSpitze() > 60 && temperaturSpitze() < 490){
    display.fillRect(0,20,128,10, BLACK);
    display.setCursor(0,20);
    sprintf(str, "%d", uint16_t(temperaturSpitze()));
    display.print(str);
    display.display();
    delay(1000);
  }
  
  digitalWrite(PIN_Selbsthaltung, LOW);
  while(1);
}
