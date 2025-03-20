#include "ComputerCard.h"

#define SHIFT_REG_SIZE 6
#define RUNGLER_DAC_BITS 3

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
			xorBits[i] = true;
		}
	}

	virtual void ProcessSample()
	{
		int16_t runglerOut = 0;
		int16_t xorOut = 0;

		mainClock = PulseIn1RisingEdge();

		if (Connected(Input::Pulse2))
		{
			xorClock = PulseIn2RisingEdge();
		}
		else
		{
			xorClock = mainClock;
		}

		mainKnob = KnobVal(Main);

		if (Connected(Input::Audio2))
		{
			vca = AudioIn2() + 2048;	  // Convert to 0-4095
			vca = vca * KnobVal(Y) >> 12; // Attenuate with Y knob
		}
		else
		{
			vca = KnobVal(Y);
		}

		if (mainClock || xorClock)
		{
			if (Connected(Input::Audio1))
			{
				data = AudioIn1() + 2048; // Convert to 0-4095
			}
			else
			{
				data = rnd12();
			}

			comparator = data > mainKnob;
		}

		if (mainClock)
		{
			// Shift the main bits
			rotate(bits);

			if (comparator)
			{
				bits[0] = !bits[0]; // Toggle the first bit
			}
		}

		if (xorClock)
		{
			// Shift the xor bits
			rotate(xorBits);

			if (comparator)
			{
				if (!bits[SHIFT_REG_SIZE - 1])
				{
					xorBits[0] = !xorBits[0]; // Toggle the first bit
				}
			}
			else
			{
				if (bits[SHIFT_REG_SIZE - 1])
				{
					xorBits[0] = !xorBits[0]; // Toggle the first bit
				}
			}
		}

		for (int i = SHIFT_REG_SIZE - RUNGLER_DAC_BITS - 1; i < SHIFT_REG_SIZE; i++)
		{
			runglerOut |= (bits[i] << (i - (SHIFT_REG_SIZE - RUNGLER_DAC_BITS)));
			xorOut |= (xorBits[i] << (i - (SHIFT_REG_SIZE - RUNGLER_DAC_BITS)));
		}

		// convert 3 bit outputs to 12 bit values between -2048 and 2047
		runglerOut = (runglerOut << (12 - RUNGLER_DAC_BITS));
		xorOut = (xorOut << (12 - RUNGLER_DAC_BITS));

		// Attenuate the outputs based on the vca value
		runglerOut = (runglerOut * vca) >> 12;
		xorOut = (xorOut * vca) >> 12;

		runglerOut -= 2048; // Convert to -2048 to 2047
		xorOut -= 2048;		// Convert to -2048 to 2047

		// Output rungler signals
		AudioOut1(runglerOut);
		AudioOut2(xorOut);

		//Output CV signals

		//Output pulse signals
		PulseOut1(bits[SHIFT_REG_SIZE - 1]);
		PulseOut2(xorBits[SHIFT_REG_SIZE - 1]);

		// show shiftreg state on LEDs
		for (int i = 0; i < 6; i++)
		{
			bool *selectBits = (Connected(Input::Pulse2) && !Connected(Input::Pulse1)) ? xorBits : bits;
			LedBrightness(ledMap[i], (selectBits[i] ? KnobVal(Y) : 0) * 4095 >> 12);
		}
	}

private:
	bool bits[SHIFT_REG_SIZE];
	bool xorBits[SHIFT_REG_SIZE];
	bool mainClock = false;
	bool xorClock = false;
	int16_t mainKnob;
	int16_t data;
	int8_t ledMap[6] = {0, 2, 4, 1, 3, 5};
	int16_t vca = 0;
	bool comparator;

	void rotate(bool *array)
	{
		// Shift the bits to the right
		bool lastBit = array[SHIFT_REG_SIZE - 1];

		for (int i = SHIFT_REG_SIZE - 1; i > 0; i--)
		{
			array[i] = array[i - 1];
		}
		// Set the first bit to the new value
		array[0] = lastBit;
	}
};

int main()
{
	WeirdingMachine wm;
	wm.EnableNormalisationProbe();
	wm.Run();
}
