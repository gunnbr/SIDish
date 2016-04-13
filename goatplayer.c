// Functions both to handle both the synthesizer and playback
// routines.
// Both combined in this file to allow optimization for embedded
// hardware by using global variables

uint8_t gNextOutputValue = 0;

uint32_t totalTicks = 0;

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

// SID Registers
// 16 bit FREQUENCY
// 12 bit DUTY_CYCLE in SID, only 8 bits here
// 8 bit CONTROL register
// 8 bit ATTACK_DECAY
// 8 bit SUSTAIN_RELEASE

enum EnvelopePhase
{
    Off,
    Attack,
    Decay,
    Sustain,
    Release,
};

#define CONTROL_GATE            (0x01)
#define CONTROL_SYNCHRONIZE     (0x02) // NOT IMPLEMENTED
#define CONTROL_RING_MODULATION (0x04) // NOT IMPLEMENTED
#define CONTROL_TESTBIT         (0x08) // NOT IMPLEMENTED
#define CONTROL_TRIANGLE        (0x10)
#define CONTROL_SAWTOOTH        (0x20)
#define CONTROL_PULSE           (0x40)
#define CONTROL_NOISE           (0x80)

// Number of cycles between each modification of the fader
// Based on the BITRATE and the times for the SID chip given in the SID Wizard documentation
const uint16_t AttackCycles[16] = { 1, 4, 8, 12, 19, 28, 34, 40, 50, 125, 250, 400, 500, 1500, 2500, 4000 };
const uint16_t DecayReleaseCycles[16] = { 3, 12, 24, 36, 57, 84, 102, 120, 150, 375, 750, 1200, 1500, 4500, 7500, 12000 };

struct Voice
{
    // The number of steps through the waveform for each cycle
    // of the bitrate
    uint16_t steps;

    // The current accumulated position through the waveform
    // High byte is the output value + 32
    // Low byte is the accumulated error offset (essentially a fixed decimal point value)
    uint16_t tableOffset;

    // Envelope values
    uint8_t attackDecay;
    uint8_t sustainRelease;
    
    // Current phase in the envelope generator
    enum EnvelopePhase envelopePhase;

    // Cycles remaining until we adjust the fadeAmount
    uint16_t phaseStepCountdown;
    
    // Amount to fade the current value (based on the current position in the ADSR envelope)
    uint8_t fadeAmount;
    
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

#if TEST_MODE
struct Instrument FakeInstruments[1] =
{
    {0x00, 0xF0, 0, 0, 0, 0, 0, 0, 0, "TestInstrument"}
};
    
void EnableFakeInstruments()
{
    instruments = FakeInstruments;
}
#endif

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

void KeyOn(uint8_t channel, uint8_t key, uint8_t instrument)
{
    channels[channel].steps = pgm_read_word(&SAWTOOTH_TABLE[key]);
    channels[channel].tableOffset = 0;
    channels[channel].attackDecay = pgm_read_word(&instruments[instrument - 1].attackDecay);
    channels[channel].sustainRelease = pgm_read_word(&instruments[instrument - 1].sustainRelease);
    channels[channel].fadeAmount = 32;
    channels[channel].envelopePhase = Attack;
    channels[channel].phaseStepCountdown = AttackCycles[(channels[channel].attackDecay & 0xF0) >> 4];
    // TODO: Store the instrument # so that if we use instrument #0, it keeps the same instrument as before (I think).
    //printf("Instrument %u: AD: 0x%02X SR: 0x%02X ", instrument, channels[channel].attackDecay, channels[channel].sustainRelease);
    //printf("Key On: %u Attack Countdown: %u\n", key, channels[channel].phaseStepCountdown);
}

void KeyOff(uint8_t channel)
{
    channels[channel].envelopePhase = Release;
    channels[channel].phaseStepCountdown = DecayReleaseCycles[channels[channel].sustainRelease & 0x0F];
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
                KeyOn(channel, note, instrument);

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
        if (channels[channel].envelopePhase != Off)
        {
            //printf("Channel %u Offset: 0x%04X + Steps: 0x%04X = ", channel, channels[channel].tableOffset, channels[channel].steps);
            channels[channel].tableOffset += channels[channel].steps;
            //printf("0x%04X ", channels[channel].tableOffset);

            uint16_t offset = channels[channel].tableOffset >> 8;
            //printf("Offset: 0x%04X ", offset);
            
            if (offset >= 64)
            {
                offset -= 64;
                channels[channel].tableOffset &= 0xFF;
                channels[channel].tableOffset |= offset << 8;
            }

#if 1 // SAWTOOTH
            int8_t waveformValue = offset - 32;
#else // TRIANGLE
            int8_t waveformValue = offset * 2;
            if (waveformValue >= 64)
            {
              waveformValue = 128 - waveformValue;
            }
            waveformValue -= 32;
#endif
            //printf("Wave: %d\n", waveformValue);
            
            int16_t shortWaveformValue = (int16_t)waveformValue;
            int8_t fadedValue = (int8_t) (shortWaveformValue * (32 - channels[channel].fadeAmount) / 32);
            //printf("waveform: %2d short: %2d fadeAmount: %2u faded: %2d\n",
            //        waveformValue, shortWaveformValue, channels[channel].fadeAmount, fadedValue);
            
            outputValue += fadedValue;

            channels[channel].phaseStepCountdown--;
            if (channels[channel].phaseStepCountdown == 0)
            {
                // TODO: Parse out and save the A D S R values ahead of time and store
                // in the struct so we don't have to do it a lot here?
                switch (channels[channel].envelopePhase)
                {
                case Attack:
                    channels[channel].fadeAmount--;
                    if (channels[channel].fadeAmount == 0)
                    {
                        // TODO: Figure out exactly how the decay works. Is it "X ms" to decay from the
                        //       maximum to the sustain value or is it "X ms" total if we were decaying
                        //       to the minimum?
                        channels[channel].envelopePhase = Decay;
                        channels[channel].phaseStepCountdown = DecayReleaseCycles[channels[channel].attackDecay & 0x0F];
                    }
                    else
                    {
                        channels[channel].phaseStepCountdown = AttackCycles[(channels[channel].attackDecay & 0xF0) >> 4];
                    }
                    break;

                case Decay:
                    {
                        uint8_t sustainLevel = 0x0F - ((channels[channel].sustainRelease & 0xF0) >> 4);
                        sustainLevel <<= 1;
                        
                        // Decaying from the maximum value to the sustain level
                        // TODO: Problem here when Sustain is F
                    
                        channels[channel].fadeAmount++;
                        if (channels[channel].fadeAmount >= sustainLevel)
                        {
                            channels[channel].envelopePhase = Sustain;
                            // TODO: Really should just disable the phaseCountdown here, but maybe it doesn't matter
                        }
                        else
                        {
                            channels[channel].phaseStepCountdown = DecayReleaseCycles[channels[channel].attackDecay & 0x0F];
                        }
                    }
                    break;

                case Sustain:
                    // Do nothing. Just sustain
                    break;

                case Release:
                    // Fade from the sustain level to 0 (fadeAmount of 32)
                    channels[channel].fadeAmount++;
                    if (channels[channel].fadeAmount == 32)
                    {
                        channels[channel].envelopePhase = Off;
                    }
                    else
                    {
                        channels[channel].phaseStepCountdown = DecayReleaseCycles[channels[channel].sustainRelease & 0x0F];
                    }
                    break;

                case Off:
                    // Doesn't need to be here except to avoid the compiler warning
                    break;
                }
            }
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
#endif

    return 0;
}
