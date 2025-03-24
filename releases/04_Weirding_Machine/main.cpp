#include "ComputerCard.h"

#define SHIFT_REG_SIZE 6
#define RUNGLER_DAC_BITS 3
#define RIGHT true

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
			bits[i] = 1;
		}
	}

	virtual void ProcessSample()
	{
		int16_t runglerOut = 0;
		Switch sw = SwitchVal();

		fwdClock = PulseIn1RisingEdge();
		backClock = PulseIn2RisingEdge();


		if (Connected(Input::Audio2))
		{
			turingP = AudioIn2();
		}
		else
		{
			turingP = KnobVal(Knob::Main);
		}

		clip(turingP, 0, 4095);

		if (turingP < 15)
		{
			turingP = 0;
		}
		else if (turingP > 4095 - 15)
		{
			turingP = 4095;
		}

		if (Connected(Input::CV2))
		{
			vca = CVIn2() * KnobVal(Knob::Y) >> 12;;
			clip(vca, 0, 2048);
			vca <<= 1; // Convert to 0-4095
		}
		else
		{
			vca = KnobVal(Knob::Y);
		}

		if (Connected(Input::CV1))
		{
			offset = CVIn1() * (KnobVal(Knob::X)-2048) >> 12;	
			offset += 2048; // Convert to 0-4095
		}
		else
		{
			offset = KnobVal(Knob::X);
		}

		if (fwdClock)
		{

			rotate(bits, direction);

			if (sw == Switch::Down)
			{
				bits[0] = !bits[0]; // Toggle the first bit
			}
			else if (sw == Switch::Up)
			{
				// LEAVE BITS UNCHANGED
			}
			else // sw == Switch::Middle
			{
				if (Connected(Input::Audio1))
				{
					data = AudioIn1() + 2048; // Convert to 0-4095
				}
				else
				{
					data = rnd12();
				}

				clip(data, 1, 4094);

				comparator = data > turingP;

				if (comparator)
				{
					bits[0] = !bits[0]; // Toggle the first bit
				}
			}
		}

		for (int i = SHIFT_REG_SIZE - RUNGLER_DAC_BITS - 1; i < SHIFT_REG_SIZE; i++)
		{
			runglerOut |= (bits[i] << (i - (SHIFT_REG_SIZE - RUNGLER_DAC_BITS)));
		}

		// convert 3 bit output to 12 bit values between -2048 and 2047
		runglerOut = (runglerOut << (12 - RUNGLER_DAC_BITS));

		// Attenuate the output based on the vca value
		runglerOut = (runglerOut * vca) >> 13;

		// Add offset to the output
		runglerOut += offset;
		runglerOut -= 2048; // Convert to -2048 to 2047
		runglerOut *= -1; // Invert the signal
		clip(runglerOut, -2048, 2047);

		// Output rungler signal
		AudioOut1(runglerOut);
		AudioOut2(runglerOut);

		// TODO Output CV signals

		// Output pulse signals
		PulseOut1(bits[SHIFT_REG_SIZE - 4]);
		PulseOut2(bits[SHIFT_REG_SIZE - 1]);

		// show shiftreg state on LEDs
		for (int i = 0; i < 6; i++)
		{
			LedBrightness(ledMap[i], (bits[i] ? vca : 0) * 4095 >> 12);
		}
	}

private:
	bool bits[SHIFT_REG_SIZE];
	bool fwdClock = false;
	bool backClock = false;
	int16_t turingP;
	int16_t data;
	int16_t vca = 0;
	bool comparator;
	int8_t ledMap[SHIFT_REG_SIZE] = {0, 2, 4, 1, 3, 5};
	bool direction = RIGHT;
	int16_t offset = 0;

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
