/*
Simple reverb for Music Thing Workshop System Computer

Chris Johnson 2024



Algorithm is a slight modification of that in the Dattorro paper
     https://ccrma.stanford.edu/~dattorro/EffectDesignPart1.pdf 
see verb_int.c for details


Interrupt service routine buffer_full() called at 48kHz by ADC DMA (ADC runs at 384kHz)
ADC samples both audio channels, along with knobs and CV in, and sends outputs to DAC (via DMA)
at 48kHz.

Reverb DSP is run within the buffer_full() ISR.  DSP runs in around 12us, out of 20.8us per sample.

The function of knobs, CV and Pulse input/output are controlled by MIDI SysEx commands.


*/

#define ENABLE_MIDI
//#define ENABLE_DEBUGGING

#ifdef ENABLE_MIDI
#include "bsp/board.h"
#include "tusb.h"
#endif

#ifdef ENABLE_DEBUGGING
#include <stdio.h>
#define debug(f_, ...) printf((f_), __VA_ARGS__)
#else
#define debug(f_, ...) 
#endif


#include "sysex_sentences.h"
#include "verb_int.h"
#include "pico/stdlib.h" // for sleep_ms
#include "pico/time.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/adc.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "pico/multicore.h"
#include "hardware/flash.h"
#include <float.h>
#include <math.h>
#include <string.h>


#include "computer.h"
#include "clock.h"
#include "divider.h"
#include "bernoulligate.h"
#include "turingmachine.h"
#include "noise_gate.h"


#define MIDI_NOTE_ON 0x90
#define MIDI_NOTE_OFF 0x80
#define MIDI_CC 0xB0

#define OPT_PITCH OPT_PITCH_OF_MIDI_NOTE

uint32_t configFlashAddr = (PICO_FLASH_SIZE_BYTES - 4096) - (PICO_FLASH_SIZE_BYTES - 4096)%4096;

uint8_t config[CONFIG_LENGTH];
uint8_t packet[CONFIG_LENGTH+10];

//uint8_t defaultConfig[] = {0, 0, 1, 0, 5, 30, 0, 0, 0, 2, 127, 127, 4, 0, 127, 2, 127, 127, 1, 127, 127, 8, 4, 127, 2, 0, SYSEX_CONFIGURED_MARKER};
//uint8_t defaultConfig[] = {0, 0, 1, 0, 5, 30, 1, 0, 127, 2, 0, 127, 1, 0, 127, 2, 0, 127, 5, 1, 2, 0, 0, 5, 4, 4, 0, 1, 0, 127, 127, 127, 0, 127, 0, 127, 127, 127, 127, 0, 127, 87, SYSEX_CONFIGURED_MARKER};

// For Machina Bristronica 2024
uint8_t defaultConfig[] =   {0, 0, 1, 0, 2, 0, 5, 127, 127, 1, 0, 127, 5, 127, 127, 5, 127, 127, 3, 127, 127, 127, 127, 127, 127, 127, 127, 127, 1, 0, 127, 8, 5, 10, 0, 127, 127, 127, 127, 0, 127, 87, SYSEX_CONFIGURED_MARKER};


clock clk[2];
divider pulseout1_divider, pulseout2_divider, cvout1_divider, cvout2_divider, tm_divider, bg_divider;
turing_machine tm;
bernoulli_gate bg;
noise_gate ng;

volatile int32_t knobs[4] = {0,0,0,0}; // 0-4095
volatile bool pulse[2] = {0,0}; 
volatile bool last_pulse[2] = {0,0}; 
volatile int32_t cv[2] = {0,0}; // -2047 - 2048
volatile int16_t dacOutL = 0, dacOutR = 0;
volatile int16_t adcInL = 0x800, adcInR = 0x800;

volatile uint8_t mxPos = 0; // external multiplexer value


uint16_t ADC_Buffer[2][8];
uint16_t SPI_Buffer[2][2];

uint8_t adc_dma;
uint8_t spi_dma;

dma_channel_config adc_dmacfg;
dma_channel_config spi_dmacfg;


uint8_t dmaPhase = 0;

void ProcessSample();

uint16_t __not_in_flash_func(dacval)(int16_t value, uint16_t dacChannel)
{
	return (dacChannel | 0x3000) | (((uint16_t)((value&0x0FFF)+0x800))&0x0FFF);
}
void __not_in_flash_func(buffer_full)()
{
//	gpio_put(DEBUG_1, true);
	static int mux_state = 0;

	static volatile int32_t knobssm[4] = {0,0,0,0};
	static volatile int32_t cvsm[2] = {0,0};

	adc_select_input(0);
   
	int next_mux_state = (mux_state+1)&0x3;
	gpio_put(MX_A, next_mux_state&1);
	gpio_put(MX_B, next_mux_state&2);
	
	uint8_t cpuPhase = dmaPhase;
	dmaPhase = 1-dmaPhase;
    
    dma_hw->ints0 = 1u << adc_dma; //reset adc interrupt flag
    dma_channel_set_write_addr(adc_dma, ADC_Buffer[dmaPhase], true); // start writing into new buffer

	dma_channel_set_read_addr(spi_dma, SPI_Buffer[dmaPhase], true); // start reading from new buffer

	
	int cvi=mux_state%2;

	// ~100Hz LPF on CV input
	cvsm[cvi]=(127*(cvsm[cvi])+16*ADC_Buffer[cpuPhase][3])>>7;
	cv[cvi]=2048 - (cvsm[cvi]>>4);
		
	adcInR = ((ADC_Buffer[cpuPhase][0]+ADC_Buffer[cpuPhase][4])-0x1000)>>1;
	adcInL = ((ADC_Buffer[cpuPhase][1]+ADC_Buffer[cpuPhase][5])-0x1000)>>1;

	last_pulse[0]=pulse[0];
	last_pulse[1]=pulse[1];
	pulse[0]=gpio_get(PULSE_1_INPUT);
	pulse[1]=gpio_get(PULSE_2_INPUT);


	int knob=mux_state;
	knobssm[knob]=(127*(knobssm[knob])+16*ADC_Buffer[cpuPhase][2])>>7;
	knobs[knob]=knobssm[knob]>>4;

	ProcessSample();
	
	SPI_Buffer[cpuPhase][0] = dacval(dacOutL, DAC_CHANNEL_A);
	SPI_Buffer[cpuPhase][1] = dacval(dacOutR, DAC_CHANNEL_B);
	
	mux_state = next_mux_state;
//	gpio_put(DEBUG_1, false);

}



////////////////////////////////////////
// Shorthand functions for pulse/CV outputs, with LEDs

void PulseOut1(bool val)
{
	gpio_put(leds[3], val);
	gpio_put(PULSE_1_RAW_OUT, !val);
}
void PulseOut2(bool val)
{
	gpio_put(leds[5], val);
	gpio_put(PULSE_2_RAW_OUT, !val);
}

void CVOut1(uint32_t val)
{
	pwm_set_gpio_level(CV_OUT_1, val);
}
void CVOut2(uint32_t val)
{
	pwm_set_gpio_level(CV_OUT_2, val);
}

void CVOut1Gate(bool val)
{
	pwm_set_gpio_level(CV_OUT_1, val?0:1024);
}
void CVOut2Gate(bool val)
{
	pwm_set_gpio_level(CV_OUT_2, val?0:1024);
}

int32_t continuous_source_from_config(int offset)
{
	int32_t ret=0;
	switch (config[offset])
	{
	case OPT_MAIN_KNOB:
		ret = knobs[KNOB_MAIN];
		
		break;
	case OPT_KNOB_X:
		ret = knobs[KNOB_X];
		break;
	case OPT_KNOB_Y:
		ret = knobs[KNOB_Y];
		break;
	case OPT_CV_IN_1:
		ret = cv[0]+2047;
		break;
	case OPT_CV_IN_2:
		ret = cv[1]+2047;
		break;
	case OPT_A_CONSTANT:
		ret = ((int32_t)config[offset+1])*41;
		if (ret==4100) ret=4095;
		break;
	}

	if (config[offset]<=OPT_KNOB_Y && config[offset+1] != OPT_ONLY)
	{
		ret += cv[config[offset+1]-OPT_PLUS_CV_IN_1];
	}
	return ret;
}

// Returns tempo in Hz from a "TempoSource" dropdown
float tempo_source_from_config(int offset)
{
	int32_t ret=0;
	if (config[offset] == OPT_A_CONSTANT)
	{
		int val = 100*config[offset+1]
			+ 10*config[offset+2]
			+ config[offset+3];
		float fval = val;
		return (config[offset+4]==OPT_HZ)?fval:fval*0.0166666666f;
	}
	else if (config[offset] == OPT_TAP_TEMPO)
	{
		return 1; // TAP TEMPO NOT YET IMPLEMENTED

	}
	else
	{
		// tempo from knob or CV: use exponential mapping
		int32_t int_clock_tempo = continuous_source_from_config(offset);
		return expf(1.9531e-3f*int_clock_tempo - 3.0f);
	}

}


////////////////////////////////////////
// Turing machine


void tm_step()
{
	uint32_t chosenBit = turing_machine_step(&tm, continuous_source_from_config(SEN_TM_MAIN_KNOB));
	uint32_t volt = turing_machine_volt(&tm);
	
	// TM pulse -> Pulse out
	if (config[SEN_PULSE_OUT_1] == OPT_TURING_MACHINE_PULSE_OUT) PulseOut1(chosenBit);
	if (config[SEN_PULSE_OUT_2] == OPT_TURING_MACHINE_PULSE_OUT) PulseOut2(chosenBit);

	// TM analogue -> CV out
	if (config[SEN_CV_OUT_1] == OPT_TURING_MACHINE_ANALOGUE_OUT) CVOut1(1024-(volt<<2));
	if (config[SEN_CV_OUT_2] == OPT_TURING_MACHINE_ANALOGUE_OUT) CVOut2(1024-(volt<<2));

}

////////////////////////////////////////
// Bernoulli gate 

void bg_div_step(bool risingEdge)
{
	static bool lastDiv = false;
	
	divider_set(&bg_divider, config[SEN_BG_CLOCK+1]+1);

	bool newDiv = divider_step(&bg_divider, risingEdge);
				
	if (newDiv != lastDiv)
	{
		bool bg_value = bernoulli_gate_step(&bg,
											continuous_source_from_config(SEN_BG_RAND),
											newDiv);


		bool chanA = bg_value, chanB = 1-bg_value;
		if (bg.awi)
		{
			chanA = chanA && newDiv;
			chanB = chanB && newDiv;
		}
		
		if (config[SEN_PULSE_OUT_1] == OPT_BERNOULLI_GATE_OUT_A) PulseOut1(chanA);
		if (config[SEN_PULSE_OUT_1] == OPT_BERNOULLI_GATE_OUT_B) PulseOut1(chanB);
		if (config[SEN_PULSE_OUT_2] == OPT_BERNOULLI_GATE_OUT_A) PulseOut2(chanA);
		if (config[SEN_PULSE_OUT_2] == OPT_BERNOULLI_GATE_OUT_B) PulseOut2(chanB);
	}

	lastDiv = newDiv;
}



////////////////////////////////////////
// Non-application-specific functions for dealing with configuration over sysex


// declarations of application-level handling functions
void handle_midi_message(uint8_t* packet);
void post_config_processing();
void post_flash_processing();

void set_default_config()
{
	for (int i=0; i<CONFIG_LENGTH; i++)
	{
		config[i] = defaultConfig[i];
	}
}



void set_config_from_flash()
{
	for (int i=0; i<CONFIG_LENGTH; i++)
	{
		config[i] = *((uint8_t*)(XIP_BASE+configFlashAddr+i));
	}
	if (config[SYSEX_CONFIGURED_MARKER_INDEX] != SYSEX_CONFIGURED_MARKER)
	{
		set_default_config();
	}
	
	post_config_processing();
}

int set_config_from_sysex(uint8_t *packet)
{
	if (packet[SYSEX_INDEX_LENGTH] != CONFIG_LENGTH)
		return 1; //error
	
	// copy sysex to RAM config
	memcpy(config, &(packet[4]), CONFIG_LENGTH);
	// run any application-specific processing
		
	post_config_processing();	
	return 0; // success
}

void save_config_to_flash()
{
	// shut down the other core
	multicore_lockout_start_blocking();
	// erase page of flash
	uint32_t ints = save_and_disable_interrupts();
	flash_range_erase(configFlashAddr, 4096);
	restore_interrupts(ints);

	// write RAM config to flash
	ints = save_and_disable_interrupts();
	flash_range_program(configFlashAddr, config, CONFIG_LENGTH);
	restore_interrupts(ints);

	post_flash_processing();
			
	// restore other core
	multicore_lockout_end_blocking();
}

		
#ifdef ENABLE_MIDI
void process_sys_ex_command(uint8_t *packet)
{
	if (packet[SYSEX_INDEX_MANUFACTURER] != SYSEX_MANUFACTURER_DEV)
		return;
	
	switch (packet[SYSEX_INDEX_COMMAND])
	{
	case SYSEX_COMMAND_PREVIEW: // preview: save config from sysex to ram, but not to flash
		set_config_from_sysex(packet);
		break;
		
	case SYSEX_COMMAND_WRITE_FLASH: 

		// If successful in setting RAM config from sysex...
		if (!set_config_from_sysex(packet))
		{
			// save RAM config to flash
			save_config_to_flash();
		}
		break;

	case SYSEX_COMMAND_READ:
		packet[0]=0xF0; // start sysex
		packet[SYSEX_INDEX_MANUFACTURER] = SYSEX_MANUFACTURER_DEV;
		packet[SYSEX_INDEX_COMMAND] = SYSEX_COMMAND_READ; // indicate what command this is in response to
		packet[SYSEX_INDEX_LENGTH] = CONFIG_LENGTH;
		memcpy(&(packet[4]), config, CONFIG_LENGTH); // copy config into sysex packet
		packet[CONFIG_LENGTH+4]=0xF7; // end sysex
		tud_midi_stream_write(0, packet, CONFIG_LENGTH+5);
		break;

	case SYSEX_COMMAND_READ_CARD_RELEASE:
		packet[0]=0xF0; // start sysex
		packet[SYSEX_INDEX_MANUFACTURER] = SYSEX_MANUFACTURER_DEV;
		packet[SYSEX_INDEX_COMMAND] = SYSEX_COMMAND_READ_CARD_RELEASE; // indicate what command this is in response to
		packet[SYSEX_INDEX_LENGTH] = 4; // 4-byte response of card ID number, and major and minor versions
		packet[4] = CARD_ID_LOW;
		packet[5] = CARD_ID_HIGH;
		packet[6] = CARD_VER_MAJOR;
		packet[7] = CARD_VER_MINOR;
		packet[8]=0xF7; // end sysex
		tud_midi_stream_write(0, packet, 9);
		break;

	default:
		break;
	}
}

void midi_task()
{
	// Read incoming packet if availabie
	while (tud_midi_available())
	{
		uint32_t bytes_read = tud_midi_stream_read(packet, sizeof(packet));

		if (packet[0] == 0xF0) // If sysex, handle with standard routine
		{
			process_sys_ex_command(packet);
		}
		else // else pass on to application midi message handler
		{
			handle_midi_message(packet);
		}
	
		// Debugging only -- send back any data received as a sysex packaet
		memmove(packet+1, packet, bytes_read);
		packet[0]=0xF0;
		packet[bytes_read+1]=0xF7;
		for (int i=1; i<=bytes_read; i++)
			packet[i]&=0x7F;
		tud_midi_stream_write(0, packet, bytes_read+2);
	}
}
#endif

////////////////////////////////////////////////////////////////////////////////
// Application-specific functions for dealing with config and midi messages


bool midi_channel(uint8_t *packet, int sen)
{
	uint8_t channel = packet[0]&0x0F;
	
	return (config[sen]==OPT_ANY_MIDI_CHANNEL || config[sen] == channel+1);
}

bool midi_gate(uint8_t *packet, int sen)
{
	uint8_t noteNum = packet[1];
	return (config[sen]==OPT_ANY_MIDI_NOTE || config[sen] == noteNum+1) &&
		midi_channel(packet, sen+1);
}
bool midi_cc(uint8_t *packet, int sen)
{
	uint8_t ccNum = packet[1];
	return (config[sen] == ccNum) && midi_channel(packet, sen+1);
}

void handle_midi_message(uint8_t* packet)
{
	uint8_t messageType = packet[0]&0xF0;
		
	// If a note on or note off
	if (messageType == MIDI_NOTE_ON || messageType == MIDI_NOTE_OFF)
	{
		// Gates
		bool noteOn = (messageType==MIDI_NOTE_ON);
		if (config[SEN_PULSE_OUT_1] == OPT_GATE_OF && midi_gate(packet, SEN_PULSE_OUT_1+1)) PulseOut1(noteOn);
		if (config[SEN_PULSE_OUT_2] == OPT_GATE_OF && midi_gate(packet, SEN_PULSE_OUT_2+1)) PulseOut2(noteOn);
		if (config[SEN_CV_OUT_1] == OPT_GATE_OF && midi_gate(packet, SEN_CV_OUT_1+1)) CVOut1Gate(noteOn);
		if (config[SEN_CV_OUT_2] == OPT_GATE_OF && midi_gate(packet, SEN_CV_OUT_2+1)) CVOut2Gate(noteOn);

		if (config[SEN_TM_CLOCK] == OPT_GATE_OF && midi_gate(packet, SEN_TM_CLOCK+1)
			&& messageType == MIDI_NOTE_ON)
			tm_step();
		
		// Pitch CV
		uint16_t noteNum = packet[1];
		if (config[SEN_CV_OUT_1] == OPT_PITCH && midi_channel(packet, SEN_CV_OUT_1+1)) CVOut1(noteNum*14);
		if (config[SEN_CV_OUT_2] == OPT_PITCH && midi_channel(packet, SEN_CV_OUT_2+1)) CVOut2(noteNum*14);

	}
	// If MIDI CC
	else if (messageType == MIDI_CC)
	{
		uint8_t ccVal = packet[2];
		if (config[SEN_CV_OUT_1]==OPT_MIDI_CC && midi_cc(packet, SEN_CV_OUT_1+1)) CVOut1(ccVal*16);
		if (config[SEN_CV_OUT_2]==OPT_MIDI_CC && midi_cc(packet, SEN_CV_OUT_2+1)) CVOut2(ccVal*16);
	}
}


reverb *dv=0;


// After saving to flash, reset reverb buffers (occasionally had reverb overflow)
void post_flash_processing()
{
	reverb_reset(dv);
}

// Processing after config changed
void post_config_processing()
{
	clock_set_freq_hz(&clk[0], tempo_source_from_config(SEN_CLKA_TEMPO));
	clock_set_freq_hz(&clk[1], tempo_source_from_config(SEN_CLKB_TEMPO));
	
	turing_machine_set_length(&tm, config[SEN_TM_LENGTH]);
	
	bernoulli_gate_set_toggle(&bg, config[SEN_BG_OPTS]);
	bernoulli_gate_set_and_with_input(&bg, config[SEN_BG_OPTS+1]);
}

unsigned int lcg_rand()
{
    static uint32_t lcg_seed = 1;
	lcg_seed = 1664525 * lcg_seed + 1013904223;
    return lcg_seed;
}

volatile int inDefaultConfigLoad;

// Handles USB. Not synchronised to ADC/audio
void usb_worker()
{
	inDefaultConfigLoad = 0;
	set_config_from_flash();

#ifdef ENABLE_MIDI
	board_init();
	tusb_init();
#endif

	// If switch held down (momentary) for 1s at startup,
	// load default configuration and write it into flash
	{
		int loadDefaultConfig=1;
		for (int i=0; i<10; i++)
		{
			sleep_us(100000); // wait 0.1s
			
			// If switch is not in momentary position, abort
			if (knobs[KNOB_SWITCH]>=1000)
			{
				loadDefaultConfig = 0;
				break;
			}
		}
		if (loadDefaultConfig)
		{
			inDefaultConfigLoad = 1;
			sleep_us(100000); // wait 0.1s
			set_default_config();
			save_config_to_flash();
		}
	}
	while(1)
	{
#ifdef ENABLE_MIDI
		tud_task();
		midi_task();
#endif

		// Do this processing in the second core, as it's a expensive
		// floating point calculation
		clock_set_freq_hz(&clk[0], tempo_source_from_config(SEN_CLKA_TEMPO));
		clock_set_freq_hz(&clk[1], tempo_source_from_config(SEN_CLKB_TEMPO));

		// Stall for a random amount of time, to minimise tones in audio interference
		sleep_us(lcg_rand()%1024);
	}
}


void audio_worker()
{
	// Audio worker must be able to stop when USB worker wants to write to flash
	multicore_lockout_victim_init();
	
	
    adc_set_round_robin(0b0001111U);

	// enabled, with DMA request when FIFO contains data, no erro flag, no byte shift
    adc_fifo_setup(true, true, 1, false, false);

	
	uint16_t frequency = 124;//249; // 48MHz/250 = 192kHz
    adc_set_clkdiv(frequency);
    
    //claim and setup dmas
    adc_dma = dma_claim_unused_channel(true);
    spi_dma = dma_claim_unused_channel(true);

    adc_dmacfg = dma_channel_get_default_config(adc_dma);
    spi_dmacfg = dma_channel_get_default_config(spi_dma);

    //reading from ADC to buffer, so increment on write, but no increment on read
    channel_config_set_transfer_data_size(&adc_dmacfg, DMA_SIZE_16);
    channel_config_set_read_increment(&adc_dmacfg, false);
    channel_config_set_write_increment(&adc_dmacfg, true);

    //clock the dma to the ADC
    channel_config_set_dreq(&adc_dmacfg, DREQ_ADC);

	// Setup DMA to transmit 4 ADC samples
    dma_channel_configure(adc_dma, &adc_dmacfg, ADC_Buffer[dmaPhase], &adc_hw->fifo, 8, true);

	
	// Turn on IRQ for ADC DMA
    dma_channel_set_irq0_enabled(adc_dma, true);

	// Call buffer_full when ADC DMA finished
    irq_set_enabled(DMA_IRQ_0, true);
    irq_set_exclusive_handler(DMA_IRQ_0, buffer_full); 



	// Set up DMA for SPI
    spi_dmacfg = dma_channel_get_default_config(spi_dma);

    channel_config_set_transfer_data_size(&spi_dmacfg, DMA_SIZE_16);

	// SPI DMA timed to SPI TX
    channel_config_set_dreq(&spi_dmacfg, SPI_DREQ);

	// Set up DMA to transmit 2 samples to SPI
    dma_channel_configure(spi_dma, &spi_dmacfg, &spi_get_hw(SPI_PORT)->dr, NULL, 2, false);


	adc_select_input(0);
	
	
	// Sample processing loop
	noise_gate_init(&ng);

	adc_run(true);
	
	while(1)
	{
		//	printf("%d %d %d %d - %d %d \n",knobs[0],knobs[1],knobs[2],knobs[3],cv[0],cv[1]);
	}

}

const int startupSampleDelay = 72000;
void __not_in_flash_func(ProcessSample)()
{
//	gpio_put(DEBUG_2, true);
	static int32_t mix1, mix2, mixf1, mixf2;
	
	static int32_t frame = 0;
	
	static int oc=0; // clipping indicator counter

	int inStartupState = (frame < startupSampleDelay);
	
	// Get dry/wet knob position
	// add some dead-zones to get 100% wet/dry at end of travel
	// 0-4096 inclusive

	int32_t knob;
	knob = continuous_source_from_config(SEN_REV_WETDRY);
	int32_t drywet = clamp((((knob)*71936)>>16)-200, 0, 4096);

	// Decay time
	knob = continuous_source_from_config(SEN_REV_DECAY);
	int32_t knobx = clamp((((knob)*71936)>>12)-3200,50,65500);
	//reverb_set_size(dv, knobx);

	// Tone
	knob = continuous_source_from_config(SEN_REV_TONE);
	reverb_set_tilt(dv, clamp(knob,0,4095)*16);

	if (adcInL>=2000 || adcInL<=-2000 || adcInR>=2000 || adcInR<=-2000)
	{
		oc=4800;
	}
		
	// multiply adcIn channels (-2047 to 2048 each) to -16384 to 16384
	// and process reverb

	
	if (knobs[KNOB_SWITCH]<1000)
	{
		// If switch held down, freeze input
		adcInL=0; adcInR=0;
		reverb_set_freeze_size(dv, knobx);
	}
	else
	{
		// normal reverb size
		reverb_set_size(dv, knobx);
	
	}

	dv->modulate = 0x7ff;
	dv->modulatedist=16;

	if (inStartupState)
	{
		adcInL=0; adcInR=0; // mute ADC for first 1.5 seconds
	}

	// 12kHz notch filter, to remove interference from mux lines
	int32_t mix = (adcInL-adcInR)<<2; // ±16384
	int32_t ooa0=16302, a2oa0=16221;//Q=100;
//	int32_t ooa0=15603, a2oa0=14823;//Q=10;
	int32_t mixf =(ooa0*(mix + mix2)-a2oa0*mixf2)>>14;
	mix2=mix1; mix1=mix;
	mixf2=mixf1; mixf1=mixf;
	
	
	int32_t gated_mono_in = (knobs[KNOB_SWITCH]<3000)?noise_gate_tick(&ng, mixf):mixf;
	reverb_process(dv, gated_mono_in); 

	int32_t left = reverb_get_left(dv);
	int32_t right = reverb_get_right(dv);
		
	int32_t dry = gated_mono_in<<1; // +- 32768

	// Get absolute values of wet output, for VU meter
	int32_t aleft = left;
	if (aleft<0) aleft=-aleft;
	// Should probably be using constant-power crossfade here,
	// though wet and dry signals may not be of similar amplitude
	// ( (0 to 4096)  * (-32768 to 32768) )/2^16 = -2048 to 2048
	left = (drywet*left + (4096-drywet)*dry)>>16; 
	right = (drywet*right + (4096-drywet)*dry)>>16;

	// send values to DAC, with final stage of clipping
	dacOutL = clamp(left,-2047, 2047);
	dacOutR = clamp(right, -2047, 2047);

	
	if (inStartupState)
	{
		dacOutL=0; dacOutR=0; // mute DAC for first 1.5 seconds
	}

	
//	gpio_put(DEBUG_2, false);
	if (dacOutL==2047 || dacOutL==-2047 || dacOutR==2047 || dacOutR==-2047)
		oc = 4800;
			
	dv->modulate = 0x7ff;


	if (oc) oc--;

	if (frame < 1000000) frame++;

	// Show LED pattern during startup state
	if (inStartupState)
	{
		int t = frame >> 12;
		gpio_put(leds[0], ((t+0)%6)==0 || inDefaultConfigLoad);
		gpio_put(leds[2], ((t+1)%6)==0 || inDefaultConfigLoad);
		gpio_put(leds[4], ((t+2)%6)==0 || inDefaultConfigLoad);
		gpio_put(leds[5], ((t+3)%6)==0 || inDefaultConfigLoad);
		gpio_put(leds[3], ((t+4)%6)==0 || inDefaultConfigLoad);
		gpio_put(leds[1], ((t+5)%6)==0 || inDefaultConfigLoad);
		return;
	}
	
	// Display VU meter
	gpio_put(leds[4], aleft>4095);
	gpio_put(leds[2], aleft>8191);
	gpio_put(leds[0], aleft>16383);

	// Display reverb input clipping 
	gpio_put(leds[1], oc>0);

	
	////////////////////////////////////////
	// Actions driven by pulse inputs
	for (int pulse_index=0; pulse_index<2; pulse_index++)
	{
		bool pulseInRisingEdge = pulse[pulse_index] && !last_pulse[pulse_index];
		bool pulseInFallingEdge = !pulse[pulse_index] && last_pulse[pulse_index];
		uint8_t pulse_opt = OPT_PULSE_IN_1+pulse_index;
			
		// Pulse in -> pulse out 
		if (pulseInRisingEdge || pulseInFallingEdge)
		{
			if (config[SEN_PULSE_OUT_1] == pulse_opt)
			{
				divider_set(&pulseout1_divider, config[SEN_PULSE_OUT_1+1]+1);
				PulseOut1(divider_step(&pulseout1_divider, pulseInRisingEdge));
			}
			if (config[SEN_PULSE_OUT_2] == pulse_opt)
			{
				divider_set(&pulseout2_divider, config[SEN_PULSE_OUT_2+1]+1);
				PulseOut2(divider_step(&pulseout2_divider, pulseInRisingEdge));
			}
		}
		  
		// Pulse in -> TM
		if (pulseInRisingEdge)
		{
			if (config[SEN_TM_CLOCK] == pulse_opt)
			{
				divider_set(&tm_divider, config[SEN_TM_CLOCK+1]+1);
				tm_step();
			}
		}
	}
		
		
	////////////////////////////////////////
	// Actions driven by the two internal clocks
	for (int clk_index=0; clk_index<2; clk_index++)
	{
		if (clock_tick(&clk[clk_index])) // true for both rising and falling edges
		{	
			bool risingEdge = clock_state(&clk[clk_index]);
			int clockOpt = OPT_INTERNAL_CLOCK_A+clk_index;

			// Clocks -> pulse out
			if (config[SEN_PULSE_OUT_1] == clockOpt)
			{
				divider_set(&pulseout1_divider, config[SEN_PULSE_OUT_1+1]+1);
				PulseOut1(divider_step(&pulseout1_divider, risingEdge));
			}
			if (config[SEN_PULSE_OUT_2] == clockOpt)
			{
				divider_set(&pulseout2_divider, config[SEN_PULSE_OUT_2+1]+1);
				PulseOut2(divider_step(&pulseout2_divider, risingEdge));
			}

			// Clocks -> CV out
			if (config[SEN_CV_OUT_1] == clockOpt)
			{
				divider_set(&cvout1_divider, config[SEN_CV_OUT_1+1]+1);
				CVOut1Gate(divider_step(&cvout1_divider, risingEdge));
			}
			if (config[SEN_CV_OUT_2] == clockOpt)
			{
				divider_set(&cvout2_divider, config[SEN_CV_OUT_2+1]+1);
				CVOut2Gate(divider_step(&cvout2_divider, risingEdge));
			}

			// Clocks -> Bernoulli gate
			if (config[SEN_BG_CLOCK] == clockOpt)
			{
				bg_div_step(risingEdge);
			}

			// Clocks -> TM
			if (config[SEN_TM_CLOCK] == clockOpt && risingEdge)
			{
				divider_set(&tm_divider, config[SEN_TM_CLOCK+1]+1);
				if (divider_step(&tm_divider, risingEdge))
					tm_step();
			}
			

		}
	}
	
}

int main()
{
	set_sys_clock_khz(144000, true);

#ifdef ENABLE_DEBUGGING
    stdio_init_all();
#endif
	
	sleep_ms(50);
	
	SetupComputerIO();

	dv = reverb_create();

	turing_machine_init(&tm);
	bernoulli_gate_init(&bg);
	
	clock_init(&clk[0]);
	clock_init(&clk[1]);
	
	divider_init(&pulseout1_divider);
	divider_init(&pulseout2_divider);
	divider_init(&cvout1_divider);
	divider_init(&cvout2_divider);
	divider_init(&tm_divider);
	divider_init(&bg_divider);

	multicore_launch_core1(audio_worker);

	usb_worker();
}

