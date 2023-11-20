// Pin-definitions
#define LATCH_PIN 33
#define CLOCK_PIN 25
#define DATA_PIN 32


/**
 * Shift Register output for Nixie tubes
 * Bit  tube
 * 0-3     1
 * 4-7     2
 * 8-11    3
 * 12-15   4
 * 16-19   5
 * 20-23   6
 */
// next shift register state
unsigned long nixieOutput[24] = { 1, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1, 0 };
// last shift register state
unsigned long lastNixieOutput[24] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
uint32_t lastChangeTimestamp = 0;

/**
 * return true if there was a change at nixieOutput variable
 */
bool checkForDisplayUpdate() {
  for (int i=23; i>0; i--) {  
    if (nixieOutput[i] != lastNixieOutput[i]) {
      return true;
    }
  }
  return false;
}

uint32_t getLastChangeTimestamp() {
  return lastChangeTimestamp;
}
/**
 * set all pins for the shift register
 */
void setupShiftRegister() {
  pinMode(LATCH_PIN, OUTPUT);  // store
  pinMode(CLOCK_PIN, OUTPUT);  // shift
  pinMode(DATA_PIN, OUTPUT);   // data
}

void displayTime() {
  DateTime dateTime = rtc.now();
  writeNumbers(dateTime.hour(), dateTime.minute(), dateTime.second());
}

void displayDate() {
  DateTime dateTime = rtc.now();
  writeNumbers(dateTime.day(), dateTime.month(), dateTime.year() % 100);
}

/**
 * Update 24bit shitregister
 *
 */
void refreshShiftRegister() {
  // only update shift register, if there was a change
  if (checkForDisplayUpdate()) {
    Serial.println("Value changed, now update Tubes");
    digitalWrite(LATCH_PIN, LOW);  // lock register value

    for (int i = 23; i >= 0; i--) {  // Start with last bit
      digitalWrite(CLOCK_PIN, LOW);            // start current write periode
      digitalWrite(DATA_PIN, nixieOutput[i]);  // write bit i to first register position
      digitalWrite(CLOCK_PIN, HIGH);           // shift bit to next position

      Serial.print(nixieOutput[i]);
      if (i % 4 == 0) {
        Serial.print(" ");
      }
      if (i % 8 == 0 && i > 1 && i < 24) {
        Serial.print("- ");
      }
    }
    Serial.println();

    digitalWrite(LATCH_PIN, HIGH);  // released register value
      
    lastChangeTimestamp = rtc.now().secondstime();

    // copy output values to tmp var
    for (int i=0; i<24; i++) {
      lastNixieOutput[i] = nixieOutput[i];
    }
  }
}


void writeNumbers(int number0, int number1, int number2, int number3, int number4, int number5) {
  // digit 1 - number2
  writeDigit(6, number5);
  // digit 2 - number2
  writeDigit(5, number4);
  // digit 3 - number1
  writeDigit(4, number3);
  // digit 4 - number1
  writeDigit(3, number2);
  // digit 5 - number0
  writeDigit(2, number1);
  // digit 6 - number0
  writeDigit(1, number0);
}

/**
 * number0 -> digit 5 & digit 6
 * number1 -> digit 3 & digit 4
 * number2 -> digit 1 & digit 2
 */
void writeNumbers(int number0, int number1, int number2) {
  // digit 1 - number2
  writeDigit(6, number2 % 10);
  // digit 2 - number2
  writeDigit(5, number2 / 10);
  // digit 3 - number1
  writeDigit(4, number1 % 10);
  // digit 4 - number1
  writeDigit(3, number1 / 10);
  // digit 5 - number0
  writeDigit(2, number0 % 10);
  // digit 6 - number0
  writeDigit(1, number0 / 10);
}

/**
 * write random numbers to all digits
 */
void displayRandomNumbers() {
  writeDigit(1, random(0, 9));
  writeDigit(2, random(0, 9));
  writeDigit(3, random(0, 9));
  writeDigit(4, random(0, 9));
  writeDigit(5, random(0, 9));
  writeDigit(6, random(0, 9));
}

/**
 * set 4 bit to write a number
 * Converts decimal number to 4-Bit Binary and store it in nixieOutput variable
 */
void writeDigit(int digit, int number) {
  // find position
  int startBit = digit * 4 - 4;
  int endBit = digit * 4;
  // dec to bin
  for (int i = startBit; i < endBit; i++) {
    nixieOutput[i] = number % 2;
    number = number / 2;
  }
}

void randomAnimation(int amount, int _delay) {
  for (int i = 0; i < amount; i++) {
    displayRandomNumbers();
    refreshShiftRegister();
    delay(_delay);
  }
}
