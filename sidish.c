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
//   If we use an alternate method for our DAC, such as the Adafruit
//     Wave Shield, we may not need to use this timer.
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

#include <avr/pgmspace.h>
#include <avr/interrupt.h>

#include "sidish.h"

#include "songdata.h"
#include "tables.h"

// Ugly to include this like this, but it works for now.
// TODO: Find a better way to compile the same .c file into 2 different object files soon.
#include "goatplayer.c"

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

void print8hex(uint8_t value)
{
    put("0123456789ABCDEF"[value >> 4]);
    put("0123456789ABCDEF"[value & 0x0F]);
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

#if USE_WAVESHIELD

// For Adafruit WaveShield
// pin 2 is DAC chip select (PD2)
// pin 3 is DAC serial clock (PD3)
// pin 4 is DAC serial data in (PD4)
// pin 5 is LDAC if used (PD5)
#define DAC_CS   (PD2)
#define DAC_CLK  (PD3)
#define DAC_DATA (PD4)
#define DAC_LDAC (PD5)

#endif

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

    InitializeSong(song_start);
    
    sei();
}

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
  // TODO: Optimize this
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

void OutputByte(uint8_t value)
{
#if USE_WAVESHIELD
    DacSend(value);
#else
    digitalWrite(4, HIGH);
    OCR2B = value;
    digitalWrite(4, LOW);
#endif
}

// This is the audio output interrupt that gets called
// at the audio output bitrate
ISR(TIMER0_COMPA_vect) 
{
    OutputAudioAndCalculateNextByte();
}

int main (void)
{
    setup();

#if TEST_MODE
    uint8_t key = 40;

    KeyOn(0, key, 4);

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
                    KeyOn(0, key, 4);
                }
            }
            else if (command == 31 || command == 'j')
            {
                print("DOWN: ");
                if (key > 0)
                {
                    key--;
                    KeyOn(0, key, 4);
                }
            }
            else if (command == '\n')
            {
                print("Key OFF\n");
                KeyOff(0);
                continue;
            }
            else if (command == ' ')
            {
                print("RESET INSTRUMENTS\n");
                EnableFakeInstruments();
                continue;
            }
            else
            {
                print("KEYB: ");
                print8int(command);
                print(" ");
            }
                    
            print("Key: ");
            print8int(key);
            print("\n");
        }
    }
#else
    
    while(1);
    
#endif
}
