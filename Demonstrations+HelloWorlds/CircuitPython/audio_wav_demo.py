"""
Demonstrate playing audio out the DAC

"""

import time
import math
import audiocore

from mtm_computer import Computer, map_range

comp = Computer()

wav0 = audiocore.WaveFile("amenfull_22k_s16.wav")  # stereo

comp.play_audio(wav0)

while True:
    print("hi", time.monotonic())
    time.sleep(0.3)
