"""
Demonstrate how to do some simple CV output

Functions:
- main knob -- controls frequency of CV outs
- X knob -- amplitude of CV 1 out
- Y knob -- amplitude of CV 2 out
- switch -- phase difference of X/Y
- LEDs -- brightness mirrors state of knobs, bottom two LEDs show CV output

"""
import time
import math
from mtm_computer import Computer, map_range

comp = Computer()

last_time = 0

t = 0
dt = 0.01

while True:
    comp.update()

    dt = map_range(comp.knob_main, 0,65535, 0.0001, 1)
    phase = map_range(comp.switch, 0,65535, 0, math.pi*2)
    a1 = comp.knob_x / 2 
    a2 = comp.knob_y / 2
    comp.cv_1_out = int( a1 * math.sin(t) + a1 )
    comp.cv_2_out = int( a2 * math.sin(t + phase) + a2 )
    t += dt
    
    # LEDs reflect state of knobs/CV
    comp.led_brightness(0, comp.knob_main)
    comp.led_brightness(1, comp.knob_x)
    comp.led_brightness(2, comp.knob_y)
    comp.led_brightness(3, comp.switch)
    comp.led_brightness(4, comp.cv_1_out)
    comp.led_brightness(5, comp.cv_2_out)
    
    # Print out the input state periodically
    if time.monotonic() - last_time > 0.1:
        last_time = time.monotonic()
        print("knobs:", comp.knob_main, comp.knob_x, comp.knob_y, comp.switch,
              " cvin:", comp.cv_1_in, comp.cv_2_in,
              "cvout:", comp.cv_1_out, comp.cv_1_out)

