#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <TM1637Display.h>
#include <ArduinoJson.h>
#include <ArduinoJson.hpp>
#include <WiFiS3.h>
#include <PubSubClient.h>


//LCD Screen all Digital Pins 
#define ENCODER_CLK //2
#define ENCODER_DT //3
#define ENCODER_SW //4
#define ENCODER_5V // 5V pin digital
#define ENCODER_GND // GND pin digital

//Display Pins
#define CLK1 7  
#define DIO1 6
#define CLK2 5
#define DIO2 4
//Ventil
#define RELAY_1 2
#define RELAY_2 3

const uint8_t sensorPin1 = A1;     // Analog Pin fur Sensor 1 //A1= 15
const uint8_t sensorPin2 = A2;     // Analog Pin fur Sensor 2 //A2= 16

//Offsets
const float voltageTank_1 = 3.46;
const float voltageTank_2 = 3.71;
const float voltage_offset = 0.75;
const float tank_offset_1 = 0.705;
const float tank_offset_2 = 0.705;



const unsigned int defaultFillStart = 300;
const unsigned int defaultFillStop = 600;
const unsigned int defaultFillTimer = 300;
unsigned long lcdTimeoutSeconds = 30; // Default timeout 30 seconds


enum OperatingMode { MODE_AUTO, MODE_HAND, MODE_OFF };
OperatingMode currentMode = MODE_HAND; // default

int lastClkState = HIGH;
int encoderDelta = 0;

//Display
TM1637Display display1(CLK1,DIO1);
TM1637Display display2(CLK2,DIO2);

//Wifi
#define WIFI_SSID "Making Wifi Great Again"
#define WIFI_PASS "fiqqdtba"
const char* mqtt_server = "192.168.178.50";

WiFiClient espClient;
PubSubClient client(espClient);
#define MSG_BUFFER_SIZE	(50)
char msg[MSG_BUFFER_SIZE];


const uint16_t maxTankValue1 = 5000 / tank_offset_1; //   , voltage: 3,46
const uint16_t maxTankValue2 = 5000 / tank_offset_2; //   , voltage: 3,71
const uint16_t resistor1 = ((voltageTank_1 / voltage_offset) / 0.02);  // used pull down resistor in Ohm mit offset ausgerechnet (3,46V/0,75V=4,61V   4,61V/0,02A=230,666 OHM)  verbaut 220 OHM
const uint16_t resistor2 = ((voltageTank_2 / voltage_offset) / 0.02);  // used pull down resistor in Ohm mit offset ausgerechnet (3,71V/0,75V=4,95V   4,95V/0,02A=247,333 OHM)  verbaut 220 OHM
const uint8_t vref = 50;
const uint8_t measurements = 170;
const uint8_t highLowFilter = 10;

class TankSensor {
  public:
    uint8_t pin;
    uint16_t resistor;
    uint8_t vref;
    uint8_t measures;
    uint8_t highLow;
    uint16_t maxValue;
    int32_t minAdc;
    int32_t maxAdc;
    int32_t adc = 0;

    TankSensor(uint8_t p, uint16_t r, uint8_t v, uint16_t m, uint8_t mes, uint8_t hLF) {
      pin = p;
      resistor = r;
      vref = v;
      maxValue = m;
      measures = mes;
      highLow = hLF;
      minAdc = (0.004 * resistor * 16384 / (vref / 10.0));
      maxAdc = (0.020 * resistor * 16384 / (vref / 10.0));
    }

    int begin() {
      pinMode(pin, INPUT);
      return 1;
    }

    void check(){
      uint8_t err = 0;
      if (minAdc < 0) {
        Serial.println(F("[Sensor] E:resistor might be to low for your VREF"));
        err++;
      }
      if (maxAdc > 16384 - 1) {
        Serial.println(F("[Sensor] E:resistor might be to large for your VREF"));
        err++;
      }
      if (err == 0)
        Serial.println(F("[Sensor] I:parameters ok, you can remove .check() from setup"));
      else {
        Serial.print(F("minAdc=")); Serial.println(minAdc);
        Serial.print(F("maxAdc=")); Serial.println(maxAdc);
      }
    }                      
  
    int getValue() {
      adc = 0;
      int32_t maxMeas[highLow];
      int32_t minMeas[highLow];
      Serial.println("Measure start");
      for (uint8_t i = 0; i < measures; i++) {
        int32_t meas = analogRead(pin);
        if (i < highLow) {
          maxMeas[i] = meas;
          minMeas[i] = meas;
        } else {
          for (uint8_t j = 0; j < highLow; j++) {
            if (meas > maxMeas[j]) { 
              maxMeas[j] = meas; 
              break; 
            }
          }
          for (uint8_t k = 0; k < highLow; k++) {
            if (meas < minMeas[k]) { 
              minMeas[k] = meas; 
              break; 
            }
          }
        }
        adc += meas;
        delay(10);
      }
      for (uint8_t p = 0; p < highLow; p++) {
        adc = adc - maxMeas[p] - minMeas[p];
      }
      adc = adc / (measures - 2 * highLow);
      //int32_t value = (adc - 186) * 500L / (931 - 186);                          // for 1023*500 we need a long
      int32_t value = (adc - minAdc) * int32_t(maxValue) / (maxAdc - minAdc);      // for 1023*500 we need a long  // -> pressure
      if (value > maxValue) value = maxValue;
      else if (value < 0) value = 0;
      return  value;
    }
};

TankSensor tankSensor1(sensorPin1, resistor1 , vref, maxTankValue1, measurements, highLowFilter); 
TankSensor tankSensor2(sensorPin2, resistor2 , vref, maxTankValue2, measurements, highLowFilter);

bool showFillTrigger = false;

const uint8_t SEG_FILL[] = {
    SEG_A | SEG_E | SEG_F | SEG_G,  // F
    SEG_E | SEG_F,                  // I
    SEG_D | SEG_E | SEG_F,          // L
    SEG_D | SEG_E | SEG_F           // L
};


LiquidCrystal_I2C lcd(0x27, 20, 4);

enum MenuState {
  MAIN,
  MODE,
  VOLUMES,
  EDIT_INT,
  OTHER,
  EDIT_TIMEOUT
};
MenuState currentMenu = MAIN;

int selectedIndex = 0;
int lastPosition = 0;
bool buttonPressed = false;
unsigned long lastButtonPress = 0;
const int debounceTime = 200;


int volumeValues[3] = {defaultFillStart, defaultFillStop, defaultFillTimer}; // Start, Stop, Timer
int editIndex = 0;

unsigned long lastInteractionTime = 0;
bool lcdOn = true;

unsigned long getTimeoutMs() {
  return lcdTimeoutSeconds * 1000UL;
}

void setup() {
  Serial.begin(9600);
  pinMode(ENCODER_SW, INPUT_PULLUP);
	pinMode(ENCODER_CLK, INPUT_PULLUP);
	pinMode(ENCODER_DT, INPUT_PULLUP);
	// pinMode(ENCODER_GND, OUTPUT);
	// pinMode(ENCODER_5V, OUTPUT);
	// digitalWrite(ENCODER_GND, LOW);
	// digitalWrite(ENCODER_5V, HIGH);

  lcd.begin();
  lcd.backlight();

  lcd.print("Initializing...");
  delay(1000);
  lcd.clear();
  displayMenu();

  analogReadResolution(14);
  setup_wifi();

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  displayAndTankSetup();
  pinMode(RELAY_1, OUTPUT);
  pinMode(RELAY_2, OUTPUT);
}

unsigned long lastMsgSend = 0;

void loop() {
  handleEncoder();
  handleButton();

  // Auto-off LCD after timeout
  if (lcdOn && millis() - lastInteractionTime > getTimeoutMs()) {
    lcdOn = false;
    lcd.noBacklight();
    lcd.clear();
  }

  int schalter = 0;
  int ventil = 0;

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if (millis() - lastMsgSend > 5000) {
    lastMsgSend = millis();
    
    StaticJsonDocument<32> doc;
    char output[55];
    int tankValue1 = (tankSensor1.getValue() * voltage_offset); 
    int tankValue2 = (tankSensor2.getValue() * voltage_offset);

    display1.showNumberDec(tankValue1,false);
    if (showFillTrigger == true) { display2.setSegments(SEG_FILL); } //Fill auf Display 2 
    else { display2.showNumberDec(tankValue2,false); }

    doc["t1"] = tankValue1;
    doc["t2"] = tankValue2;
    doc["s"] = schalter;
    doc["v"] = ventil;
    doc["w"] = WiFi.status();
    Serial.println("Read");
    serializeJson(doc, output);
    Serial.println(output);
    client.publish("/home/watertank", output);
    Serial.println("Sent");

  }
}

void wakeLCD() {
  lcdOn = true;
  lcd.backlight();
  displayMenu();
}

void handleEncoder() {
  int clkState = digitalRead(ENCODER_CLK);
  if (clkState != lastClkState && clkState == LOW) {
    int dtState = digitalRead(ENCODER_DT);
    int delta = (dtState != clkState) ? 1 : -1;

    lastInteractionTime = millis();
    if (!lcdOn) {
      wakeLCD();
      lastClkState = clkState;
      return;
    }

    if (currentMenu == EDIT_INT) {
      volumeValues[editIndex] += delta;
      if (volumeValues[editIndex] < 0) volumeValues[editIndex] = 0;
    } else {
      int menuSize = getMenuSize();
      selectedIndex += delta;

      // Clamp selection between 0 and menuSize - 1
      if (selectedIndex < 0) selectedIndex = 0;
      if (selectedIndex >= menuSize) selectedIndex = menuSize - 1;
    }

    if (currentMenu == EDIT_TIMEOUT) {
      lcdTimeoutSeconds += delta;
      if (lcdTimeoutSeconds < 5) lcdTimeoutSeconds = 5;
      if (lcdTimeoutSeconds > 300) lcdTimeoutSeconds = 300;
    }

    displayMenu();
  }
  lastClkState = clkState;
}

void handleButton() {
  if (digitalRead(ENCODER_SW) == LOW && !buttonPressed) {
    if (millis() - lastButtonPress > debounceTime) {
      buttonPressed = true;
      lastButtonPress = millis();
      lastInteractionTime = millis();
      if (!lcdOn) wakeLCD();
      else onButtonPress();
    }
  }

  if (digitalRead(ENCODER_SW) == HIGH && buttonPressed) {
    buttonPressed = false;
  }
}

void onButtonPress() {
  switch (currentMenu) {
    case MAIN:
      if (selectedIndex == 0) currentMenu = MODE;
      else if (selectedIndex == 1) currentMenu = VOLUMES;
      else if (selectedIndex == 2) currentMenu = OTHER;
      selectedIndex = 0;
      break;

    case MODE:
      if (selectedIndex == 3) { // Back
        currentMenu = MAIN;
      } else {
        currentMode = static_cast<OperatingMode>(selectedIndex); // Save current mode
        // Here you could add actions for Auto, Hand, Off
      }
      selectedIndex = 0;
      break;

    case VOLUMES:
      if (selectedIndex == 3) { // Back
        currentMenu = MAIN;
        selectedIndex = 0;
      } else {
        currentMenu = EDIT_INT;
        editIndex = selectedIndex;
      }
      break;

    case EDIT_INT:
      currentMenu = VOLUMES;
      selectedIndex = 0;
      break;

    case OTHER:
      if (selectedIndex == 1) { // Back
        currentMenu = MAIN;
        selectedIndex = 0;
      } else {
        currentMenu = EDIT_TIMEOUT;
        editIndex = selectedIndex;
      }
      break;

    case EDIT_TIMEOUT:
      currentMenu = OTHER;
      selectedIndex = 0;
      break;
  }

  lastPosition = digitalRead(ENCODER_CLK);
  displayMenu();
}

int getMenuSize() {
  switch (currentMenu) {
    case MAIN: return 3;
    case MODE: return 4;     // 3 options + Back
    case VOLUMES: return 4;  // 3 options + Back
    case EDIT_INT: return 1;
    case OTHER: return 2; // 1 options + Back
    case EDIT_TIMEOUT: return 1;
    default: return 0;
  }
}

void displayMenu() {
  lcd.clear();
  String modeText;
  switch (currentMode) {
    case MODE_AUTO: modeText = "Auto"; break;
    case MODE_HAND: modeText = "Hand"; break;
    case MODE_OFF:  modeText = "Off";  break;
  }
  lcd.setCursor(20 - modeText.length(), 0); // Right-align in 20-char width
  lcd.print(modeText);
  switch (currentMenu) {
    case MAIN:
      printFirstLine("Main Menu:", modeText);
      lcd.setCursor(0, 1); lcd.print(selectedIndex == 0 ? "> Mode" : "  Mode");
      lcd.setCursor(0, 2); lcd.print(selectedIndex == 1 ? "> Volumes" : "  Volumes");
      lcd.setCursor(0, 3); lcd.print(selectedIndex == 2 ? "> Other" : "  Other");
      break;

    case MODE: {
      const char* modeItems[] = {"Auto", "Hand", "Off", "<< Back"};
      int menuSize = getMenuSize();
      int scrollStart = constrain(selectedIndex - 1, 0, 1);

      printFirstLine("Mode:", modeText);
      for (int i = 0; i < 3; i++) {
        int idx = scrollStart + i;
        lcd.setCursor(0, i + 1);
        if (idx < menuSize) {
          lcd.print(selectedIndex == idx ? "> " : "  ");
          lcd.print(modeItems[idx]);
        }
      }
      break;
    }

    case VOLUMES: {
      const char* volItems[] = {"Start", "Stop", "Timer", "<< Back"};
      int menuSize = getMenuSize();
      int scrollStart = constrain(selectedIndex - 1, 0, 1);

      lcd.setCursor(0, 0); lcd.print("Volumes:");
      for (int i = 0; i < 3; i++) {
        int idx = scrollStart + i;
        lcd.setCursor(0, i + 1);
        if (idx < 3) {
          lcd.print(selectedIndex == idx ? "> " : "  ");
          lcd.print(volItems[idx]);
          lcd.setCursor(12, i + 1);
          lcd.print(volumeValues[idx]);
        } else if (idx == 3) {
          lcd.print(selectedIndex == idx ? "> << Back" : "  << Back");
        }
      }
      break;
    }

    case EDIT_INT:
      lcd.setCursor(0, 0);
      lcd.print("Editing: ");
      switch (editIndex) {
        case 0: lcd.print("Start"); break;
        case 1: lcd.print("Stop"); break;
        case 2: lcd.print("Timer"); break;
      }
      lcd.setCursor(0, 2);
      lcd.print("Value: ");
      lcd.print(volumeValues[editIndex]);
      lcd.setCursor(0, 3);
      lcd.print("Press to save");
      break;

      case OTHER: {
        const char* otherItems[] = {"LCD Timeout", "<< Back"};
        int menuSize = getMenuSize();
        int scrollStart = constrain(selectedIndex - 1, 0, 1);
        lcd.setCursor(0, 0); lcd.print("Other:");
        for (int i = 0; i < menuSize; i++) {
          int idx = scrollStart + i;
          lcd.setCursor(0, i + 1);
          if (idx < menuSize) {
            lcd.print(selectedIndex == idx ? "> " : "  ");
            lcd.print(otherItems[i]);
            if (i == 0) {
              lcd.setCursor(14, i + 1);
              lcd.print(lcdTimeoutSeconds);
              lcd.print("s");
            }
          } else if (idx == (menuSize - 1)) {
            lcd.print(selectedIndex == idx ? "> << Back" : "  << Back");
          }
        }
        break;
      }

      case EDIT_TIMEOUT:
        lcd.setCursor(0, 0); lcd.print("Set LCD Timeout:");
        lcd.setCursor(0, 2); lcd.print("Timeout: ");
        lcd.print(lcdTimeoutSeconds); lcd.print("s");
        lcd.setCursor(0, 3); lcd.print("Press to save");
        break;
  }
}

void printFirstLine(String leftText, String modeText) {
  lcd.setCursor(0, 0); lcd.print(leftText);
  lcd.setCursor(20 - modeText.length(), 0); lcd.print(modeText); // Re-draw mode
}



void displayAndTankSetup() {
  display1.setBrightness(0);
  display2.setBrightness(0);
  display1.showNumberDec(9999,false);
  display2.showNumberDec(9999,false);
  tankSensor1.begin(); 
  tankSensor2.begin(); 
  display1.showNumberDec(9977,false);
  display2.showNumberDec(9977,false);
  uint8_t testOne = 1;
  Serial.print(testOne);
  tankSensor1.check(); 
  uint8_t testTwo = 2;
  Serial.print(testTwo);
  tankSensor2.check(); 
  display1.showNumberDec(9966,false);
  display2.showNumberDec(9966,false);
}

void setup_wifi() {
  delay(100);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint8_t counter = 0;
  while (WiFi.status() != WL_CONNECTED && counter < 5) {
    delay(500);
    Serial.print(".");
    counter++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    randomSeed(micros());
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  }
  else {
    Serial.println("Could not connect to WIFI");
  }

}

void callback(char* topic, uint8_t* payload, unsigned int length) { }

int reconnect() {
  Serial.write("reconnect logic start");
  setup_wifi();
  uint8_t runLoop = 0;
  while (runLoop < 5) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      client.publish("outTopic", "hello world");
      client.subscribe("inTopic");
      return runLoop;
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
      runLoop++;
    }
    return runLoop;
  }
}
