/*
  Control stepper motor speed for EQ platform

  Avgustin Tomsic
  August 2024
*/

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Bounce2.h>
#include <AccelStepper.h>

// rotary encoder pins
#define CLK_PIN 2  // rotary encoder clock pin
#define SW_PIN 3   // rotary encoder switch pin
#define DT_PIN 8   // rotary encoder data pin

// OLED display options
#define SCREEN_WIDTH 128     // OLED display width, in pixels
#define SCREEN_HEIGHT 64     // OLED display height, in pixels
#define OLED_RESET -1        // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C  // I2C device address
#define HOME_MOTOR_SPEED 500 // returning home position motor speed

bool editMode = false;                       // if we are in editing configuration mode 
unsigned long lastUpdateEditModeScreen = 0;  // when we last updated screen in edit mode for blinking
bool editModeBlinkDark = false;              // toggle blinking in edit mode when value selected
unsigned long lastUserInteraction = 0;       // stored time when user done some interaction in edit mode - auto cancel edit mode after timeout
int stepperSpeed = 1;                        // variable for storing stepper speed
bool motorRunning = false;                   // variable to store if motor should be in running state   

// setup mode variables
uint8_t currentPageIndex;
// Possible values:
// 1 - speed
// 2 - start/pause/continue
// 3 - return home


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

  debouncerSwitch.attach(SW_PIN);       // Attach  debounce to the rotary encoder press pin
  debouncerSwitch.interval(2);          // Set debounce interval (in milliseconds)
  debouncerClk.attach(CLK_PIN);         // Attach to the rotary encoder clock pin
  debouncerClk.interval(2);             // Set debounce interval (in milliseconds)

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
    if (motorRunning){
        stepper.runSpeed();
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
    char speed[7] = "Speed:";
    display.print(speed);
    display.setCursor(22, 0);
    display.print(stepperSpeed);
    
    display.setCursor(2, 22);
    char startButton[7] = "Start";
    display.print(startButton);

    display.setCursor(22, 22);
    char homeButton[7] = "Home";
    display.print(homeButton);

    if (editMode){
        // one of options selected to edit - blink that option
        if (editModeBlinkDark) {
            switch (currentPageIndex) {
                case 1:  // speed
                display.fillRect(0, 0, 12, 10, SSD1306_BLACK);
                break;
            }
        }
    }
    else {
        // Possible values:
        // 1 - speed
        // 2 - start
        // 3 - return home
        switch (currentPageIndex) {
        case 1:  // speed
            display.drawFastHLine(2, 15, 12, WHITE);
            break;
        case 2:  // start
            display.drawFastHLine(2, 24, 12, WHITE);
            break;
        case 3:  // return home
            display.drawFastHLine(22, 24, 12, WHITE);
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
    case 1:  // speed
        editMode = true;
        break;
    case 2:  // start
        startStopButtonPressed();
        break;
    case 3:  // return home
        returnHomePressed();
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
    case 1:  // speed
        stepperSpeed = stepperSpeed + change;
        if (stepperSpeed<1){
            stepperSpeed = 1;
        }
        else if (stepperSpeed>1000){
            stepperSpeed = 1000;
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
    motorRunning = !motorRunning;
}
            
void returnHomePressed(){
    motorRunning = true;
    stepper.setSpeed(-HOME_MOTOR_SPEED); 
    stepper.setAcceleration(20);
    stepper.runSpeed();
}
