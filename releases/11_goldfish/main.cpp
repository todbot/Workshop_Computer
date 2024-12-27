#include "ComputerCard.h"
#include "quantiser.h"

#define BUFFER_SIZE 44100

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
	Goldfish() {
		// constructor
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

		// Read inputs
		int16_t cv1 = CVIn1();		 // -2048 to 2047
		int16_t cv2 = CVIn2();		 // -2048 to 2047
		int16_t audioL = AudioIn1(); // -2048 to 2047
		int16_t audioR = AudioIn2(); // -2048 to 2047

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
			thing2 = y>>1;
		};

		// simple crossfade
		cvMix = (thing1 * (4095 - main)) + (thing2 * main) >> 12;

		CVOut1(cvMix);

		clockRate = ((4095 - x) * 48000 * 2 + 50) >> 12;

		clock++;
		if (clock > clockRate)
		{
			clock = 0;
			PulseOut1(true);
			LedOn(4, true);
			pulseTimer1 = 200;
			clockPulse = true;
		};

		if ((cvMix > (y - 2048)) && (clockPulse || PulseIn1RisingEdge()))
		{
			PulseOut2(true);
			pulseTimer2 = 200;
			LedOn(5, true);
		};

		if (Connected(Input::Pulse1))
		{
			// connected
			if (PulseIn1RisingEdge())
			{
				CVOut2MIDINote(quantSample(cvMix));
				pulseTimer1 = 200;
			};
		}
		else
		{
			// not connected
			if (clockPulse)
			{
				CVOut2MIDINote(quantSample(cvMix));
			}
		}

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
	};

private:
	int16_t audioBufferL[BUFFER_SIZE] = {0};
	int16_t audioBufferR[BUFFER_SIZE] = {0};
	int clockRate;
	int clock = 0;
	int pulseTimer1 = 200;
	int pulseTimer2;
	bool clockPulse = false;
};

int main()
{
	Goldfish gf;
	gf.EnableNormalisationProbe();
	gf.Run();
}
