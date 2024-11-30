#include "ComputerCard.h"

/// Dual sample and hold

/// Updates audio output to take the value of the audio input
/// only when the corresponding pulse input has a rising edge.

/// If audio input not connected, sample random noise instead.

/// If pulse input not connected, update output every sample,
/// (tracking audio input if connected, or producing white noise
/// if audio input not connected.)
class SampleAndHold : public ComputerCard
{
public:
	
	// random number generator
	int32_t rnd()
	{
		static uint32_t lcg_seed = 1;
		lcg_seed = 1664525 * lcg_seed + 1013904223;
		return lcg_seed >> 16;
	}
	
	virtual void ProcessSample()
	{
		// If rising edge on pulse 1, or no pulse 1 jack
		if (PulseIn1RisingEdge() || Disconnected(Input::Pulse1))
		{
			int16_t result;
			
			// If audio in 1 is connected, sample that
			if (Connected(Input::Audio1))
			{
				result = AudioIn1();
			}
			else // otherwise, sample random noise
			{
				result = rnd();
			}
			
			// sent result to audio out 1
			AudioOut1(result);
		}

		
		// Concise repeat of the above, for second channel
		if (PulseIn2RisingEdge() || Disconnected(Input::Pulse2))
		{
			AudioOut2(Connected(Input::Audio2)?AudioIn2():rnd());
		}
	}
};

int main()
{
	SampleAndHold sh;
	sh.EnableNormalisationProbe();
	sh.Run();
}

  
