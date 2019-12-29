#include "pch.h"
#include <iostream>
#include <Windows.h>
#include <Mmeapi.h>
#include "sidish.h"
#include "resource.h"

#define BUFFER_SIZE (BITRATE)

LPSTR gAudioBuffers[2];
int gNextBuffer = 0;

using namespace std;

extern "C" void print(char *message)
{
	cout << message;
}

extern "C" void print8int(int8_t num)
{
	cout << std::dec << num;
}

extern "C" void print8hex(uint8_t num)
{
	cout << std::hex << num;
}

extern "C" void printint(int num)
{
	cout << std::dec << num;
}

extern "C" uint32_t pgm_read_dword(const char *data)
{
	return *(uint32_t *)data;
}

extern "C" uint16_t pgm_read_word(const uint32_t *data)
{
	return (uint16_t)*data;
}

extern "C" uint8_t pgm_read_byte(const char *data)
{
	return *(uint8_t*)data;
}

void PrintResult(MMRESULT result)
{
	switch (result) {
	case MMSYSERR_INVALHANDLE:
		cout << "Invalid handle\n";
		break;

	case MMSYSERR_NODRIVER:
		cout << "No driver\n";
		break;

	case MMSYSERR_NOMEM:
		cout << "No memory\n";
		break;

	case WAVERR_UNPREPARED:
		cout << "Unprepared\n";
		break;

	case MMSYSERR_INVALPARAM:
		cout << "Invalid parameter\n";
		break;

	case MMSYSERR_HANDLEBUSY:
		cout << "Handle is busy (on another thread)\n";
		break;

	default:
		cout << "Other: " << result << "\n";
		break;
	}
}

char *gpOutputBuffer;

// Outputs the next byte of audio data
extern "C" void OutputByte(uint8_t value)
{
	*gpOutputBuffer++ = value;
}

void FillBuffer(char *buffer) 
{
#if false
	// Timestamp used to create interesting test sound
	// See http://goo.gl/hQdTi
	static DWORD t = 0;

	for (DWORD i = 0; i < BUFFER_SIZE; i++, t++)
	{
		buffer[i] = static_cast<char>((((t * (t >> 8 | t >> 9) & 46 & t >> 8)) ^ (t & t >> 13 | t >> 6)) & 0xFF);
	}
#endif

	gpOutputBuffer = buffer;

	for (int i = 0; i < BUFFER_SIZE; i++)
	{
		OutputAudioAndCalculateNextByte();
	}
}

int main()
{
	cout << "Initializing buffers\n";

	gAudioBuffers[0] = (LPSTR)LocalAlloc(LMEM_FIXED, BUFFER_SIZE);
	gAudioBuffers[1] = (LPSTR)LocalAlloc(LMEM_FIXED, BUFFER_SIZE);

	UINT numDevices = waveOutGetNumDevs();

	cout << "Opening device first device of " << numDevices << "\n";

	if (numDevices == 0)
	{
		cout << "No devices to open\n";
		return -1;
	}

	HANDLE audioEvent = CreateEventA(NULL, TRUE, FALSE, "AUDIO_EVENT");

	WAVEFORMATEX wfx = { WAVE_FORMAT_PCM, 1, BITRATE, BITRATE, 1, 8, 0 };
	HWAVEOUT audioDevice;
	MMRESULT result = waveOutOpen(&audioDevice, WAVE_MAPPER, &wfx,
		(DWORD_PTR)audioEvent, NULL,
		WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE | CALLBACK_EVENT);

	if (result != MMSYSERR_NOERROR)
	{
		cout << "Failed to open device: " << result << "\n";
		return -1;
	}
	else
	{
		std::wcout << "Audio driver is open. Device is 0x" << std::hex << audioDevice << std::dec << "\n";
	}

	cout << "Preparing the headers\n";

	WAVEHDR headers[2];

	headers[0] = { (LPSTR)gAudioBuffers[0], BUFFER_SIZE, 0, 0, 0, 0, 0, 0 };
	result = waveOutPrepareHeader(audioDevice, &headers[0], sizeof(WAVEHDR));
	if (result != MMSYSERR_NOERROR)
	{
		cout << "Failed to prepare header 0: ";
		PrintResult(result);
		return -1;
	}

	headers[1] = { (LPSTR)gAudioBuffers[1], BUFFER_SIZE, 0, 0, 0, 0, 0, 0 };
	result = waveOutPrepareHeader(audioDevice, &headers[1], sizeof(WAVEHDR));
	if (result != MMSYSERR_NOERROR)
	{
		cout << "Failed to prepare header 1: ";
		PrintResult(result);
		return -1;
	}

	HRSRC hSongResource = FindResource(NULL, MAKEINTRESOURCE(IDR_RCDATA1), RT_RCDATA);
	if (!hSongResource) 
	{
		cout << "Failed to load song resource\n";
		return -1;
	}

	const char *pSongData = (const char *)LoadResource(NULL, hSongResource);
	int success = InitializeSong(pSongData);
	if (!success) 
	{
		return -1;
	}

	FillBuffer(gAudioBuffers[0]);
	FillBuffer(gAudioBuffers[1]);

	cout << "Filling the buffers\n";
	ResetEvent(audioEvent);

	result = waveOutWrite(audioDevice, &headers[0], sizeof(WAVEHDR));
	if (result != MMSYSERR_NOERROR)
	{
		cout << "Failed to write buffer 0";
		PrintResult(result);
		return -1;
	}

	result = waveOutWrite(audioDevice, &headers[1], sizeof(WAVEHDR));
	if (result != MMSYSERR_NOERROR)
	{
		cout << "Failed to write buffer 1";
		PrintResult(result);
		return -1;
	}

	cout << "Sleeping for a while...\n";

	bool bDone = false;
	UINT8 currentBuffer = 0;

	while (!bDone)
	{
		DWORD dwResult = WaitForSingleObject(audioEvent, 20000);
		if (dwResult == WAIT_OBJECT_0)
		{
			//cout << "Event triggered!\n";
			if (headers[gNextBuffer].dwFlags & WHDR_DONE) {
				//cout << "    Refilling buffer #" << gNextBuffer;
				FillBuffer(gAudioBuffers[gNextBuffer]);
				result = waveOutWrite(audioDevice, &headers[gNextBuffer], sizeof(WAVEHDR));
				if (result != MMSYSERR_NOERROR)
				{
					cout << "Failed to write buffer " << gNextBuffer;
					PrintResult(result);
					return -1;
				}

				gNextBuffer++;
				gNextBuffer %= 2;
			}
			else 
			{
				cout << "Event triggered--flags are 0x" << std::hex << headers[gNextBuffer].dwFlags << std::dec;
			}
		}
		else if (dwResult == WAIT_ABANDONED) 
		{
			cout << "Wait abandoned\n";
			bDone = true;
		}
		else if (dwResult == WAIT_TIMEOUT) 
		{
			cout << "Timeout waiting for buffer\n";
			bDone = true;
		}
		else if (dwResult == WAIT_FAILED) 
		{
			cout << "Wait failed. Error: 0x" << std::hex << GetLastError() << std::dec << "\n";
			bDone = true;
		}
		ResetEvent(audioEvent);
	}

	waveOutUnprepareHeader(audioDevice, &headers[0], 0);
	waveOutUnprepareHeader(audioDevice, &headers[1], 0);

	cout << "Exiting\n";
}
