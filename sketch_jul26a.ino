#include <Arduino.h>
#include <Wire.h>
#include <OneButton.h>
#include <LiquidCrystal_I2C.h>
#include <TM1637Display.h>
#include <ArduinoJson.h>
#include <ArduinoJson.hpp>
//#include <WiFiS3.h>
#include <PubSubClient.h>

// ****************************************************************************
// Pin Setup
// ****************************************************************************
#define PIN_ROTARY_CLK    2   
#define PIN_ROTARY_DAT    3   
#define PIN_ROTARY_SW     4   
#define PIN_ROTARY_5V     5   // Set to HIGH to be the 5V pin for the Rotary Encoder
#define PIN_ROTARY_GND    6   // Set to LOW to be the GND pin for the Rotary Encoder

#define RELAY_3           8

#define CLK1              13  
#define DIO1              12
#define CLK2              11
#define DIO2              10

const uint8_t sensorPin1 = A1;     // Analog Pin fur Sensor 1 //A1= 15
const uint8_t sensorPin2 = A2;     // Analog Pin fur Sensor 2 //A2= 16

// ****************************************************************************
// LCD Setup
// ****************************************************************************
#define LCD_I2C_ADDRESS   0x27
#define LCD_ROW_COUNT       4    // Number of Rows
#define LCD_COL_COUNT       20   // Number of Characters per Row


// ****************************************************************************
// Object Creation
// ****************************************************************************
OneButton btnRot(PIN_ROTARY_SW, HIGH);
LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, LCD_COL_COUNT, LCD_ROW_COUNT);
TM1637Display display1(CLK1,DIO1);
TM1637Display display2(CLK2,DIO2);



// ****************************************************************************
// Program global Varaibles
// ****************************************************************************
bool isLcdOn = false;
bool lcdHasBeenDisabled = false;
bool defaultSettings = true;
volatile int rotaryCount = 0;
volatile int rotaryPressCount = 0;
volatile int rotaryPressCountOld = 0;
volatile int menuSize = 0;
int currentMenu;
int nextMenu;
int currentMenuSize;
const uint8_t defaultModus = 2; // Auto = 0, Hand = 1, Aus = 2
uint8_t modus = defaultModus;  

// ****************************************************************************
// Rotary Internal Variables
// ****************************************************************************
byte rotaryDisabled;
volatile byte aFlag = 0; 
volatile byte bFlag = 0;
volatile byte reading = 0; 

// ****************************************************************************
// Timer Variables
// ****************************************************************************
unsigned long currentTime;
unsigned long previousTime = 0;
unsigned long eventInterval = 15000;

// ****************************************************************************
// Grenzwert Variables in Liter und millisec
// ****************************************************************************
const unsigned int defaultFillStart = 300;
const unsigned int defaultFillStop = 600;
const unsigned long defaultFillTimer = 300000;
int fillStart = defaultFillStart;
int fillStop = defaultFillStop;
unsigned long fillTimer = defaultFillTimer;

// ****************************************************************************
// Menu Setup
// ****************************************************************************

/* Menu Summary
  - 0 mainMenu {
    - 1 modusMenu 
      {
        - 11 Auto
        - 12 Hand
        - 13 Aus

        - size-1 <--
      } 
    - 2 grenwertMenu 
      {
        - 21 Füllbeginn
        - 22 Füllstop
        - 23 Max Fülldauer

        - size-1 <--
      }

  }
*/

#define mainMenuId 0

#define modusMenuId 1
#define grenzwertMenuId 2

#define fillbeginnId 21
#define fillstopId 22
#define max_FilldauerId 23


int indexArray[ ] = {
  0,
    1,
    2,
      11,
      12,
      13,
      14,
      21,
      22,
      23,
      24
};

int nonMenuIndexArray[ ] = {
  11,
  12,
  13,
};

String mainMenu[ ] = { //mainMenuActive //id: 0
  "Modus",
  "Grenzwerte"
};

String modusMenu[ ] = { //modusMenuActive //id: 1
  "Auto", 
  "Hand",
  "Aus",
  "<--"
};

String grenzwertMenu[ ] = { //grenzwertMenuActive //id: 2
  "Fuellbeginn",
  "Fuellstop",
  "Max Fuelldauer",
  "<--"
};

String fillstartMenu[ ] = { 
  "Fuellbeginn",
  "Fuellstop",
  "Max Fuelldauer",
  "<--"
};



// ****************************************************************************
// Called by the Interrupt pin when the Rotary Encoder Turned
// ****************************************************************************
void PinA() {
	if (rotaryDisabled) return;
	cli(); //stop interrupts happening before we read pin values
		   // read all eight pin values then strip away all but pinA and pinB's values
	reading = PIND & 0xC;
	//check that both pins at detent (HIGH) and that we are expecting detent on this pin's rising edge
	if (reading == B00001100 && aFlag) {
		rotaryUp();
    turnLcdOn();
		bFlag = 0; //reset flags for the next turn
		aFlag = 0; //reset flags for the next turn
	}
	//signal that we're expecting pinB to signal the transition to detent from free rotation
	else if (reading == B00000100) bFlag = 1;
	sei(); //restart interrupts
}

void PinB() {
	if (rotaryDisabled) return;
	cli(); //stop interrupts happening before we read pin values
		   //read all eight pin values then strip away all but pinA and pinB's values
	reading = PIND & 0xC;
	//check that both pins at detent (HIGH) and that we are expecting detent on this pin's rising edge 
	if (reading == B00001100 && bFlag) {
		rotaryDown();
    turnLcdOn();
		bFlag = 0; //reset flags for the next turn
		aFlag = 0; //reset flags for the next turn
	}
	//signal that we're expecting pinA to signal the transition to detent from free rotation
	else if (reading == B00001000) aFlag = 1;
	sei(); //restart interrupts
}


// ****************************************************************************
// rotary*() - Functions for Rotary Encoder actions
// ****************************************************************************
void rotaryUp() {
  if (rotaryCount > 0) {
    rotaryCount--;
  }
	else {
    rotaryCount = 0;
  }
}

void rotaryDown() {
  if (rotaryCount < menuSize) {
    rotaryCount++;
  }
	else {
    rotaryCount = menuSize;
  }
}

void rotaryClick() {
  if (isLcdOn) {
    updateNextMenu();
  }
}

void rotaryLongPress() {
  turnLcdOn();
  setMainMenu();
	rotaryPressCount = 0;
}


// ****************************************************************************
// Initilazations
// ****************************************************************************
void initializeRotaryEncoder() {
	pinMode(PIN_ROTARY_CLK, INPUT_PULLUP);
	pinMode(PIN_ROTARY_DAT, INPUT_PULLUP);
	pinMode(PIN_ROTARY_SW, INPUT_PULLUP);
	pinMode(PIN_ROTARY_GND, OUTPUT);
	pinMode(PIN_ROTARY_5V, OUTPUT);
	digitalWrite(PIN_ROTARY_GND, LOW);
	digitalWrite(PIN_ROTARY_5V, HIGH);
	attachInterrupt(0, PinA, RISING);
	attachInterrupt(1, PinB, RISING);
	btnRot.attachClick(&rotaryClick);
	btnRot.attachLongPressStart(&rotaryLongPress);
	btnRot.setPressTicks(2000);
	rotaryDisabled = 0;
}


void initializeLcd() {
	lcd.begin();
	lcd.backlight();
  setMainMenu();
}





// ****************************************************************************
// LCD Functions
// ****************************************************************************
void turnLcdOn() {
  previousTime = currentTime;
  isLcdOn = true;
}


void setMainMenu() {
  lcd.clear();
  int size = sizeof(mainMenu) / sizeof(String);
  currentMenu = 0;
  nextMenu = 0;
  currentMenuSize = size;
  rotaryCount = 0;
  showMenu(mainMenu, size, mainMenuId);
}


void showMenu(String givenMenu[], int size, int nextId) {
  menuSize = size-1;
  for (int i = 0; i < size; i++) {
    lcd.setCursor(0, i);
    String value = givenMenu[i];

    if (rotaryCount == i) {
      lcd.print("*");
    }
    else {
      lcd.print(" ");
    }

    lcd.setCursor(2, i);
    lcd.print(value);
  }

  currentMenu = nextId;
  currentMenuSize = size;
  nextMenu = nextId;
  isLcdOn = true;

  printDebug();
}


void moveCursor(int topLimit, int size) {
  for (int i = topLimit; i < size; i++) {
    lcd.setCursor(0, i);
    if (rotaryCount == i) {
      lcd.print("*");
    }
    else {
      lcd.print(" ");
    }
  }

  isLcdOn = true;

  printDebug();
}


void updateLcd() {
  if (isLcdOn) {
  
    lcd.setCursor(17, 0);
    lcd.print("D:");
    lcd.setCursor(19, 0);
    if (defaultSettings) {
    lcd.print("✓");
    } else {
      lcd.print("X");
    }
    rotaryDisabled = 1;
    if (currentMenu == nextMenu) { 
      moveCursor(0, currentMenuSize); 
    } 
    else { selectCorrectMenu(); }
    rotaryDisabled = 0;

    if (lcdHasBeenDisabled) {
      lcdHasBeenDisabled = false;
      lcd.display();
      lcd.backlight();
    }
    else if (currentTime - previousTime >= eventInterval) {
        previousTime = currentTime;
        lcd.noDisplay();
        lcd.noBacklight();
        isLcdOn = false;
        lcdHasBeenDisabled = true;
    }
  }

  else { }
}


void updateNextMenu() {
  if (currentMenu == 0) { //in mainMenu
    nextMenu = rotaryCount + 1; 
  } 

  else {
    if (rotaryCount == currentMenuSize - 1) { // aka zurück
      nextMenu = currentMenu / 10;
    }

    else {
      int temp = currentMenu * 10 + rotaryCount + 1;
      if (checkForNonMenu(temp)) {
        doNonMenuStuff(temp);
      }
      
      else if (checkArrayIndex(temp)) {
        nextMenu = temp;
      }
    }
    
  }

  rotaryCount = 0;
}


bool checkArrayIndex(int nextElement) {
  int s2 = sizeof(indexArray) / sizeof(int);
  for (int i = 0; i < s2; i++) {
    if (indexArray[i] > nextElement) {
      return false;
    }
    else if (indexArray[i] == nextElement) {
      return true;
    }    
  }
  return false;
}

bool checkForNonMenu(int nextElement) {
  int s1 = sizeof(nonMenuIndexArray) / sizeof(int);
  for (int i = 0; i < s1; i++) {
    if (nonMenuIndexArray[i] == nextElement) {
      return true;
    }
  }
}

void selectCorrectMenu() {
  int size;
  lcd.clear();
  rotaryCount = 0;
  switch (nextMenu) {
  case mainMenuId:
    size = sizeof(mainMenu) / sizeof(String);
    showMenu(mainMenu, size, mainMenuId);
    break;

  case modusMenuId:
    size = sizeof(modusMenu) / sizeof(String);
    showMenu(modusMenu, size, modusMenuId);
    displaySelectedModus(0, true);
    break;
  
  case grenzwertMenuId:
    size = sizeof(grenzwertMenu) / sizeof(String);
    showMenu(grenzwertMenu, size, grenzwertMenuId);
    break;

  default:
    size = sizeof(mainMenu) / sizeof(String);
    showMenu(mainMenu, size, mainMenuId);
    break;
  }
}


void doNonMenuStuff(int selectedItem) { //TODO
  int parentMenuIndex = selectedItem / 10;
  int actualItemIndex = selectedItem - (parentMenuIndex * 10);
  if (parentMenuIndex == 1) { // modus menu
    uint8_t old = modus;
    modus = actualItemIndex - 1;
    displaySelectedModus(old, false);
  }
  if (parentMenuIndex == 2) { // grenzwerte stuff

  }
}

void displaySelectedModus(uint8_t oldModus, bool firstTime) {
  if (firstTime) {
    lcd.setCursor(1, modus);
    lcd.print("#");
  } else {
    if (modus != defaultModus) {
      defaultSettings = false;
    }
    if (oldModus != modus) {
      lcd.setCursor(1, oldModus);
      lcd.print(" ");
      lcd.setCursor(1, modus);
      lcd.print("#");
    }
  }
}

// ****************************************************************************
// Debug
// ****************************************************************************
void printDebug() {
  lcd.setCursor(10, 3);
  lcd.print(rotaryCount);
  lcd.print(" ");
  lcd.print(rotaryPressCount);
  lcd.print(" ");
  lcd.print(currentMenu);
  lcd.print(" ");
  lcd.print(nextMenu);
}


// ****************************************************************************
// setup() - Initialization Function
// ****************************************************************************
void setup() {
  Serial.begin(9600);
	initializeRotaryEncoder();
	initializeLcd();
  pinMode(RELAY_3, OUTPUT);
}



// ****************************************************************************
// loop() - Main Program Loop Function 
// ****************************************************************************
void loop() {

  currentTime = millis();
  
	updateLcd();
	btnRot.tick();
	delay(50);
}