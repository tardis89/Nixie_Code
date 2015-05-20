/*
Nixie Clock - RTC with Minutes with fade out/in on the ones digit

This code displays the minutes from the RTC. When a change is detected in the ones digit of the minutes, 
the PWM fades out the digit, increments it, then fades back in. A button can be pressed to increment the RTC time by one minute each p
The circuit:
* LED attached from pin 13 to ground
* pushbutton attached from pin 2 to +5V
* 10K resistor attached from pin 2 to ground
* BCD output pins on pins 8-11

created 16 May 2015
by Caleb Davison

*/
#include <Wire.h>
#include "Adafruit_MCP23017.h"
#include "RTClib.h"
#include "RTC_DS3231.h"
#include "SPI.h"
#include <FlexiTimer2.h> // Manual PWM library (http://www.arduino.cc/playground/Main/FlexiTimer2)


#define SQW_FREQ DS3231_SQW_FREQ_1024     //0b00001000   1024Hz

// LED PWM Pins
#define LED_minOnes 13
#define LED2 12

RTC_DS3231 RTC;
Adafruit_MCP23017 mcp;

// Period of the PWM wave (and therefore the number of levels)
#define PERIOD 256

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



// constants won't change. They're used here to
// set pin numbers:
const int minutesButton = 2;    // the number of the pushbutton pin
const int ledPin = 11;      // the number of the LED pin


// Variables will change:
int ledState = HIGH;         // the current state of the output pin
int buttonState;             // the current reading from the input pin
int lastButtonState = LOW;   // the previous reading from the input pin
int ones = 0;
int tens = 0;
int onesOrValue = 0;

// the following variables are long's because the time, measured in miliseconds,
// will quickly become a bigger number than can be stored in an int.
long lastDebounceTime = 0;  // the last time the output pin was toggled
long debounceDelay = 50;    // the debounce time; increase if the output flickers

// PWM Variables
byte brightness = 255;    // how bright the LED is
byte fadeAmount = -5;    // how many points to fade the LED by

bool fadeMinOnes = false;
bool fadeMinTens = false;

DateTime prevTime;

void setup() {
  Serial.begin(9600);

  Serial.println("Intializing the I2C");
  mcp.begin();      // use default address 0

  // initialize the IO of Port A to outputs
  Wire.beginTransmission(0x20);
  Wire.write(0x00); // IODIRA register
  Wire.write(0x00); // set all of port A to outputs
  Wire.endTransmission();

  pinMode(minutesButton, INPUT);
  pinMode(ledPin, OUTPUT);

  //--------RTC SETUP ------------
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
    //Serial.println("RTC is older than compile time!  Updating");
    RTC.adjust(DateTime(__DATE__, __TIME__));
  }

  RTC.enable32kHz(true);
  RTC.SQWEnable(true);
  RTC.BBSQWEnable(true); // enable the backup battery for when loss of power
  RTC.SQWFrequency( SQW_FREQ );

  Serial.println("Setting the LED to HIGH");
  digitalWrite(ledPin, ledState);

  // Initialize the PWM's
  AnyPWM::init();       // initialize the PWM timer
  pinMode(LED_minOnes, OUTPUT); // declare LED pin to be an output
  pinMode(LED2, OUTPUT); // declare LED pin to be an output

  Serial.println("Finished initialization");

}

void minutesButtonDebounce(DateTime now) {
  // read the state of the switch into a local variable:
  int reading = digitalRead(minutesButton);

  // check to see if you just pressed the button
  // (i.e. the input went from LOW to HIGH),  and you've waited
  // long enough since the last press to ignore any noise:

  // If the switch changed, due to noise or pressing:
  if (reading != lastButtonState) {
    // reset the debouncing timer
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    // whatever the reading is at, it's been there for longer
    // than the debounce delay, so take it as the actual current state:

    // if the button state has changed:
    if (reading != buttonState) {
      buttonState = reading;

      // only toggle the LED if the new button state is HIGH
      // only increase clock by one minute if the new button state is HIGH
      if (buttonState == HIGH) {
        ledState = !ledState;
        int addMin = now.minute() + 1;
        if (addMin == 60) {
          addMin = 0;
        }
        RTC.adjust( DateTime ( now.year(), now.month(), now.day(), now.hour(), (addMin), now.second() ) );
      }
    }
  }
  // set the LED:
  digitalWrite(ledPin, ledState);
  //  serial.print("Changing LED to"); serial.print(ledState);

  // save the reading.  Next time through the loop, it'll be the lastButtonState:
  lastButtonState = reading;
}

void displayTime(DateTime now) {
  ones = (now.minute()) % 10;
  tens = ((now.minute()) % 100 - ones) / 10;
  onesOrValue = (tens << 4) | ones;
  mcp.writeGPIOAB(onesOrValue);
}

void loop() {
  
  DateTime now = RTC.now();

  Serial.println(now.minute());
  if(prevTime.minute() != now.minute()){
     Serial.println("The hour has changed!"); 
     prevTime = now;
     fadeMinOnes = true;
  }
  
  minutesButtonDebounce(now);

  // the logic for fading in/out the ones digit on the minutes of the clock
  if (fadeMinOnes) {
    // change the brightness for next time through the loop:
    brightness = brightness + fadeAmount;

    //if (brightness < 0) brightness = 0;
    //if (brightness > 255) brightness = 255;
    // if 0 brightness, increment the time then reverse the fadeAmount so it will get bright again
    if (brightness == 0) {
      fadeAmount = -fadeAmount;
      displayTime(now);
    }
    // if full brightness, stop fading logic and keep high
    if (brightness == 255) {
      fadeAmount = -fadeAmount;
      fadeMinOnes = false;
    }
  } // end fadeMinOnes if statement

  // set the brightness of the LED:
  AnyPWM::analogWrite(LED_minOnes, brightness);

  delay(10);
}

