#include "ComputerCard.h"
#include "quantiser.h"

#define BUFFER_SIZE 48000
#define SMALL_BUFFER_SIZE 24000

bool tmp;

uint32_t __not_in_flash_func(rnd12)()
{
	static uint32_t lcg_seed = 1;
	lcg_seed = 1664525 * lcg_seed + 1013904223;
	return lcg_seed >> 20;
}

bool __not_in_flash_func(zeroCrossing)(int16_t a, int16_t b)
{
	return (a < 0 && b >= 0) || (a >= 0 && b < 0);
};

/// Goldfish
class Goldfish : public ComputerCard
{
public:
	Goldfish()
	{
		// constructor
		sampleRamp = 0;
		loopRamp = 0;
		clockRate = 0;
		runMode = SwitchVal() == Switch::Middle ? PLAY : DELAY;
		startPosAudio = 0;
		startPosControl = 0;
	};

	virtual void ProcessSample()
	{

		int16_t noise = rnd12() - 2048;
		Switch s = SwitchVal();

		x = KnobVal(Knob::X);
		y = KnobVal(Knob::Y);
		main = KnobVal(Knob::Main);

		int lowPassX = (lastLowPassX * 15 + x) >> 4;
		int lowPassMain = (lastLowPassMain * 15 + main) >> 4;
		lastLowPassX = lowPassX;
		lastLowPassMain = lowPassMain;

		bool clockPulse = false;
		bool randPulse = false;

		// Read inputs
		cv1 = CVIn1();				 // -2048 to 2047
		cv2 = CVIn2();				 // -2048 to 2047
		int16_t audioL = AudioIn1(); // -2048 to 2047
		int16_t audioR = AudioIn2(); // -2048 to 2047

		int16_t qSample;

		int16_t lastSampleL = 0;
		int16_t lastSampleR = 0;

		// BUFFERS LOOPS/DELAYSSS

		if ((s == Switch::Down) && (lastSwitchVal != Switch::Down))
		{
			runMode = RECORD;
			audioLoopLength = 0;
			cvLoopLength = 0;
			audioWriteIndexL = 0;
			audioWriteIndexR = 0;
			controlWriteIndex = 0;
		}
		else if ((s == Switch::Up) && (lastSwitchVal != Switch::Up))
		{
			runMode = DELAY;
		}
		else if ((s == Switch::Middle) && (lastSwitchVal != Switch::Middle))
		{
			runMode = PLAY;
			loopRamp = 0;
			audioReadIndexL = 0;
			audioReadIndexR = 0;
			controlReadIndex = 0;
		};

		lastSwitchVal = s;

		////INTERNAL CLOCK AND RANDOM CLOCK SKIP / SWITCHED GATE

		clockRate = ((4095 - lowPassX) * BUFFER_SIZE * 2 + 50) >> 12;

		cvMix = calcCVMix(noise);

		clock++;
		if (clock > clockRate)
		{
			clock = 0;
			clockPulse = true;
		};

		if (noise > y - 2048)
		{
			randPulse = true;
		};

		incr = ((4095 - lowPassMain) * 2048 >> 12) + 64;

		switch (runMode)
		{
		case DELAY:
		{
			// record the loop, output audio delayed (patch your own feedback) and output normal cv mix signals

			qSample = quantSample(cvMix);

			CVOut1(cvMix);

			sampleRamp += incr;

			if (sampleRamp > 8192)
			{
				sampleRamp -= 8192;
				// Calculate new sample for outputsssss
				audioBufferL[audioWriteIndexL] = audioL;
				audioBufferR[audioWriteIndexR] = audioR;
				audioReadIndexL = (audioWriteIndexL - clockRate + (BUFFER_SIZE * 2)) % BUFFER_SIZE;
				audioReadIndexR = (audioWriteIndexR - clockRate + (BUFFER_SIZE * 2)) % BUFFER_SIZE;
				AudioOut1(audioBufferL[audioReadIndexL]);
				AudioOut2(audioBufferR[audioReadIndexR]);
				audioWriteIndexL = (audioWriteIndexL + 1) % BUFFER_SIZE;
				audioWriteIndexR = (audioWriteIndexR + 1) % BUFFER_SIZE;
			};

			break;
		}
		case RECORD:
		{
			// reset clock, stop recording, playback, loop at the end of the delay buffer
			qSample = quantSample(cvMix);

			if ((!Connected(Input::Pulse1) && clockPulse) || (Connected(Input::Pulse1) && PulseIn1RisingEdge()))
			{
				CVOut2MIDINote(qSample);
				LedOn(4, true);
				pulseTimer1 = 200;

				if (randPulse)
				{
					PulseOut2(true);
					LedOn(5, true);
					pulseTimer2 = 200;
				};
			};

			sampleRamp += incr;

			if (sampleRamp > 8192)
			{
				sampleRamp -= 8192;
				cvMix = calcCVMix(noise);
				audioBufferL[audioWriteIndexL] = audioL;
				audioBufferR[audioWriteIndexR] = audioR;
				AudioOut1(audioL);
				AudioOut2(audioR);

				cvBuffer[controlWriteIndex] = cvMix;
				CVOut1(cvMix);

				audioWriteIndexL++;
				audioWriteIndexR++;

				if (audioWriteIndexL >= BUFFER_SIZE)
				{
					audioWriteIndexL = 0;
				} else audioLoopLength++;

				controlWriteIndex++;
				if (controlWriteIndex >= SMALL_BUFFER_SIZE)
				{
					controlWriteIndex = 0;
				} else cvLoopLength++;

				
			}

			break;
		}
		case PLAY:
		{
			// play the loop, ignore the input

			qSample = quantSample(cvBuffer[controlReadIndex]);
			lastSampleL = audioBufferL[audioReadIndexL];
			lastSampleR = audioBufferR[audioReadIndexR];

			sampleRamp += incr;

			if (sampleRamp > 8192)
			{
				sampleRamp -= 8192;

				AudioOut1(audioBufferL[audioReadIndexL]);
				AudioOut2(audioBufferR[audioReadIndexR]);

				CVOut1(cvBuffer[controlReadIndex]);

				

				if (Connected(Input::Pulse2))
				{
					startPosAudio = (cvMix + 2048) * (BUFFER_SIZE - 1) >> 12;
					startPosControl = (cvMix + 2048) * (SMALL_BUFFER_SIZE - 1) >> 12;
				}
				else
				{
					startPosAudio = 0;
					startPosControl = 0;
				};

				audioReadIndexL++;
				audioReadIndexR++;

				if (audioReadIndexL >= BUFFER_SIZE)
				{
					audioReadIndexL = 0;
				};

				if (audioReadIndexR >= BUFFER_SIZE)
				{
					audioReadIndexR = 0;
				};

				controlReadIndex++;
				if (controlReadIndex >= SMALL_BUFFER_SIZE)
				{
					controlReadIndex = 0;
				};

				tmp = zeroCrossing(audioBufferL[audioReadIndexL], lastSampleL) || zeroCrossing(audioBufferR[audioReadIndexR], lastSampleR);

				if (reset && ((Connected(Input::Audio1) && tmp) ||
							  (Connected(Input::Audio2) && tmp)))
				{
					reset = false;
					loopRamp = 0;
					audioReadIndexL = startPosAudio;
					audioReadIndexR = startPosAudio;
					controlReadIndex = startPosControl;
				};

				loopRamp++;

				if (loopRamp >= audioLoopLength)
				{
					loopRamp = 0;
					audioReadIndexL = startPosAudio;
					audioReadIndexR = startPosAudio;
					controlReadIndex = startPosControl;
				};
			}
			break;
		}
		};

		if ((!Connected(Input::Pulse1) && clockPulse) || (Connected(Input::Pulse1) && PulseIn1RisingEdge()))
		{
			CVOut2MIDINote(qSample);
			LedOn(4, true);
			pulseTimer1 = 200;
			PulseOut1(true);
			if (randPulse)
			{
				PulseOut2(true);
				LedOn(5, true);
				pulseTimer2 = 200;
			};
		};

		if (PulseIn2RisingEdge())
		{
			reset = true;
		};

		// If a pulse1 is ongoing, keep counting until it ends
		if (pulseTimer1)
		{
			pulseTimer1--;
			if (pulseTimer1 == 0) // pulse ends
			{
				PulseOut1(false);
				LedOff(4);
			}
		};

		// If a pulse2 is ongoing, keep counting until it ends
		if (pulseTimer2)
		{
			pulseTimer2--;
			if (pulseTimer2 == 0) // pulse ends
			{
				PulseOut2(false);
				LedOff(5);
			}
		};
	};

private:
	int16_t audioBufferL[BUFFER_SIZE] = {0};
	int16_t audioBufferR[BUFFER_SIZE] = {0};
	int16_t cvBuffer[SMALL_BUFFER_SIZE] = {0};
	int clockRate;
	int clock;
	int pulseTimer1 = 200;
	int pulseTimer2;
	bool clockPulse = false;
	int sampleRamp;
	int loopRamp;
	uint32_t audioReadIndexL = 0;
	uint32_t audioReadIndexR = 0;
	uint32_t controlReadIndex = 0;
	uint32_t controlWriteIndex = 0;
	uint32_t audioWriteIndexL = 0;
	uint32_t audioWriteIndexR = 0;
	uint32_t startPosAudio;
	uint32_t startPosControl;
	int lastLowPassX = 0;
	int lastLowPassMain = 0;
	int lastCVmix = 0;
	int incr;
	int audioLoopLength;
	int cvLoopLength;
	bool reset = false;

	Switch lastSwitchVal;
	int x;
	int y;
	int main;
	int16_t cv1;
	int16_t cv2;
	int16_t cvMix;

	enum RunMode
	{
		RECORD,
		DELAY,
		PLAY
	} runMode;

	int16_t calcCVMix(int16_t noise)
	{
		int16_t result = 0;
		int16_t thing1 = 0;
		int16_t thing2 = 0;

		if (Connected(Input::CV1) && Connected(Input::CV2))
		{
			thing1 = cv1 * x >> 12;
			thing2 = cv2 * y >> 12;
		}
		else if (Connected(Input::CV1))
		{
			thing1 = cv1 * x >> 12;
			thing2 = y >> 1;
		}
		else if (Connected(Input::CV2))
		{
			thing1 = noise * x >> 12;
			thing2 = cv2 * y >> 12;
		}
		else
		{
			thing1 = noise * x >> 12;
			thing2 = y >> 1;
		};

		// simple crossfade
		result = (thing1 * (4095 - main)) + (thing2 * main) >> 12;
		return result;
	};
};

int main()
{
	Goldfish gf;
	gf.EnableNormalisationProbe();
	gf.Run();
}
