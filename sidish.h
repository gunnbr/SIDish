#ifndef __SIDISH_H
#define __SIDISH_H

// Test mode disables the player routines and allows alternate
// control of the synthesizer
#define TEST_MODE (0)

// If set, uses the Adafruit Waveshield for audio output.
// If not set, uses a PWM pin as the DAC
#define USE_WAVESHIELD (1)

// Outputs the next byte of audio data
void OutputByte(uint8_t value);

// Outputs the previously calculated byte of audio and calculate
// the next byte
void OutputAudioAndCalculateNextByte();

void InitializeSong(const char *);
void GoatPlayerTick(void);

void print(char *message);
void print8int(int8_t);
void print8hex(uint8_t);
void printint(int);

#endif // __SIDISH_H
