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
#include <SoftPWM.h>

#define SQW_FREQ DS3231_SQW_FREQ_1024     //0b00001000   1024Hz

RTC_DS3231 RTC;
Adafruit_MCP23017 mcp;
Adafruit_MCP23017 mcp2;


// constants won't change. They're used here to set pin numbers:
const int hoursButton = 3; // the pin number (D3) of the hours set button
const int minutesButton = 2;    // the pin number of minutes set button
const int LEDselectButton = 4; // the pin number of the LED select button
const int analogPin = 3; // this is for A3

// Variables will change:
int buttonState_hours;             // the current reading from the input pin
int lastButtonState_hour = LOW;   // the previous reading from the input pin
int buttonState_min;             // the current reading from the input pin
int lastbuttonState_min = LOW;   // the previous reading from the input pin
int buttonState_LEDselect;  // the current reading from the input pin
int lastbuttonState_LEDselect = LOW; // the current reading from the input pin

int minOrValue = 0;
int hoursOrValue = 0;
int secOrValue = 0;

int val = 0; // variable to store the analog voltage read

// the following variables are long's because the time, measured in miliseconds,
// will quickly become a bigger number than can be stored in an int.
long lastDebounceTime_hours = 0;  // the last time the hours output pin was toggled
long lastDebounceTime_min = 0;  // the last time the minutes output pin was toggled
long lastDebounceTime_LEDselect = 0; // the last time the LED select pin was toggled
long debounceDelay = 50;    // the debounce time; increase if the output flickers

long ButtonHighTime = 0;
const int ButtonPressTime = 2000; // 2 seconds

DateTime prevTime;

// Declare Array Values for easy and quick reference
int hourTens = 0;
int hourOnes = 1;
int minTens = 2;
int minOnes = 3;
int secTens = 4;
int secOnes = 5;

volatile int time[6] = { 0, 0, 0, 0, 0, 0 }; // used to separate each h/m/s of time to a part of the array for reference
int brightness_nixie[6] = { 255, 255, 255, 255, 255, 255 }; // initial brightness for each nixie tube
int fadeAmount[6] = { -17, -17, -17, -17, -17, -17 }; // fade amount for each time value (17 had to be used because 5 wasn't fast enough and woudl skip numbers)
bool fade[6] = { 0, 0, 0, 0, 0, 0 }; // bool values for entering the fade out/in loops for each nixie tube
const int Nixie_PWM[6] = { 8, 9,10, 11, 12, 13}; // the PWM pins for the nixie tubes
const int LED_PWM[3] = { 14, 15, 16};

int RGB_ON[3] = { 1, 1, 1}; // R/G/B enable bits for software enable/disable logic
int RGB_ProgramState = 0; // 0 is all on mode where all three colors will be enabled
int RGB_Brightness[3] = { 255, 255, 255 }; // brightness setting for the LED's

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
  pinMode(LEDselectButton, INPUT);

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
  // set all the pwm pins for the Nixie tubes and LED's to be outputs
  SoftPWMBegin();
  SoftPWMSetFadeTime(ALL, 25, 25);
  for(int i=0; i<6; i++) {
    SoftPWMSet(Nixie_PWM[i], 0);
    if (i < 3) {
      SoftPWMSet(LED_PWM[i], 0);
    }
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

void LEDselectButtonDebounce() {
  // read the state of the switch into a local variable:
  int reading = digitalRead(LEDselectButton);
  
  // check to see if you just pressed the button
  // (i.e. the input went from LOW to HIGH),  and you've waited
  // long enough since the last press to ignore any noise:
  
  // If the switch changed, due to noise or pressing:
  if (reading != lastbuttonState_LEDselect) {
    // reset the debouncing timer
    lastDebounceTime_LEDselect = millis();
  }
  
  if ((millis() - lastDebounceTime_LEDselect) > debounceDelay) {
    // whatever the reading is at, it's been there for longer
    // than the debounce delay, so take it as the actual current state:
    if (buttonState_LEDselect == HIGH && ((millis() - ButtonHighTime) > ButtonPressTime)) {
      // if held for more than ButtonPressTime
      Serial.println("You've pressed it for longer than 2 seconds");
      if (RGB_ProgramState == 1) {
        // RED MODE
        Serial.println("Saving the brighness for the RED LED");
        RGB_Brightness[0] = val;
      }else if (RGB_ProgramState == 2) {
        // GREEN MODE
        Serial.println("Saving the brighness for the GREEN LED");
        RGB_Brightness[1] = val;
        
      }else if (RGB_ProgramState == 3) {
        // BLUE MODE
        Serial.println("Saving the brighness for the BLUE LED");
        RGB_Brightness[2] = val;
        
      }
    }
    // if the button state has changed:
    if (reading != buttonState_LEDselect) {
      buttonState_LEDselect = reading;
      
      if (buttonState_LEDselect == HIGH) {
        // add code here for handling multiple LED brighnesses
        Serial.println("You pressed the program button!");
        Serial.println(RGB_ProgramState);
        ButtonHighTime = millis();
      }
    } else if (buttonState_LEDselect == LOW){
      // logic for if the button was released
      if (ButtonHighTime != 0) {
        Serial.println("The button was released!");
      }
      // if not held for more than the ButtonPressTime, do this code, otherwise reset counter for next time it's pressed
      if ((millis() - ButtonHighTime) < ButtonPressTime) {
        Serial.println("The button was held for less than 2 seconds");
        Serial.println(RGB_ProgramState);
        RGB_ProgramState = RGB_ProgramState + 1;
        Serial.println(RGB_ProgramState);
        if (RGB_ProgramState == 1) {
          Serial.println("Setting to RED brightness program.");
//          RGB_ON[0] = 1;
//          RGB_ON[1] = 0;
//          RGB_ON[2] = 0;
        } else if (RGB_ProgramState == 2) {
          Serial.println("Setting to BLUE brightness program.");
//          RGB_ON[0] = 0;
//          RGB_ON[1] = 1;
//          RGB_ON[2] = 0;
        } else if (RGB_ProgramState == 3) {
          Serial.println("Setting to GREEN brightness program.");
//          RGB_ON[0] = 0;
//          RGB_ON[1] = 0;
//          RGB_ON[2] = 1;
        } else if (RGB_ProgramState > 3) {
          Serial.println("Setting to Default Mode.");
          RGB_ProgramState = 0;
//          RGB_ON[0] = 1;
//          RGB_ON[1] = 1;
//          RGB_ON[2] = 1;
        }
      }
      
      ButtonHighTime = 0;
      
    }
  }
  lastbuttonState_LEDselect = reading; // save the reading.  Next time through the loop, it'll be the lastbuttonState_min:
}

void displayTime(DateTime now) {
//  Serial.println("Displaying the time!");
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
  brightness_nixie[clockPos] = brightness_nixie[clockPos] + fadeAmount[clockPos];

  // if 0 brightness, increment the time then reverse the fadeAmount so it will get bright again
  if (brightness_nixie[clockPos] == 0) {
    fadeAmount[clockPos] = -fadeAmount[clockPos];
    displayTime(setTime);
  }
  // if full brightness, stop fading logic and keep high
  if (brightness_nixie[clockPos] == 255) {
    fadeAmount[clockPos] = -fadeAmount[clockPos];
    fade[clockPos] = false;
  }
}

// Main Loop
void loop() {

  DateTime now = RTC.now();

  if (prevTime.hour() != now.hour()) {
//    Serial.println("The hour value has changed!");
    // check to see if the tens digit has also changed
    if ( ((prevTime.hour() / 10) % 10) != ((now.hour() / 10) % 10)) {
//      Serial.println("The tens value has changed too!");
      fade[hourTens] = true;
    }
    // update the previous time
    prevTime = now;
    fade[hourOnes] = true;
  }
  if (prevTime.minute() != now.minute()) {
//    Serial.println("The minute value has changed!");
    // check to see if the tens digit has also changed
    if ( ((prevTime.minute() / 10) % 10) != ((now.minute() / 10) % 10)) {
//      Serial.println("The tens value has changed too!");
      fade[minTens] = true;
    }
    // update the previous time
    prevTime = now;
    fade[minOnes] = true;
  }
  if (prevTime.second() != now.second()) {
//    Serial.println("The second value has changed!");
    // check to see if the tens digit has also changed
    if ( ((prevTime.second() / 10) % 10) != ((now.second() / 10) % 10)) {
//      Serial.println("The tens value has changed too!");
      fade[secTens] = true;
    }
    // update the previous time
    prevTime = now;
    fade[secOnes] = true;
  }
  
  // don't allow button presses within the first 3 seconds of powering on
  // this prevents noise "button" presses that occur rapidly at startup
  if (millis() > 3000) {
    hoursButtonDebounce(now);
    minutesButtonDebounce(now);
    LEDselectButtonDebounce();
  }
  
  
  val = analogRead(analogPin);
//  Serial.println(val);
  val = val/4;
//  Serial.println(val);
  if (val > 255) {
    val = 255;
  }else if (val <= 20 && val > 10) {
    val = 10;
  }else if (val < 10){
    val = 0;
  }

  // loop through the hours/min/sec digits for the clock and set the brightness for each tube
  for(int i=0; i<6; i++) {
    if (fade[i]) setBrightness(now, i);
    // set the brightness of the Nixie Tube
    SoftPWMSet(Nixie_PWM[i], brightness_nixie[i]);
  }
  
  // Process the Red LED display
  if (RGB_ON[0]) {
    if (RGB_ProgramState == 1)
      SoftPWMSet(LED_PWM[0], val);
    else
      SoftPWMSet(LED_PWM[0], RGB_Brightness[0]);
  } else {
    SoftPWMSet(LED_PWM[0], 0);
  }
  // Process the Green LED display
  if (RGB_ON[1]) {
    if (RGB_ProgramState == 2)
      SoftPWMSet(LED_PWM[1], val);
    else
      SoftPWMSet(LED_PWM[1], RGB_Brightness[1]);
  } else {
    SoftPWMSet(LED_PWM[1], 0);
  }
  // Process the Blue LED display
  if (RGB_ON[2]) {
    if (RGB_ProgramState == 3)
      SoftPWMSet(LED_PWM[2], val);
    else
      SoftPWMSet(LED_PWM[2], RGB_Brightness[2]);
  } else {
    SoftPWMSet(LED_PWM[2], 0);
  }
  
//  Serial.println(brightness[secOnes]);

  delay(25);
}
