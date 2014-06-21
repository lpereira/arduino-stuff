#include <avr/wdt.h>
#include <avr/pgmspace.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>

// Should be a power of 2!
#define N_TASKS 4

static const int dancerIdEepromAddress = 511;

static const int bluetoothRxPin = 3;
static const int bluetoothTxPin = 4;
static const int ledPin = 13;
static int elWire1Pin = 5;
static int elWire2Pin = 6;

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
: 
    conn(rx, tx) {
    }

  void begin() { 
    conn.begin(9600); 
  }

  void set_name(const char* name) { 
    send_command("NAME", name); 
  }
  void set_pin(const char* pin) { 
    send_command("PIN", pin); 
  }
  void set_baud_rate(const BaudRate r) {
    char cmd[] = {
      (char) (r + '0'), '\0'    };
    send_command("BAUD", cmd);
  }

  bool available() { 
    return conn.available(); 
  }
  int read() { 
    return conn.read(); 
  }
  void write(const char c) { 
    conn.write(c); 
  }
  void write(const char* s) { 
    conn.write(s); 
  }

private:
  void send_command(const char *command, const char *parameter) {
    conn.write("AT+");
    conn.write(command);
    conn.write(parameter);
    while (!conn.available()) delay(5);
    delay(10);
    while (conn.available()) Serial.print((char)conn.read());
  }

  SoftwareSerial conn;
};

class Bg {
private:
  static Bg *tasks_[N_TASKS];
  static char curr_task_;

public:
  Bg(long interval)
: 
    interval_(interval)
      , next_run_(millis()) {
      }

  virtual bool run() = 0;

  static void addTask(Bg *task) {
    for (char i = 0; i < N_TASKS; i++) {
      if (!Bg::tasks_[i]) {
        Bg::tasks_[i] = task;
        Serial.print('T');
        Serial.println((int)i);
        return;
      }
    }

    Serial.print('T');
    Serial.println('?');
    delete task;
  }

  static void schedule() {
    Bg::tasks_[Bg::curr_task_] = runTask(Bg::tasks_[Bg::curr_task_], millis());
    Bg::curr_task_ = (Bg::curr_task_ + 1) & (N_TASKS - 1);
  }

  static void begin() {
    Bg::curr_task_ = 0;
    for (int task = 0; task < N_TASKS; task++)
      Bg::tasks_[task] = NULL;
  }
private:
  static Bg *runTask(Bg *task, const long m) {
    if (task) {
      if (m >= task->next_run_) {
        if (task->run()) {
          task->next_run_ = task->interval_ + m;
        } 
        else {
          delete task;
          task = NULL;
        }
      }
    }
    return task;
  }

  long interval_;
  long next_run_;
};

Bg *Bg::tasks_[N_TASKS];
char Bg::curr_task_;

static const unsigned char pwmFadeTable[] PROGMEM = {
  2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
  4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6,
  6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7,
  7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 10, 20,
  30, 40, 50, 80, 90, 100, 120, 150, 200, 255
};

class FadeBg : 
public Bg {
public:
  FadeBg(char pin, long duration, bool in)
    : Bg(duration ? duration : 1)
    , pin_(pin)
    , percentage_(in ? 0 : 100)
    , in_(in) {}

  virtual bool run();
private:
  char percentage_;
  char pin_;
  bool in_;
};

bool FadeBg::run() {
  char old_percentage = percentage_;
  if (in_) {
    if (percentage_ == 100)
      return false;
    percentage_++;
  } else {
    if (!percentage_)
      return false;
    percentage_--;
  }
  unsigned char val = pgm_read_byte(&pwmFadeTable[old_percentage]);
  if (pin_ < 0) {
    analogWrite(elWire1Pin, val);
    analogWrite(elWire2Pin, val);
  } 
  else {
    analogWrite(pin_, val);
  }

  Serial.print((int)percentage_);
  Serial.print('%');
  Serial.print('=');
  Serial.println((int)val);

  return true;
}

class StrobeBg : 
public Bg {
public:
  StrobeBg(char pin, long interval, char count)
: 
    Bg(interval)
      , pin_(pin)
        , count_(count * 2) {
        }

  virtual bool run();
private:
  char count_;
  char pin_;
};

bool StrobeBg::run() {
  if (pin_ < 0) {
    digitalWrite(elWire1Pin, count_ & 1);
    digitalWrite(elWire2Pin, count_ & 1);
  } 
  else {
    digitalWrite(pin_, count_ & 1);
  }
  return --count_;
}

enum State {
  IDLE,
  WAITING_ANALOG_ID,
  WAITING_ANALOG_VALUE,
  WAITING_DIGITAL_ID,
  WAITING_DIGITAL_VALUE,
  WAITING_STROBE_ID,
  WAITING_STROBE_VALUE1,
  WAITING_STROBE_VALUE2,
  WAITING_FADE_IN_ID,
  WAITING_FADE_IN_VALUE,
  WAITING_FADE_OUT_ID,
  WAITING_FADE_OUT_VALUE,
  WAITING_DANCER_NUMBER
};

static Bluetooth bluetooth(bluetoothRxPin, bluetoothTxPin);
static State state = IDLE;
static int id, value;

void setup()
{
  Serial.begin(9600);
  bluetooth.begin();

  char dancer_number = EEPROM.read(dancerIdEepromAddress) % 10;
  Serial.print("Dancer number: ");
  Serial.println((int)dancer_number);

  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  if (dancer_number == 2) {
    Serial.println("Workaround for dancer 2 fucked up ports");
    pinMode(5, INPUT);
    pinMode(6, INPUT);
    elWire1Pin = 9;
    elWire2Pin = 10;
  }
  pinMode(elWire1Pin, OUTPUT);
  pinMode(elWire2Pin, OUTPUT);

  Serial.println("EL Wire/Bluetooth controller");
  Serial.println("Ver 4");

  Bg::begin();

  char name[strlen("Dancarino X") + 1];
  memcpy(name, "Dancarino X", sizeof( "Dancarino X"));
  name[10] = dancer_number + '0';
  bluetooth.set_name(name);

  Serial.print("Device name: ");
  Serial.println(name);

  bluetooth.set_pin("0000");

  Serial.println("Bluetooth module ready");
}

void loop()
{
  if (!bluetooth.available()) {
    Bg::schedule();
    return;
  }

  int chr = bluetooth.read();

  switch (state) {
  case IDLE:
    //      blinkStatusLed();

    if (chr == 'A') {
      state = WAITING_ANALOG_ID;
    } 
    else if (chr == 'D') {
      state = WAITING_DIGITAL_ID;
    } 
    else if (chr == 'T') {
      testMode();
    } 
    else if (chr == 'R') {
      reset();
    } 
    else if (chr == 'L') {
      blinkStatusLed();
    } 
    else if (chr == 'S') {
      state = WAITING_STROBE_ID;
    } 
    else if (chr == 'O') {
      state = WAITING_FADE_OUT_ID;
    } 
    else if (chr == 'I') {
      state = WAITING_FADE_IN_ID;
    } 
    else if (chr == 'N') {
      state = WAITING_DANCER_NUMBER;
    }
    return;
  case WAITING_DANCER_NUMBER:
    EEPROM.write(dancerIdEepromAddress, chr % 10);
    reset();
    break;
  case WAITING_FADE_OUT_ID:
    id = chr;
    state = isValidId() ? WAITING_FADE_OUT_VALUE : IDLE;
    break;
  case WAITING_FADE_OUT_VALUE:
    Bg::addTask(new FadeBg(idToPin(id), chr, false));
    state = IDLE;
    break;
  case WAITING_FADE_IN_ID:
    id = chr;
    state = isValidId() ? WAITING_FADE_IN_VALUE : IDLE;
    break;
  case WAITING_FADE_IN_VALUE:
    Bg::addTask(new FadeBg(idToPin(id), chr, true));
    state = IDLE;
    break;
  case WAITING_STROBE_ID:
    id = chr;
    state = isValidId() ? WAITING_STROBE_VALUE1 : IDLE;
    break;
  case WAITING_STROBE_VALUE1:
    value = chr;
    state = WAITING_STROBE_VALUE2;
    break;
  case WAITING_STROBE_VALUE2:
    Bg::addTask(new StrobeBg(idToPin(id), value, chr));
    state = IDLE;
    break;
  case WAITING_ANALOG_ID:
    id = chr;
    state = isValidId() ? WAITING_ANALOG_VALUE : IDLE;
    return;
  case WAITING_ANALOG_VALUE:
    if (id == 2) {
      analogWrite(idToPin(0), constrain(chr, 0, 255));
      analogWrite(idToPin(1), constrain(chr, 0, 255));
    } 
    else {
      analogWrite(idToPin(id), constrain(chr, 0, 255));
    }
    state = IDLE;
    break;
  case WAITING_DIGITAL_ID:
    id = chr;
    state = isValidId() ? WAITING_DIGITAL_VALUE : IDLE;
    return;
  case WAITING_DIGITAL_VALUE:
    if (id == 2) {
      digitalWrite(idToPin(0), !!chr);
      digitalWrite(idToPin(1), !!chr);
    } 
    else {
      digitalWrite(idToPin(id), !!chr);
    }
    state = IDLE;
    break;
  }
}

boolean isValidId() {
  return id == 0 || id == 1 || id == 2;
}

int idToPin(int id) {
  if (id == 0)
    return elWire1Pin;
  if (id == 1)
    return elWire2Pin;
  return -1;
}

void testModeBlinkInternal(int el1Mode, int el2Mode) {
  digitalWrite(elWire1Pin, el1Mode);
  digitalWrite(elWire2Pin, el2Mode);
  delay(200);
}

void testMode() {
#if 0
  fadeIn(elWire1Pin, 1000);
  fadeOut(elWire1Pin, 1000);

  fadeIn(elWire2Pin, 1000);
  fadeOut(elWire2Pin, 1000);
#endif

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

void reset() {
  wdt_enable(WDTO_15MS);
  while (1);
}

void blinkStatusLed() {
  for (int i = 0; i < 10; i++) {
    digitalWrite(ledPin, i & 1);
    delay(100);
  }
}


