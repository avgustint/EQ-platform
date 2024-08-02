/*
  Control stepper motor speed for EQ platform with option to set tracking speed or to return platform to home starting position

  Avgustin Tomsic
  August 2024
*/

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Bounce2.h>
#include <AccelStepper.h>
#include <EEPROM.h>

// rotary encoder pins
#define CLK_PIN 2  // rotary encoder clock pin
#define SW_PIN 3   // rotary encoder switch pin
#define DT_PIN 8   // rotary encoder data pin

// OLED display options
#define SCREEN_WIDTH 128     // OLED display width, in pixels
#define SCREEN_HEIGHT 64     // OLED display height, in pixels
#define OLED_RESET -1        // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C  // I2C device address

// Define switch pin
#define HOME_SWITCH_PIN 6 // home direction end switch pin
#define END_SWITCH_PIN 7  // end of tracking switch pin 

bool editMode = false;                       // if we are in editing configuration mode 
unsigned long lastUpdateEditModeScreen = 0;  // when we last updated screen in edit mode for blinking
bool editModeBlinkDark = false;              // toggle blinking in edit mode when value selected
int stepperTrackingSpeed = 1;                // variable for storing stepper tracking speed
int homeSpeed = 100;                         // variable for storing stepper home direction speed
int initialStepperTrackingSpeed = 1;         // variable for storing stepper tracking speed
int initialHomeSpeed = 100;                  // variable for storing stepper home direction speed
uint8_t motorRunning = 0;                    // variable to store if motor should be in running state 0-not running, 1-runnning forward, -1-running backward  
int stepper_speed_address = 0;               // address to store stepper speed into EEPROM   
int home_speed_address = 2;                  // address to store home direction speed in EEPROM

// setup mode variables
uint8_t currentPageIndex;
// Possible values:
// 1 - tracking speed
// 2 - home speed
// 3 - start/pause/continue
// 4 - return home
// 5 - store config


// initialize objects
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);  // initialize OLED display
Bounce debouncerSwitch = Bounce();                                         // Create a Bounce object to prevent any glitches from rotary encoder switch
Bounce debouncerClk = Bounce();                                            // Create a Bounce object to prevent any glitches from rotary encoder clock signal
Bounce debouncerMotor = Bounce();                                          // Create a Bounce object to prevent any glitches from motor encoder clock signal

// Define a stepper motor 1 for arduino
// direction Digital 9 (CW), pulses Digital 8 (CLK)
AccelStepper stepper(1, 8, 9);

// initialize the controller
void setup() {
    // start communication with OLED screen
    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        while (1)
        ;  // Don't proceed, loop forever
    }
    // setup controller pins
    pinMode(CLK_PIN, INPUT_PULLUP);
    pinMode(DT_PIN, INPUT_PULLUP);
    pinMode(SW_PIN, INPUT_PULLUP);

    // Set home and end switch pin as input
    pinMode(HOME_SWITCH_PIN, INPUT_PULLUP);
    pinMode(END_SWITCH_PIN, INPUT_PULLUP);

    debouncerSwitch.attach(SW_PIN);       // Attach  debounce to the rotary encoder press pin
    debouncerSwitch.interval(2);          // Set debounce interval (in milliseconds)
    debouncerClk.attach(CLK_PIN);         // Attach to the rotary encoder clock pin
    debouncerClk.interval(2);             // Set debounce interval (in milliseconds)

    readSpeedConfigFromEeprom();

    // Change these to suit your stepper if you want
    stepper.setMaxSpeed(10);//1100
    stepper.setAcceleration(32000);
    stepper.setSpeed(1);
    
    // clear and initialize the display
    display.clearDisplay();
    display.display();  // this command will display all the data which is in buffer
    display.setTextWrap(false);
}

// main microcontroller loop
void loop() {
  // Update the debouncer
  debouncerSwitch.update();
  debouncerClk.update();
  debouncerMotor.update();

  // Read debounced rotary encoder press button state
  if (debouncerSwitch.fell()) {
    encoderPressed();
  }

  // Read debounced rotary encoder rotating clock event
  if (debouncerClk.fell()) {
    encoderRotated();
  }

  if (editMode) {
    // edit mode
    if (checkTimeoutExpired(lastUpdateEditModeScreen, 300)) {  // update the screen every 300 miliseconds to show that blinking animation on selected value
      editModeBlinkDark = !editModeBlinkDark;                  // toggle between visible or hidden selected value
      lastUpdateEditModeScreen = millis();                     // store last time we toggled the stare for next itteration
      updateMainScreen();                                      // update screen in edit mode
    }
  } else {
    // normal mode
    runTheStepper();
  }
}

// run the stepper motor if needed
void runTheStepper(){
    if (motorRunning!=0){
        // check end position reached 
        if (motorRunning>0 && !digitalRead(END_SWITCH_PIN)){
            motorRunning=0;
            updateMainScreen();
        }
        else if (motorRunning<0 && !digitalRead(HOME_SWITCH_PIN)){
            motorRunning=0;
            updateMainScreen();
        }
        else{
            stepper.runSpeed();
        }
    }
}

// common function to check if different timeouts already expired from last change
bool checkTimeoutExpired(unsigned long lastChange, unsigned int timeoutDuration) {
  unsigned long currentMillis = millis();
  return ((currentMillis - lastChange) >= timeoutDuration);
}


// display main screen info in normal operation - switch between different screens
void updateMainScreen() {
    display.fillRect(0, 0, 128, 64, SSD1306_BLACK);
    display.setTextSize(1);
    display.setTextColor(WHITE);

    // show main screen with all valuable information
    display.setCursor(2, 0);
    char speed[12] = "Track speed:";
    display.print(speed);
    display.setCursor(22, 0);
    display.print(stepperTrackingSpeed);
    
    display.setCursor(2, 22);
    char homeSpeed[11] = "Home speed:";
    display.print(homeSpeed);
    display.setCursor(22, 22);
    display.print(homeSpeed);

    display.setCursor(2, 42);
    char startButton[7] = "Start";
    display.print(startButton);

    display.setCursor(22, 42);
    char homeButton[7] = "Home";
    display.print(homeButton);

    if (trackingSpeedValue!=initialStepperTrackingSpeed || homeSpeedValue!=initialHomeSpeed){
        display.setCursor(42, 42);
        char saveButton[7] = "Save";
        display.print(saveButton);
    }

    display.setCursor(2, 50);
    switch (motorRunning) {
    case 0:  // stopped
        char stopped[14] = "Motor: Stopped";
        display.print(stopped);
        break;
    case 2:  // tracking
        char running[14] = "Motor: Running";
        display.print(running);
        break;
    case 3:  // return home
        char home[21] = "Motor: Returning home";
        display.print(home);
        break;
    }
    if (editMode){
        // one of options selected to edit - blink that option
        if (editModeBlinkDark) {
            switch (currentPageIndex) {
                case 1:  // tracking speed
                display.fillRect(0, 0, 12, 10, SSD1306_BLACK);
                break;
                case 2:  // home speed
                display.fillRect(0, 22, 12, 10, SSD1306_BLACK);
                break;
            }
        }
    }
    else {
        // Possible values:
        // 1 - tracking speed
        // 2- home speed
        // 3 - start
        // 4 - return home
        // 5 - save config
        switch (currentPageIndex) {
        case 1:  // tracking speed
            display.drawFastHLine(2, 15, 12, WHITE);
            break;
        case 2:  // home speed
            display.drawFastHLine(22, 15, 12, WHITE);
            break;
        case 3:  // start
            display.drawFastHLine(2, 42, 12, WHITE);
            break;
        case 4:  // return home
            display.drawFastHLine(22, 24, 12, WHITE);
            break;
        case 4: // save configuration
            if (trackingSpeedValue!=initialStepperTrackingSpeed || homeSpeedValue!=initialHomeSpeed){
                display.drawFastHLine(42, 42, 12, WHITE);
            }
            break;
        }
    }

    display.display();
}

// user presses encoder button
void encoderPressed() {
  if (editMode) {
    editMode = false;
  } else {
    // currently not in edit mode and button pressed
    switch (currentPageIndex) {
    case 1:  // tracking speed
        editMode = true;
        motorRunning = 0; // stop rotating
        break;
    case 2:  // home speed
        editMode = true;
        motorRunning = 0; // stop rotating
        break;
    case 3:  // start
        startStopButtonPressed();
        break;
    case 4:  // return home
        returnHomePressed();
        break;
    case 5:  // save configuration
        saveSpeedConfiguration();
        break;
    }
    updateMainScreen();
  }
}

// event when user is rotating the rotary encoder button
void encoderRotated() {
  uint8_t change = 0;

  int dtState = digitalRead(DT_PIN);
  if (dtState == HIGH) {
    change = 1;  // rotated clockwise
  } else {
    change = -1;  // rotated anticlockwise
  }
  if (editMode) {  // edit mode
    switch (currentPageIndex) {
    case 1:  // tracking speed
        stepperTrackingSpeed = stepperTrackingSpeed + change;
        if (stepperTrackingSpeed<1){
            stepperTrackingSpeed = 1;
        }
        else if (stepperTrackingSpeed>1000){
            stepperTrackingSpeed = 1000;
        }
        break;
    case 2:  // home speed
        homeSpeed = homeSpeed + change;
        if (homeSpeed<1){
            homeSpeed = 1;
        }
        else if (homeSpeed>1000){
            homeSpeed = 1000;
        }
        break;
    }
  } else {
    // rotate between options
    currentPageIndex = currentPageIndex + change;
    if (currentPageIndex<1){
        currentPageIndex = 3;
    }
    else if (currentPageIndex>3){
        currentPageIndex = 1;
    }
  }
  updateMainScreen();
}

void startStopButtonPressed(){
    stepper.setSpeed(stepperTrackingSpeed);
    stepper.setAcceleration(20);
    motorRunning = motorRunning!=0?0:1;
}
            
void returnHomePressed(){
    stepper.setSpeed(-homeSpeed); 
    stepper.setAcceleration(20);
    motorRunning = -1;
}

void saveSpeedConfiguration(){
    writeIntIntoEEPROM(stepper_speed_address,stepperTrackingSpeed);
    writeIntIntoEEPROM(home_speed_address,homeSpeed);
    initialStepperTrackingSpeed = stepperTrackingSpeed;
    initialHomeSpeed = homeSpeed  
}

void writeIntIntoEEPROM(int address, int number) {
  byte byte1 = number >> 8; // Get the higher 8 bits
  byte byte2 = number & 0xFF; // Get the lower 8 bits
  EEPROM.write(address, byte1); // Store the first byte
  EEPROM.write(address + 1, byte2); // Store the second byte
}

void readSpeedConfigFromEeprom(){
    int trackingSpeedValue = (EEPROM.read(stepper_speed_address) << 8) | EEPROM.read(stepper_speed_address+1); // Read tracking speed
    int homeSpeedValue = (EEPROM.read(home_speed_address) << 8) | EEPROM.read(home_speed_address+1); // Read home speed

    // Check if the values are valid (not undefined)
    if (trackingSpeedValue != 0xFFFF && homeSpeedValue != 0xFFFF) {
        stepperTrackingSpeed = trackingSpeedValue;
        homeSpeed = homeSpeedValue;              
    } else {
        // Values are undefined (EEPROM was cleared or uninitialized)
        stepperTrackingSpeed = 1;         
        homeSpeed = 100; 
    }
    initialStepperTrackingSpeed = stepperTrackingSpeed;
    initialHomeSpeed = homeSpeed  
}
