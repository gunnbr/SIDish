// Functions both to handle both the synthesizer and playback
// routines.
// Both combined in this file to allow optimization for embedded
// hardware by using global variables

uint8_t gNextOutputValue = 0;

// SID Registers

// 16 bits
// Technically the ticks per cycle, not frequency
// Key 49 (A4): 440 Hz = 2272 (actually 436.3 Hz)
// Key 47 (G4): 392 Hz = 2551 (actually 388 Hz)
// Key 44 (E4): 329 Hz = 3032 (actually 327 Hz)
// Key 40 (C4): 261 Hz = 3822 (actually 259.325 Hz)
short VOICE1_FREQUENCY = 3822;
short VOICE2_FREQUENCY = 3032;
short VOICE3_FREQUENCY = 2551;

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

uint32_t totalTicks = 0;
uint8_t erroron = 1;

#define VBI_COUNT (16000 / 50)
#define SONG_STEP_COUNT (3) // Essentially the tempo

uint16_t vbiCount = VBI_COUNT;
uint8_t songStepCountdown = 1;

// Pointer to the start of each orderlist.
const char *orderlist[3];

// Pointer to the start of each pattern.
// TODO: Optimize memory usage by reducing this value.
// This alone takes a lot of the available RAM on an ATmega328
const char *pattern[256];

// The position in the orderlist for each channel
uint8_t orderlistPosition[3];

// The number of times remaining to repeat the current pattern before
// moving to the next one
uint8_t patternRepeatCountdown[3];

// Pointer to the current position in the song data for each channel
const char *songPosition[3];

struct Voice
{
    // The number of steps through the SINE_TABLE for each cycle
    // of the bitrate
    uint16_t steps;   // channelSteps[4];

    // The current accumulated position through the SINE_TABLE
    uint16_t tableOffset; // Formerly channelCounts[4];

    // The error percent accumulated with each cycle of the bitrate
    uint8_t errorSteps;

    // The accumulated error percent
    uint8_t errorPercent;

    // Whether the channel is on or not
    uint8_t channelOn;
} channels[4];

// Instrument definition
struct Instrument
{
    uint8_t attackDecay;     // +0      byte    Attack/Decay
    uint8_t sustainRelease;  // +1      byte    Sustain/Release
    uint8_t waveOffset;      // +2      byte    Wavepointer
    uint8_t pulseOfset;      // +3      byte    Pulsepointer
    uint8_t filterOffset;    // +4      byte    Filterpointer
    uint8_t speedOffset;     // +5      byte    Vibrato param. (speedtable pointer)
    uint8_t vibratoDelay;    // +6      byte    Vibrato delay
    uint8_t gateoffTime;     // +7      byte    Gateoff timer
    uint8_t hardRestart;     // +8      byte    Hard restart/1st frame waveform
    char    name[16];        // +9      16      Instrument name
} *instruments;


void InitializeSong(const char *songdata)
{
    const char *data = songdata;

    print("\n\n\n******** Initializing *******\n\n");
    
    uint32_t header = pgm_read_dword(data);
    if (header != 0x35535447)
    {
        print("Header is not from GoatTracker\n");
        return;
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

    uint8_t subtunes = pgm_read_byte(data++);
    print("# subtunes: ");
    print8int(subtunes);
    print("\n");

    for(i = 0 ; i < subtunes ; i++)
    {
        // TODO: Handle multiple subtunes
        uint8_t size = pgm_read_byte(data++);
        orderlist[0] = data;
        data += size + 1;

        print("Subtune ");
        print8int(i);
        print(" Orderlist 1 Size ");
        print8int(size);
        print("\n");

        size = pgm_read_byte(data++);
        orderlist[1] = data;
        data += size + 1;

        print("Subtune ");
        print8int(i);
        print(" Orderlist 2 Size ");
        print8int(size);
        print("\n");

        size = pgm_read_byte(data++);
        orderlist[2] = data;
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

    instruments = (struct Instrument *)data;

    for (i = 0 ; i < numInstruments ; i++)
    {
        data += 25;
        
        uint8_t ad = pgm_read_byte(&instruments[i].attackDecay);
        uint8_t sr = pgm_read_byte(&instruments[i].sustainRelease);
        for (uint8_t x = 0 ; x < 16 ; x++)
        {
            name[x] = pgm_read_byte(&instruments[i].name[x]);
        }
        print("Instrument ");
        print8int(i);
        print(" (");
        print(name);
        print(") ");
        print(": ADSR 0x");
        print8hex(ad);
        print8hex(sr);
        print("\n");
    }
    
    uint8_t size = pgm_read_byte(data++);
    print("Wavetable Size: ");
    print8int(size);
    print("\n");
    data += size * 2;

    size = pgm_read_byte(data++);
    print("Pulsetable Size: ");
    print8int(size);
    print("\n");
    data += size * 2;

    size = pgm_read_byte(data++);
    print("Filtertable Size: ");
    print8int(size);
    print("\n");
    data += size * 2;

    size = pgm_read_byte(data++);
    print("Speedtable Size: ");
    print8int(size);
    print("\n");
    data += size * 2;

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
        print("\n");

        data += 4*length;
    }

    // Initializa the song to get ready to play
    for (uint8_t channel = 0 ; channel < 3 ; channel++)
    {
        // Start each channel at the first pattern in the order list
        orderlistPosition[i] = 0;

        uint8_t patternNumber;
        do
        {
            // Get the pattern number from the current position
            patternNumber = pgm_read_byte(orderlist[channel] + orderlistPosition[channel]);
            if (patternNumber >= 0xD0 && patternNumber <= 0xDF)
            {
                orderlistPosition[channel]++;
                uint8_t repeatCount = patternNumber & 0x0F;
                if (repeatCount == 0)
                {
                    repeatCount = 16;
                }
                patternRepeatCountdown[channel] = repeatCount;
            }
            else if (patternNumber >= 0xE0 && patternNumber <= 0xFE)
            {
                orderlistPosition[channel]++;
                // TODO: Handle transpose codes!
            }
        } while (patternNumber >= 0xD0);
        
        // Start each channel at the first song position for each pattern
        songPosition[channel] = pattern[patternNumber];
        print("Channel ");
        print8int(channel);
        print(" Initial Pattern: ");
        print8int(patternNumber);
        print("\n");
    }
}

void SetKey(uint8_t channel, uint8_t key, uint8_t instrument)
{
    channels[channel].steps = pgm_read_word(&FREQUENCY_TABLE[key]);
    channels[channel].errorSteps = pgm_read_byte(&ERRORPERCENT_TABLE[key]);
    channels[channel].tableOffset = 0;
    channels[channel].errorPercent = 0;
    channels[channel].channelOn = 1;
}

void KeyOff(uint8_t channel)
{
    channels[channel].channelOn = 0;
}

// Returns TRUE when the song is finished
int GoatPlayerTick()
{
    int songFinished = 0;
    
    songStepCountdown--;
    if (songStepCountdown > 0)
    {
        // TODO Process effects here
        return songFinished;
    }

    songStepCountdown = SONG_STEP_COUNT;

    for(uint8_t channel = 0; channel < 3 ; channel++)
    {
        uint8_t note;
        uint8_t instrument;
        
        do
        {
            note = pgm_read_byte(songPosition[channel]);
            if (note >= 0x60 && note <= 0xBC)
            {
                instrument = pgm_read_byte(songPosition[channel] + 1);
                
                note -= 0x60;
                SetKey(channel, note, instrument);

                // printf("NOTE ON -- Channel %u Note: %u Instrument: %u\n", channel, note, instrument);
            }
            else if (note == 0xBE)
            {
                KeyOff(channel);
            }
            else if (note == 0xFF)
            {
                if (patternRepeatCountdown[channel] > 0)
                {
                    patternRepeatCountdown[channel] -= 1;
                    uint8_t patternNumber = pgm_read_byte(orderlist[channel] + orderlistPosition[channel]);
                    songPosition[channel] = pattern[patternNumber];
                    continue;
                }
                
                orderlistPosition[channel]++;

                uint8_t patternNumber;
                do
                {
                    // Get the pattern number from the current position
                    patternNumber = pgm_read_byte(orderlist[channel] + orderlistPosition[channel]);

                    if (patternNumber >= 0xD0 && patternNumber <= 0xDF)
                    {
                        orderlistPosition[channel]++;
                        uint8_t repeatCount = patternNumber & 0x0F;
                        if (repeatCount == 0)
                        {
                            repeatCount = 16;
                        }
                        patternRepeatCountdown[channel] = repeatCount;
                    }
                    else if (patternNumber >= 0xE0 && patternNumber <= 0xFE)
                    {
                        orderlistPosition[channel]++;
                        // TODO: Handle transpose codes!
                    }
                    else if (patternNumber == 0xFF)
                    {
                        orderlistPosition[channel]++;
                        patternNumber = pgm_read_byte(orderlist[channel] + orderlistPosition[channel]);

                        print("END ");
                        print8int(channel);
                        print(" Next ");
                        print8int(patternNumber);
                        print("\n");

                        orderlistPosition[channel] = patternNumber;

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
                    
                    songPosition[channel] = pattern[patternNumber];
                } while (patternNumber >= 0xD0);
            }
        } while (note == 0xFF);

        // TODO: Handle all the rest of the interesting parts
        songPosition[channel] += 4;
    }

    return songFinished;
}

int OutputAudioAndCalculateNextByte(void)
{
    OutputByte(gNextOutputValue);

    int8_t outputValue = 0;
    
    // 4 channels are actually supported, but since GoatTracker
    // only supports 3 and I need more cycles, only process 3
    // of them.
    
    for (uint8_t channel = 0 ; channel < 3 ; channel++)
    {
        if (channels[channel].channelOn)
        {
            channels[channel].tableOffset += channels[channel].steps;
            channels[channel].errorPercent += channels[channel].errorSteps;
            if (channels[channel].errorPercent >= 100)
            {
#if TEST_MODE
                if (erroron)
#endif
                    channels[channel].tableOffset++;
                channels[channel].errorPercent -= 100;
            }
            
            if (channels[channel].tableOffset >= TABLE_SIZE)
            {
                channels[channel].tableOffset -= TABLE_SIZE;
            }
            
            outputValue += (int8_t)pgm_read_byte(&SINE_TABLE[channels[channel].tableOffset]);
        }
    }  
    
    // Scale -128 to 128 values to 0 to 255
    gNextOutputValue = (uint8_t)((int16_t)outputValue + 128);
    
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

    return 0;
#endif
}
