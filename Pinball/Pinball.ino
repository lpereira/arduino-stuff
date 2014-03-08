/*
 * Pinball Display Driver
 * Arudino version by Leandro Pereira
 *
 * Based on code by Daniel Quadros, available at
 * http://dqsoft.blogspot.com.br/2011/06/projeto-epoch-parte4-software-cont.html
 */

/*
 * Com o ethernet shield, alguns dos pinos usados aqui nao funcionam pq o shield
 * os usa. Mas temos os pinos analogicos que podem ser usados como saidas digitais
 * se fizer pinMode(An, OUTPUT) / digitalWrite(An, LOW|HIGH).
 *
 * Pinos que podemos usar: d0, d1, d4, d5, d6, d7, d8, d9, a0, a1, a2, a3, a4, a5
 */

#include <SPI.h>         
#include <Ethernet.h>
#include <EthernetUdp.h>

#define DISP_A0 7
#define DISP_A1 6
#define DISP_A2 5
#define DISP_A3 A1

#define DISP_B0 1
#define DISP_B1 A2
#define DISP_B2 4 
#define DISP_B3 A0

#define DISP_S0 A5
#define DISP_S1 A4
#define DISP_S2 A3

byte mac[] = {  0xBA, 0xAA, 0xAA, 0xAD, 0x1D, 0xEA };
unsigned int localPort = 8888;
IPAddress timeServer(200, 192, 232, 8);
IPAddress freeNode(93, 152, 160, 101);
byte packetBuffer[48];
int epochincrements = 0;
EthernetUDP Udp;
EthernetClient irc;
char valor[16];
unsigned char disp = 0;
unsigned long epoch = -1;
unsigned long lastMillis;

void cls()
{
  for (char i = 0; i < 16; i++)
    valor[15 - i] = 15; /* 15 = off */
}

ISR(TIMER1_COMPA_vect) {
    char digito = valor[15 - disp] << 4 | valor[disp];
    digitalWrite(DISP_S0, disp & 1);
    digitalWrite(DISP_S1, disp & 2);
    digitalWrite(DISP_S2, disp & 4);
    digitalWrite(DISP_B0, digito & (1<<0));
    digitalWrite(DISP_B1, digito & (1<<1));
    digitalWrite(DISP_B2, digito & (1<<2));
    digitalWrite(DISP_B3, digito & (1<<3));
    digitalWrite(DISP_A0, digito & (1<<4));
    digitalWrite(DISP_A1, digito & (1<<5));
    digitalWrite(DISP_A2, digito & (1<<6));
    digitalWrite(DISP_A3, digito & (1<<7));
    disp = (disp + 1) & 7;
}

void setupInterrupts()
{
  cli();
  
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;
  OCR1A = 32;
  TCCR1B |= 1<<WGM12;
  TCCR1B |= (1<<CS12)|(1<<CS10);
  TIMSK1 |= 1<<OCIE1A;
  
  sei();
}

void setup()
{
  lastMillis = millis();

  pinMode(DISP_A0, OUTPUT);
  pinMode(DISP_A1, OUTPUT);
  pinMode(DISP_A2, OUTPUT);
  pinMode(DISP_A3, OUTPUT);

  pinMode(DISP_B0, OUTPUT);
  pinMode(DISP_B1, OUTPUT);
  pinMode(DISP_B2, OUTPUT);
  pinMode(DISP_B3, OUTPUT);

  pinMode(DISP_S0, OUTPUT);
  pinMode(DISP_S1, OUTPUT);
  pinMode(DISP_S2, OUTPUT);

  cls();
  valor[15] = 6;  // b
  valor[14] = 0;  // 0
  valor[13] = 0;  // 0
  valor[12] = 14; // t
  valor[8]  = 0;

  setupInterrupts();
  delay(100);

  while (!Ethernet.begin(mac)) {
    valor[10] = (valor[10] + 1) % 10;
    delay(200);
  }

  valor[10] = 15;
  valor[8] = 1;
  delay(100);

  Udp.begin(localPort);
  synchronizeClock(10, true);

/*
valor[8]++;
  while (!irc.connect(freeNode, 6667)) {
    valor[10] = (valor[10] + 1) % 10;
    delay(200);
  }

  valor[8]++;
  irc.write("NICK lhc_aberto\n");
  valor[8]++;
  irc.write("USER lhc_aberto 0 * :Cliente de IRC no Arduino!\n");
  valor[8]++;
  irc.write("JOIN ##lhc\n");
  valor[8]++;
  // Consome MOTD
  while (irc.available())
    irc.read();
*/
  cls();
  updateClock();
}

void reboot() {
  asm volatile("jmp 0");
}

void synchronizeClock(int statusdisplay, bool duringBoot) {
  int tries = 0;

  sendNTPpacket(timeServer);
  while (!parseNtpPacket()) {
    valor[statusdisplay] = (valor[statusdisplay] + 1) % 10;
    delay(200);
    tries++;
    if (tries > 32) {
      if (duringBoot) reboot();
      break;
    }
  }

  valor[statusdisplay] = 15;
}

bool parseNtpPacket() {
  if (!Udp.parsePacket())
    return false;

  Udp.read(packetBuffer,48);

  unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
  unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);  
  unsigned long secsSince1900 = highWord << 16 | lowWord;  
  const unsigned long seventyYears = 2208988800UL;     
  epoch = secsSince1900 - seventyYears;  
  
  return true;
}

void updateClock()
{
  char hour = ((epoch % 86400L) / 3600);
  if (hour < 9) {
    valor[14] = 0;
    valor[13] = hour;
  } else {
    valor[14] = hour / 10;
    valor[13] = hour % 10;
  }

  char minute = ((epoch % 3600) / 60);
  if (minute < 9) {
    valor[12] = 0;
    valor[11] = minute;
  } else {
    valor[12] = minute / 10;
    valor[11] = minute % 10;
  }
  
  char second = epoch % 60;
  if (second < 9) {
    valor[10] = 0;
    valor[9] = second;
  } else {
    valor[10] = second / 10;
    valor[9] = second % 10;
  }

  valor[15] = valor[8] = 15;
}

void updateUptime()
{
    const unsigned long lhcFounded = 1318451337;
    unsigned long uptime = epoch - lhcFounded;

    for (int i = 0; i < 8; i++) {
      valor[i] = uptime % 10;
      uptime /= 10;
    }
}

char ircbuffer[32];
char ircbuffer_ptr=0;

void processIRCMessage()
{
  if (ircbuffer[0] == 'P' && ircbuffer[1] == 'I' && ircbuffer[2] == 'N' && ircbuffer[3] == 'G') {
    ircbuffer[1] = 'O';
    irc.write(ircbuffer);
  }
}

void loop()
{
/*
if (irc.available()) {
    char ch = irc.read();
    if (ircbuffer_ptr == sizeof(ircbuffer))
      goto p;
    ircbuffer[ircbuffer_ptr++] = ch;
    if (ch == '\r' || ch == '\n') {
p:
      ircbuffer[ircbuffer_ptr]='\0';
      processIRCMessage();
      ircbuffer_ptr=0;
    }
  }
*/  
  if (millis() - lastMillis <= 1000)
     return;

  epoch++;
  epochincrements++;
  lastMillis = millis();

  updateClock();
  updateUptime();

  if (epochincrements > 3600) {
    synchronizeClock(8, false);
    epochincrements = 0;
  }
}

// send an NTP request to the time server at the given address 
unsigned long sendNTPpacket(IPAddress& address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, 48); 
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49; 
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp: 		   
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, 48);
  Udp.endPacket(); 
}
