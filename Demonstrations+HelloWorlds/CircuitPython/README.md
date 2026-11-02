# CircuitPython for MTM Computer

## Setup

1. Download the MTM build of CircuitPython from
   https://github.com/todbot/circuitpython/releases
   It includes the `mcp4822` module needed for audio out the Audio Out jacks.
   Stock CircuitPython for the
   [Raspberry Pi Pico](https://circuitpython.org/board/raspberry_pi_pico/)
   also works, but audio then goes out the Pulse Out jacks as PWM.

2. Put the MTM Computer in RPI-RP2 UF2 bootloader mode
   (e.g. hold BOOTSEL button under main knob + RESET button then release
   BOOTSEL)

3. Drag the CircuitPython UF2 file to the RPI-RP2 drive.
   Your computer may indicate a write error, this is normal with UF2
   bootloading

4. Wait a few seconds and a CIRCUITPY drive will appear on your desktop

5. Copy the `mtm_computer.py` file to the CIRCUITPY drive.

6. Copy one of the demo files to CIRCUITPY drive. Rename it `code.py`.

7. Open up a serial terminal to the CircuitPython REPL
   (using Mu, Thonny, tio, screen) to see the output of the demo


## Demos

- [`leds_demo_code.py`](./leds_demo_code.py)
  -- Simple chase pattern on the six LEDs

- [`code_knobs_demo.py`](./code_knobs_demo.py)
  -- Read all mux inputs (knobs, switch, CVs ins),
   and mirror them on the LEDs, X-knob and Y-knob send out CV too

- [`two_cv_sines_code.py`](./two_cv_sines_code.py)
  -- Output two sine waves on CV 1 and CV 2, controllable with knobs & switch

- [`beatfader_demo.py`](./beatfader_demo.py)
  -- Fade between drum loops playing out the audio outs


## Using `mtm_computer`

```py
from mtm_computer import Computer
comp = Computer()   # sets up all I/O, reads calibration from EEPROM
while True:
    comp.update()   # call often, reads knobs, switch, CV & pulse ins
    comp.cv_1_out = comp.knob_x
    comp.led_brightness(0, comp.knob_main)
```

Startup and calibration mirror the C++
[ComputerCard](../PicoSDK/ComputerCard/) library:

- Analog values are 0-65535. For CV ins and outs, 32768 is about 0V and
  65535 about +6V.
- `cv_out_midi_note(ch, note)` and `cv_out_millivolts(ch, mv)` use the
  card's EEPROM calibration for accurate 1V/oct (note 60 = 0V).
  `cv_in_millivolts(ch)` and `audio_in_millivolts(ch)` use the input
  calibration. Check `comp.cv_outs_calibrated` and `comp.inputs_calibrated`,
  or `comp.print_calibration()`.
- `switch_position` is 0 (down), 1 (middle), or 2 (up).
- `pulse_in(ch)`, `pulse_in_rising_edge(ch)`, `pulse_out(ch, val)`
- `enable_normalisation_probe()` turns on `connected(Computer.CV1)` etc.
  jack detection.
- `audio_out()` starts an `audiomixer.Mixer` playing out Audio Out 1 & 2 and
  returns it. Without the `mcp4822` module it falls back to
  `pulse_outs_to_audio()`.
- `hardware_version`, `unique_id`

The CPU is set to 192 MHz (a multiple of 48 MHz) for less ADC noise.
Pass `Computer(cpu_freq=None)` to leave it alone.


## Implementation notes

- CV outs are PWM, like ComputerCard, but without its sigma-delta
  modulation, so they have about 11 bits of resolution.

- An easy way to put the Computer into UF2 mode without pulling off the knob
  is to paste this into the REPL:
  ```py
  import microcontroller
  microcontroller.on_next_reset(microcontroller.RunMode.BOOTLOADER)
  microcontroller.reset()
  ```
- And to save the UF2 of the entire CircuitPython setup, do:
  `picotool save -r 0x10000000 0x10200000  my_app.uf2`
  Note that this saves only the first 2MB of the 16MB flash. This should be
  fine unless you're storing lots of WAV files or other assets.
