#include "ComputerCard.h"
#include "quantiser.h"

#define BUFFER_SIZE 22050

#define SEMI_TONE static_cast<int32_t>(1 / 12) << 12
#define WHOLE_TONE static_cast<int32_t>(2 / 12) << 12

uint32_t __not_in_flash_func(rnd12)()
{
	static uint32_t lcg_seed = 1;
	lcg_seed = 1664525 * lcg_seed + 1013904223;
	return lcg_seed >> 20;
}

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
	};

	virtual void ProcessSample()
	{

		int16_t noise = rnd12() - 2048;
		Switch s = SwitchVal();

		int32_t x = KnobVal(Knob::X);
		int32_t y = KnobVal(Knob::Y);
		int32_t main = KnobVal(Knob::Main);

		int16_t cvMix = 0;
		int16_t thing1 = 0;
		int16_t thing2 = 0;
		bool clockPulse = false;
		bool trigL = false;
		bool trigR = false;

		// Read inputs
		int16_t cv1 = CVIn1();		 // -2048 to 2047
		int16_t cv2 = CVIn2();		 // -2048 to 2047
		int16_t audioL = AudioIn1(); // -2048 to 2047
		int16_t audioR = AudioIn2(); // -2048 to 2047


		// CROSSFADING CV MIXER WITH INPUTS NORMALED TO NOISE AND VOLTAGE OFFSET

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
		cvMix = (thing1 * (4095 - main)) + (thing2 * main) >> 12;

		CVOut1(cvMix);

		////INTERNAL CLOCK AND RANDOM CLOCK SKIP / SWITCHED GATE

		clockRate = ((4095 - y) * BUFFER_SIZE * 2 + 50) >> 12;

		if (Connected(Input::Pulse1))
		{
			// connected

			if (PulseIn1RisingEdge())
			{
				CVOut2MIDINote(quantSample(cvMix));
				if (cvMix > (y - 2048))
				{
					PulseOut2(true);
					trigR = true;
					pulseTimer2 = 200;
					LedOn(5, true);
				}
				else
				{
					pulseTimer1 = 200;
					PulseOut1(true);
					trigL = true;
					LedOn(4, true);
				}
			};
		}
		else
		{
			// not connected, use internal clock

			clock++;
			if (clock > clockRate)
			{
				clock = 0;
				PulseOut1(true);
				trigL = true;
				LedOn(4, true);
				pulseTimer1 = 200;
				CVOut2MIDINote(quantSample(cvMix));
				if (cvMix > (y - 2048))
				{
					PulseOut2(true);
					trigR = true;
					pulseTimer2 = 200;
					LedOn(5, true);
				}
			}
		};

		// If a pulse is ongoing, keep counting until it ends
		if (pulseTimer1)
		{
			pulseTimer1--;
			if (pulseTimer1 == 0) // pulse ends
			{
				PulseOut1(false);
				LedOff(4);
			}
		};

		// If a pulse is ongoing, keep counting until it ends
		if (pulseTimer2)
		{
			pulseTimer2--;
			if (pulseTimer2 == 0) // pulse ends
			{
				PulseOut2(false);
				LedOff(5);
			}
		};

		// BUFFERS LOOPS/DELAYSSS

		startPos = x * BUFFER_SIZE >> 12;

		if (PulseIn2RisingEdge())
		{
			readIndexL = startPos;
			readIndexR = startPos;
		};

		

		int incr = 2048 - (main>>1) + 40;

		if (s == Switch::Down)
		{
			// reset clock, stop recording, playback, loop at the end of the delay
		}
		else if (s == Switch::Up)
		{
			// play the loop, ignore the input
		}
		else if (s == Switch::Middle)
		{
			// record the loop, output audio delayed (patch your own feedback)
			sampleRamp += incr;

			if (sampleRamp > 8192)
			{
				sampleRamp -= 8192;
				// Calculate new sample for outputsssss
				writeSamples(audioL, audioR, cv1, cv2, trigL, trigR);
				readIndexL = (writeIndexL - clockRate + (BUFFER_SIZE * 2)) % BUFFER_SIZE;
				readIndexR = (writeIndexR - clockRate + (BUFFER_SIZE * 2)) % BUFFER_SIZE;
				AudioOut1(delayBufferL[readIndexL]);
				AudioOut2(delayBufferR[readIndexR]);
				writeIndexL = (writeIndexL + 1) % BUFFER_SIZE;
				writeIndexR = (writeIndexR + 1) % BUFFER_SIZE;
			}
		}
	};

private:
	int16_t delayBufferL[BUFFER_SIZE] = {0};
	int16_t delayBufferR[BUFFER_SIZE] = {0};
	int16_t cvBufferL[BUFFER_SIZE] = {0};
	int16_t cvBufferR[BUFFER_SIZE] = {0};
	bool triggerBufferL[BUFFER_SIZE] = {0};
	bool triggerBufferR[BUFFER_SIZE] = {0};
	int clockRate;
	int clock;
	int pulseTimer1 = 200;
	int pulseTimer2;
	bool clockPulse = false;
	int sampleRamp;
	int loopRamp;
	int readIndexL = 0;
	int readIndexR = 0;
	int writeIndexL = 0;
	int writeIndexR = 0;
	int startPos = 0;

	void writeSamples(int16_t audioL, int16_t audioR, int16_t cv1, int16_t cv2, bool trigL, bool trigR)
	{
		// write samples to buffers
		delayBufferL[writeIndexL] = audioL;
		delayBufferR[writeIndexR] = audioR;
		cvBufferL[writeIndexL] = cv1;
		cvBufferR[writeIndexR] = cv2;
		triggerBufferL[writeIndexL] = trigL;
		triggerBufferR[writeIndexR] = trigR;
	};
};

int main()
{
	Goldfish gf;
	gf.EnableNormalisationProbe();
	gf.Run();
}
