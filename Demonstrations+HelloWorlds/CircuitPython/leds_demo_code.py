"""
Demonstrate how to set the LEDs on MTM Computer
"""
import time
from mtm_computer import Computer

computer = Computer()

computer.print_calibration()

i = 0
while True:
    # Turn on/off LEDs
    for _ in range(24):
        print("LED on", i)
        computer.led_off(i-1)  # turn off last LED
        computer.led_on(i)  # turn on next LED
        i = (i+1) % len(computer.leds)
        time.sleep(0.2)
    computer.led_off(i-1)
        
    # Set brightness of LEDs
    for i in range(len(computer.leds)):
        print("fading up LED", i)
        for j in range(100):
            computer.led_brightness(i, j*600)
            time.sleep(0.01)
