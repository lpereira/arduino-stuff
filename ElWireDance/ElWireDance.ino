#include <avr/wdt.h>
#include <SoftwareSerial.h>

// HC-06 Bluetooth class
// @lafp, 18-01-2014
class Bluetooth {
 public:
  enum BaudRate {
    BR_1200 = 1,
    BR_2400,
    BR_4800,
    BR_9600,
    BR_19200,
    BR_38400,
    BR_57600
  };
 
  Bluetooth(const int rx, const int tx)
    : conn(rx, tx) {}

  void begin() { conn.begin(9600); }

  void set_name(const char* name) { send_command("NAME", name); }
  void set_pin(const char* pin) { send_command("PIN", pin); }
  void set_baud_rate(const BaudRate r) {
    char cmd[] = {(char) (r + '0'), '\0'};
    send_command("BAUD", cmd);
  }

  bool available() { return conn.available(); }
  int read() { return conn.read(); }
  void write(const char c) { conn.write(c); }
  void write(const char* s) { conn.write(s); }

 private:
  void send_command(const char *command, const char *parameter) {
    conn.write("AT+");
    conn.write(command);
    conn.write(parameter);
    while (!conn.available());
    while (conn.available()) conn.read();
  }

  SoftwareSerial conn;
};

enum State {
  IDLE,
  WAITING_ANALOG_ID,
  WAITING_ANALOG_VALUE,
  WAITING_DIGITAL_ID,
  WAITING_DIGITAL_VALUE
};

static const int bluetoothRxPin = 3;
static const int bluetoothTxPin = 4;
static const int ledPin = 13;
static const int elWire1Pin = 5;
static const int elWire2Pin = 6;

Bluetooth bluetooth(bluetoothRxPin, bluetoothTxPin);
State state = IDLE;
boolean ledPinState = false;
int id, value;

void setup()
{
  Serial.begin(9600);
  bluetooth.begin();

  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  pinMode(elWire1Pin, OUTPUT);
  pinMode(elWire2Pin, OUTPUT);
}

void loop()
{
  if (!bluetooth.available())
    return;

  int chr = bluetooth.read();
  switch (state) {
    case IDLE:
      blinkStatusLed();
      if (chr == 'A') {
        state = WAITING_ANALOG_ID;
      } else if (chr == 'D') {
        state = WAITING_DIGITAL_ID;
      } else if (chr == 'T') {
        testMode();
      } else if (chr == 'R') {
        reset();
      }
      return;
    case WAITING_ANALOG_ID:
      id = chr;
      state = isValidId() ? WAITING_ANALOG_VALUE : IDLE;
      return;
    case WAITING_ANALOG_VALUE:
      analogWrite(idToPin(id), constrain(chr, 0, 255));
      state = IDLE;
      break;
    case WAITING_DIGITAL_ID:
      id = chr;
      state = isValidId() ? WAITING_DIGITAL_VALUE : IDLE;
      return;
    case WAITING_DIGITAL_VALUE:
      digitalWrite(idToPin(id), !!chr);
      state = IDLE;
      break;
  }
}

boolean isValidId() {
  return id == 0 || id == 1;
}

int idToPin(int id) {
  return (id == 0) ? elWire1Pin : elWire2Pin;
}

void testModeBlinkInternal(int el1Mode, int el2Mode) {
   digitalWrite(elWire1Pin, el1Mode);
   digitalWrite(elWire2Pin, el2Mode);
   delay(200);
}

void testMode() {
  fadeInOut(elWire1Pin);
  fadeInOut(elWire2Pin);

  for (int j = 0; j < 4; j++) {
    for (int i = 0; i < 5; i++) {
      testModeBlinkInternal(HIGH, LOW);
      testModeBlinkInternal(LOW, HIGH);
    }
    for (int i = 0; i < 5; i++) {
      testModeBlinkInternal(HIGH, HIGH);
      testModeBlinkInternal(LOW, LOW);
    }
  }
}

void fadeInOutInternal(int pin, int value, int &increment, int &timeToDelay) {
    analogWrite(pin, value);
    
    if (value % 5 == 0) {
      increment++;
      timeToDelay--;
    }
    delay(timeToDelay);
}

void fadeInOut(int pin) {
  int increment = 1;
  int timeToDelay = 50;

  for (int i = 0; i < 255; i += increment)
    fadeInOutInternal(pin, i, increment, timeToDelay);

  increment = 1;
  timeToDelay = 50;
  for (int i = 255; i >= 0; i -= increment)
    fadeInOutInternal(pin, i, increment, timeToDelay);
}

void reset() {
  wdt_enable(WDTO_15MS);
  while (1);
}

void blinkStatusLed() {
  digitalWrite(ledPin, ledPinState);
  ledPinState ^= 1;
}

