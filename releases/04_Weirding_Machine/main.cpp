#include "ComputerCard.h"

#define SHIFT_REG_SIZE 6
#define RUNGLER_DAC_BITS 3
#define RIGHT 1
#define LEFT 0

// 12 bit random number generator
uint32_t __not_in_flash_func(rnd12)()
{
	static uint32_t lcg_seed = 1;
	lcg_seed = 1664525 * lcg_seed + 1013904223;
	return lcg_seed >> 20;
}

/// Weirding Machine
class WeirdingMachine : public ComputerCard
{
public:
	WeirdingMachine()
	{
		// CONSTRUCTOR
		for (int i = 0; i < SHIFT_REG_SIZE; i++)
		{
			bits[i] = true;
		}
	}

	virtual void ProcessSample()
	{
		int16_t runglerOut = 0;

		mainClock = PulseIn1RisingEdge();

		mainKnob = KnobVal(Knob::Main);

		if(mainKnob < 15)
		{
			mainKnob = 0;
		}
		else if (mainKnob > 4095 - 15)
		{
			mainKnob = 4095;
		}

		vca = KnobVal(Knob::Y);

		if (mainClock)
		{
			if (Connected(Input::Audio1))
			{
				data = AudioIn1() + 2048; // Convert to 0-4095
			}
			else
			{
				data = rnd12();
				clip(data, 1, 4094);
			}

			comparator = data > mainKnob;

			rotate(bits, RIGHT);

			if (comparator)
			{
				bits[0] = !bits[0]; // Toggle the first bit
			}
			else
			{
				//DANGER ZOOOOONE
				int dummy = 9;
			}
		}

		for (int i = SHIFT_REG_SIZE - RUNGLER_DAC_BITS - 1; i < SHIFT_REG_SIZE; i++)
		{
			runglerOut |= (bits[i] << (i - (SHIFT_REG_SIZE - RUNGLER_DAC_BITS)));
		}

		// convert 3 bit output to 12 bit values between -2048 and 2047
		runglerOut = (runglerOut << (12 - RUNGLER_DAC_BITS));

		// Attenuate the output based on the vca value
		runglerOut = (runglerOut * vca) >> 12;

		runglerOut -= 2048; // Convert to -2048 to 2047

		// Output rungler signal
		AudioOut1(runglerOut);
		AudioOut2(runglerOut);

		//TODO Output CV signals

		//Output pulse signals
		PulseOut1(bits[SHIFT_REG_SIZE - 2]);
		PulseOut2(bits[SHIFT_REG_SIZE - 1]);

		// show shiftreg state on LEDs
		for (int i = 0; i < 6; i++)
		{
			LedBrightness(i, (bits[i] ? KnobVal(Knob::Y) : 0) * 4095 >> 12);
		}
	}

private:
	bool bits[SHIFT_REG_SIZE];
	bool mainClock = false;
	int16_t mainKnob;
	int16_t data;
	int16_t vca = 0;
	bool comparator;

	void rotate(bool *array, bool direction)
	{
		if (direction) // Rotate right
		{
			bool lastBit = array[SHIFT_REG_SIZE - 1];

			for (int i = SHIFT_REG_SIZE - 1; i > 0; i--)
			{
				array[i] = array[i - 1];
			}
			array[0] = lastBit;
		}
		else // Rotate left
		{
			bool firstBit = array[0];

			for (int i = 0; i < SHIFT_REG_SIZE - 1; i++)
			{
				array[i] = array[i + 1];
			}
			array[SHIFT_REG_SIZE - 1] = firstBit;
		}
	}

	void clip(int16_t &value, int16_t min, int16_t max)
	{
		if (value < min)
		{
			value = min;
		}
		else if (value > max)
		{
			value = max;
		}
	}
};

int main()
{
	WeirdingMachine wm;
	wm.EnableNormalisationProbe();
	wm.Run();
}
