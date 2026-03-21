"""
Demonstrate how to update and read the inputs on MTM Computer
"""
import time
from mtm_computer import Computer

comp = Computer()

last_time = 0

while True:
    comp.update()
    
    # LEDs reflect state of knobs/CV
    comp.led_brightness(0, comp.knob_main)
    comp.led_brightness(1, comp.knob_x)
    comp.led_brightness(2, comp.knob_y)
    comp.led_brightness(3, comp.switch)
    comp.led_brightness(4, comp.cv_1_in)
    comp.led_brightness(5, comp.cv_2_in)
    
    # CV outs echo the values of the X,Y knob
    comp.cv_1_out = comp.knob_x
    comp.cv_2_out = comp.knob_y

    # Print out the input state periodically
    if time.monotonic() - last_time > 0.1:
        last_time = time.monotonic()
        print("knobs: main=%5d x=%5d y=%5d sw=%5d" %
              (comp.knob_main, comp.knob_x, comp.knob_y, comp.switch),
              "cvin:", comp.cv_1_in, comp.cv_2_in)

