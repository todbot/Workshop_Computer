# ComputerCard

ComputerCard is a header-only C++ library, providing a framework that
manages the hardware aspects of the [Music Thing Modular Workshop
System Computer](https://www.musicthing.co.uk/workshopsystem/).

ComputerCard was designed to work with the [RPi Pico SDK](https://github.com/raspberrypi/pico-sdk) but also works with the Arduino environment using the earlephilhower RP2040 board [as described below](#arduino-ide)

It aims to present a very simple C++ interface for card programmers 
to use the jacks, knobs, switch and LEDs, for programs running at
a fixed 48kHz audio sample rate.

Behind the scenes, the ComputerCard class:
- Manages the ADC and external multiplexer to collect analogue samples from audio inputs, CV inputs, knobs and switch.
- Applies smoothing, some simple hysteresis, and range extension to knobs and CV, to provide relatively low-noise values that span the whole range (-2048 to 2047). Knob/CV values are updated every audio frame.
- If enabled, determines whether jacks are connected to the six input sockets by driving the 'normalisation probe' pin and detecting its signal on the inputs.
- Manages PWM signals for CV output and brightness control of the six LEDs.
- Sends audio outputs to external DAC
- Reads EEPROM calibration, if set, to for calibrated MIDI note CV outputs.


## Usage:
Basic usage is intended to be extremely simple: for example, here is complete code for a Sample and Hold card:

```{.cpp}
#include "ComputerCard.h"

// Derive a new class from ComputerCard
class SampleAndHold : public ComputerCard
{
public:
	virtual void ProcessSample() // executed every sample at 48kHz
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
1. Add `#include "ComputerCard.h"` to each cpp file that uses it. Make sure to `#define COMPUTERCARD_NOIMPL` before this include in all but one translation unit, so that the implementation is only included once.

2. Derive a new class (such as the `SampleAndHold` class above) from the abstract ComputerCard class, representing the particular card being written.

3. Do any card-specific setup in the constructor of the derived class

4. Override the pure virtual `ComputerCard::ProcessSample()` method to implement the per-sample functionality required for the particular card. `ComputerCard::ProcessSample()` is called at a fixed 48kHz sample rate.

5. Now, create an instance of the new derived class (for example, in `main()`)

6. Call the `ComputerCard::EnableNormalisationProbe()` method on this instance, if the normalisation probe is used.

7. Call the `ComputerCard::Run()` method on this instance to start audio processing. `ComputerCard::Run()` is blocking (never returns).


### Notes
- Make sure execution of `ComputerCard::ProcessSample` always runs quickly enough that it has returned before the next execution begins (~20us).
- It is anticipated that only one instance of a ComputerCard will be created.

### Limitations / potential future improvements
- Only core 0 of the RP2040 is used
    - In particular, this prevents the Pico SDK USB stdio from being used, as this code must run on core0 and interferes with the 48kHz audio callback
- No built-in delta-sigma modulation of CV outputs, limiting CV precision of 1V/octave signals to about 7 cents
- There is no way to configure CV/knob smoothing filters


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
```
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
