#ifndef __SIDISH_H
#define __SIDISH_H

// Test mode disables the player routines and allows alternate
// control of the synthesizer
#define TEST_MODE (0)

// If set, uses the Adafruit Waveshield for audio output.
// If not set, uses a PWM pin as the DAC
#define USE_WAVESHIELD (1)

// Bitrate for output (DON'T CHANGE WITHOUT MAKING OTHER MODIFICATIONS IN THE CODE!!)
#define BITRATE (16000)

// Number of elements in the waveform tables
#define TABLE_SIZE (1024)

// Number of predefined keys in the frequency table
#define NUM_PIANO_KEYS (87)

// Outputs the next byte of audio data
#if __cplusplus 
extern "C" 
#endif
void OutputByte(uint8_t value);

// Outputs the previously calculated byte of audio and calculate
// the next byte
// Returns True if the song is finished, false otherwise
#if __cplusplus 
extern "C"
#endif
int OutputAudioAndCalculateNextByte();

#if __cplusplus 
extern "C"
#endif
int InitializeSong(const char *);

#if __cplusplus 
extern "C"
#endif
void print(char *message);
#if __cplusplus 
extern "C"
#endif
void print8int(int8_t);
#if __cplusplus 
extern "C"
#endif
void print8hex(uint8_t);
#if __cplusplus 
extern "C"
#endif
void printint(int);

#endif // __SIDISH_H
