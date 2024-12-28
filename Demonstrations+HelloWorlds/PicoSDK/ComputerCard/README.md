# ComputerCard

ComputerCard is a  [MIT licensed](https://opensource.org/license/mit) header-only C++ library, that
manages the hardware aspects of the [Music Thing Modular Workshop
System Computer](https://www.musicthing.co.uk/workshopsystem/).

It aims to present a very simple C++ framework for card programmers to use the jacks, knobs, switch and LEDs, for programs running at a fixed 48kHz audio sample rate.

ComputerCard was designed to work with the [RPi Pico SDK](https://github.com/raspberrypi/pico-sdk) but also works with the Arduino environment using the earlephilhower RP2040 board [as described below](#arduino-ide)


Behind the scenes, the ComputerCard class:
- Manages the ADC and external multiplexer to collect analogue samples from audio inputs, CV inputs, knobs and switch.
- Applies smoothing, some simple hysteresis, and range extension to knobs and CV, to provide relatively low-noise values that span the whole range (-2048 to 2047 for jacks, 0 to 4095 for knobs). Knob/CV values are updated every audio frame.
- If enabled, determines whether jacks are connected to the six input sockets by driving the 'normalisation probe' pin and detecting its signal on the inputs.
- Manages PWM signals for CV output and brightness control of the six LEDs.
- Sends audio outputs to external DAC.
- Reads EEPROM calibration, if set, for calibrated MIDI note CV outputs.


## Usage:
Basic usage is intended to be extremely simple. For example, here is complete code for a Sample and Hold card:

```c++
#include "ComputerCard.h"

// Derive a new class from ComputerCard
class SampleAndHold : public ComputerCard
{
public:
	virtual void ProcessSample() // executed every sample, at 48kHz
	{
		// Update audio out 1 with value from audio in 1,
		// only if rising edge seen on pulse 1 input
		if (PulseIn1RisingEdge()) AudioOut1(AudioIn1());
	}
};

int main()
{
	// Create and run our new Sample and Hold
	SampleAndHold sh;
	sh.Run();
}
```
  
More generally, the process is:
1. Add `#include "ComputerCard.h"` to each source file that uses it. If linking more than one source file, use `#define COMPUTERCARD_NOIMPL` before this include in all but one file, so that the implementation is only included once.

2. Derive a new class (such as the `SampleAndHold` class above) from the abstract `ComputerCard` class, representing the particular card being written.

3. Do any card-specific setup in the constructor of the derived class.

4. Override the pure virtual `ComputerCard::ProcessSample()` method to implement the per-sample functionality required for the particular card. `ComputerCard::ProcessSample()` is called at a fixed 48kHz sample rate.

5. Now, create an instance of the new derived class (for example, in `main()`)

6. If the normalisation probe is used, call the `ComputerCard::EnableNormalisationProbe()` method on this instance.

7. Call the `ComputerCard::Run()` method on this instance to start audio processing. `ComputerCard::Run()` is blocking (never returns).

### Examples
ComputerCard contains several examples in the `examples/` directory.
For beginners just starting with ComputerCard, the first example to look at is `passthrough` to introduce the basic functions, followed by `sample_and_hold` for typical usage of these in a 'real' card.

- `midi_device` — example of USB MIDI being used alongside ComputerCard. Sends Computer knob values to the USB host as CC messages.
- `normalisation_probe` — minimal example of patch cable detection. LEDs are lit when corresponding sockets have a jack plugged in.
- `passthrough` — simple demonstration of using the jacks, knobs, switch and LEDs.
- `sample_and_hold` — dual sample and hold, demonstrating jacks, normalisation probe and pseudo-random numbers
- `sine_wave` — 440Hz sine wave generator, demonstrating scanning and linear interpolation of a lookup table using integer arithmetic
- `talkie_pcm` — not a good pedagogical example, but a hack of the [TalkiePCM](https://github.com/pschatzmann/TalkiePCM/) speech library to make it interface with ComputerCard.
    
### Notes
- Make sure execution of `ComputerCard::ProcessSample` always runs quickly enough that it has returned before the next execution begins (1/48kHz = ~20μs). (See the [guidance below](#programming) on achieving this)
- It is anticipated that only one instance of a ComputerCard will be created.

### Limitations / potential future improvements
- Only core 0 of the RP2040 is used
    - In particular, this prevents the Pico SDK USB stdio from being used, as this code must run on core0 and interferes with the 48kHz audio callback
- No built-in delta-sigma modulation of CV outputs, limiting CV precision of 1V/octave signals to about 7 cents
- There is no way to configure CV/knob smoothing filters.
- There is no way to change the sample rate

## [Using the RPi Pico SDK (Linux command line)](#pico-sdk)
- Clone and install the [RPi Pico SDK](https://github.com/raspberrypi/pico-sdk)
- Set the `PICO_SDK_PATH` environment variable to the location at with the Pico SDK is installed
- Clone this repository
- Create a subdirectory `build` within this repository, and `cd` into it
- Run `cmake ..`
- Run `make`
- The example cards in `examples/` will be built, and all the `uf2` files all placed in the `build/` directory
- Flash one of these `uf2` files to the Workshop System Computer in the usual way

You can create your own projects using ComputerCard by
- creating a new directory and source file in `examples/` and adding the appropriate `add_example` line to `CMakeLists.txt`.
- or, this being a single-header library, by just copying `ComputerCard.h` into your own Pico SDK project.

## [Using Visual Studio Code (with RPi Pico plugin)](#vscode)
Disclaimer: the instructions below appear to work but are likely far from optimal (I am not a VSCode user myself)
- Install [Visual Studio Code](https://code.visualstudio.com/) 
- Install the [RPi Pico VSCode plugin](https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico)
- In VSCode, click the Raspberry Pi Pico icon on the Activity Bar (by default, the bottom icon on the bar of icons going down the left side of the screen)
- Click 'Import Project' in the 'Raspberry Pi Pico Project: Quick Access' bar that appears. Select this ComputerCard directory and continue to import the project
- Go to the CMake icon in the Activity Bar.
- Mouseover 'PROJECT OUTLINE' and click the 'Configure all' button (looks like a piece of paper with arrow pointing right out of it). The example projects should appear within the 'Project Outline' area.
- Build all of these by hovering over 'PROJECT OUTLINE' again and clicking the Build All button (looks like an arrow pointing into a container of dots)


## [Using the Arduino IDE](#arduino-ide)

Thanks to divmod for figuring this out and writing this section

### Installation

- Install Arduino IDE: (https://www.arduino.cc/en/software)[https://www.arduino.cc/en/software]
- Install Arduino Pico by adding it as a new board: (https://github.com/earlephilhower/arduino-pico?tab=readme-ov-file#installation)[https://github.com/earlephilhower/arduino-pico?tab=readme-ov-file#installation]

### Create a new sketch

- Open Arduino IDE and create a new sketch and save it
- In the Arduino IDE, select Raspberry Pi Pico as your board
- Download ComputerCard.h file from this ComputerCard repository
- Store this file in the directory of your sketch (there should be an .ino file with your sketch name in this directory).

### Code
- As a start you can just copy the Sample&Hold example from the following file into your sketch (the .ino file): https://raw.githubusercontent.com/TomWhitwell/Workshop_Computer/refs/heads/main/Demonstrations%2BHelloWorlds/PicoSDK/ComputerCard/examples/sample_and_hold/main.cpp
- Replace the `main` function with the following code:
```c++
SampleAndHold sh;

void setup() {
  sh.EnableNormalisationProbe();
}

void loop() {
  sh.Run();
}
```
- Note: The `Run()` method is blocking (never returns), and therefore takes over control from the Arduino `loop()` function. Code to be executed every sample goes in the ComputerCard `ProcessSample` function.
- Sketch -> Export Compiled Binary
- Your sketch directory should contain a build/rp2040.rp2040.rpipico directory (or similar) with a .uf2 file.
  Copy this file to your Computer.
- Done.


# [Programming for ComputerCard](#programming)

ComputerCard is designed to allow audio signals (with bandwidths up to ~20kHz) to be processed at low latency. To do this, the computations for each sample must be calculated individually, by calling the users `ProcessSample()` function at 48kHz. The `ProcessSample()` function for one sample must finish before the one for the next sample starts, meaning that the user's code for each sample must execute in 1/48000th of a second, or ~20μs. This is perfectly feasible on the RP2040, but requires some attention to code performance. Specifically;

1. calculations on audio signals usually need to be done with integers rather than floating point.
2. relatively lengthy calculations (more than ~20μs) must be done on a different RP2040 core to the audio, or split between `ProcessSample` calls to ensure than no one call goes above the maximum duration. In particular, USB handling must be on a different core. The `second_core` example shows one way to execute longer/slower computations for CV signals on the second core, while the `midi_device` example demonstrates one way to use USB on the second core. 
3. Particularly for larger programs, it may be necessary to force the code called by `ProcessSample` into RAM, so that delays in fetching of code from the flash card do not cause `ProcessSample()` to exceed its allowed time.

We'll discuss each of these below

## 1. Integer calculations

Floating-point operations on the RP2040 are software emulated, and are around [35 times slower](https://forums.raspberrypi.com/viewtopic.php?t=308794#p1848188) than the native 32-bit integer addition, subtraction and multiplication.

At the specified 133MHz clock rate, floating point operations take around 500ns, allowing at most 40 (in practice, likely rather fewer) per sample. Even a single call to more complicated floating point functions such as `sin` takes too long to run in the `ProcessSample` function. For this reason, the ComputerCard API does not use floating-point variables.

The approach I have taken instead is to use signed 32-bit integers (`int32_t`, as defined in the `cstdint` header) to process samples, essentially using a fixed-point number representation. The hardware 32-bit integer multiply on the RP2040 makes most operations on such numbers very efficient. 

### Example: crossfading audio signals
Let's look at an example of code which averages the two audio inputs and puts this mixed signal onto both audio outputs:

$$\mbox{out} = \frac{\mbox{in}_1 + \mbox{in}_2}{2}$$
```c++
int32_t mix = (AudioIn1() + AudioIn2()) >> 1;
AudioOut1(mix);
AudioOut2(mix);
```
Because the RP2040 has hardware multiply and shift instructions, but not a division instruction, where possible I replace divisions with multiply (`*`) and bit-shift-right (`>>`), which on the RP2040 acts as a divide-by-power-of-two on both signed and unsigned integers[^1].


[^1]: albeit with different rounding behaviour: for signed types `>>` is implementation-defined in C++, but GCC compiles to the ARM `ASR` (arithmetic shift right) instruction, which divides rounding to negative infinity.  C++ divide (`/`) rounds towards zero. 


An ever-present concern with integer operations such as these is the possibility of integer overflow (exceeding the representable range of integers), and the wrapping of values that occurs in this case. 
- C++ integer promotion rules mean that the `int16_t` return value of `AudioIn` functions (in fact only containing a 12-bit range -2048 to 2047) are promoted to the (32-bit signed) `int` before operations. The relevant wrapping values are therefore $\pm 2^{31}$, far larger than any integers used here.
- Saving `mix` to a 32-bit integer introduces wrapping at $\pm 2^{31}$, again not a problem here, given the 12-bit outputs of the `AudioIn` functions. (In fact, we could use an `int16_t` for the `mix` variable, but there is no need to do so.)

In addition to this, **integer values passed to the `AudioOut` functions will wrap if they are outside the 12-bit range -2048 to 2047**. In the present example it is relatively easy to show that this will not occur, but if there is any doubt, it is worth clamping the variable before sending it to the output functions, as follows:
```c++
int32_t mix = (AudioIn1() + AudioIn2()) >> 1;

if (mix<-2048) mix = -2048;
if (mix>2047) mix = 2047;

AudioOut1(mix);
AudioOut2(mix);
```

Let's now extend our program so that it uses the main knob to crossfade between the two audio inputs.
Denoting the knob value as $k$, varying from 0 to 1, our crossfade would be [^2]

$$ \mbox{out} = (1-k)\\, \mbox{in}_1 + k\\, \mbox{in}_2.$$ 

[^2]: Ideally, for audio signals, we'd use an energy-preserving crossfade rather than this linear one.

In ComputerCard, the knobs return positive 12-bit values 0-4095, and we instead calculate: 
```c++
int32_t k = KnobVal(Knob::Main);
int32_t mix = (AudioIn1()*(4095-k) + AudioIn2()*(k)) >> 12;

AudioOut1(mix);
AudioOut2(mix);
```

Even though `k` is non-negative, we use an signed `int32_t` type (not `uint32_t`) because operations involving both signed and unsigned integers will often result in the signed type being converted to unsigned, which is not desired here. It's easiest just to keep everything as `int32_t`.

The choice of `4095 - k` not `4096 - k` means that this crossfade perfectly isolates each of the two inputs at the ends of knob travel, at the expense of a very slight decline ($4095/4096$) in amplitude.

Let's again examine the possibility of overflow. The multiply operations here calculate the product of signed 12-bit and unsigned 12-bit numbers, resulting in a signed 24-bit number. We then add two of these, which would in general produce up to a signed 25-bit number, but here because of the `k` and `4095-k` multiplicands, the sum is in fact signed 24-bit. This is well within the signed 32-bit range of the integer type, so there is no risk of overflow during the evaluation of the expression. Shifting right by 12 bits produces a signed 12-bit result (-2048 to 2047) which will not wrap in the `AudioOut` functions.


### Example: first-order IIR LPF
One of the most common DSP operations is a first-order filter, such as the IIR lowpass filter

$$ y[n] = a y[n-1] + b x[n], $$

with filter coefficients $a$ ($0 < a < 1$) and $b$. For unity gain at DC, $b = 1-a$, and so our filter equation,

$$ y[n] = a y[n-1] + (1-a) x[n], $$

looks almost identical to the crossfade example above. Choosing the input data $x$ to be the first audio input, and sending the filter output $y$ to the first audio output, we might write

```c++
int32_t a = 4089;
y = (a*y + (4096-a)*AudioIn1()) >> 12;

int32_t out = y;
if (out<-2048) out=-2048;
if (out>2047) out=2047;
AudioOut1(out);
```
where 
- the filter coefficient value $a = 0.9983 \approx 4089/2^{12}$ corresponds to a cutoff of about 13Hz – very low for audio but typical of an AC-coupling filter or a filter for CV,
- `y` is assumed to be an `int32_t` that persists between calls to `ProcessSample()` (likely a class member), and
- clipping has been applied to the filter output, as it is not obvious that this will be in the 12-bit range -2048 to 2047 for all possible input signals

However, as well as integer overflow, another consideration with integer/fixed-point arithmetic is roundoff error. Suppose that in the code above, previous audio input has resulted in `y` having the value 500, and the audio input then falls silent (`AudioIn1` returns zero). For this low-pass filter, we would expect `y` to drop to zero exponentially. The relevant expression becomes:
```c++
y = (4089*y) >> 12;
```
Repeated evaluation of this shows that the decay of `y` from an initial value of 500 is far from exponential – in fact it drops linearly to zero! In this problem the amount by which `y` decreases per sample is between 0 and 1 if evaluated exactly, but in integer arithmetic this decay must be an integer and is quantised to a decay of 1 per sample.

This problem can be mitigated by amplifying the signal going into the filter by a large factor, the attenuating the filter output by the same amount. Here we amplify and attenuate by $2^7 = 128$:
```c++
int32_t a = 4089;
y = (a*y + (4096-a)*(AudioIn1()<<7)) >> 12;

int32_t out = y>>7;
if (out<-2048) out=-2048;
if (out>2047) out=2047;

AudioOut1(out);
```
This gives a much closer approximation to the floating-point behaviour.

As before, we must check for overflow, and for the first time in these examples, we are here getting close to overflow with 32-bit integers.  The expression `(4096-a)*(AudioIn1()<<7)` is `(unsigned 12-bit) * (signed 12-bit) << 7`, giving a signed 31-bit result, to which `a*y`, 24-bit value, is added, potentially using all 32 bits. There is a tradeoff here between:
- precision with which `a` can be specified (here, 12-bit)
- bit-depth of the audio signal (here, 12-bit)
- amount of amplification/attenuation to reduce roundoff (here, 7-bit)
which together must not exceed the 32 bits available. Different operations may favour a different allocation of bits. 

Three further comments:
- Sometimes, depending on the filter, the increased precision offered by the `int64_t` type is needed, though this is slower than `int32_t`.
- On the RP2040, the `>>` operation on signed types rounds to negative infinity. Sometimes it may be worth adding a constant into the filter expression to alter this behaviour to round-to-nearest.  
- The roundoff error on a low-pass filter such as this produces a very primative hysteresis-like effect, which may occasionally be useful. This is used in ComputerCard to reduce noise/jitter in knob values.

### 2. Lengthy calculations
Options for dealing with calculations that exceed the available ~20μs are:
- optimise these calculations, for example using lookup tables [^3]
- split the calculations that do not have to be done every sample up in to parts small enough to do in successive `ProcessSample` functions,
- offload long calculations onto the second RP2040 core.

For USB processing, the TinyUSB function `tud_task` may take longer than one sample time, and so this needs to be done on a different core from the audio. See the `midi_device` example for how this can be done. I'm planning to add some multicore stuff into ComputerCard itself, in due course, including an option to run the audio callback on core1, not the default core0.

[^3]: While floating point calculations are typically too slow to perform every sample, it's convenient to have them available for calculating lookup tables when the card first starts. Lookup tables can of course be calculated on a much more powerful computer and hard-coded as constant arrays.

### 4. Putting code in RAM
To force a function into RAM, rather than flash, surround the name in function definition by  [__not_in_flash_func()](https://www.raspberrypi.com/documentation/pico-sdk/runtime.html#group_pico_platform_1gad9ab05c9a8f0ab455a5e11773d610787). The `ComputerCard.h` file has various examples of this. 
