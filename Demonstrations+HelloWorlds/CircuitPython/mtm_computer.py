# SPDX-FileCopyrightText: Copyright (c) 2024 Tod Kurt
# SPDX-FileCopyrightText: Copyright (c) 2024 Tom Whitwell
# SPDX-FileCopyrightText: Copyright (c) 2025 Chris Johnson (ComputerCard.h reference)
#
# SPDX-License-Identifier: MIT
"""
`mtm_computer`
================================================================================

CircuitPython library for Music Thing Modular Workshop System Computer.

Mirrors the startup and calibration of the ComputerCard.h C++ library (v0.4.0),
including EEPROM calibration for CV outputs (volts, MIDI notes) and inputs.

Public analog values use the CircuitPython 16-bit convention (0-65535):
  - Knobs: 0-65535
  - CV in / CV out: 0-65535, 32768 ~ 0V, 65535 ~ +6V
  - LED brightness: 0-65535

Calibrated methods use physical units: ``cv_out_midi_note()``,
``cv_out_millivolts()``, ``cv_in_millivolts()``, ``audio_in_millivolts()``.

Audio out the Audio Out jacks needs the MTM CircuitPython build with the
``mcp4822`` module, see ``audio_out()``.

* Author(s): Tod Kurt, Tom Whitwell
"""

import time
import microcontroller
import board
import busio
import digitalio
import analogio
import pwmio
import audiomixer

# Pins (matching ComputerCard.h)
AUDIO_IN_1 = board.GP27
AUDIO_IN_2 = board.GP26
MUX_IO_1 = board.GP28
MUX_IO_2 = board.GP29
MUX_LOGIC_A = board.GP24
MUX_LOGIC_B = board.GP25
PULSE_1_INPUT = board.GP2
PULSE_2_INPUT = board.GP3
PULSE_1_RAW_OUT = board.GP8
PULSE_2_RAW_OUT = board.GP9
CV_OUT_1 = board.GP23
CV_OUT_2 = board.GP22
DAC_CS = board.GP21
DAC_SDI = board.GP19
DAC_SCK = board.GP18
EEPROM_SDA = board.GP16
EEPROM_SCL = board.GP17
BOARD_ID_0 = board.GP7
BOARD_ID_1 = board.GP6
BOARD_ID_2 = board.GP5
NORMALISATION_PROBE = board.GP4
# layout: 0 1 / 2 3 / 4 5
LED_PINS = (board.GP10, board.GP11, board.GP12, board.GP13, board.GP14, board.GP15)

EEPROM_ADDRESS = 0x50
EEPROM_VAL_ID = 2001
EEPROM_NUM_BYTES = 88
EEPROM_INPUT_ADDR = 88
EEPROM_INPUT_VAL_ID = 2002
EEPROM_INPUT_NUM_BYTES = 38

HARDWARE_PROTO1 = 0x2A
HARDWARE_PROTO2_REV1 = 0x30
HARDWARE_REV1_1 = 0x0C
HARDWARE_UNKNOWN = 0xFF

# Sigma-delta DAC space that the EEPROM calibration is stored in.
# Higher values give lower voltages (inverting output stage).
_CV_DAC_MAX = 524287


def map_range(s, a1, a2, b1, b2):
    """Like Arduino ``map()``"""
    return b1 + ((s - a1) * (b2 - b1) / (a2 - a1))

def clamp(value, lo, hi):
    return lo if value < lo else hi if value > hi else value

def gamma_correct(x):
    """Simple quadratic LED gamma correction, 0-65535 in & out"""
    return clamp(int((x * x) / 65535), 0, 65535)


def _crc_ccitt(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc

def _xorshift32(x):
    x ^= (x << 13) & 0xFFFFFFFF
    x ^= x >> 17
    x ^= (x << 5) & 0xFFFFFFFF
    return x

def _signed32(b, i):
    v = (b[i] << 24) | (b[i+1] << 16) | (b[i+2] << 8) | b[i+3]
    return v - (1 << 32) if v & 0x80000000 else v


class Computer:
    """
    Sets up and manages all hardware I/O on the Workshop System Computer.
    Call ``update()`` often in your main loop to read the knobs & inputs.
    """

    # Input indices for connected() / disconnected()
    AUDIO1 = 0
    AUDIO2 = 1
    CV1 = 2
    CV2 = 3
    PULSE1 = 4
    PULSE2 = 5

    def __init__(self, cpu_freq=192_000_000):
        # Multiple of 48MHz reduces ADC noise, as in ComputerCard
        if cpu_freq:
            try:
                microcontroller.cpu.frequency = cpu_freq
            except (AttributeError, NotImplementedError, ValueError) as e:
                print("could not set cpu frequency:", e)

        self._knobs = [0] * 4        # 12-bit
        self._knobs_smooth = [0] * 4  # 16-bit
        self._cv = [0, 0]             # signed 12-bit, + is positive voltage
        self._cv_smooth = [0, 0]      # 16-bit
        self._pulse = [False, False]
        self._last_pulse = [False, False]
        self._mux_state = 0
        self._cv_value = [0, 0]       # in _CV_DAC_MAX space

        self._use_norm_probe = False
        self._norm_probe_count = 0
        self._norm_probe_seed = 1
        self._np = 0                  # expected probe bit history
        self._plug_state = [0] * 6    # observed bit history per input
        self._connected = [False] * 6

        self.leds = [pwmio.PWMOut(pin, frequency=60_000, duty_cycle=0)
                     for pin in LED_PINS]

        self._audio_in = (analogio.AnalogIn(AUDIO_IN_1),
                          analogio.AnalogIn(AUDIO_IN_2))
        self._analog_mux1 = analogio.AnalogIn(MUX_IO_1)
        self._analog_mux2 = analogio.AnalogIn(MUX_IO_2)
        self._mux_a = digitalio.DigitalInOut(MUX_LOGIC_A)
        self._mux_b = digitalio.DigitalInOut(MUX_LOGIC_B)
        self._mux_a.switch_to_output()
        self._mux_b.switch_to_output()

        # raw high = jack low
        self._pulse_out = [digitalio.DigitalInOut(PULSE_1_RAW_OUT),
                           digitalio.DigitalInOut(PULSE_2_RAW_OUT)]
        for p in self._pulse_out:
            p.switch_to_output(value=True)

        # pull-up needed to activate input transistors
        self._pulse_in = [digitalio.DigitalInOut(PULSE_1_INPUT),
                          digitalio.DigitalInOut(PULSE_2_INPUT)]
        for p in self._pulse_in:
            p.switch_to_input(pull=digitalio.Pull.UP)

        # Same PWM rate as ComputerCard, but no sigma-delta, so ~11-bit
        cv_freq = microcontroller.cpu.frequency // 2000
        self._cv_pwm = [pwmio.PWMOut(CV_OUT_1, frequency=cv_freq, duty_cycle=32768),
                        pwmio.PWMOut(CV_OUT_2, frequency=cv_freq, duty_cycle=32768)]

        self._norm_probe_pin = digitalio.DigitalInOut(NORMALISATION_PROBE)
        self._norm_probe_pin.switch_to_output(value=False)

        self._hw_version = self._probe_hardware_version()

        self._cal_points = [[], []]   # (tenths of volt, dac setting) per channel
        self._cal_coeffs = [None, None]  # (mi, bi) per channel
        self._input_cal = [(0, 192000)] * 4  # (adc offset, mV per adc Q16)
        try:
            self._i2c = busio.I2C(scl=EEPROM_SCL, sda=EEPROM_SDA,
                                  frequency=100_000)
        except (RuntimeError, ValueError) as e:
            print("EEPROM I2C init failed:", e)
            self._i2c = None
        self._cv_outs_calibrated = self._read_eeprom()
        self._inputs_calibrated = self._read_input_eeprom()

        self.cv_1_out = 32768
        self.cv_2_out = 32768

        self.mixer = None
        self._audio = None

        for _ in range(64):  # let smoothing settle on all mux inputs
            self.update()

    def update(self):
        """Read one mux position (one knob + one CV in). Call often."""
        mux = self._mux_state
        raw1 = self._analog_mux1.value
        raw2 = self._analog_mux2.value

        # switch mux now so it settles before the next read
        next_mux = (mux + 1) & 3
        self._mux_a.value = bool(next_mux & 1)
        self._mux_b.value = bool(next_mux & 2)

        self._knobs_smooth[mux] = (3 * self._knobs_smooth[mux] + raw1) >> 2
        self._knobs[mux] = self._knobs_smooth[mux] >> 4

        cvi = mux & 1  # mux 0,2: CV1, mux 1,3: CV2
        self._cv_smooth[cvi] = (3 * self._cv_smooth[cvi] + raw2) >> 2
        self._cv[cvi] = clamp(2048 - (self._cv_smooth[cvi] >> 4), -2048, 2047)

        self._last_pulse[0] = self._pulse[0]
        self._last_pulse[1] = self._pulse[1]
        self._pulse[0] = not self._pulse_in[0].value
        self._pulse[1] = not self._pulse_in[1].value

        if self._use_norm_probe:
            self._update_norm_probe(cvi, raw2 >> 4)

        self._mux_state = next_mux

    # Normalisation probe: GP4 sends a pseudo-random bit pattern, which reaches
    # an input through its switched jack only when nothing is plugged in.
    # Same 16-step cycle as ComputerCard, one step per update().

    def enable_normalisation_probe(self):
        """Enable connected()/disconnected() jack detection.
        Disconnected inputs then read as 0V / low."""
        self._use_norm_probe = True

    def connected(self, input_index):
        """True if a jack is plugged into input (e.g. ``Computer.CV1``)"""
        return self._connected[input_index]

    def disconnected(self, input_index):
        return not self._connected[input_index]

    def _update_norm_probe(self, cvi, adc2):
        npc = self._norm_probe_count
        ps = self._plug_state
        if npc == 0:
            self._norm_probe_seed = (1664525 * self._norm_probe_seed
                                     + 1013904223) & 0xFFFFFFFF
            bit = self._norm_probe_seed >> 31
            self._norm_probe_pin.value = bool(bit)
            self._np = ((self._np << 1) | bit) & 0xFFFFFFFF
        # CV ins alternate mux positions, so take one each over two steps
        if npc == 14 or npc == 15:
            ps[self.CV1 + cvi] = ((ps[self.CV1 + cvi] << 1)
                                  | (adc2 < 1800)) & 0xFFFFFFFF
        if npc == 15:
            for i in (self.AUDIO1, self.AUDIO2):
                low = (self._audio_in[i].value >> 4) < 1800
                ps[i] = ((ps[i] << 1) | low) & 0xFFFFFFFF
            for i in (0, 1):
                ps[self.PULSE1 + i] = ((ps[self.PULSE1 + i] << 1)
                                       | self._pulse[i]) & 0xFFFFFFFF
            for i in range(6):
                self._connected[i] = self._np != ps[i]

        if not self._connected[self.CV1]:
            self._cv[0] = 0
        if not self._connected[self.CV2]:
            self._cv[1] = 0
        if not self._connected[self.PULSE1]:
            self._pulse[0] = False
        if not self._connected[self.PULSE2]:
            self._pulse[1] = False
        self._norm_probe_count = (npc + 1) & 0xF

    # Knobs & switch

    @property
    def knob_main(self):
        """Main knob, 0-65535"""
        return self._knobs[0] << 4

    @property
    def knob_x(self):
        """X knob, 0-65535"""
        return self._knobs[1] << 4

    @property
    def knob_y(self):
        """Y knob, 0-65535"""
        return self._knobs[2] << 4

    @property
    def switch(self):
        """Raw switch value, 0-65535"""
        return self._knobs[3] << 4

    @property
    def switch_position(self):
        """Switch position: 0=down (momentary), 1=middle, 2=up"""
        val = self._knobs[3]
        return (val > 1000) + (val > 3000)

    # CV inputs

    @property
    def cv_1_in(self):
        """CV in 1, 0-65535 (32768 ~ 0V)"""
        return (self._cv[0] + 2048) << 4

    @property
    def cv_2_in(self):
        """CV in 2, 0-65535 (32768 ~ 0V)"""
        return (self._cv[1] + 2048) << 4

    def cv_in(self, channel):
        """CV in by channel (0 or 1), 0-65535 (32768 ~ 0V)"""
        return (self._cv[channel] + 2048) << 4

    def cv_in_millivolts(self, channel):
        """CV in by channel (0 or 1) in millivolts, using input calibration"""
        return self._adc_to_millivolts(self._cv[channel], 2 + channel)

    # Audio inputs, read directly (not smoothed)

    def audio_in(self, channel):
        """Audio in by channel (0 or 1), 0-65535 (32768 ~ 0V)"""
        return 65535 - self._audio_in[channel].value

    def audio_in_millivolts(self, channel):
        """Audio in by channel (0 or 1) in millivolts, using input calibration"""
        adc = 2048 - (self._audio_in[channel].value >> 4)
        return self._adc_to_millivolts(adc, channel)

    def _adc_to_millivolts(self, adc, cal_channel):
        offset, mv_per_adc_q16 = self._input_cal[cal_channel]
        return ((adc - offset) * mv_per_adc_q16) >> 16

    # CV outputs

    def _set_cv(self, channel, dac):
        dac = clamp(dac, 0, _CV_DAC_MAX)
        self._cv_value[channel] = dac
        self._cv_pwm[channel].duty_cycle = dac >> 3

    @property
    def cv_1_out(self):
        """CV out 1, 0-65535 (32768 ~ 0V, 65535 ~ +6V), uncalibrated"""
        return (_CV_DAC_MAX - self._cv_value[0]) >> 3

    @cv_1_out.setter
    def cv_1_out(self, val):
        self.cv_out(0, val)

    @property
    def cv_2_out(self):
        """CV out 2, 0-65535 (32768 ~ 0V, 65535 ~ +6V), uncalibrated"""
        return (_CV_DAC_MAX - self._cv_value[1]) >> 3

    @cv_2_out.setter
    def cv_2_out(self, val):
        self.cv_out(1, val)

    def cv_out(self, channel, val):
        """Set CV out by channel (0 or 1), 0-65535 (32768 ~ 0V), uncalibrated"""
        self._set_cv(channel, _CV_DAC_MAX - (clamp(int(val), 0, 65535) << 3))

    def cv_out_midi_note(self, channel, note_num):
        """Set CV out from MIDI note, calibrated 1V/oct, note 60 = 0V"""
        mi, bi = self._cal_coeffs[channel]
        self._set_cv(channel, ((mi * (int(note_num) - 60)) >> 4) + bi)

    def cv_out_millivolts(self, channel, millivolts):
        """Set CV out to millivolts (-6000 to 6000), calibrated.
        Returns True if the value was out of range and clamped."""
        mi, bi = self._cal_coeffs[channel]
        dac = ((((mi * int(millivolts)) >> 9) * 1573) >> 12) + bi
        self._set_cv(channel, dac)
        return not 0 <= dac <= _CV_DAC_MAX

    # Pulse I/O

    def pulse_in(self, channel):
        """Pulse in by channel (0 or 1), True = high"""
        return self._pulse[channel]

    @property
    def pulse_1_in(self):
        return self._pulse[0]

    @property
    def pulse_2_in(self):
        return self._pulse[1]

    def pulse_in_rising_edge(self, channel):
        """True for one update() after pulse in goes high"""
        return self._pulse[channel] and not self._last_pulse[channel]

    def pulse_in_falling_edge(self, channel):
        """True for one update() after pulse in goes low"""
        return not self._pulse[channel] and self._last_pulse[channel]

    def pulse_out(self, channel, val):
        """Set pulse out by channel (0 or 1), True = high"""
        self._pulse_out[channel].value = not val

    # LEDs

    def led_brightness(self, index, value):
        """Set LED brightness 0-65535, gamma corrected"""
        self.leds[index].duty_cycle = gamma_correct(clamp(int(value), 0, 65535))

    def led_on(self, index, on=True):
        self.leds[index].duty_cycle = 65535 if on else 0

    def led_off(self, index):
        self.leds[index].duty_cycle = 0

    # Audio

    def audio_out(self, sample_rate=22050, voice_count=2, channel_count=2,
                  buffer_size=2048):
        """Start an ``audiomixer.Mixer`` playing out Audio Out 1 & 2 via the
        MCP4822 DAC, and return it (also as ``self.mixer``).
        Falls back to ``pulse_outs_to_audio()`` without the ``mcp4822`` module."""
        try:
            import mcp4822
        except ImportError:
            print("no mcp4822 module (needs MTM CircuitPython), audio on pulse outs")
            return self.pulse_outs_to_audio(sample_rate, voice_count, channel_count)
        self._stop_audio()
        self._audio = mcp4822.MCP4822(DAC_SCK, DAC_SDI, DAC_CS, gain=1)
        return self._start_mixer(sample_rate, voice_count, channel_count, buffer_size)

    def pulse_outs_to_audio(self, sample_rate=22050, voice_count=2,
                            channel_count=2, buffer_size=2048):
        """Like ``audio_out()`` but PWM audio on the pulse outs, which then
        can't be used with ``pulse_out()``"""
        import audiopwmio
        self._stop_audio()
        for p in self._pulse_out:
            if p:
                p.deinit()
        self._pulse_out = [None, None]
        self._audio = audiopwmio.PWMAudioOut(left_channel=PULSE_1_RAW_OUT,
                                             right_channel=PULSE_2_RAW_OUT)
        return self._start_mixer(sample_rate, voice_count, channel_count, buffer_size)

    def _stop_audio(self):
        if self._audio:
            self._audio.deinit()
            self._audio = None

    def _start_mixer(self, sample_rate, voice_count, channel_count, buffer_size):
        self.mixer = audiomixer.Mixer(voice_count=voice_count,
                                      sample_rate=sample_rate,
                                      channel_count=channel_count,
                                      bits_per_sample=16,
                                      samples_signed=True,
                                      buffer_size=buffer_size)
        self._audio.play(self.mixer)
        return self.mixer

    # Hardware info

    @property
    def hardware_version(self):
        """Hardware version, one of the HARDWARE_* constants"""
        return self._hw_version

    @property
    def cv_outs_calibrated(self):
        """True if CV out calibration was read from EEPROM"""
        return self._cv_outs_calibrated

    @property
    def inputs_calibrated(self):
        """True if input calibration was read from EEPROM"""
        return self._inputs_calibrated

    @property
    def unique_id(self):
        """64-bit card ID from the flash chip, mixed as in ComputerCard"""
        uid = int.from_bytes(microcontroller.cpu.uid, "little")
        lo, hi = uid & 0xFFFFFFFF, uid >> 32
        for _ in range(6):
            lo = _xorshift32(lo ^ hi)
            hi = _xorshift32(hi ^ lo)
        return (hi << 32) | lo

    def print_calibration(self):
        for ch in (0, 1):
            print("CV Out %d:" % (ch + 1))
            for volts10, dac in self._cal_points[ch]:
                print("  %+.1fV -> DAC %d" % (volts10 / 10, dac))
            print("  mi=%d bi=%d" % self._cal_coeffs[ch])
        print("CV outs calibrated:", self._cv_outs_calibrated)
        print("inputs calibrated:", self._inputs_calibrated, self._input_cal)
        print("HW version:", hex(self._hw_version))

    # Startup internals

    def _probe_hardware_version(self):
        pins = [digitalio.DigitalInOut(p) for p in (BOARD_ID_0, BOARD_ID_1, BOARD_ID_2)]
        for p in pins:
            p.switch_to_input(pull=digitalio.Pull.DOWN)
        time.sleep(0.001)
        board_id = sum(p.value << (2 * i) for i, p in enumerate(pins))
        for p in pins:
            p.pull = digitalio.Pull.UP
        time.sleep(0.001)
        board_id |= sum(p.value << (2 * i + 1) for i, p in enumerate(pins))
        for p in pins:
            p.deinit()
        if board_id in (HARDWARE_PROTO1, HARDWARE_PROTO2_REV1, HARDWARE_REV1_1):
            return board_id
        return HARDWARE_UNKNOWN

    def _read_eeprom_bytes(self, address, count):
        """Read count bytes from EEPROM, or None on failure"""
        if self._i2c is None:
            return None
        buf = bytearray(count)
        while not self._i2c.try_lock():
            pass
        try:
            self._i2c.writeto_then_readfrom(EEPROM_ADDRESS, bytes((address,)), buf)
        except OSError:
            return None
        finally:
            self._i2c.unlock()
        return buf

    def _set_default_calibration(self, channel):
        self._cal_points[channel] = [(-20, 347700), (0, 261200), (20, 174400)]
        self._calc_cal_coeffs(channel)

    def _calc_cal_coeffs(self, channel):
        """Linear fit of calibration points, as ComputerCard::CalcCalCoeffs"""
        pts = self._cal_points[channel]
        n = len(pts)
        sum_v = sum_dac = sum_v2 = sum_vdac = 0.0
        for volts10, dac in pts:
            v = volts10 * 0.1
            sum_v += v
            sum_dac += dac
            sum_v2 += v * v
            sum_vdac += v * dac
        denom = n * sum_v2 - sum_v * sum_v
        m = (n * sum_vdac - sum_v * sum_dac) / denom if denom else 0.0
        b = (sum_dac - m * sum_v) / n
        self._cal_coeffs[channel] = (int(m * 4 / 3 + 0.5), int(b + 0.5))

    def _read_eeprom(self):
        """Load CV out calibration, return True if valid for both channels"""
        for ch in (0, 1):
            self._set_default_calibration(ch)
        buf = self._read_eeprom_bytes(0, EEPROM_NUM_BYTES)
        if buf is None or ((buf[0] << 8) | buf[1]) != EEPROM_VAL_ID:
            return False
        if _crc_ccitt(buf[:86]) != (buf[86] << 8) | buf[87]:
            return False
        ok = True
        for ch in (0, 1):
            offset = 4 + 41 * ch
            num_points = buf[offset]
            if not 2 <= num_points <= 10:
                ok = False  # leave channel on defaults
                continue
            pts = []
            for i in range(num_points):
                o = offset + 1 + 5 * i
                volts10 = buf[o] - 256 if buf[o] > 127 else buf[o]
                pts.append((volts10, _signed32(buf, o + 1) & 0xFFFFFFFF))
            self._cal_points[ch] = pts
            self._calc_cal_coeffs(ch)
        return ok

    def _read_input_eeprom(self):
        """Load input calibration, return True if valid"""
        buf = self._read_eeprom_bytes(EEPROM_INPUT_ADDR, EEPROM_INPUT_NUM_BYTES)
        if buf is None or ((buf[0] << 8) | buf[1]) != EEPROM_INPUT_VAL_ID:
            return False
        if _crc_ccitt(buf[:36]) != (buf[36] << 8) | buf[37]:
            return False
        self._input_cal = [(_signed32(buf, 4 + 8 * ch), _signed32(buf, 8 + 8 * ch))
                           for ch in range(4)]
        return True
