/*
Name: Nixie Clock
Author: Caleb Davison
Created: 16 May 2015
Description: This code displays the minutes from the RTC. 
When the time changes, the PWM fades out the changed digit, increments it, then fades back in. 
There is a button for both the hours and minutes to change RTC time
*/
#include <Wire.h>
#include "Adafruit_MCP23017.h"
#include "RTClib.h"
#include "RTC_DS3231.h"
#include "SPI.h"
#include <FlexiTimer2.h> // Manual PWM library (http://www.arduino.cc/playground/Main/FlexiTimer2)

#define SQW_FREQ DS3231_SQW_FREQ_1024     //0b00001000   1024Hz
#define PERIOD 256 // Period of the PWM wave (and therefore the number of levels)

RTC_DS3231 RTC;
Adafruit_MCP23017 mcp;
Adafruit_MCP23017 mcp2;

/********* START PWM CODE FUNCTIONS/DECLARATIONS **********/
namespace AnyPWM {
extern volatile byte pinLevel[12];
void pulse();
void analogWrite(byte pin, byte level);
void init();
}

// Variables to keep track of the pin states
volatile byte AnyPWM::pinLevel[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

// Set a digital out pin to a specific level
void AnyPWM::analogWrite(byte pin, byte level) {
  if (pin > 1 && pin < 14 && level >= 0 && level < PERIOD) {
    pin -= 2;
    AnyPWM::pinLevel[pin] = level;
    if (level == 0) {
      digitalWrite(pin + 2, LOW);
    }
  }
}

// Initialize the timer routine; must be called before calling
// AnyPWM::analogWrite!
void AnyPWM::init() {
  // (PERIOD * 64) Hertz seems to be a high enough frequency to produce
  // a steady PWM signal on all 12 output pins
  FlexiTimer2::set(1, 1.0 / (PERIOD * 64), AnyPWM::pulse);
  FlexiTimer2::start();
}

// Routine to emit the PWM on the pins
void AnyPWM::pulse() {
  static int counter = 0;
  for (int i = 0; i < 12; i += 1) {
    if (AnyPWM::pinLevel[i]) {
      digitalWrite(i + 2, AnyPWM::pinLevel[i] > counter);
    }
  }
  counter = ++counter > PERIOD ? 0 : counter;
}
/********** END PWM CODE FUNCTIONS/DECLARATIONS ***********/

// constants won't change. They're used here to set pin numbers:
const int hoursButton = 3; // the pin number of the hours set button
const int minutesButton = 2;    // the pin number of minutes set button

// Variables will change:
int buttonState_hours;             // the current reading from the input pin
int lastButtonState_hour = LOW;   // the previous reading from the input pin
int buttonState_min;             // the current reading from the input pin
int lastbuttonState_min = LOW;   // the previous reading from the input pin

int minOrValue = 0;
int hoursOrValue = 0;
int secOrValue = 0;

// the following variables are long's because the time, measured in miliseconds,
// will quickly become a bigger number than can be stored in an int.
long lastDebounceTime_hours = 0;  // the last time the output pin was toggled
long lastDebounceTime_min = 0;  // the last time the output pin was toggled
long debounceDelay = 50;    // the debounce time; increase if the output flickers

DateTime prevTime;

// Declare Array Values for easy and quick reference
int hourTens = 0;
int hourOnes = 1;
int minTens = 2;
int minOnes = 3;
int secTens = 4;
int secOnes = 5;

volatile int time[6] = { 0, 0, 0, 0, 0, 0 }; // used to separate each h/m/s of time to a part of the array for reference
int brightness[6] = { 255, 255, 255, 255, 255, 255 }; // initial brightness for each nixie tube
int fadeAmount[6] = { -17, -17, -17, -17, -17, -17 }; // fade amount for each time value (17 had to be used because 5 wasn't fast enough and woudl skip numbers)
bool fade[6] = { 0, 0, 0, 0, 0, 0 }; // bool values for entering the fade out/in loops for each nixie tube
const int LED[6] = { 8, 9,10, 11, 12, 13}; // the PWM pins for the nixie tubes

void setup() {
  Serial.begin(115200);

  Serial.println("Intializing the I2C");
  
  Serial.println("Setting Up 0x20...");
  // initialize the IO of Port A and B to outputs for 0x20
  mcp.begin();      // use default address 0

  Wire.beginTransmission(0x20);
  Wire.write(0x00); // IODIRA register
  Wire.write(0x00); // set all of port A to outputs
  Wire.endTransmission();

  Wire.beginTransmission(0x20);
  Wire.write(0x01); // IODIRB register
  Wire.write(0x00); // set all of port B to outputs
  Wire.endTransmission();
 
  Serial.println("Setting Up 0x21...");
  // initialize the IO of Port A and B to outputs for 0x21
  mcp2.begin(1);

  Wire.beginTransmission(0x21);
  Wire.write(0x00); // IODIRA register
  Wire.write(0x00); // set all of port A to outputs
  Wire.endTransmission();
  
  Wire.beginTransmission(0x21);
  Wire.write(0x01); // IODIRB register
  Wire.write(0x00); // set all of port B to outputs
  Wire.endTransmission();
  
  Serial.println("Setting Up hours and minutes buttons...");

  pinMode(hoursButton, INPUT);
  pinMode(minutesButton, INPUT);

  //--------RTC SETUP ------------
  Serial.println("Setting Up RTC...");

  Wire.begin();
  RTC.begin();

  if (! RTC.isrunning()) {
    Serial.println("RTC is NOT running!");
    // following line sets the RTC to the date & time this sketch was compiled
    RTC.adjust(DateTime(__DATE__, __TIME__));
  }

  DateTime now = RTC.now();
  prevTime = now;
  DateTime compiled = DateTime(__DATE__, __TIME__);
  if (now.unixtime() < compiled.unixtime()) {
    Serial.println("RTC is older than compile time!  Updating");
    RTC.adjust(DateTime(__DATE__, __TIME__));
  }

  RTC.enable32kHz(true);
  RTC.SQWEnable(true);
  RTC.BBSQWEnable(true); // enable the backup battery for when loss of power
  RTC.SQWFrequency( SQW_FREQ );

  // Initialize the PWM's
  AnyPWM::init();       // initialize the PWM timer
  // set all the pwm pins for the Nixie tubes to be outputs
  for(int i=0; i<6; i++) {
    pinMode(LED[i], OUTPUT);
  }
  Serial.println("Finished initialization");

  displayTime(now);
}

void hoursButtonDebounce(DateTime now) {
  // read the state of the switch into a local variable:
  int reading = digitalRead(hoursButton);

  // If the switch changed, due to noise or pressing:
  if (reading != lastButtonState_hour) {
    // reset the debouncing timer
    lastDebounceTime_hours = millis();
  }
  if ((millis() - lastDebounceTime_hours) > debounceDelay) {
    // whatever the reading is at, it's been there for longer
    // than the debounce delay, so take it as the actual current state:

    // if the button state has changed:
    if (reading != buttonState_hours) {
      buttonState_hours = reading;
      // only toggle the LED if the new button state is HIGH
      // only increase clock by one hour if the new button state is HIGH
      if (buttonState_hours == HIGH) {
        if (fade[hourOnes] == false) {
          int addHour = now.hour() + 1;
          if (addHour == 24) {
            addHour = 0;
          }
          RTC.adjust( DateTime ( now.year(), now.month(), now.day(), (addHour), now.minute(), now.second() ) );
        }
      }
    }
  }

  // save the reading.  Next time through the loop,
  // it'll be the lastButtonState:
  lastButtonState_hour = reading;
}

void minutesButtonDebounce(DateTime now) {
  // read the state of the switch into a local variable:
  int reading = digitalRead(minutesButton);

  // check to see if you just pressed the button
  // (i.e. the input went from LOW to HIGH),  and you've waited
  // long enough since the last press to ignore any noise:

  // If the switch changed, due to noise or pressing:
  if (reading != lastbuttonState_min) {
    // reset the debouncing timer
    lastDebounceTime_min = millis();
  }

  if ((millis() - lastDebounceTime_min) > debounceDelay) {
    // whatever the reading is at, it's been there for longer
    // than the debounce delay, so take it as the actual current state:

    // if the button state has changed:
    if (reading != buttonState_min) {
      buttonState_min = reading;

      // only toggle the LED if the new button state is HIGH
      // only increase clock by one minute if the new button state is HIGH
      if (buttonState_min == HIGH) {
        if (fade[minOnes] == false) {
          int addMin = now.minute() + 1;
          int Hours = now.hour();
          if (addMin == 60) {
            addMin = 0;
            Hours = now.hour() + 1;
            if (Hours == 24) {
              Hours = 0;
            }
          }
          RTC.adjust( DateTime ( now.year(), now.month(), now.day(), (Hours), (addMin), now.second() ) );
        }
      }
    }
  }
  lastbuttonState_min = reading; // save the reading.  Next time through the loop, it'll be the lastbuttonState_min:
}


void displayTime(DateTime now) {
  Serial.println("Displaying the time!");
  time[hourOnes] = (now.hour()) % 10;
  time[hourTens] = ((now.hour()) % 100 - time[hourOnes]) / 10;
  hoursOrValue = (time[hourTens] << 4) | time[hourOnes];

  time[minOnes] = (now.minute()) % 10;
  time[minTens] = ((now.minute()) % 100 - time[minOnes]) / 10;
  minOrValue = (time[minOnes] << 4) | time[minTens];

  time[secOnes] = (now.second()) % 10;
  time[secTens] = ((now.second()) % 100 - time[secOnes]) / 10;
  secOrValue = (time[secTens] << 4) | time[secOnes];
  
  double tmp = minOrValue;
  mcp.writeGPIOAB((hoursOrValue << 8) | minOrValue);
  mcp2.writeGPIOAB(secOrValue << 8);
}

void setBrightness(DateTime setTime, int clockPos) {
  brightness[clockPos] = brightness[clockPos] + fadeAmount[clockPos];

  // if 0 brightness, increment the time then reverse the fadeAmount so it will get bright again
  if (brightness[clockPos] == 0) {
    fadeAmount[clockPos] = -fadeAmount[clockPos];
    displayTime(setTime);
  }
  // if full brightness, stop fading logic and keep high
  if (brightness[clockPos] == 255) {
    fadeAmount[clockPos] = -fadeAmount[clockPos];
    fade[clockPos] = false;
  }
}

// Main Loop
void loop() {

  DateTime now = RTC.now();

  if (prevTime.hour() != now.hour()) {
    Serial.println("The hour value has changed!");
    // check to see if the tens digit has also changed
    if ( ((prevTime.hour() / 10) % 10) != ((now.hour() / 10) % 10)) {
      Serial.println("The tens value has changed too!");
      fade[hourTens] = true;
    }
    // update the previous time
    prevTime = now;
    fade[hourOnes] = true;
  }
  if (prevTime.minute() != now.minute()) {
    Serial.println("The minute value has changed!");
    // check to see if the tens digit has also changed
    if ( ((prevTime.minute() / 10) % 10) != ((now.minute() / 10) % 10)) {
      Serial.println("The tens value has changed too!");
      fade[minTens] = true;
    }
    // update the previous time
    prevTime = now;
    fade[minOnes] = true;
  }
  if (prevTime.second() != now.second()) {
    Serial.println("The second value has changed!");
    // check to see if the tens digit has also changed
    if ( ((prevTime.second() / 10) % 10) != ((now.second() / 10) % 10)) {
      Serial.println("The tens value has changed too!");
      fade[secTens] = true;
    }
    // update the previous time
    prevTime = now;
    fade[secOnes] = true;
  }

  hoursButtonDebounce(now);
  minutesButtonDebounce(now);

  // loop through the hours/min/sec digits for the clock and set the brightness for each tube
  for(int i=0; i<6; i++) {
    // the logic for fading in/out the ones digit on the minutes of the clock
    if (fade[i]) setBrightness(now, i);
    // set the brightness of the Nixie Tube:
    AnyPWM::analogWrite(LED[i], brightness[i]);

  }
//  Serial.println(brightness[secOnes]);

  delay(5);
}
