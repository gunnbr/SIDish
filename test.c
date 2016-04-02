#include <stdio.h>
#include <stdint.h>
#include <sys/stat.h>

#include "sidish.h"

#define TABLE_SIZE (1024)
#define NUM_PIANO_KEYS (87)

uint16_t FREQUENCY_TABLE[NUM_PIANO_KEYS];
uint8_t ERRORPERCENT_TABLE[NUM_PIANO_KEYS];
uint16_t SINE_TABLE[TABLE_SIZE];


#define pgm_read_byte(x) *(x)

#include "goatplayer.c"

FILE *outputfp;
unsigned int gTotalBytesWritten = 0;

void OutputByte(uint8_t value)
{
    fwrite(&value, 1, 1, outputfp);
    gTotalBytesWritten++;
}


int main(void)
{
    FILE *fp;
    fp = fopen("Comic_Bakery.sng", "rb");
    if (fp == null)
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
    if (songdata == null)
    {
        printf("Failed to malloc for song data.\n");
        return -1;
    }

    size_t bytesRead = fread(songdata, songstat.st_size, 1, fp);
    if (bytesRead != songstat.st_size)
    {
        printf("Failed to read song data. Got %u of %u bytes.\n", bytesRead, songstat.sr_size);
        return -1;
    }

    fclose(fp);
    
    InitializeSong(songdata);

    outputfp = fopen("tonetest.wav", "w");
    if (outputfp == null)
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
    
    // Chunk size (2 bytes)
    value = 16;
    fwrite(&value, 4, 1, outputfp);

    // Format code (4 bytes)
    value = 1;   //  WAVE_FORMAT_PCM
    fwrite(&value, 4, 1, outputfp);

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

    // Now all the rest of the data is written

    while (GoatPlayerTick())
    {
        OutputAudioAndCalculateNextByte();
    };

    // Go back and fill in the final length
    fseek(outputfp, 4, SEEK_SET);
    gTotalBytesWritten += 24; // 24 is the length of the header
    fwrite(&gTotalBytesWritten, sizeof(gTotalBytesWritten), 1, outputfp);
    fclose(outputfp);
}
