#include "ComputerCard.h"

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
		//CONSTRUCTOR
		// Set up the shift register
		vShiftReg = 0;
	}

	virtual void ProcessSample()
	{
		AudioOut1(rnd12() - 2048);
	}

	void clip(int32_t &a)
    {
        if (a < -2047)
            a = -2047;
        if (a > 2047)
            a = 2047;
    };

private:

int8_t vShiftReg;

};


int main()
{
	WeirdingMachine wm;
	wm.EnableNormalisationProbe();
	wm.Run();
}

  
