/*
ComputerCard  - by Chris Johnson

ComputerCard is a header-only C++ library, providing a class that
manages the hardware aspects of the Music Thing Modular Workshop
System Computer.

It aims to present a very simple C++ interface for card programmers 
to use the jacks, knobs, switch and LEDs, for programs running at
a fixed 48kHz audio sample rate.

See examples/ directory
*/


#ifndef COMPUTERCARD_H
#define COMPUTERCARD_H

#include "hardware/gpio.h"
#include "hardware/pwm.h"

#define PULSE_1_RAW_OUT 8
#define PULSE_2_RAW_OUT 9

#define CV_OUT_1 23
#define CV_OUT_2 22

class ComputerCard
{
	constexpr static int numLeds = 6;
	constexpr static uint8_t leds[numLeds] = { 10, 11, 12, 13, 14, 15 };
public:

	/// Knob index, used by KnobVal
	enum Knob {Main, X, Y};
	/// Switch position, used by SwitchVal
	enum Switch {Down, Middle, Up};
	/// Input jack socket, used by Connected and Disconnected
	enum Input {Audio1, Audio2, CV1, CV2, Pulse1, Pulse2};
	/// Hardware version
	enum HardwareVersion {Proto1=0x2a, Proto2_Rev1=0x30, Unknown=0xFF};
	
	ComputerCard();

	/** \brief Start audio processing.

        The Run method starts audio processing, calling ProcessSample using an interrupt.
        Run is a blocking function (it never returns)
	*/
	void Run()
	{
		ComputerCard::thisptr = this;
		AudioWorker();
	}

	/// Use before Run() to enable Connected/Disconnected detection
	void EnableNormalisationProbe() {useNormProbe = true;}
	
	/// Callback, called once per sample at 48kHz
	virtual void ProcessSample() = 0;




	/// Read knob position (returns 0-4095)
	int32_t __not_in_flash_func(KnobVal)(Knob ind) {return knobs[ind];}

	/// Read switch position
	Switch __not_in_flash_func(SwitchVal)() {return static_cast<Switch>((knobs[3]>1000) + (knobs[3]>3000));}


	/// Set Audio 1 output (values -2048 to 2047)
	void __not_in_flash_func(AudioOut1)(int16_t val)
	{
		dacOut[0] = val;
	}
	
	/// Set Audio 2 output (values -2048 to 2047)
	void __not_in_flash_func(AudioOut2)(int16_t val)
	{
		dacOut[1] = val;
	}
	/// Set CV 1 output (values -2048 to 2047)
	void __not_in_flash_func(CVOut1)(int16_t val)
	{
		pwm_set_gpio_level(CV_OUT_1, (2047-val)>>1);
	}
	
	/// Set CV 2 output (values -2048 to 2047)
	void __not_in_flash_func(CVOut2)(int16_t val)
	{
		pwm_set_gpio_level(CV_OUT_2, (2047-val)>>1);
	}


	/// Set CV 1 output (values -2048 to 2047)
	void __not_in_flash_func(CVOut1_calibrated)(int16_t val)
	{
		pwm_set_gpio_level(CV_OUT_1, (2047-val)>>1);
	}
	
	/// Set CV 2 output (values -2048 to 2047)
	void __not_in_flash_func(CVOut2_calibrated)(int16_t val)
	{
		pwm_set_gpio_level(CV_OUT_2, (2047-val)>>1);
	}
	
	/// Set CV 1 output from calibrated MIDI note number (values 0 to 127)
	void __not_in_flash_func(CVOut1MIDINote)(uint8_t noteNum)
	{
		pwm_set_gpio_level(CV_OUT_1, MIDIToDac(noteNum, 0) >> 8);
	}
	
	/// Set CV 2 output from calibrated MIDI note number (values 0 to 127)
	void __not_in_flash_func(CVOut2MIDINote)(uint8_t noteNum)
	{
		pwm_set_gpio_level(CV_OUT_2, MIDIToDac(noteNum, 1) >> 8);
	}
	
	/// Set Pulse 1 output (true = on)
	void __not_in_flash_func(PulseOut1)(bool val)
	{
		gpio_put(PULSE_1_RAW_OUT, !val);
	}
	
	/// Set Pulse 2 output (true = on)
	void __not_in_flash_func(PulseOut2)(bool val)
	{
		gpio_put(PULSE_2_RAW_OUT, !val);
	}
	
	/// Return audio in 1 (-2048 to 2047)
	int16_t __not_in_flash_func(AudioIn1)(){return adcInL;}

	/// Return audio in 1 (-2048 to 2047)
	int16_t __not_in_flash_func(AudioIn2)(){return adcInR;}

	/// Return CV in 1 (-2048 to 2047)
	int16_t __not_in_flash_func(CVIn1)(){return cv[0];}

	/// Return CV in 2 (-2048 to 2047)
	int16_t __not_in_flash_func(CVIn2)(){return cv[1];}

	/// Read pulse in 1
	bool __not_in_flash_func(PulseIn1)(){return pulse[0];}
	/// Return true for one sample on pulse 1 rising edge
	bool __not_in_flash_func(PulseIn1RisingEdge)(){return pulse[0] && !last_pulse[0];}
	/// Return true for one sample on pulse 1 falling edge
	bool __not_in_flash_func(PulseIn1FallingEdge)(){return !pulse[0] && last_pulse[0];}

	/// Read pulse in 2
	bool __not_in_flash_func(PulseIn2)(){return pulse[1];}
	/// Return true for one sample on pulse 2 falling edge
	bool __not_in_flash_func(PulseIn2FallingEdge)(){return !pulse[1] && last_pulse[1];}
	/// Return true for one sample on pulse 2 rising edge
	bool __not_in_flash_func(PulseIn2RisingEdge)(){return pulse[1] && !last_pulse[1];}


	/// Return true if jack connected to input
	bool __not_in_flash_func(Connected)(Input i){return connected[i];}
	/// Return true if no jack connected to input
	bool __not_in_flash_func(Disconnected)(Input i){return !connected[i];}


	/// Set LED brightness, values 0-4095
	// Led numbers are:
	// 0 1
	// 2 3
	// 4 5
	void __not_in_flash_func(LedBrightness)(uint32_t index, uint16_t value)
	{
		pwm_set_gpio_level(leds[index], (value*value)>>8);
	}
	
	/// Turn LED on/off
	void __not_in_flash_func(LedOn)(uint32_t index, bool value = true)
	{
		pwm_set_gpio_level(leds[index], value?65535:0);
	}

	/// Turn LED off
	void __not_in_flash_func(LedOff)(uint32_t index)
	{
		pwm_set_gpio_level(leds[index], 0);
	}

	

	/// Return hardware version
	HardwareVersion GetHardwareVersion() {return hw;}

	/// Return ID number unique to flash card
	uint64_t UniqueCardID()	{return uniqueID;}
	
	static ComputerCard *ThisPtr() {return thisptr;}
private:
	
	typedef struct
	{
		float m, b;
		int32_t mi, bi;
	} CalCoeffs;

	typedef struct
	{
		int32_t dacSetting;
		int8_t voltage;
	} CalPoint;

	static constexpr int calMaxChannels = 2;
	static constexpr int calMaxPoints = 10;
	
	uint8_t numCalibrationPoints[calMaxChannels];
	CalPoint calibrationTable[calMaxChannels][calMaxPoints];
	CalCoeffs calCoeffs[calMaxChannels];

	uint64_t uniqueID;
	
	uint8_t ReadByteFromEEPROM(unsigned int eeAddress);
	int ReadIntFromEEPROM(unsigned int eeAddress);
	uint16_t CRCencode(const uint8_t *data, int length);
	void CalcCalCoeffs(int channel);
	int ReadEEPROM();
	uint32_t MIDIToDac(int midiNote, int channel);
	
	HardwareVersion hw;
	HardwareVersion ProbeHardwareVersion();
	
	int16_t dacOut[2];
	
	volatile int32_t knobs[4] = { 0, 0, 0, 0 }; // 0-4095
	volatile bool pulse[2] = { 0, 0 };
	volatile bool last_pulse[2] = { 0, 0 };
	volatile int32_t cv[2] = { 0, 0 }; // -2047 - 2048
	volatile int16_t adcInL = 0x800, adcInR = 0x800;

	volatile uint8_t mxPos = 0; // external multiplexer value

	volatile int32_t plug_state[6] = {0,0,0,0,0,0};
	volatile bool connected[6] = {0,0,0,0,0,0};
	bool useNormProbe;

	
	volatile uint8_t runADCMode;


// Buffers that DMA reads into / out of
	uint16_t ADC_Buffer[2][8];
	uint16_t SPI_Buffer[2][2];

	uint8_t adc_dma, spi_dma; // DMA ids



	uint8_t dmaPhase = 0;

	// Convert signed int16 value into data string for DAC output
	uint16_t __not_in_flash_func(dacval)(int16_t value, uint16_t dacChannel)
	{
		return (dacChannel | 0x3000) | (((uint16_t)((value & 0x0FFF) + 0x800)) & 0x0FFF);
	}
	uint32_t next_norm_probe();

	void BufferFull();

	void AudioWorker();
	
	static void AudioCallback()
	{
		thisptr->BufferFull();
	}
	static ComputerCard *thisptr;
};


#ifndef COMPUTERCARD_NOIMPL


#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/flash.h"
#include "hardware/i2c.h"
#include "hardware/irq.h"
#include "hardware/spi.h"

// Input normalisation probe pin
#define NORMALISATION_PROBE 4

// Mux pins
#define MX_A 24
#define MX_B 25

// ADC input pins
#define AUDIO_L_IN_1 26
#define AUDIO_R_IN_1 27
#define MUX_IO_1 28
#define MUX_IO_2 29


#define DAC_CHANNEL_A 0x0000
#define DAC_CHANNEL_B 0x8000

#define DAC_CS 21
#define DAC_SCK 18
#define DAC_TX 19

#define EEPROM_SDA 16
#define EEPROM_SCL 17

#define PULSE_1_INPUT 2
#define PULSE_2_INPUT 3

#define DEBUG_1 0
#define DEBUG_2 1

#define SPI_PORT spi0
#define SPI_DREQ DREQ_SPI0_TX


#define BOARD_ID_0 7
#define BOARD_ID_1 6
#define BOARD_ID_2 5

// The ADC (/DMA) run mode, used to stop DMA in a known state before writing to flash
#define RUN_ADC_MODE_RUNNING 0
#define RUN_ADC_MODE_REQUEST_ADC_STOP 1
#define RUN_ADC_MODE_ADC_STOPPED 2
#define RUN_ADC_MODE_REQUEST_ADC_RESTART 3


#define EEPROM_ADDR_ID 0
#define EEPROM_ADDR_VERSION 2
#define EEPROM_ADDR_CRC_L 87
#define EEPROM_ADDR_CRC_H 86
#define EEPROM_VAL_ID 2001
#define EEPROM_NUM_BYTES 88

#define EEPROM_PAGE_ADDRESS 0x50



ComputerCard *ComputerCard::thisptr;

// Return pseudo-random bit for normalisation probe
uint32_t __not_in_flash_func(ComputerCard::next_norm_probe)()
{
	static uint32_t lcg_seed = 1;
	lcg_seed = 1664525 * lcg_seed + 1013904223;
	return lcg_seed >> 31;
}

// Main audio core function
void __not_in_flash_func(ComputerCard::AudioWorker)()
{

	adc_select_input(0);
	adc_set_round_robin(0b0001111U);

	// enabled, with DMA request when FIFO contains data, no erro flag, no byte shift
	adc_fifo_setup(true, true, 1, false, false);


	// ADC clock runs at 48MHz
	// 48MHz ÷ (124+1) = 384kHz ADC sample rate
	//                 = 8×48kHz audio sample rate
	adc_set_clkdiv(124);

	// claim and setup DMAs for reading to ADC, and writing to SPI DAC
	adc_dma = dma_claim_unused_channel(true);
	spi_dma = dma_claim_unused_channel(true);

	dma_channel_config adc_dmacfg, spi_dmacfg;
	adc_dmacfg = dma_channel_get_default_config(adc_dma);
	spi_dmacfg = dma_channel_get_default_config(spi_dma);

	// Reading from ADC into memory buffer, so increment on write, but no increment on read
	channel_config_set_transfer_data_size(&adc_dmacfg, DMA_SIZE_16);
	channel_config_set_read_increment(&adc_dmacfg, false);
	channel_config_set_write_increment(&adc_dmacfg, true);

	// Synchronise ADC DMA the ADC samples
	channel_config_set_dreq(&adc_dmacfg, DREQ_ADC);

	// Setup DMA for 8 ADC samples
	dma_channel_configure(adc_dma, &adc_dmacfg, ADC_Buffer[dmaPhase], &adc_hw->fifo, 8, true);

	// Turn on IRQ for ADC DMA
	dma_channel_set_irq0_enabled(adc_dma, true);

	// Call buffer_full ISR when ADC DMA finished
	irq_set_enabled(DMA_IRQ_0, true);
	irq_set_exclusive_handler(DMA_IRQ_0, ComputerCard::AudioCallback);



	// Set up DMA for SPI
	spi_dmacfg = dma_channel_get_default_config(spi_dma);
	channel_config_set_transfer_data_size(&spi_dmacfg, DMA_SIZE_16);

	// SPI DMA timed to SPI TX
	channel_config_set_dreq(&spi_dmacfg, SPI_DREQ);

	// Set up DMA to transmit 2 samples to SPI
	dma_channel_configure(spi_dma, &spi_dmacfg, &spi_get_hw(SPI_PORT)->dr, NULL, 2, false);

	adc_run(true);

	while (1)
	{
		// If ready to restart
		if (runADCMode == RUN_ADC_MODE_REQUEST_ADC_RESTART)
		{
			runADCMode = RUN_ADC_MODE_RUNNING;

			dma_hw->ints0 = 1u << adc_dma; // reset adc interrupt flag
			dma_channel_set_write_addr(adc_dma, ADC_Buffer[dmaPhase], true); // start writing into new buffer
			dma_channel_set_read_addr(spi_dma, SPI_Buffer[dmaPhase], true); // start reading from new buffer

			adc_set_round_robin(0);
			adc_select_input(0);
			adc_set_round_robin(0b0001111U);
			adc_run(true);
		}
	}
}


// Per-audio-sample ISR, called when two sets of ADC samples have been collected from all four inputs
void __not_in_flash_func(ComputerCard::BufferFull)()
{
	static int mux_state = 0;
	static int norm_probe_count = 0;

	// Internal variables for IIR filters on knobs/cv
	static volatile int32_t knobssm[4] = { 0, 0, 0, 0 };
	static volatile int32_t cvsm[2] = { 0, 0 };
	static int np = 0, np1 = 0, np2 = 0;

	adc_select_input(0);

	// Advance external mux to next state
	int next_mux_state = (mux_state + 1) & 0x3;
	gpio_put(MX_A, next_mux_state & 1);
	gpio_put(MX_B, next_mux_state & 2);

	// Set up new writes into next buffer
	uint8_t cpuPhase = dmaPhase;
	dmaPhase = 1 - dmaPhase;

	dma_hw->ints0 = 1u << adc_dma; // reset adc interrupt flag
	dma_channel_set_write_addr(adc_dma, ADC_Buffer[dmaPhase], true); // start writing into new buffer
	dma_channel_set_read_addr(spi_dma, SPI_Buffer[dmaPhase], true); // start reading from new buffer

	////////////////////////////////////////
	// Collect various inputs and put them in variables for the DSP

	// Set CV inputs, with ~240Hz LPF on CV input
	int cvi = mux_state % 2;
	cvsm[cvi] = (15 * (cvsm[cvi]) + 16 * ADC_Buffer[cpuPhase][3]) >> 4;
	cv[cvi] = 2048 - (cvsm[cvi] >> 4);


	// Set audio inputs, by averaging the two samples collected
	adcInR = ((ADC_Buffer[cpuPhase][0] + ADC_Buffer[cpuPhase][4]) - 0x1000) >> 1;

	adcInL = ((ADC_Buffer[cpuPhase][1] + ADC_Buffer[cpuPhase][5]) - 0x1000) >> 1;

	// Set pulse inputs
	last_pulse[0] = pulse[0];
	last_pulse[1] = pulse[1];
	pulse[0] = !gpio_get(PULSE_1_INPUT);
	pulse[1] = !gpio_get(PULSE_2_INPUT);

	// Set knobs, with ~60Hz LPF
	int knob = mux_state;
	knobssm[knob] = (127 * (knobssm[knob]) + 16 * ADC_Buffer[cpuPhase][2]) >> 7;
	knobs[knob] = knobssm[knob] >> 4;


	////////////////////////////
	// Normalisation probe

	if (useNormProbe)
	{
		// Set normalisation probe output value
		// and update np to the expected history string
		if (norm_probe_count == 0)
		{
			int32_t normprobe = next_norm_probe();
			gpio_put(NORMALISATION_PROBE, normprobe);
			np = (np<<1)+(normprobe&0x1);
		}

		// CV sampled at 24kHz comes in over two successive samples
		if (norm_probe_count == 14 || norm_probe_count == 15)
		{
			plug_state[2+cvi] = (plug_state[2+cvi]<<1)+(ADC_Buffer[cpuPhase][3]<1800);
		}

		// Audio and pulse measured every sample at 48kHz
		if (norm_probe_count == 15)
		{
			plug_state[Input::Audio1] = (plug_state[Input::Audio1]<<1)+(ADC_Buffer[cpuPhase][5]<1800);
			plug_state[Input::Audio2] = (plug_state[Input::Audio2]<<1)+(ADC_Buffer[cpuPhase][4]<1800);
			plug_state[Input::Pulse1] = (plug_state[Input::Pulse1]<<1)+(pulse[0]);
			plug_state[Input::Pulse2] = (plug_state[Input::Pulse2]<<1)+(pulse[1]);

			for (int i=0; i<6; i++)
			{
				connected[i] = (np != plug_state[i]);
			}
		}
		
		// Force disconnected values to zero, rather than the normalisation probe garbage
		if (Disconnected(Input::Audio1)) adcInL = 0;
		if (Disconnected(Input::Audio2)) adcInR = 0;
		if (Disconnected(Input::CV1)) cv[0] = 0;
		if (Disconnected(Input::CV2)) cv[1] = 0;
		if (Disconnected(Input::Pulse1)) pulse[0] = 0;
		if (Disconnected(Input::Pulse2)) pulse[1] = 0;
	}
	
	////////////////////////////////////////
	// Run the DSP
	ProcessSample();

	////////////////////////////////////////
	// Collect DSP outputs and put them in the DAC SPI buffer
	// CV/Pulse outputs are done immediately in ProcessSample

	SPI_Buffer[cpuPhase][0] = dacval(dacOut[0], DAC_CHANNEL_A);
	SPI_Buffer[cpuPhase][1] = dacval(dacOut[1], DAC_CHANNEL_B);

	mux_state = next_mux_state;

	// Indicate to usb core that we've finished running this sample.
	if (runADCMode == RUN_ADC_MODE_REQUEST_ADC_STOP)
	{
		adc_run(false);
		adc_set_round_robin(0);
		adc_select_input(0);

		runADCMode = RUN_ADC_MODE_ADC_STOPPED;
	}

	norm_probe_count = (norm_probe_count + 1) & 0xF;
}

ComputerCard::HardwareVersion ComputerCard::ProbeHardwareVersion()
{
	gpio_set_pulls(BOARD_ID_0, false, true);
	gpio_set_pulls(BOARD_ID_1, false, true);
	gpio_set_pulls(BOARD_ID_2, false, true);
	sleep_us(1);
	uint8_t pd = gpio_get(BOARD_ID_0) | (gpio_get(BOARD_ID_0) << 2) | (gpio_get(BOARD_ID_2) << 4);
	gpio_set_pulls(BOARD_ID_0, true, false);
	gpio_set_pulls(BOARD_ID_1, true, false);
	gpio_set_pulls(BOARD_ID_2, true, false);
	sleep_us(1);
	uint8_t pu = (gpio_get(BOARD_ID_0) << 1) | (gpio_get(BOARD_ID_0) << 3) | (gpio_get(BOARD_ID_2) << 5);
	gpio_set_pulls(BOARD_ID_0, false, true);
	gpio_set_pulls(BOARD_ID_1, false, true);
	gpio_set_pulls(BOARD_ID_2, false, true);
	uint8_t id = pd | pu;

	switch (id)
	{
	case HardwareVersion::Proto1:
	case HardwareVersion::Proto2_Rev1:
		return static_cast<ComputerCard::HardwareVersion>(id);
	default:
		return HardwareVersion::Unknown;
	}
}

ComputerCard::ComputerCard()
{
		
	runADCMode = RUN_ADC_MODE_RUNNING;

	adc_run(false);
	adc_select_input(0);

	sleep_ms(50);
	

	useNormProbe = false;
	for (int i=0; i<6; i++)
	{
		connected[i] = false;
	}

	
	// Initialize PWM for LEDs, in pairs due pinout and PWM hardware
	for (int i = 0; i < numLeds; i+=2)
	{	
		gpio_set_function(leds[i], GPIO_FUNC_PWM);
		gpio_set_function(leds[i]+1, GPIO_FUNC_PWM);

		// now create PWM config struct
		pwm_config config = pwm_get_default_config();
		pwm_config_set_wrap(&config, 65535); // 16-bit PWM


		// now set this PWM config to apply to the two outputs
		pwm_init(pwm_gpio_to_slice_num(leds[i]), &config, true); 
		pwm_init(pwm_gpio_to_slice_num(leds[i]+1), &config, true); 

		// set initial level 
		pwm_set_gpio_level(leds[i], 0);
		pwm_set_gpio_level(leds[i]+1, 0);
	}

	
	// Board version ID pins
	gpio_set_dir(BOARD_ID_0, GPIO_IN);
	gpio_set_dir(BOARD_ID_1, GPIO_IN);
	gpio_set_dir(BOARD_ID_2, GPIO_IN);
	hw = ProbeHardwareVersion();
	
	// Normalisation probe pin
	gpio_init(NORMALISATION_PROBE);
	gpio_set_dir(NORMALISATION_PROBE, GPIO_OUT);
	gpio_put(NORMALISATION_PROBE, false);

	adc_init(); // Initialize the ADC

	// Set ADC pins
	adc_gpio_init(AUDIO_L_IN_1);
	adc_gpio_init(AUDIO_R_IN_1);
	adc_gpio_init(MUX_IO_1);
	adc_gpio_init(MUX_IO_2);

	// Initialize Mux Control pins
	gpio_init(MX_A);
	gpio_init(MX_B);
	gpio_set_dir(MX_A, GPIO_OUT);
	gpio_set_dir(MX_B, GPIO_OUT);

	// Initialize pulse out
	gpio_init(PULSE_1_RAW_OUT);
	gpio_set_dir(PULSE_1_RAW_OUT, GPIO_OUT);
	gpio_put(PULSE_1_RAW_OUT, true); // set raw value high (output low)

	gpio_init(PULSE_2_RAW_OUT);
	gpio_set_dir(PULSE_2_RAW_OUT, GPIO_OUT);
	gpio_put(PULSE_2_RAW_OUT, true); // set raw value high (output low)

	// Initialize pulse in
	gpio_init(PULSE_1_INPUT);
	gpio_set_dir(PULSE_1_INPUT, GPIO_IN);
	gpio_pull_up(PULSE_1_INPUT); // NB Needs pullup to activate transistor on inputs

	gpio_init(PULSE_2_INPUT);
	gpio_set_dir(PULSE_2_INPUT, GPIO_IN);
	gpio_pull_up(PULSE_2_INPUT); // NB: Needs pullup to activate transistor on inputs


	// Setup SPI for DAC output
	spi_init(SPI_PORT, 15625000);
	spi_set_format(SPI_PORT, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
	gpio_set_function(DAC_SCK, GPIO_FUNC_SPI);
	gpio_set_function(DAC_TX, GPIO_FUNC_SPI);
	gpio_set_function(DAC_CS, GPIO_FUNC_SPI);

	// Setup I2C for EEPROM
	i2c_init(i2c0, 100 * 1000);
	gpio_set_function(EEPROM_SDA, GPIO_FUNC_I2C);
	gpio_set_function(EEPROM_SCL, GPIO_FUNC_I2C);



	// Setup CV PWM
	// First, tell the CV pins that the PWM is in charge of the value.
	gpio_set_function(CV_OUT_1, GPIO_FUNC_PWM);
	gpio_set_function(CV_OUT_2, GPIO_FUNC_PWM);

	// now create PWM config struct
	pwm_config config = pwm_get_default_config();
	pwm_config_set_wrap(&config, 2047); // 11-bit PWM


	// now set this PWM config to apply to the two outputs
	// NB: CV_A and CV_B share the same PWM slice, which means that they share a PWM config
	// They have separate 'gpio_level's (output compare unit) though, so they can have different PWM on-times
	pwm_init(pwm_gpio_to_slice_num(CV_OUT_1), &config, true); // Slice 1, channel A
	pwm_init(pwm_gpio_to_slice_num(CV_OUT_2), &config, true); // slice 1 channel B (redundant to set up again)

	// set initial level to half way (0V)
	pwm_set_gpio_level(CV_OUT_1, 1024);
	pwm_set_gpio_level(CV_OUT_2, 1024);

// If not using UART pins for UART, instead use as debug lines
#ifndef ENABLE_UART_DEBUGGING
	// Debug pins
	gpio_init(DEBUG_1);
	gpio_set_dir(DEBUG_1, GPIO_OUT);

	gpio_init(DEBUG_2);
	gpio_set_dir(DEBUG_2, GPIO_OUT);
#endif

	// Read EEPROM calibration values
	ReadEEPROM();

	// Read unique card ID
	flash_get_unique_id((uint8_t *) &uniqueID);
	// Do some mixing up of the bits using full-cycle 64-bit LCG
	// Should help ensure most bytes change even if many bits of
	// the original flash unique ID are the same between flash chips.
	for (int i=0; i<20; i++)
	{
		uniqueID = uniqueID * 6364136223846793005ULL + 1442695040888963407ULL;
	}
}



// Read a byte from EEPROM
uint8_t ComputerCard::ReadByteFromEEPROM(unsigned int eeAddress)
{
	uint8_t deviceAddress = EEPROM_PAGE_ADDRESS | ((eeAddress >> 8) & 0x0F);
	uint8_t data = 0xFF;

	uint8_t addr_low_byte = eeAddress & 0xFF;
	i2c_write_blocking(i2c0, deviceAddress, &addr_low_byte, 1, false);

	i2c_read_blocking(i2c0, deviceAddress, &data, 1, false);
	return data;
}

// Read a 16-bit integer from EEPROM
int ComputerCard::ReadIntFromEEPROM(unsigned int eeAddress)
{
	uint8_t highByte = ReadByteFromEEPROM(eeAddress);
	uint8_t lowByte = ReadByteFromEEPROM(eeAddress + 1);
	return (highByte << 8) | lowByte;
}

uint16_t ComputerCard::CRCencode(const uint8_t *data, int length)
{
	uint16_t crc = 0xFFFF; // Initial CRC value
	for (int i = 0; i < length; i++)
	{
		crc ^= ((uint16_t)data[i]) << 8; // Bring in the next byte
		for (uint8_t bit = 0; bit < 8; bit++)
		{
			if (crc & 0x8000)
			{
				crc = (crc << 1) ^ 0x1021; // CRC-CCITT polynomial
			}
			else
			{
				crc = crc << 1;
			}
		}
	}
	return crc;
}


int ComputerCard::ReadEEPROM()
{
	// Set up default values in the calibration table,
	// to be used if EEPROM read fails
	calibrationTable[0][0].voltage = -20; // -2V
	calibrationTable[0][0].dacSetting = 347700;
	calibrationTable[0][1].voltage = 0; // 0V
	calibrationTable[0][1].dacSetting = 261200;
	calibrationTable[0][2].voltage = 20; // +2V
	calibrationTable[0][2].dacSetting = 174400;

	calibrationTable[1][0].voltage = -20; // -2V
	calibrationTable[1][0].dacSetting = 347700;
	calibrationTable[1][1].voltage = 0; // 0V
	calibrationTable[1][1].dacSetting = 261200;
	calibrationTable[1][2].voltage = 20; // +2V
	calibrationTable[1][2].dacSetting = 174400;

	if (ReadIntFromEEPROM(EEPROM_ADDR_ID) != EEPROM_VAL_ID)
	{
		return 1;
	}
	uint8_t buf[EEPROM_NUM_BYTES];
	for (int i = 0; i < EEPROM_NUM_BYTES; i++)
	{
		buf[i] = ReadByteFromEEPROM(i);
	}


	uint16_t calculatedCRC = CRCencode(buf, 86);
	uint16_t foundCRC = ((uint16_t)buf[EEPROM_ADDR_CRC_H] << 8) | buf[EEPROM_ADDR_CRC_L];

	if (calculatedCRC != foundCRC)
	{
		return 1;
	}

	int bufferIndex = 4;

	for (uint8_t channel = 0; channel < calMaxChannels; channel++)
	{
		int channelOffset = bufferIndex + (41 * channel); // channel 0 = 4, channel 1 = 45
		numCalibrationPoints[channel] = buf[channelOffset++];
		for (uint8_t point = 0; point < numCalibrationPoints[channel]; point++)
		{
			// Unpack Pack targetVoltage (int8_t) from buf
			int8_t targetVoltage = (int8_t)buf[channelOffset++];

			// Unack dacSetting (uint32_t) from buf (4 bytes)
			uint32_t dacSetting = 0;
			dacSetting |= ((uint32_t)buf[channelOffset++]) << 24; // MSB
			dacSetting |= ((uint32_t)buf[channelOffset++]) << 16;
			dacSetting |= ((uint32_t)buf[channelOffset++]) << 8;
			dacSetting |= ((uint32_t)buf[channelOffset++]); // LSB

			// Write settings into calibration table
			calibrationTable[channel][point].voltage = targetVoltage;
			calibrationTable[channel][point].dacSetting = dacSetting;
		}
		CalcCalCoeffs(channel);
	}

	return 0;
}

void ComputerCard::CalcCalCoeffs(int channel)
{
	float sumV = 0.0;
	float sumDAC = 0.0;
	float sumV2 = 0.0;
	float sumVDAC = 0.0;
	int N = numCalibrationPoints[channel];

	for (int i = 0; i < N; i++)
	{
		float v = calibrationTable[channel][i].voltage * 0.1;
		float dac = calibrationTable[channel][i].dacSetting;
		sumV += v;
		sumDAC += dac;
		sumV2 += v * v;
		sumVDAC += v * dac;
	}

	float denominator = N * sumV2 - sumV * sumV;
	if (denominator != 0)
	{
		calCoeffs[channel].m = (N * sumVDAC - sumV * sumDAC) / denominator;
	}
	else
	{
		calCoeffs[channel].m = 0.0;
	}
	calCoeffs[channel].b = (sumDAC - calCoeffs[channel].m * sumV) / N;

	calCoeffs[channel].mi = (calCoeffs[channel].m * 1.333333333333333f + 0.5f);
	calCoeffs[channel].bi = calCoeffs[channel].b + 0.5f;
}


uint32_t ComputerCard::MIDIToDac(int midiNote, int channel)
{
	int32_t dacValue = ((calCoeffs[channel].mi * (midiNote - 60)) >> 4) + calCoeffs[channel].bi;
	if (dacValue > 524287) dacValue = 524287;
	if (dacValue < 0) dacValue = 0;
	return dacValue;
}

#endif

#endif
