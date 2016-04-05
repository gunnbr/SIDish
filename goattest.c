#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <math.h>

#include "sidish.h"

// This table holds the number of steps through the
// SINE_TABLE for each cycle of the BITRATE.
// This is essentially a fixed point floating point table.
// The high 16 bits are used as the lookup into the SINE_TABLE
// and the low 16 bits are the error in units of 1/65536ths.
uint32_t FREQUENCY_TABLE[NUM_PIANO_KEYS];
int8_t SINE_TABLE[TABLE_SIZE];


#define pgm_read_byte(x) *(uint8_t*)(x)
#define pgm_read_word(x) *(uint16_t*)(x)
#define pgm_read_dword(x) *(uint32_t*)(x)

#include "goatplayer.c"

FILE *outputfp;
unsigned int gTotalBytesWritten = 0;

void print(char *message)
{
    printf("%s", message);
}

void print8int(int8_t value)
{
    printf("%d", value);
}

void print8hex(uint8_t value)
{
    printf("%02X", value);
}

void OutputByte(uint8_t value)
{
    fwrite(&value, 1, 1, outputfp);
    gTotalBytesWritten++;
}

void InitializeTables()
{
    int x;

    printf("Sine Table\n----------\n");
    printf("const int8_t sineTable[] PROGMEM = {");

    for (x = 0 ; x < TABLE_SIZE ; x++)
    {
        SINE_TABLE[x] = (int8_t)(32*sin(2*M_PI*x/TABLE_SIZE));
        printf("%d, ", SINE_TABLE[x]);
    }
    printf("};\n");

    // TODO: Change this so that the frequency table is a 16bit integer.
    // The high 8 bits are what we put in the FREQUENCY_TABLE now while
    // the low 8 bits are the "error percent" in 256th's, making this
    // essentially a fixed point floating point math.
    // When we want to use the value, just use the high 8 bits to look
    // up in the SINE_TABLE just as we do now.
    for (x = 0 ; x < NUM_PIANO_KEYS ; x++)
    {
        double frequency = pow(2, (x-49.0)/12.0) * 440.0;
        double steps = (frequency / (double)BITRATE * (double)TABLE_SIZE);
        uint16_t frequencySteps = (uint16_t) steps;
        uint16_t error = (uint16_t)floor(steps * 65536 - frequencySteps * 65536);
        FREQUENCY_TABLE[x] = ((uint16_t)frequencySteps << 16) | ((uint16_t)error & 0xFFFF);
    }

    printf("const uint32_t FREQUENCY_TABLE[] PROGMEM = {");
    for(x = 0 ; x < NUM_PIANO_KEYS ; x++)
    {
        printf("0x%08X,", FREQUENCY_TABLE[x]);
    }
    printf("};\n");
}

#define SONGNAME "Comic_Bakery.sng"
//#define SONGNAME "EnvelopeTest.sng"

int main(void)
{
    InitializeTables();
    
    FILE *fp;
    fp = fopen(SONGNAME, "rb");
    if (fp == NULL)
    {
        printf("Failed to open the song data.\n");
        return -1;
    }

    struct stat songstat;
    int error = fstat(fileno(fp), &songstat);
    if (error != 0)
    {
        printf("Failed to stat the song data.\n");
        return -1;
    }

    char *songdata = malloc(songstat.st_size);
    if (songdata == NULL)
    {
        printf("Failed to malloc for song data.\n");
        return -1;
    }

    size_t bytesRead = fread(songdata, 1, songstat.st_size, fp);
    if (bytesRead != songstat.st_size)
    {
        printf("Failed to read song data. Got %zu of %u bytes.\n", bytesRead, (uint32_t)songstat.st_size);
        return -1;
    }

    fclose(fp);
    
    InitializeSong(songdata);

    outputfp = fopen("tonetest.wav", "w");
    if (outputfp == NULL)
    {
        printf("Failed to open output file.\n");
        return -1;
    }

    // Write the .wave file header
    fwrite("RIFF", 4, 1, outputfp);

    // Total size of the rest of the file (4 unsigned bytes to be filled in later)
    unsigned int value = 0;
    fwrite(&value, 4, 1, outputfp);

    fwrite("WAVE", 4, 1, outputfp);

    fwrite("fmt ", 4, 1, outputfp);
    
    // Chunk size (4 bytes)
    value = 16;
    fwrite(&value, 4, 1, outputfp);

    // Format code (2 bytes)
    value = 1;   //  WAVE_FORMAT_PCM
    fwrite(&value, 2, 1, outputfp);

    //Number of interleaved channels (2 bytes)
    uint16_t shortvalue = 1; // # WAVE_FORMAT_PCM
    fwrite(&shortvalue, 2, 1, outputfp);

    //Sample rate (4 bytes)
    value = BITRATE;
    fwrite(&value, 4, 1, outputfp);

    // Data rate (average bytes per second) (4 bytes)
    // Same as bitrate since we have 1 byte per sample
    value = BITRATE;
    fwrite(&value, 4, 1, outputfp);

    // Data block size (2 bytes)
    shortvalue = 1;
    fwrite(&shortvalue, 2, 1, outputfp);

    // Bits per sample (2 bytes)
    shortvalue = 8;
    fwrite(&shortvalue, 2, 1, outputfp);

    fwrite("data", 4, 1, outputfp);

    // Now calculate and write all the rest of the data
    while (!OutputAudioAndCalculateNextByte());

    // Go back and fill in the final length
    fseek(outputfp, 4, SEEK_SET);
    gTotalBytesWritten += 24; // 24 is the length of the header
    fwrite(&gTotalBytesWritten, sizeof(gTotalBytesWritten), 1, outputfp);
    fclose(outputfp);
}
