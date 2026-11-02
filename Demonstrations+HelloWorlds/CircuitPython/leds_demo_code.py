"""
Demonstrate how to set the LEDs on MTM Computer
"""
import time

from mtm_computer import Computer

computer = Computer()

print('hello')
i = 0
while True:
    print("led on", i)
    computer.led_off(i-1)
    computer.led_brightness(i, 65535)  # 0-65535
    i = (i+1) % len(computer.leds)
    time.sleep(0.2)

