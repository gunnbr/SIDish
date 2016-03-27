// A synthesizer and player for the Arduino that is similar
// (but not identical and not intended to be identical) to
// the famous Commodore 64 SID chip
// by Brian Gunn (gunnbr@schminktronics.com) 


// We need 2 timers:
// 1) Used to generate the PWM audio out (Pin 3, using Timer 2A)
//    Needs to run at (some frequency) above the audio frequency
//      so we don't hear the PWM frequency. I can't find a
//      reference for picking this value right now, but this article may help:
//           http://www.ti.com/lit/an/spraa88a/spraa88a.pdf
//      For now, I'll use 250 kHz (16 MHz processor clock with a
//      /64 prescaler). That seems like a reasonably high enough value.
//      Using Fast PWM, we just need to set OCR2A to a value between
//      0 and 255 to set the duty cycle. For details, see:
//           https://www.arduino.cc/en/Tutorial/SecretsOfArduinoPWM
//
//      Even better, this article and the accompanying chart shows that
//      using 62.5 kHz with a single output Fast PWM gives 8 bits of depth,
//      which is exactly what I want, so I'll start with it.
//           http://wiki.openmusiclabs.com/wiki/PWMDAC
//
// 2) Runs at the sample output rate
//    I'm starting with 16,000 Hz which I'm guessing is a 
//      good balance between audio fidelity and cycles available
//      for the synthesizer routines. This allows us to be
//      well above the Nyquist frequency for the highest
//      piano key. If we don't have enough cycles (or need more
//      for something else), dropping to 8,000 Hz still allows
//      nearly all piano frequencies but doubles the cycles available
//      for other things.
//    Then again, it looks like I can run both of these off of timer 2
//      if I set up Timer 2b with a prescale of /1024, which gives me
//      a bitrate of 16 MHz / 1024 = 15,625 Hz, very close to  my desired
//      bitrate. This leaves both other timers open for other applications,
//      but does mean that if dropping the bitrate to ~8 kHz requires us to switch
//      to another timer. Maybe--I need to research this more. This is only the prescaler,
//      so we can set something else to reduce this rate further. The question is,
//      can I set that something else to give me 15,625?
//
// For hooking up the hardware, see this article:
//   https://developer.mbed.org/users/4180_1/notebook/using-a-speaker-for-audio-output/

#define USE_WAVESHIELD (1)

#include <avr/pgmspace.h>
#include <avr/interrupt.h>

// SID Registers
const short US_PER_TICK = 62; // 1,000,000 uS / 16,000 Hz

// 16 bits
// Technically the ticks per cycle, not frequency
// Key 49 (A4): 440 Hz = 2272 (actually 436.3 Hz)
// Key 47 (G4): 392 Hz = 2551 (actually 388 Hz)
// Key 44 (E4): 329 Hz = 3032 (actually 327 Hz)
// Key 40 (C4): 261 Hz = 3822 (actually 259.325 Hz)

short VOICE1_FREQUENCY = 3822;
short VOICE2_FREQUENCY = 3032;
short VOICE3_FREQUENCY = 2551;

uint16_t VOICE1_STEPS = 16;
uint16_t VOICE2_STEPS = 21;
uint16_t VOICE3_STEPS = 25;

// 12 bits in SID, only 8 bits here
short VOICE1_DUTY = 0x80;
short VOICE2_DUTY = 0x80;
short VOICE3_DUTY = 0x80;

// 8 bits
unsigned char VOICE1_CONTROL = 0;
unsigned char VOICE2_CONTROL = 0;
unsigned char VOICE3_CONTROL = 0;

// 8 BITS
unsigned char VOICE1_ATTACKDECAY = 0;

// 8 BITS
unsigned char VOICE1_SUSTAINRELEASE = 0;

// Synthesizer parameters
unsigned long voice1count = 0;
unsigned long voice2count = 0;
unsigned long voice3count = 0;
uint8_t voice1errorsteps = 0;
uint8_t voice1errorpercent = 0;
uint8_t voice1key = 0;

// For Adafruit WaveShield
// pin 2 is DAC chip select (PD2)
// pin 3 is DAC serial clock (PD3)
// pin 4 is DAC serial data in (PD4)
// pin 5 is LDAC if used (PD5)
#define DAC_CS   (PD2)
#define DAC_CLK  (PD3)
#define DAC_DATA (PD4)
#define DAC_LDAC (PD5)

void print(char *message)
{
    while (*message != 0)
    {
        while(!(UCSR0A & (1<<UDRE0)));

        UDR0 = *message++;
    }
}

void put(char message)
{
    while(!(UCSR0A & (1<<UDRE0)));
    UDR0 = message;
}

void print8int(int8_t value)
{
    char printzero = 0;
    
    if(value < 0)
    {
        print("-");
        value = -value;
    }
    if (value >= 100)
    {
        print("1");
        printzero = 1;
        value -= 100;
    }
    uint8_t tens = value / 10;
    if (tens != 0 || printzero)
    {
        put("0123456789"[tens]);
    }
    uint8_t ones = value - (tens * 10);
    put("0123456789"[ones]);
}

void printint(int value)
{
    char printzero = 0;
    
    if(value < 0)
    {
        put('-');
        value = -value;
    }
    
    if (value >= 1000)
    {
        put('1');
        value -= 1000;
        printzero = 1;
    }
    uint8_t hundreds = value / 100;
    if (hundreds != 0 || printzero)
    {
        printzero = 1;
        put("0123456789"[hundreds]);
    }
    
    value -= (hundreds * 100);
    uint8_t tens = value / 10;
    if (tens != 0 || printzero)
    {
        printzero = 1;
        put("0123456789"[tens]);
    }
    
    value -= (tens * 10);
    uint8_t ones = value;
    put("0123456789"[ones]);
}

void setup()
{
    cli();
    
    // Page 65 - list of interrupts
    // Page 93 - 8-bit Timer/Counter0 with PWM
    // Page 111 - 16-bit Timer/Counter1 with PWM
    // Page 138 - information about timer prescaler settings
    // Page 141 - 8-bit Timer/Counter2 with PWM and Asynchronous Operation
    
#if USE_WAVESHIELD
    // Set all output pins
    DDRD = (1<<DDD5)|(1<<DDD4)|(1<<DDD3)|(1<<DDD2);

    // Set the chip select high
    // Set LDAC to low to use unbuffered mode
    PORTD = (1<<DAC_CS);

#else
    DDRD = (1<<DDD4)|(1<<DDD3);

    // The fast Pulse Width Modulation or fast PWM mode (WGM22:0 = 3 or 7) 
    // The counter counts from BOTTOM to TOP then restarts from BOTTOM. 
    // TOP is defined as 0xFF when WGM2:0 = 3.
    // In non-inverting Compare Output mode, the Output Compare (OC2x)
    //  is cleared on the compare match between TCNT2 and OCR2x, and set at BOTTOM.
    //  Setting the COM2x1:0 bits to two will produce a non-inverted PWM
    // Set prescaler to 1 with CS20
    
    // Output B frequency: 16 MHz / Prescaler / 256
    // Prescaler = 1: 16 MHz / 1 / 256 = 62.5 kHz
    // Output B duty cycle: OCR2A / 255
    TCCR2A = _BV(COM2B1) | _BV(WGM21) | _BV(WGM20);
    TCCR2B = _BV(CS20);
    OCR2B = 0x80;
#endif

    // In Clear Timer on Compare or CTC mode (WGM22:0 = 2), the counter is 
    // cleared to zero when the counter value (TCNT2) matches the OCR2A
    // An interrupt can be generated each time the counter value reaches 
    // the TOP value by using the OCF2A Flag
    
    // Output B interrupt frequency: 16 MHz / Prescale / OCR2B  = 7,812.5 Hz
    // Prescale = 8, OCR2B = 125: 16 MHz / 8 / 125 = 16,000 Hz bitrate
    
    // ACK! Timer 2 won't work by itself since both halves (A and B) run off
    // the same prescaler, but I need to use different prescalers.
    
    // Timer1:
    // Output A Frequency: 16 MHz / Prescaler / 256
    // Prescaler = 1: 16 MHz / 1 / 256 = 62.5 kHz
    // Output A duty cycle: OCR1A / 65536 (annoying because this is 16 bit)
    
    // Timer0:
    // In Clear Timer on Compare or CTC mode (WGM02:0 = 2), the counter is 
    // cleared to zero when the counter value (TCNT0) matches the OCR0A
    // An interrupt can be generated each time the counter value reaches 
    // the TOP value by using the OCF0A Flag
    // Set prescaler to 8 with CS01
    
    // Output A interrupt frequency: 16 MHz / Prescale / (OCR0A + 1)
    // Prescale = 8, OCR0A = 125: 16 MHz / 8 / 125 = 16,000 Hz bitrate
    TCCR0A = _BV(WGM01);
    TCCR0B = _BV(CS01);
    OCR0A = 124;
    TIMSK0 = _BV(OCIE0A); // Enable interrupt

    // Setup serial 9600 bps
    UBRR0H = 0;
    UBRR0L = 103;

    // Enable the transmitter and receiver
    UCSR0B = (1<<RXEN0)|(1<<TXEN0);

    // Serup serial for 8N1
    UCSR0C = 6; 
    
    sei();
}

uint8_t tone1on = 1;
uint8_t tone2on = 0;
uint8_t tone3on = 0;

short programcount = 267;

uint8_t gNextOutputValue = 0;

// Based on 16 kHz bitrate and 1024 table size
const uint16_t FREQUENCY_TABLE[] PROGMEM = { 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 6, 6, 7, 7, 7, 8, 8, 9, 9, 10, 11, 11, 12, 13, 14, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 28, 29, 31, 33, 35, 37, 39, 42, 44, 47, 50, 53, 56, 59, 63, 66, 70, 75, 79, 84, 89, 94, 100, 106, 112, 119, 126, 133, 141, 150, 159, 168, 178, 189, 200, 212, 225, 238, };

// Based on 16 kHz bitrate and 1024 table size
const uint8_t ERRORPERCENT_TABLE[] PROGMEM = { 66, 76, 86, 97, 9, 21, 34, 48, 63, 79, 95, 13, 32, 52, 72, 95, 18, 43, 69, 97, 27, 58, 91, 27, 64, 4, 45, 90, 37, 86, 39, 95, 54, 17, 83, 54, 28, 8, 91, 80, 74, 73, 79, 91, 9, 35, 67, 8, 57, 16, 83, 60, 48, 47, 58, 82, 19, 70, 35, 17, 15, 32, 66, 21, 97, 95, 17, 64, 38, 40, 71, 35, 31, 64, 33, 43, 95, 91, 35, 29, 76, 80, 43, 70, 63, 28, 67, };

// 1024 values ranged -32 to 31
const int8_t sineTable[] PROGMEM = { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 12, 12, 12, 12, 12, 12, 13, 13, 13, 13, 13, 14, 14, 14, 14, 14, 14, 15, 15, 15, 15, 15, 15, 16, 16, 16, 16, 16, 16, 17, 17, 17, 17, 17, 17, 18, 18, 18, 18, 18, 18, 19, 19, 19, 19, 19, 19, 19, 20, 20, 20, 20, 20, 20, 21, 21, 21, 21, 21, 21, 21, 22, 22, 22, 22, 22, 22, 22, 23, 23, 23, 23, 23, 23, 23, 23, 24, 24, 24, 24, 24, 24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 25, 26, 26, 26, 26, 26, 26, 26, 26, 26, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 32, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 26, 26, 26, 26, 26, 26, 26, 26, 26, 25, 25, 25, 25, 25, 25, 25, 25, 24, 24, 24, 24, 24, 24, 24, 24, 23, 23, 23, 23, 23, 23, 23, 23, 22, 22, 22, 22, 22, 22, 22, 21, 21, 21, 21, 21, 21, 21, 20, 20, 20, 20, 20, 20, 19, 19, 19, 19, 19, 19, 19, 18, 18, 18, 18, 18, 18, 17, 17, 17, 17, 17, 17, 16, 16, 16, 16, 16, 16, 15, 15, 15, 15, 15, 15, 14, 14, 14, 14, 14, 14, 13, 13, 13, 13, 13, 12, 12, 12, 12, 12, 12, 11, 11, 11, 11, 11, 10, 10, 10, 10, 10, 10, 9, 9, 9, 9, 9, 8, 8, 8, 8, 8, 7, 7, 7, 7, 7, 7, 6, 6, 6, 6, 6, 5, 5, 5, 5, 5, 4, 4, 4, 4, 4, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, -1, -1, -1, -2, -2, -2, -2, -2, -3, -3, -3, -3, -3, -4, -4, -4, -4, -4, -5, -5, -5, -5, -5, -6, -6, -6, -6, -6, -7, -7, -7, -7, -7, -7, -8, -8, -8, -8, -8, -9, -9, -9, -9, -9, -10, -10, -10, -10, -10, -10, -11, -11, -11, -11, -11, -12, -12, -12, -12, -12, -12, -13, -13, -13, -13, -13, -14, -14, -14, -14, -14, -14, -15, -15, -15, -15, -15, -15, -16, -16, -16, -16, -16, -16, -17, -17, -17, -17, -17, -17, -18, -18, -18, -18, -18, -18, -19, -19, -19, -19, -19, -19, -19, -20, -20, -20, -20, -20, -20, -21, -21, -21, -21, -21, -21, -21, -22, -22, -22, -22, -22, -22, -22, -23, -23, -23, -23, -23, -23, -23, -23, -24, -24, -24, -24, -24, -24, -24, -24, -25, -25, -25, -25, -25, -25, -25, -25, -26, -26, -26, -26, -26, -26, -26, -26, -26, -27, -27, -27, -27, -27, -27, -27, -27, -27, -27, -28, -28, -28, -28, -28, -28, -28, -28, -28, -28, -28, -29, -29, -29, -29, -29, -29, -29, -29, -29, -29, -29, -29, -29, -29, -30, -30, -30, -30, -30, -30, -30, -30, -30, -30, -30, -30, -30, -30, -30, -30, -30, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -32, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -31, -30, -30, -30, -30, -30, -30, -30, -30, -30, -30, -30, -30, -30, -30, -30, -30, -30, -29, -29, -29, -29, -29, -29, -29, -29, -29, -29, -29, -29, -29, -29, -28, -28, -28, -28, -28, -28, -28, -28, -28, -28, -28, -27, -27, -27, -27, -27, -27, -27, -27, -27, -27, -26, -26, -26, -26, -26, -26, -26, -26, -26, -25, -25, -25, -25, -25, -25, -25, -25, -24, -24, -24, -24, -24, -24, -24, -24, -23, -23, -23, -23, -23, -23, -23, -23, -22, -22, -22, -22, -22, -22, -22, -21, -21, -21, -21, -21, -21, -21, -20, -20, -20, -20, -20, -20, -19, -19, -19, -19, -19, -19, -19, -18, -18, -18, -18, -18, -18, -17, -17, -17, -17, -17, -17, -16, -16, -16, -16, -16, -16, -15, -15, -15, -15, -15, -15, -14, -14, -14, -14, -14, -14, -13, -13, -13, -13, -13, -12, -12, -12, -12, -12, -12, -11, -11, -11, -11, -11, -10, -10, -10, -10, -10, -10, -9, -9, -9, -9, -9, -8, -8, -8, -8, -8, -7, -7, -7, -7, -7, -7, -6, -6, -6, -6, -6, -5, -5, -5, -5, -5, -4, -4, -4, -4, -4, -3, -3, -3, -3, -3, -2, -2, -2, -2, -2, -1, -1, -1, -1, -1, 0, 0, 0, 0, 0, };


#if USE_WAVESHIELD

//#define DAC_SCK_PULSE() { digitalWrite(DAC_CLK, HIGH); digitalWrite(DAC_CLK, LOW);}
#define DAC_SCK_PULSE() { PORTD |= (1 << 3); PORTD &= ~(1 << 3);}

// Send bit b of d
//#define DAC_SENDBIT(d, b) {digitalWrite(DAC_DATA, d&_BV(b)); DAC_SCK_PULSE();}
#define DAC_SENDBIT(d, b) {if (d&_BV(b)) PORTD |= (1 << 4); else PORTD &= ~(1 << 4);DAC_SCK_PULSE();}

//------------------------------------------------------------------------------
// send 12 bits to dac
// TODO: Test compiler optimization and how long this takes
// Original comment: trusted compiler to optimize and it does 
//                   csLow to csHigh takes 8 - 9 usec on a 16 MHz Arduino
inline void DacSend(uint8_t data) 
{
  // Enable the DAC to receive data
  //digitalWrite(DAC_CS, LOW);
  PORTD &= ~(1 << 2);

  // Send DAC config bits
  //digitalWrite(DAC_DATA, LOW);
  PORTD &= ~(1 << 4);
  
  DAC_SCK_PULSE();  // DAC A
  DAC_SCK_PULSE();  // unbuffered
  
  //digitalWrite(DAC_DATA, HIGH);
  PORTD |= (1 << 4);
  DAC_SCK_PULSE();  // 1X gain
  DAC_SCK_PULSE();  // no SHDN
  
  // Send the 8 bits of data as the most
  // significant bits to the DAC
  DAC_SENDBIT(data,  7);
  DAC_SENDBIT(data,  6);
  DAC_SENDBIT(data,  5);
  DAC_SENDBIT(data,  4);
  DAC_SENDBIT(data,  3);
  DAC_SENDBIT(data,  2);
  DAC_SENDBIT(data,  1);
  DAC_SENDBIT(data,  0);

  // Then send 4 bits of 0 as the LSBs
  //digitalWrite(DAC_DATA, LOW);
  PORTD &= ~(1 << 4);

  DAC_SCK_PULSE();
  DAC_SCK_PULSE();
  DAC_SCK_PULSE();
  DAC_SCK_PULSE();  
  
  //digitalWrite(DAC_CS, HIGH);
  PORTD |= (1 << 2);
}
#endif

uint32_t totalTicks = 0;
uint8_t voice1erroron = 1;

// This is the audio output interrupt that gets called
// at the audio output bitrate
ISR(TIMER0_COMPA_vect) 
{
#if USE_WAVESHIELD
  DacSend(gNextOutputValue);
#else
  digitalWrite(4, HIGH);
  OCR2B = gNextOutputValue;
  digitalWrite(4, LOW);
#endif

  int outputValue = 0;

  // VOICE 1 - sine wave
  if (tone1on)
  {
    voice1count += VOICE1_STEPS;
    voice1errorpercent += voice1errorsteps;
    if (voice1errorpercent >= 100)
    {
        if (voice1erroron)
        {
            voice1count++;
        }
        voice1errorpercent -= 100;
    }
    if (voice1count >= sizeof(sineTable))
    {
      voice1count -= sizeof(sineTable);
    }
    outputValue =  (int8_t)pgm_read_byte(&sineTable[voice1count]);
  }
  
  // VOICE 2 - sine wave
  if (tone2on)
  {
    voice2count += VOICE2_STEPS;
    if (voice2count >= sizeof(sineTable))
    {
      voice2count -= sizeof(sineTable);
    }
    outputValue +=  (int8_t)pgm_read_byte(&sineTable[voice2count]);
  }
  
  // VOICE 3 - sine wave
  if (tone3on)
  {
    voice3count += VOICE3_STEPS;
    if (voice3count >= sizeof(sineTable))
    {
      voice3count -= sizeof(sineTable);
    }
    outputValue +=  (int8_t)pgm_read_byte(&sineTable[voice3count]);
  }
  
  // Prepare output
  outputValue += 128;
  if (outputValue < 0)
  {
    outputValue = 0;
  }
  else if (outputValue > 255)
  {
    outputValue = 255;
  }

#if 0
  totalTicks++;
  switch(totalTicks)
  {
    case 1:
      tone1on = 1;
      break;
    case 15500:
      tone1on = 0;
      break;
    case 16500:
      tone2on = 1;
      break;
    case 31500:
      tone2on = 0;
      break;
    case 32500:
      tone3on = 1;
      break;
    case 47500:
      tone3on = 0;
      break;
    case 48500:
      tone1on = 1;
      tone2on = 1;
      tone3on = 1;
      break;
    case 63500:
      tone1on = 0;
      tone2on = 0;
      tone3on = 0;
      break;
    case 72500:
      totalTicks = 0;
      break;
  }
#endif
  gNextOutputValue = (uint8_t)outputValue;
  
#if 0
  programcount--;
  if (programcount == 0)
  {
    programcount = 267;
    programStep();
  }
#endif
}

uint8_t dutyUp = 0;

short beatcount = 60;
uint8_t state = 0;

void programStep()
{
  beatcount--;
  if (beatcount == 0)
  {
    beatcount = 60;
    state++;
    switch (state)
    {
      case 0:
        VOICE1_CONTROL = 1;
        VOICE2_CONTROL = 0;
        VOICE3_CONTROL = 0;
        break;
      case 1:
        VOICE1_CONTROL = 0;
        VOICE2_CONTROL = 1;
        VOICE3_CONTROL = 0;
        break;
      case 2:
        VOICE1_CONTROL = 0;
        VOICE2_CONTROL = 0;
        VOICE3_CONTROL = 1;
        break;
      case 3:
        VOICE1_CONTROL = 1;
        VOICE2_CONTROL = 1;
        VOICE3_CONTROL = 1;
        break;
      case 5:
        state = 0;
        break;
    }
  }

#if 0
  if (dutyUp)
  {
    VOICE1_DUTY += 5;
    if (VOICE1_DUTY > 128)
    {
      dutyUp = 0;
    }
    else
    {
      VOICE1_DUTY -= 5;
      if (VOICE1_DUTY < 5)
      {
        dutyUp = 1;
      }
    }
  }
#endif
}

void SetKey(uint8_t key)
{
    VOICE1_STEPS = pgm_read_word(&FREQUENCY_TABLE[key]);
    voice1errorsteps = pgm_read_byte(&ERRORPERCENT_TABLE[key]);
    voice1errorpercent = 0;
    voice1count = 0;
    voice1key = key;
}

int main (void)
{
    setup();

    uint8_t key = 40;
    
    SetKey(key);

#if 0
    // prints title with ending line break
    print("Dumping sine table\n");

    int i;
    for (i = 0 ; i < sizeof(sineTable) ; i++)
    {
        print("Count: ");
        printint(i);
        
        print(" Value: ");
        print8int( pgm_read_byte(&sineTable[i]));
        print("\n");
    }
#endif

#if 0
    unsigned long v1count = 0;
    uint8_t v1errorpercent = 0;
    uint8_t nextOutputValue = 0;
    int16_t outputValue = 0;
    int x;

    for (x = 0 ; x < 300 ; x++)
    {
        printint(x);
        print(": ");
        
        v1count += VOICE1_STEPS;
        v1errorpercent += voice1errorsteps;
        if (v1errorpercent >= 100)
        {
            voice1count++;
            v1errorpercent -= 100;
        }
        print(" Error%: ");
        printint(v1errorpercent);
        
        if (v1count >= sizeof(sineTable))
        {
            v1count -= sizeof(sineTable);
        }
        print(" Count: ");
        printint(v1count);
        outputValue =  (int8_t)pgm_read_byte(&sineTable[v1count]);
        print(" Output: ");
        printint(outputValue);

        // Prepare output
        outputValue += 128;
        if (outputValue < 0)
        {
            outputValue = 0;
        }
        else if (outputValue > 255)
        {
            outputValue = 255;
        }
        print(" +128: ");
        printint(outputValue);

        nextOutputValue = (uint8_t)outputValue;
        print(" Next: ");
        printint(nextOutputValue);
        print("\n");
    }
#endif
    
    while (1)
    {
        if (UCSR0A & (1<<RXC0))
        {
            char command = UDR0;

            if (command == 30 || command == 'k')
            {
                print("  UP: ");
                if (key < 83)
                {
                    key++;
                    SetKey(key);
                }
            }
            else if (command == 31 || command == 'j')
            {
                print("DOWN: ");
                if (key > 0)
                {
                    key--;
                    SetKey(key);
                }
            }
            else if (command == 32)
            {
                print(" ERR: ");
                voice1erroron = !voice1erroron;
            }
            else
            {
                print("KEYB: ");
                print8int(command);
                print(" ");
            }
                    
            print("Key: ");
            print8int(key);
            print(" Err: ");

            if (voice1erroron)
            {
                print("ON");
            }
            else
            {
                print("OFF");
            }
            
            print("\n");
        }
    }
}
