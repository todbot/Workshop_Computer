#include "ComputerCard.h"
#include <cmath>
#include <stdio.h>
#include <vector>

////////////////////////////////////////
// A very simple WAV file parsing xclass.
//
// Takes a WAV file mapped into memory (e.g. in flash)
// and gives an interface to the samples and basic format info
// Currently hard-coded to 16-bit mono files
//
// Will crash if sample data is not halfword (2-byte) aligned

class WAVFile
{
private:

	// Read 16- and 32-bit little-endian values from a buffer
	// (Can't just cast, as may not be aligned correctly)
	static uint16_t GetU16(uint8_t *&ptr)
	{
		uint16_t a1=*ptr++;
		uint16_t a2=*ptr++;
		return (a2<<8)+a1;
	}

	static uint32_t GetU32(uint8_t *&ptr)
	{
		uint32_t a1=*ptr++;
		uint32_t a2=*ptr++;
		uint32_t a3=*ptr++;
		uint32_t a4=*ptr++;
		return (a4<<24)+(a3<<16)+(a2<<8)+a1;
	}

public:
	WAVFile()
	{
		dataptr = nullptr;
		numSamples = 0;
		sampleRate = 0;
		fileSize = 0;
	}

	int16_t operator[](uint32_t index)
	{
		if (dataptr && index<numSamples)
		{
			return dataptr[index];
		}
		else
		{
			return 0;
		}
	}

	int32_t SampleRate() {return sampleRate;}
	int32_t FileSize() {return fileSize;}
	int32_t NumSamples() {return numSamples;}
	
	int Load(uint8_t *startptr)
	{
		uint8_t *ptr = startptr;
		uint32_t riffMarker = GetU32(ptr);
		if (riffMarker != 0x46464952) return 1;
	
		fileSize = GetU32(ptr) + 8;
		
		uint32_t waveMarker = GetU32(ptr);
		if (waveMarker != 0x45564157) return 2;

		uint32_t fmtMarker = GetU32(ptr);
		if (fmtMarker != 0x20746d66) return 3;

		uint32_t subChunkSize = GetU32(ptr);

		uint16_t audioFormat = GetU16(ptr);
		if (audioFormat != 1) return 4; // must be mono

		uint16_t numChannels = GetU16(ptr);

		sampleRate = GetU32(ptr);

		ptr += 6; // skip 'byte rate' and 'block align' fields
	
		uint16_t bitsPerSample  = GetU16(ptr);
		if (bitsPerSample != 16) return 5; // must be 16-bit

		printf("subchunksize=%ld \n",subChunkSize);
		// skip any 'extra params' which may or may not be in the format chunk
		if (subChunkSize>16) {ptr += subChunkSize-16;}
	
		uint32_t dataMarker = GetU32(ptr);
		if (dataMarker != 0x61746164) return 6;

		numSamples = GetU32(ptr) / numChannels / (bitsPerSample/8);

		// Assumes wave data is 2-byte aligned!
		// This will be the case for simple WAV files, 
		// but likely will cause a crash for some files
		dataptr = (int16_t*)ptr;

		return 0;
	}
	
	int16_t *dataptr;
	uint32_t numSamples;
	uint32_t sampleRate;
	uint32_t fileSize;
};


////////////////////////////////////////
// Card that loads and plays WAV files from flash memory
//
// WAV files must all be 16-bit mono, and are played at 48k samples per second,
// regardless of original sample rate.
class SampleUpload : public ComputerCard
{
public:
	
	SampleUpload()
	{
		sampleIndex = 0;
		numFiles = 0;
		currentFile = 0;
		LoadWAVsFromFlash();
	}

	// Load WAV files uploaded to the RP2040 using a UF2 from the generate_sample_uf2.html page
	int LoadWAVsFromFlash()
	{
		// Look in last 256-byte block of flash to get start address and number of WAV files
		uint32_t *wavStart = ((uint32_t *)(XIP_BASE+ PICO_FLASH_SIZE_BYTES - 256 )); 
		uint32_t wavStartAddress = *wavStart;
		wavStart++;
		numFiles = *wavStart;

		// If an invalid number of files, probably no sample UF2 was uploaded
		if (numFiles == 0 || numFiles > 256)
		{
			return -1;
		}
		
		// If start address is not in the right range, probably no sample UF2 was uploaded
		if (wavStartAddress < XIP_BASE || wavStartAddress >= XIP_BASE + PICO_FLASH_SIZE_BYTES)
		{
			return -1;
		}

		// With sensible-looking data in the last page, create list of wav files
		wavfiles = std::vector<WAVFile>(numFiles);

		// Loop through wav files, processing each in turn
		uint8_t *wavptr = ((uint8_t *)(wavStartAddress));
		for (int i=0; i<numFiles; i++)
		{
			int res = wavfiles[i].Load(wavptr);
			if (res) // if this load failed, give up here
			{
				numFiles = i;
				return -2;
			}
			else
			{
				// Increment file start pointer to next wav file
				wavptr += wavfiles[i].FileSize();
			}
		}

		return 0; // success
	}
	
	virtual void ProcessSample()
	{
		// If we failed to load any WAV files, turn on bottom right LED
		if (numFiles == 0)
		{
			LedOn(5);
			return;
		}

		// Play WAV files in sequence
		AudioOut1(wavfiles[currentFile][sampleIndex]);
		sampleIndex++;
		if (sampleIndex >= wavfiles[currentFile].NumSamples())
		{
			sampleIndex = 0;
			currentFile++;
			if (currentFile >= numFiles)
			{
				currentFile = 0;
			}
		}
	}

private:
	uint32_t sampleIndex;

	int numFiles, currentFile;
	std::vector<WAVFile> wavfiles;
};


int main()
{
	SampleUpload su;
	su.Run();
}

  
