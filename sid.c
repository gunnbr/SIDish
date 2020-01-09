#ifdef WIN32
#include <stdint.h>
#include "tables.h"
#include "sidish.h"

uint32_t pgm_read_dword(const char*);
uint16_t pgm_read_word(const uint32_t*);
uint8_t pgm_read_byte(const char*);
#endif

#include "sid.h"

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

// Number of cycles between each modification of the fader
// Based on the BITRATE and the times for the SID chip given in the SID Wizard documentation
const uint16_t AttackCycles[16] = { 1, 4, 8, 12, 19, 28, 34, 40, 50, 125, 250, 400, 500, 1500, 2500, 4000 };
const uint16_t DecayReleaseCycles[16] = { 3, 12, 24, 36, 57, 84, 102, 120, 150, 375, 750, 1200, 1500, 4500, 7500, 12000 };

uint16_t gNoise = 0x42;

struct Voice
{
    // Frequency
    uint16_t frequency;

    // Envelope values
    uint8_t attackDecay;
    uint8_t sustainRelease;

    // Control bits (defined above)
    uint8_t control;

    // Pulse values
    uint16_t pulseWidth;

    // The number of steps through the waveform for each cycle
    // of the bitrate. Set from the sample rate and frequency
    uint16_t steps;

    // The current accumulated position through the waveform
    // High byte is the output value + 32
    // Low byte is the accumulated error offset (essentially a fixed decimal point value)
    uint16_t tableOffset;

    // Current phase in the envelope generator
    enum EnvelopePhase envelopePhase;

    // Cycles remaining until we adjust the fadeAmount
    uint16_t phaseStepCountdown;

    // Amount to fade the current value (based on the current position in the ADSR envelope)
    uint8_t fadeAmount;

} channels[4];


uint8_t GetNextSample()
{
    int8_t outputValue = 0;
    int8_t wrap = 0;

    // 4 channels are actually supported, but since GoatTracker
    // only supports 3 and I need more cycles, only process 3
    // of them.

    for (uint8_t channel = 0; channel < 3; channel++)
    {
        uint16_t bit = ((gNoise >> 0) ^ (gNoise >> 2) ^ (gNoise >> 3) ^ (gNoise >> 5)) & 1;
        gNoise = (gNoise >> 1) | (bit << 15);

        // printf("Noise: 0x%02X ", gNoise);

        if (channels[channel].envelopePhase != Off)
        {
            uint16_t offset = channels[channel].tableOffset >> 8;
            //printf("Offset: 0x%04X ", offset);

            if (offset >= 64)
            {
                offset -= 64;
                channels[channel].tableOffset &= 0xFF;
                channels[channel].tableOffset |= offset << 8;
                wrap = 1;
            }

            int8_t waveformValue;

            if (channels[channel].control & CONTROL_SAWTOOTH)
            {
                waveformValue = offset - 32;
                //printf(" SAWTOOTH (%u): %d\n", channel, waveformValue);
            }
            else if (channels[channel].control & CONTROL_TRIANGLE)
            {
                waveformValue = offset * 2;
                if (waveformValue >= 64)
                {
                    waveformValue = 128 - waveformValue;
                }
                waveformValue -= 32;
                //printf(" TRIANGLE (%u): %d\n", channel, waveformValue);
            }
            else if (channels[channel].control & CONTROL_PULSE)
            {
                if (wrap)
                {
                    waveformValue = 31;
                }
                else if (channels[channel].tableOffset >= channels[channel].pulseWidth)
                {
                    waveformValue = -32;
                }
                else
                {
                    waveformValue = 31;
                }
                //printf(" PULSE (%u): offset: %u pulseWidth: %u %d\n", channel, channels[channel].tableOffset, channels[channel].pulseWidth, waveformValue);
            }
            else if (channels[channel].control & CONTROL_NOISE)
            {
                waveformValue = (gNoise & 0x3F) - 32;
                //printf(" NOISE (%u): %d\n", channel, waveformValue);
            }
            else
            {
                waveformValue = 0;
            }

            int16_t shortWaveformValue = (int16_t)waveformValue;
            int8_t fadedValue = (int8_t)(shortWaveformValue * (32 - channels[channel].fadeAmount) / 32);
            //printf("waveform: %3d short: %3d fadeAmount: %2u phase: %d faded: %3d\n",
            //        waveformValue, shortWaveformValue, channels[channel].fadeAmount, channels[channel].envelopePhase, fadedValue);

            //printf(" Faded: %d\n", fadedValue);
            outputValue += fadedValue;

            channels[channel].phaseStepCountdown--;
            if (channels[channel].phaseStepCountdown == 0)
            {
                // TODO: Parse out and save the A D S R values ahead of time and store
                // in the struct so we don't have to do it a lot here?
                switch (channels[channel].envelopePhase)
                {
                case Attack:
                    if (channels[channel].fadeAmount <= 0)
                    {
                        //printf("Done attacking. SR = 0x%02X\n", channels[channel].sustainRelease);
                        uint8_t sustainLevel = (channels[channel].sustainRelease & 0xF0) >> 4;
                        //printf("SustainLevel = %u\n", sustainLevel);
                        uint8_t sustainFadeValue = 0x0F - sustainLevel;
                        sustainFadeValue <<= 1;

                        //printf("FadeValue = %u\n", sustainFadeValue);
                        if (sustainFadeValue > 0)
                        {
                            //printf("Switching to decay\n");
                            // TODO: Figure out exactly how the decay works. Is it "X ms" to decay from the
                            //       maximum to the sustain value or is it "X ms" total if we were decaying
                            //       to the minimum?
                            channels[channel].envelopePhase = Decay;
                            channels[channel].phaseStepCountdown = DecayReleaseCycles[channels[channel].attackDecay & 0x0F];
                        }
                        else
                        {
                            //printf("Switching to Sustain.\n");
                            channels[channel].envelopePhase = Sustain;
                        }
                    }
                    else
                    {
                        channels[channel].fadeAmount--;
                        channels[channel].phaseStepCountdown = AttackCycles[(channels[channel].attackDecay & 0xF0) >> 4];
                    }
                    break;

                case Decay:
                {
                    uint8_t sustainFadeValue = 0x0F - ((channels[channel].sustainRelease & 0xF0) >> 4);
                    sustainFadeValue <<= 1;
#if 0
                    print("SR: ");
                    print8hex(channels[channel].sustainRelease);
                    print(" sFV: ");
                    print8hex(sustainFadeValue);
                    print("\n");
#endif

                    // Decaying from the maximum value to the sustain level

                    if (channels[channel].fadeAmount >= sustainFadeValue)
                    {
                        channels[channel].envelopePhase = Sustain;
                        // TODO: Really should just disable the phaseCountdown here, but it doesn't entirely
                        //       matter since it just wraps around only calling Sustain every 65536 loops.
                    }
                    else
                    {
                        channels[channel].fadeAmount++;
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
                    if (channels[channel].fadeAmount >= 32)
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

            //printf("Channel %u Offset: 0x%04X + Steps: 0x%04X = ", channel, channels[channel].tableOffset, channels[channel].steps);
            channels[channel].tableOffset += channels[channel].steps;
            //printf("0x%04X ", channels[channel].tableOffset);
        }
    }

    // Scale -128 to 128 values to 0 to 255
    uint8_t returnValue = (uint8_t)((int16_t)outputValue + 128);
    //printf("Output: %d -> %u\n", outputValue, returnValue);
    return returnValue;
}

void SidInitialize() 
{
    uint8_t channel;
    for (channel = 0; channel < 4; channel++) 
    {
        channels[channel].fadeAmount = 32;
        channels[channel].envelopePhase = Off;
    }
}

void SetSidRegister(uint8_t channel, enum SidRegister address, uint8_t value)
{
    switch (address)
    {
    case Frequency:
        // set the steps based on the frequency
        channels[channel].steps = BITRATE / value;

        // Original code for setup of sawtooth or triangle wave
        //printf("Setting steps for SAWTRI, note %d\n", gTrackData[channel].currentNote);
        //channels[channel].steps = pgm_read_word(&SAWTOOTH_TABLE[currentNote]);
        channels[channel].tableOffset = 0;

        // Original code for setup of pulse wave
        //printf("Setting steps for PULSE, note %d\n", gTrackData[channel].currentNote);
        // Use the same table. We'll multiply the pulsetable value by 4 to scale
        // it from 16.00 to 64.00
        //channels[channel].steps = pgm_read_word(&SAWTOOTH_TABLE[gTrackData[channel].currentNote]);
        //channels[channel].tableOffset = 0;

        break;

    case PulseWidth:
        channels[channel].pulseWidth = value;
        break;

    case Control:
        if (!(channels[channel].control & CONTROL_GATE) &&
            (value & CONTROL_GATE))
        {
            // The gate was just turned on--enter the attack phase

            // Original code checked to see if we were in the off phase
            // before doing this, but I don't think that's right.
            channels[channel].envelopePhase = Attack;
            
            // TODO: Do we always want to reset the table? I feel like
            //       there's an option somewhere to not reset that
            channels[channel].tableOffset = 0;
        }

        if (channels[channel].control & CONTROL_GATE &&
            !(value & CONTROL_GATE))
        {
            // The gate was turned off--switch to release phase
            channels[channel].envelopePhase = Release;
            channels[channel].phaseStepCountdown = DecayReleaseCycles[channels[channel].sustainRelease & 0x0F];
        }
        break;

    case AttackDecay:
        channels[channel].attackDecay = value;
        channels[channel].phaseStepCountdown = AttackCycles[(value & 0xF0) >> 4];
        break;

    case SustainRelease:
        channels[channel].sustainRelease = value;
        break;
    }
}

uint8_t GetSidRegister(uint8_t channel, enum SidRegister address)
{
    switch (address) 
    {
        case Frequency: return channels[channel].frequency;
        case PulseWidth: return channels[channel].pulseWidth;
        case Control: return channels[channel].control;
        case AttackDecay: return channels[channel].attackDecay;
        case SustainRelease: return channels[channel].sustainRelease;
    }

    return 0;
}