// Functions both to handle both the synthesizer and playback
// routines.
// Both combined in this file to allow optimization for embedded
// hardware by using global variables

#ifdef WIN32
#include <stdint.h>
#include "sidish.h"

uint32_t pgm_read_dword(const char *);
uint16_t pgm_read_word(const uint32_t *);
uint8_t pgm_read_byte(const char *);
#endif

#include "sid.h"

uint8_t gNextOutputValue = 0;

uint32_t totalTicks = 0;

#define VBI_COUNT (BITRATE / 50)
#define DEFAULT_TEMPO (5)

uint16_t vbiCount = VBI_COUNT;

// Pointer to the start of each orderlist.
// TODO: Deal with the subtunes in a better way
#define MAX_SUBTUNES (20)
const char *orderlist[MAX_SUBTUNES][3];

// Pointer to the start of each pattern.
// TODO: Optimize memory usage by reducing this value.
// This alone takes a lot of the available RAM on an ATmega328
const char *pattern[256];

// Instrument definition
struct Instrument
{
    uint8_t attackDecay;     // +0      byte    Attack/Decay
    uint8_t sustainRelease;  // +1      byte    Sustain/Release
    uint8_t waveOffset;      // +2      byte    Wavepointer
    uint8_t pulseOffset;     // +3      byte    Pulsepointer
    uint8_t filterOffset;    // +4      byte    Filterpointer
    uint8_t speedOffset;     // +5      byte    Vibrato param. (speedtable pointer)
    uint8_t vibratoDelay;    // +6      byte    Vibrato delay
    uint8_t gateoffTime;     // +7      byte    Gateoff timer
    uint8_t hardRestart;     // +8      byte    Hard restart/1st frame waveform
    char    name[16];        // +9      16      Instrument name
} const *gInstruments;

#if TEST_MODE
const struct Instrument FakeInstruments[] PROGMEM =
{
    {0x00, 0xF0, 0, 0, 0, 0, 0, 0, 0, "TestInstrument"},
    {0x56, 0x78, 0, 0, 0, 0, 0, 0, 0, "TI2"},
};
    
void EnableFakeInstruments()
{
    gInstruments = FakeInstruments;
}
#endif

uint8_t *gWavetable, *gPulsetable, *gFiltertable, *gSpeedtable;
uint8_t gWavetableSize, gPulsetableSize, gFiltertableSize, gSpeedtableSize;

struct Track
{
    // <0 means no instrument assigned yet
    int8_t instrumentNumber;

    uint8_t wavetablePosition;
    uint8_t pulsetablePosition;
    uint8_t speedtablePosition;
    uint8_t waveformDelayCountdown;
    uint8_t currentNote;
    uint8_t originalNote;
    
    // Pointer to the current position in the song data
    const char *songPosition;
    
    // The number of times remaining to repeat the current pattern before
    // moving to the next one
    uint8_t patternRepeatCountdown;

    // The number of times remaining to delay on the current wavetable
    // position before continuing
    uint8_t wavetableDelay;
    
    // The number of times remaining to repeat the current pulse instruction
    // before moving to the next one
    uint8_t pulseRepeatCountdown;
    
    // The amount to change the pulseWidth every song tick 
    int8_t pulseChange;
    
    // The position in the orderlist
    uint8_t orderlistPosition;

    // Number of semitones to offset each note
    int8_t semitoneOffset;
    
    // Tempo for this track
    uint8_t tempo;
    
    // Countdown until the next step in the pattern data
    uint8_t trackStepCountdown;
} gTrackData[3];
        
// Start of the song data -- only needed for testing 
// by printing the offsets
const char *gSongData;

// TODO: Convert this to a single 16 byte value, then
//       move it to PROGMEM
unsigned char freqtbllo[] = {
  0x17,0x27,0x39,0x4b,0x5f,0x74,0x8a,0xa1,0xba,0xd4,0xf0,0x0e,
  0x2d,0x4e,0x71,0x96,0xbe,0xe8,0x14,0x43,0x74,0xa9,0xe1,0x1c,
  0x5a,0x9c,0xe2,0x2d,0x7c,0xcf,0x28,0x85,0xe8,0x52,0xc1,0x37,
  0xb4,0x39,0xc5,0x5a,0xf7,0x9e,0x4f,0x0a,0xd1,0xa3,0x82,0x6e,
  0x68,0x71,0x8a,0xb3,0xee,0x3c,0x9e,0x15,0xa2,0x46,0x04,0xdc,
  0xd0,0xe2,0x14,0x67,0xdd,0x79,0x3c,0x29,0x44,0x8d,0x08,0xb8,
  0xa1,0xc5,0x28,0xcd,0xba,0xf1,0x78,0x53,0x87,0x1a,0x10,0x71,
  0x42,0x89,0x4f,0x9b,0x74,0xe2,0xf0,0xa6,0x0e,0x33,0x20,0xff,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };

unsigned char freqtblhi[] = {
  0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x02,
  0x02,0x02,0x02,0x02,0x02,0x02,0x03,0x03,0x03,0x03,0x03,0x04,
  0x04,0x04,0x04,0x05,0x05,0x05,0x06,0x06,0x06,0x07,0x07,0x08,
  0x08,0x09,0x09,0x0a,0x0a,0x0b,0x0c,0x0d,0x0d,0x0e,0x0f,0x10,
  0x11,0x12,0x13,0x14,0x15,0x17,0x18,0x1a,0x1b,0x1d,0x1f,0x20,
  0x22,0x24,0x27,0x29,0x2b,0x2e,0x31,0x34,0x37,0x3a,0x3e,0x41,
  0x45,0x49,0x4e,0x52,0x57,0x5c,0x62,0x68,0x6e,0x75,0x7c,0x83,
  0x8b,0x93,0x9c,0xa5,0xaf,0xb9,0xc4,0xd0,0xdd,0xea,0xf8,0xff,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };

int InitializeSong(const char *songdata)
{
    const char *data = songdata;
    gSongData = songdata;
    
    print("\n\n\n******** Initializing *******\n\n");
    
    uint32_t header = pgm_read_dword(data);
    if (header != 0x35535447)
    {
        print("Header is not from GoatTracker\n");
        return 0;
    }
    data += 4;
    
    print("Found GoatTracker header\n");

    int i;
    
    char name[32];
    for (i = 0 ; i < 32 ; i++)
    {
        name[i] = pgm_read_byte(data++);
    }
    print("Song Name: ");
    print(name);
    print("\n");
    
    for (i = 0 ; i < 32 ; i++)
    {
        name[i] = pgm_read_byte(data++);
    }
    print("   Author: ");
    print(name);
    print("\n");

    for (i = 0 ; i < 32 ; i++)
    {
        name[i] = pgm_read_byte(data++);
    }
    print("Copyright: ");
    print(name);
    print("\n");

    uint8_t numSubtunes = pgm_read_byte(data++);
    print("# subtunes: ");
    print8int(numSubtunes);
    if (numSubtunes > MAX_SUBTUNES)
    {
        print(" ****** ERROR: > MAX_SUBTUNES ******");
    }
    print("\n");

    for(int subtune = 0 ; subtune < numSubtunes ; subtune++)
    {
        // TODO: Handle multiple subtunes
        uint8_t size = pgm_read_byte(data++);
        orderlist[subtune][0] = data;
        data += size + 1;

        print("Subtune ");
        print8int(i);
        print(" Orderlist 1 Size ");
        print8int(size);
        print("\n");

        size = pgm_read_byte(data++);
        orderlist[subtune][1] = data;
        data += size + 1;

        print("Subtune ");
        print8int(i);
        print(" Orderlist 2 Size ");
        print8int(size);
        print("\n");

        size = pgm_read_byte(data++);
        orderlist[subtune][2] = data;
        data += size + 1;
        
        print("Subtune ");
        print8int(i);
        print(" Orderlist 3 Size ");
        print8int(size);
        print("\n");
    }

    uint8_t numInstruments = pgm_read_byte(data++);
    print("Number of instruments: ");
    print8int(numInstruments);
    print("\n");

    gInstruments = (struct Instrument *)data;

    for (i = 0 ; i < numInstruments ; i++)
    {
        data += 25;
        
        uint8_t ad = pgm_read_byte(&gInstruments[i].attackDecay);
        uint8_t sr = pgm_read_byte(&gInstruments[i].sustainRelease);
        uint8_t waveOffset = pgm_read_byte(&gInstruments[i].waveOffset);
        for (uint8_t x = 0 ; x < 16 ; x++)
        {
            name[x] = pgm_read_byte(&gInstruments[i].name[x]);
        }
        print("Instrument ");
        print8int(i);
        print(" (");
        print(name);
        print(") ");
        print(": ADSR 0x");
        print8hex(ad);
        print8hex(sr);
        print(" WaveOffset: ");
        print8int(waveOffset);
        print("\n");
    }
    
    gWavetableSize = pgm_read_byte(data++);
    print("Wavetable Size: ");
    print8int(gWavetableSize);
    print("\n");
    gWavetable = (uint8_t *)data;
    for (i = 0 ; i < gWavetableSize ; i++)
    {
        print("0x");
        print8hex(i);
        uint8_t value = pgm_read_byte(&gWavetable[i]);
        print(" ");
        print8hex(value);
        print(" ");
        value = pgm_read_byte(&gWavetable[i + gWavetableSize]);
        print8hex(value);
        print("\n");        
    }
    data += gWavetableSize * 2;

    gPulsetableSize = pgm_read_byte(data++);
    print("Pulsetable Size: ");
    print8int(gPulsetableSize);
    print("\n");
    gPulsetable = (uint8_t *)data;
    data += gPulsetableSize * 2;

    gFiltertableSize = pgm_read_byte(data++);
    print("Filtertable Size: ");
    print8int(gFiltertableSize);
    print("\n");
    gFiltertable = (uint8_t *)data;
    data += gFiltertableSize * 2;

    gSpeedtableSize = pgm_read_byte(data++);
    print("Speedtable Size: ");
    print8int(gSpeedtableSize);
    print("\n");
    gSpeedtable = (uint8_t *)data;
    data += gSpeedtableSize * 2;

    uint8_t numPatterns = pgm_read_byte(data++);
    print("Number Patterns: ");
    print8int(numPatterns);
    print("\n");

    for(i = 0 ; i < numPatterns ; i++)
    {
        uint8_t length = pgm_read_byte(data++);
        pattern[i] = data;
        
        print("Pattern ");
        print8int(i);
        print(": Rows ");
        print8int(length);
#if 0
        print(" Offset: ");
        printf("0x%X", data - gSongData);
#endif
        print("\n");

        data += 4*length;
    }

    // Initializa the song to get ready to play
    for (uint8_t channel = 0 ; channel < 3 ; channel++)
    {
        // Start each channel at the first pattern in the order list
        gTrackData[channel].orderlistPosition = 0;
        gTrackData[channel].instrumentNumber = -1;
        gTrackData[channel].wavetablePosition = 0xFF;
        gTrackData[channel].semitoneOffset = 0;
        gTrackData[channel].tempo = DEFAULT_TEMPO;
        gTrackData[channel].trackStepCountdown = DEFAULT_TEMPO;
        
        uint8_t patternNumber;
        do
        {
            // Get the pattern number from the current position
            patternNumber = pgm_read_byte(orderlist[0][channel] + gTrackData[channel].orderlistPosition);
            if (patternNumber >= 0xD0 && patternNumber <= 0xDF)
            {
                gTrackData[channel].orderlistPosition++;
                uint8_t repeatCount = patternNumber & 0x0F;
                if (repeatCount == 0)
                {
                    repeatCount = 16;
                }
                gTrackData[channel].patternRepeatCountdown = repeatCount;
            }
            else if (patternNumber >= 0xE0 && patternNumber <= 0xFE)
            {
                // Handle transpose codes
                gTrackData[channel].orderlistPosition++;
                
                // Convert 0xE0 (224) through 0xFE (254) to -15 through 15
                gTrackData[channel].semitoneOffset = patternNumber - 0xF0;

                print("Transpose channel ");
                print8int(channel);
                print(" ");
                print8int(gTrackData[channel].semitoneOffset);
                print("\n");
            }
        } while (patternNumber >= 0xD0);
        
        // Start each channel at the first song position for each pattern
        gTrackData[channel].songPosition = pattern[patternNumber];
        print("Channel ");
        print8int(channel);
        print(" Initial Pattern: ");
        print8int(patternNumber);
        print("\n");
    }

	return 1;
}

void KeyOn(uint8_t channel, uint8_t key, uint8_t instrument)
{
    instrument--;
    
#if 0
    print("KeyOn Channel: ");
    print8int(channel);
    print(" Instrument: ");
    print8int(instrument);
    print(" Note: ");
    print8int(key);
#endif

    uint8_t value = pgm_read_byte(&gInstruments[instrument].attackDecay);
    SetSidRegister(channel, AttackDecay, value);

    value = pgm_read_byte(&gInstruments[instrument].sustainRelease);
    SetSidRegister(channel, SustainRelease, value);

    gTrackData[channel].instrumentNumber = instrument;
    gTrackData[channel].originalNote = key;
    
    gTrackData[channel].wavetablePosition = pgm_read_byte(&gInstruments[instrument].waveOffset);
    gTrackData[channel].pulsetablePosition = pgm_read_byte(&gInstruments[instrument].pulseOffset);
    
    // Positions are stored as 1 based, but the table data itself is stored 0 based
    gTrackData[channel].wavetablePosition--;
    gTrackData[channel].pulsetablePosition--;
    
#if 0
    print(" AD: ");
    print8hex(channels[channel].attackDecay);
    print(" SR: ");
    print8hex(channels[channel].sustainRelease);
    print(" WavetablePos: ");
    print8int(gTrackData[channel].wavetablePosition);
    print(" PulsetablePos: ");
    print8int(gTrackData[channel].pulsetablePosition);
    print("\n");
#endif

    gTrackData[channel].speedtablePosition = pgm_read_byte(&gInstruments[instrument].speedOffset);

    // Reset the table positions (may not want to do this in the future depending on 
    // what the song specifies
    gTrackData[channel].pulseRepeatCountdown = 0;

    //printf("Instrument %u: AD: 0x%02X SR: 0x%02X ", instrument, channels[channel].attackDecay, channels[channel].sustainRelease);
    //printf("Key On: %u Attack Countdown: %u\n", key, channels[channel].phaseStepCountdown);
}

// Returns TRUE when the song is finished
int GoatPlayerTick()
{
    int songFinished = 0;
    
    // Handle the wavetable
    for(uint8_t channel = 0 ; channel < 3 ; channel++)
    {
        if (gTrackData[channel].wavetablePosition == 0xFF)
        {
            continue;
        }
        
        if (gTrackData[channel].instrumentNumber < 0)
        {
            continue;
        }
        
        if (gTrackData[channel].wavetableDelay > 0)
        {
            gTrackData[channel].wavetableDelay--;
            continue;
        }
        
#if 0
        print("Channel: ");
        print8int(channel);
        print(" Instrument: ");
        print8int(gTrackData[channel].instrumentNumber);
        print(" Wave Pos 0x");
        print8hex(gTrackData[channel].wavetablePosition);
#endif

        uint8_t leftSide = pgm_read_byte(&gWavetable[gTrackData[channel].wavetablePosition]);
        uint8_t rightSide = pgm_read_byte(&gWavetable[gTrackData[channel].wavetablePosition + gWavetableSize]);

#if 0
        print(" 0x");
        print8hex(leftSide);
        print(" ");
        print8hex(rightSide);
        print("\n");
#endif
        
        if (leftSide >= 0x01 && leftSide <= 0x0F)
        {
            // Handle delay
            gTrackData[channel].wavetableDelay = leftSide;
        }
        else if (leftSide == 0 || (leftSide >= 0x10 && leftSide <= 0xDF))
        {
            // Handle waveform value
            
            if (leftSide == 0)
            {
                // If the left side is 0, process the right side
                // according to the previous left side
                leftSide = GetSidRegister(channel, Control); 
            }
            else
            {
                SetSidRegister(channel, Control, leftSide);
            }
            
            // TODO: Find a way to combine waveforms.
            //       Or actually, do I really want to support this?
            if (leftSide & (CONTROL_SAWTOOTH | CONTROL_TRIANGLE))
            {
                if (rightSide <= 0x5F)
                {
                    // Relative notes
                    gTrackData[channel].currentNote = gTrackData[channel].originalNote + rightSide;
                }
                else if (rightSide <= 0x7F)
                {
                    // Negative relative notes
                    // TODO: Verify this algorithm is correct
                    gTrackData[channel].currentNote = gTrackData[channel].originalNote - (rightSide - 0x60);
                }
                else if (rightSide == 0x80)
                {
                    // Note unchanged
                    gTrackData[channel].currentNote = gTrackData[channel].originalNote;
                }
                else if (rightSide <= 0xDF)
                {
                    // Absolute notes
                    gTrackData[channel].currentNote = rightSide - 0x81;
                }

                uint16_t frequency = freqtbllo[gTrackData[channel].currentNote] | (freqtblhi[gTrackData[channel].currentNote] << 8);
                SetSidRegister(channel, Frequency, frequency);
            }
            
            if (leftSide & CONTROL_PULSE)
            {
                if (rightSide <= 0x5F)
                {
                    gTrackData[channel].currentNote = gTrackData[channel].originalNote + rightSide;
                }
                else if (rightSide <= 0x7F)
                {
                    // TODO: Verify this algorithm is correct
                    gTrackData[channel].currentNote = gTrackData[channel].originalNote - (rightSide - 0x60);
                }
                else if (rightSide == 0x80)
                {
                    gTrackData[channel].currentNote = gTrackData[channel].originalNote;
                }
                else if (rightSide <= 0xDF)
                {
                    gTrackData[channel].currentNote = rightSide - 0x81;
                }
                
                uint16_t frequency = freqtbllo[gTrackData[channel].currentNote] | (freqtblhi[gTrackData[channel].currentNote] << 8);
                SetSidRegister(channel, Frequency, frequency);
            }
        }
        else if (leftSide == 0xFF)
        {
            if (rightSide == 0)
            {
                //printf("Wavetable end\n");
                gTrackData[channel].wavetablePosition = 0xFF;
            }
            else
            {
                // rightSide is 1 based while the data is 0 based, so subtract 1
                gTrackData[channel].wavetablePosition = rightSide - 1;
                //print("Wavetable jump to 0x");
                //print8hex(rightSide);
                //print("\n");
            }
            
            continue;
        }
        
        gTrackData[channel].wavetablePosition++;
    }
    
    // Handle the pulsetable
    for(uint8_t channel = 0 ; channel < 3 ; channel++)
    {
        if (gTrackData[channel].pulsetablePosition == 0xFF)
        {
            continue;
        }
        
        if (gTrackData[channel].instrumentNumber < 0)
        {
            continue;
        }
        
        if (gTrackData[channel].pulseRepeatCountdown > 0)
        {
            uint8_t pulseWidth = GetSidRegister(channel, PulseWidth);
            pulseWidth += gTrackData[channel].pulseChange;
            SetSidRegister(channel, PulseWidth, pulseWidth);
            gTrackData[channel].pulseRepeatCountdown--;
            //printf("Channel %u changing pulse by %d to %u\n", channel, gTrackData[channel].pulseChange, channels[channel].pulseWidth);
            continue;
        }
        
#if 0
        print("Channel: ");
        print8int(channel);
        print(" Instrument: ");
        print8int(gTrackData[channel].instrumentNumber);
        print(" Pulse Pos 0x");
        print8hex(gTrackData[channel].pulsetablePosition);
#endif

        uint8_t leftSide = pgm_read_byte(&gPulsetable[gTrackData[channel].pulsetablePosition]);
        uint8_t rightSide = pgm_read_byte(&gPulsetable[gTrackData[channel].pulsetablePosition + gPulsetableSize]);

#if 0
        print(" 0x");
        print8hex(leftSide);
        print(" ");
        print8hex(rightSide);
        print("\n");
#endif
        
        if (leftSide == 0xFF)
        {
            if (rightSide == 0)
            {
                //print("Pulsetable end\n");
                gTrackData[channel].pulsetablePosition = 0xFF;
            }
            else
            {
                gTrackData[channel].pulsetablePosition = rightSide;
#if 0
                print("Pulsetable jump to 0x");
                print8hex(rightSide);
                print("\n");
#endif
            }
            
            continue;
            
        }
        else if (leftSide < 0x80)
        {
            // Set pulse change parameters
            //printf("Pulse change: 0x%02X %d\n", leftSide, (int8_t)rightSide);
            
            gTrackData[channel].pulseRepeatCountdown = leftSide;
            gTrackData[channel].pulseChange = (int8_t) rightSide;
        }
        else
        {
            // Directly set the pulse width
            SetSidRegister(channel, PulseWidth, ((leftSide & 0x0F) << 8) | rightSide);
#if 0
            print("Set pulsewidth to 0x");
            print8hex(leftSide & 0x0F);
            print8hex(rightSide);
            print("\n");
#endif
        }
        
        gTrackData[channel].pulsetablePosition++;
    }
    
    // Handle the pattern data
    for(uint8_t channel = 0; channel < 3 ; channel++)
    {
        uint8_t note;
        uint8_t command;
        uint16_t data;
        uint8_t instrument;

        gTrackData[channel].trackStepCountdown--;
        if (gTrackData[channel].trackStepCountdown > 0)
        {
            continue;
        }
        
        gTrackData[channel].trackStepCountdown = gTrackData[channel].tempo;

        do
        {
            note = pgm_read_byte(gTrackData[channel].songPosition);
            instrument = pgm_read_byte(gTrackData[channel].songPosition + 1);
            command = pgm_read_byte(gTrackData[channel].songPosition + 2);
            data = pgm_read_byte(gTrackData[channel].songPosition + 3);
#if 0
            printf("Channel %u (0x%X): %02X %02X %X %02X\n", channel,
                gTrackData[channel].songPosition - gSongData, note, instrument, command, data);
#endif
       
            switch (command)
            {
                case 0x0F: // Set tempo
                    if (data >= 0x80)
                    {
                        print("Set tempo, channel ");
                        print8int(channel);
                        print(": 0x");
                        print8hex(data - 0x80);
                        print("\n");

                        // Set the tempo for just this channel
                        gTrackData[channel].tempo = data - 0x80;
                    }
                    else
                    {
                        print("Set global tempo: ");
                        print8hex((uint8_t)data);
                        print("\n");
                        for (int i = 0 ; i < 3 ; i++)
                        {
                            gTrackData[i].tempo = (uint8_t)data;
                        }
                    }
                    break;
            }
            
            if (note >= 0x60 && note <= 0xBC)
            {
                // In testing, I have to subtract 0x68 to get the key I expect
                note -= 0x68;
                
                // Transpose for the current orderlist setting
                note += gTrackData[channel].semitoneOffset;
                
                KeyOn(channel, note, instrument);

                // printf("NOTE ON -- Channel %u Note: %u Instrument: %u\n", channel, note, instrument);
            }
            else if (note == 0xBE)
            {
                uint8_t value = GetSidRegister(channel, Control);
                SetSidRegister(channel, Control, value & ~CONTROL_GATE);
            }
            else if (note == 0xFF)
            {
                if (gTrackData[channel].patternRepeatCountdown > 0)
                {
                    gTrackData[channel].patternRepeatCountdown -= 1;
                    uint8_t patternNumber = pgm_read_byte(orderlist[0][channel] + gTrackData[channel].orderlistPosition);
                    gTrackData[channel].songPosition = pattern[patternNumber];
                    continue;
                }
                
                gTrackData[channel].orderlistPosition++;
                
                uint8_t patternNumber;
                do
                {
                    // Get the pattern number from the current position
                    patternNumber = pgm_read_byte(orderlist[0][channel] + gTrackData[channel].orderlistPosition);

                    if (patternNumber >= 0xD0 && patternNumber <= 0xDF)
                    {
                        gTrackData[channel].orderlistPosition++;
                        uint8_t repeatCount = patternNumber & 0x0F;
                        if (repeatCount == 0)
                        {
                            repeatCount = 16;
                        }
                        gTrackData[channel].patternRepeatCountdown = repeatCount;
                    }
                    else if (patternNumber >= 0xE0 && patternNumber <= 0xFE)
                    {
                        // Handle transpose codes!
                        gTrackData[channel].orderlistPosition++;
                                   
                        // Convert 0xE0 (224) through 0xFE (254) to -15 through 15
                        gTrackData[channel].semitoneOffset = patternNumber - 0xF0;

                        print("Transpose channel ");
                        print8int(channel);
                        print(" ");
                        print8int(gTrackData[channel].semitoneOffset);
                        print("\n");
                    }
                    else if (patternNumber == 0xFF)
                    {
                        gTrackData[channel].orderlistPosition++;
                        patternNumber = pgm_read_byte(orderlist[0][channel] + gTrackData[channel].orderlistPosition);

                        print("END ");
                        print8int(channel);
                        print(" Next ");
                        print8int(patternNumber);
                        print("\n");

                        gTrackData[channel].orderlistPosition = patternNumber;

                        songFinished = 1;
                    }
                    else
                    {
                        print("Next ");
                        print8int(channel);
                        print(": ");
                        print8int(patternNumber);
                        print("\n");
                    }
                    
                    gTrackData[channel].songPosition = pattern[patternNumber];
                } while (patternNumber >= 0xD0);
            }
        } while (note == 0xFF);

        // TODO: Handle all the rest of the interesting parts
        //printf("Channel %u song position: 0x%p + 4 = ", channel, gTrackData[channel].songPosition);
        gTrackData[channel].songPosition += 4;
        //printf("0x%p\n", gTrackData[channel].songPosition);
    }

    return songFinished;
}

int OutputAudioAndCalculateNextByte(void)
{
    OutputByte(gNextOutputValue);

    gNextOutputValue = GetNextSample();

#if !TEST_MODE
    vbiCount--;
    if (vbiCount == 0)
    {
        vbiCount = VBI_COUNT;
        
        // Enable interrupts, then go through the program step.
        // This should allow the GoatPlayerTick routine to be
        // interrupted by the ISR again if it takes too long,
        // but that's okay.
        // TODO: Test to see the maximum time the GoatPlayerTick routine can take
#ifdef __AVR_ARCH__
        sei();
#endif
        return GoatPlayerTick();
    }
#endif

    return 0;
}
