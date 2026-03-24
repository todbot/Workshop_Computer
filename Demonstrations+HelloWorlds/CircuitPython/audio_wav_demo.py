"""
Demonstrate playing audio out the DAC for mtm_computer

"""

import time
import math
import audiocore

from mtm_computer import Computer, map_range

comp = Computer()

wav = audiocore.WaveFile("amenfull_22k_s16.wav") 
comp.play_audio(wav)

while True:
    print("hi", time.monotonic())
    time.sleep(0.3)


