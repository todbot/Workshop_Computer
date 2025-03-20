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
			bits[i] = false;
			xorBits[i] = false;
		}
	}

	virtual void ProcessSample()
	{
		int16_t runglerOut = 0;
		int16_t xorOut = 0;
		
		clockTick = PulseIn1RisingEdge();
		mainKnob = KnobVal(Main);

		if (clockTick)
		{
			if (Connected(Input::Audio1))
			{
				data = AudioIn1() + 2048; // Convert to 0-4095
			}
			else
			{
				data = rnd12();
			}
			// Shift the bits
			rotateBits();
			bool comparator = data > mainKnob;

			if (comparator)
			{
				bits[0] = !bits[0]; // Toggle the first bit

				if (!xorBits[SHIFT_REG_SIZE - 1])
				{
					xorBits[0] = !xorBits[0]; // Toggle the first xor bit
				}
			} else
			{
				if (xorBits[SHIFT_REG_SIZE - 1])
				{
					xorBits[0] = !xorBits[0]; // Toggle the first xor bit
				}
			}
		}

		for (int i = SHIFT_REG_SIZE - RUNGLER_DAC_BITS - 1; i < SHIFT_REG_SIZE; i++)
		{
			runglerOut |= (bits[i] << (i - (SHIFT_REG_SIZE - RUNGLER_DAC_BITS)));
			xorOut |= (xorBits[i] << (i - (SHIFT_REG_SIZE - RUNGLER_DAC_BITS)));
		}

		//convert 3 bit outputs to 12 bit values between -2048 and 2047
		runglerOut = (runglerOut << (12 - RUNGLER_DAC_BITS));
		xorOut = (xorOut << (12 - RUNGLER_DAC_BITS));

		//Attenuate the outputs based on the y knob
		runglerOut = (runglerOut * KnobVal(Y)) >> 12;
		xorOut = (xorOut * KnobVal(Y)) >> 12;

		runglerOut -= 2048; // Convert to -2048 to 2047
		xorOut -= 2048; // Convert to -2048 to 2047
	

		//Output rungler signals
		AudioOut1(runglerOut);
		AudioOut2(xorOut);

		// show shiftreg state on LEDs
		for (int i = 0; i < 6; i++)
		{
			LedOn(i, bits[i]);
		}
	}

private:
	bool bits[SHIFT_REG_SIZE];
	bool xorBits[SHIFT_REG_SIZE];
	bool clockTick = false;
	int16_t mainKnob;
	int16_t data;

	void rotateBits()
	{
		// Shift the bits to the right
		bool lastBit = bits[SHIFT_REG_SIZE - 1];
		bool lastXorBit = xorBits[SHIFT_REG_SIZE - 1];

		for (int i = SHIFT_REG_SIZE - 1; i > 0; i--)
		{
			bits[i] = bits[i - 1];
			xorBits[i] = xorBits[i - 1];
		}
		// Set the first bit to the new value
		bits[0] = lastBit;
		xorBits[0] = lastXorBit;
	}
};

int main()
{
	WeirdingMachine wm;
	wm.EnableNormalisationProbe();
	wm.Run();
}
