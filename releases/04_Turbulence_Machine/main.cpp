#include "ComputerCard.h"
#include "quantiser.h"

#define SHIFT_REG_SIZE 6
#define RUNGLER_DAC_BITS 3
#define RUNGLER_DAC_CELLS 2
#define FWD 1
#define BACK 0

// 12 bit random number generator
uint32_t __not_in_flash_func(rnd12)()
{
	static uint32_t lcg_seed = 1;
	lcg_seed = 1664525 * lcg_seed + 1013904223;
	return lcg_seed >> 20;
}

/// Weirding Machine
class TurbulenceMachine : public ComputerCard
{
public:
	TurbulenceMachine()
	{
		// CONSTRUCTOR
		for (int i = 0; i < SHIFT_REG_SIZE; i++)
		{
			bits[i] = 0;
		}
	}

	virtual void ProcessSample()
	{
		int16_t runglerOut1 = 0;
		int16_t runglerOut2 = 0;
		Switch sw = SwitchVal();

		fwdClock = PulseIn1RisingEdge();
		backClock = PulseIn2RisingEdge();

		bool theIllusionOfStability = false;

		if (Connected(Input::Audio2))
		{
			turingP = AudioIn2();
			clip(turingP, 0, 2048);
			turingP <<= 1; // Convert to 0-4095
		}
		else
		{
			turingP = KnobVal(Knob::Main);
		}

		clip(turingP, 0, 4095);

		if (turingP < 15)
		{
			turingP = 0;
			theIllusionOfStability = true;
		}
		else if (turingP > 4095 - 15)
		{
			turingP = 4095;
		}

		if (fwdClock)
		{

			rotate(bits, FWD);

			if (sw == Switch::Down)
			{
				bits[0] = 0x3; // Always set the first bit to binary 11 = int 3 = hex 0x3
			}
			else if (sw == Switch::Up || theIllusionOfStability)
			{
				bits[0] = ~bits[0] & 0x3; // Always Toggle the first bit
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

				if (data > turingP)
				{
					bits[0] = ~data & 0x3; // Toggle the first bit
				}
			}

			calcOffset();
			calcVca();
		}

		if (backClock)
		{
			rotate(bits, BACK);

			if (sw == Switch::Down)
			{
				bits[SHIFT_REG_SIZE - 1] = 0x0; // Always set the last bit to 0
			}
			else if (sw == Switch::Up || theIllusionOfStability)
			{
				bits[SHIFT_REG_SIZE - 1] = ~bits[SHIFT_REG_SIZE - 1] & 0x3; // Always Toggle the LAST bit
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

				if (data > turingP)
				{
					bits[SHIFT_REG_SIZE - 1] = ~data & 0x3; // Toggle the last bit
				}
			}
			calcOffset();
			calcVca();
		}

		for (int i = 0; i < RUNGLER_DAC_BITS; i++)
		{
			runglerOut1 |= ((bits[i] << (2 * i)) & 0x3F);
		}

		for (int i = SHIFT_REG_SIZE - RUNGLER_DAC_BITS - 1; i < SHIFT_REG_SIZE; i++)
		{
			runglerOut2 |= ((bits[i] << (2 * (i - (SHIFT_REG_SIZE - RUNGLER_DAC_BITS)))) & 0x3F);
		}

		// convert 6 bit output to 12 bit values for DAC
		runglerOut1 = (runglerOut1 << (12 - (RUNGLER_DAC_BITS * RUNGLER_DAC_CELLS)));
		runglerOut2 = (runglerOut2 << (12 - (RUNGLER_DAC_BITS * RUNGLER_DAC_CELLS)));

		// Attenuate the output based on the vca value
		runglerOut1 = (runglerOut1 * vca) >> 12;
		runglerOut2 = (runglerOut2 * vca) >> 12;

		// Add offset to the outputs
		runglerOut1 += offset;
		runglerOut2 += offset;
		runglerOut1 -= 2048; // Convert to -2048 to 2047
		runglerOut2 -= 2048; // Convert to -2048 to 2047
		int16_t quantizedRunglerOut1 = runglerOut1;
		int16_t quantizedRunglerOut2 = runglerOut2;
		runglerOut1 *= -1; // Invert the signal
		runglerOut2 *= -1; // Invert the signal
		clip(runglerOut1, -2048, 2047);
		clip(runglerOut2, -2048, 2047);
		clip(quantizedRunglerOut1, -2048, 2047);
		clip(quantizedRunglerOut2, -2048, 2047);
		quantizedRunglerOut1 = quantSample(quantizedRunglerOut1);
		quantizedRunglerOut2 = quantSample(quantizedRunglerOut2);

		// Output rungler signal
		AudioOut1(runglerOut1);
		AudioOut2(runglerOut2);

		// Output Quantized CV signals
		CVOut1MIDINote(quantizedRunglerOut1);
		CVOut2MIDINote(quantizedRunglerOut2);

		// Output pulse signals
		PulseOut1(bits[SHIFT_REG_SIZE - 4] & 0x1);
		PulseOut2(bits[SHIFT_REG_SIZE - 1] & 0x1);

		// show shiftreg state on LEDs
		for (int i = 0; i < 6; i++)
		{
			LedBrightness(ledMap[i], (bits[i] << 10) * vca >> 12);
		}
	}

private:
	int16_t bits[SHIFT_REG_SIZE];
	bool fwdClock = false;
	bool backClock = false;
	int16_t turingP;
	int16_t data;
	int16_t vca = 0;
	int8_t ledMap[SHIFT_REG_SIZE] = {0, 2, 4, 1, 3, 5};
	int16_t offset = 0;

	void rotate(int16_t *array, bool direction)
	{
		if (direction) // Rotate right
		{
			int16_t lastBit = array[SHIFT_REG_SIZE - 1];

			for (int i = SHIFT_REG_SIZE - 1; i > 0; i--)
			{
				array[i] = array[i - 1];
			}
			array[0] = lastBit;
		}
		else // Rotate left
		{
			int16_t firstBit = array[0];

			for (int i = 0; i < SHIFT_REG_SIZE - 1; i++)
			{
				array[i] = array[i + 1];
			}
			array[SHIFT_REG_SIZE - 1] = firstBit;
		}
	}

	void calcOffset()
	{
		if (Connected(Input::CV1))
		{
			offset = CVIn1() * (KnobVal(Knob::X) - 2048) >> 12;
			offset += 2048; // Convert to 0-4095
		}
		else
		{
			offset = KnobVal(Knob::X);
		}
	}

	void calcVca()
	{
		if (Connected(Input::CV2))
		{
			vca = CVIn2() * KnobVal(Knob::Y) >> 12;
			;
			clip(vca, 0, 2048);
			vca <<= 1; // Convert to 0-4095
		}
		else
		{
			vca = KnobVal(Knob::Y);
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
	TurbulenceMachine turb;
	turb.EnableNormalisationProbe();
	turb.Run();
}
