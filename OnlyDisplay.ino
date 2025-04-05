#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Encoder.h>

#define ENCODER_CLK 2
#define ENCODER_DT 3
#define ENCODER_SW 4
#define ENCODER_5V 5
#define ENCODER_GND 6

LiquidCrystal_I2C lcd(0x27, 20, 4);
Encoder knob(ENCODER_DT, ENCODER_CLK);

enum MenuState {
  MAIN,
  MODE,
  VOLUMES,
  EDIT_INT,
  OTHER,
  EDIT_TIMEOUT
};
MenuState currentMenu = MAIN;

enum OperatingMode { MODE_AUTO, MODE_HAND, MODE_OFF };
OperatingMode currentMode = MODE_HAND; // default

int selectedIndex = 0;
int lastPosition = 0;
bool buttonPressed = false;
unsigned long lastButtonPress = 0;
const int debounceTime = 200;


const unsigned int defaultFillStart = 300;
const unsigned int defaultFillStop = 600;
const unsigned int defaultFillTimer = 300;

int volumeValues[3] = {defaultFillStart, defaultFillStop, defaultFillTimer}; // Start, Stop, Timer
int editIndex = 0;

unsigned long lastInteractionTime = 0;
unsigned long lcdTimeoutSeconds = 30; // Default timeout 30 seconds
bool lcdOn = true;

unsigned long getTimeoutMs() {
  return lcdTimeoutSeconds * 1000UL;
}

void setup() {
  Serial.begin(9600);
  pinMode(ENCODER_SW, INPUT_PULLUP);
	pinMode(ENCODER_CLK, INPUT_PULLUP);
	pinMode(ENCODER_DT, INPUT_PULLUP);
	pinMode(ENCODER_GND, OUTPUT);
	pinMode(ENCODER_5V, OUTPUT);
	digitalWrite(ENCODER_GND, LOW);
	digitalWrite(ENCODER_5V, HIGH);

  lcd.begin();
  lcd.backlight();

  lcd.print("Initializing...");
  delay(1000);
  lcd.clear();
  displayMenu();
}

void loop() {
  handleEncoder();
  handleButton();

  // Auto-off LCD after timeout
  if (lcdOn && millis() - lastInteractionTime > getTimeoutMs()) {
    lcdOn = false;
    lcd.noBacklight();
    lcd.clear();
  }
}

void wakeLCD() {
  lcdOn = true;
  lcd.backlight();
  displayMenu();
}

void handleEncoder() {
  int newPos = knob.read() / 4;
  if (newPos != lastPosition) {

    lastInteractionTime = millis();
    if (!lcdOn) wakeLCD();

    int delta = newPos - lastPosition;

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

    lastPosition = newPos;
    displayMenu();
  }
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

  lastPosition = knob.read() / 4;
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
