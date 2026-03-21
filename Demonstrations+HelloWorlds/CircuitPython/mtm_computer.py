# SPDX-FileCopyrightText: Copyright (c) 2024 Tod Kurt
# SPDX-FileCopyrightText: Copyright (c) 2024 Tom Whitwell
# SPDX-FileCopyrightText: Copyright (c) 2025 Chris Johnson (ComputerCard.h reference)
#
# SPDX-License-Identifier: MIT
"""
`mtm_computer`
================================================================================

CircuitPython library for Music Thing Modular Workshop System Computer.

Mirrors the functionality of the ComputerCard.h C++ library (v0.2.8),
including EEPROM calibration for accurate CV output in volts and MIDI notes.

All public-facing analog values follow the CircuitPython convention of
16-bit unsigned (0-65535):
  - Knob properties return 0-65535
  - CV input properties return 0-65535 (32768 ~ 0V)
  - cv_out properties accept 0-65535 (32768 ~ 0V)
  - LED brightness accepts 0-65535

The calibrated output methods (cv_out_midi_note, cv_out_millivolts) use
their own natural units (MIDI note numbers and millivolts respectively)
since those are already well-defined physical quantities.

Audio output via DMA/interrupts is not supported in CircuitPython;
use the pulse_outs_to_audio() helper for audiopwmio-based audio instead.

* Author(s): Tod Kurt, Tom Whitwell -- updated with calibration
"""

import microcontroller
microcontroller.cpu.frequency = 200_000_000

import time
import board
import busio
import digitalio
import analogio
import pwmio

import audiopwmio
import audiomixer

try:
    import mtm_hardware 
    _HAS_MTM_HARDWRE = True
except ImportError:
    _HAS_MTM_HARDWARE = False
    print("No mtm_hardware module, no audio out via DAC")

# ---------------------------------------------------------------------------
# Pin definitions (matching ComputerCard.h)
# ---------------------------------------------------------------------------

# ADC input pins
AUDIO_L_IN_1 = board.GP27  # Note: swapped vs C++ (GP27=ADC1, GP26=ADC0)
AUDIO_R_IN_1 = board.GP26
MUX_IO_1 = board.A2        # GP28
MUX_IO_2 = board.A3        # GP29

# Mux control pins
MUX_LOGIC_A = board.GP24
MUX_LOGIC_B = board.GP25

# Pulse I/O pins
PULSE_1_INPUT = board.GP2
PULSE_2_INPUT = board.GP3
PULSE_1_RAW_OUT = board.GP8
PULSE_2_RAW_OUT = board.GP9

# CV output PWM pins
CV_OUT_1 = board.GP23
CV_OUT_2 = board.GP22

# DAC SPI pins (MCP4822 external DAC for audio output)
DAC_CS = board.GP21
DAC_SDI = board.GP19
DAC_SCK = board.GP18

# EEPROM I2C pins
EEPROM_SDA = board.GP16
EEPROM_SCL = board.GP17

# Board ID pins
BOARD_ID_0 = board.GP7
BOARD_ID_1 = board.GP6
BOARD_ID_2 = board.GP5

# Normalisation probe pin
NORMALISATION_PROBE = board.GP4

# LED pins (layout: 0 1 / 2 3 / 4 5)
LED_PINS = (board.GP10, board.GP11, board.GP12, board.GP13, board.GP14, board.GP15)

# DAC channel config bits for MCP4822
DAC_CONFIG_CHAN_A = 0x3000  # Channel A, 1x gain, active
DAC_CONFIG_CHAN_B = 0xB000  # Channel B, 1x gain, active

# EEPROM constants (from ComputerCard.h)
EEPROM_PAGE_ADDRESS = 0x50
EEPROM_ADDR_ID = 0
EEPROM_ADDR_VERSION = 2
EEPROM_ADDR_CRC_H = 86
EEPROM_ADDR_CRC_L = 87
EEPROM_VAL_ID = 2001
EEPROM_NUM_BYTES = 88

# Hardware version IDs
HARDWARE_PROTO1 = 0x2A
HARDWARE_PROTO2_REV1 = 0x30
HARDWARE_REV1_1 = 0x0C
HARDWARE_UNKNOWN = 0xFF


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def map_range(s, a1, a2, b1, b2):
    """Linear interpolation, like Arduino map()."""
    return b1 + ((s - a1) * (b2 - b1) / (a2 - a1))


def clamp(value, lo, hi):
    """Clamp a value to [lo, hi]."""
    if value < lo:
        return lo
    if value > hi:
        return hi
    return value


def gamma_correct(x):
    """Simple quadratic LED gamma correction. Input/output 0-65535."""
    return clamp(int((x * x) / 65535), 0, 65535)


def _crc_ccitt(data):
    """CRC-CCITT (0xFFFF initial, 0x1021 polynomial) matching ComputerCard.h."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


# ---------------------------------------------------------------------------
# Calibration data structures
# ---------------------------------------------------------------------------

class _CalPoint:
    """A single calibration point: target voltage (in 0.1V units) and DAC setting."""
    __slots__ = ('voltage', 'dac_setting')

    def __init__(self, voltage=0, dac_setting=0):
        self.voltage = voltage        # int8, in tenths of a volt
        self.dac_setting = dac_setting  # uint32, 19-bit sigma-delta target


class _CalCoeffs:
    """Linear regression coefficients for one CV output channel."""
    __slots__ = ('m', 'b', 'mi', 'bi')

    def __init__(self):
        self.m = 0.0    # float slope
        self.b = 0.0    # float intercept
        self.mi = 0      # integer slope (scaled for MIDI conversion)
        self.bi = 0      # integer intercept


# ---------------------------------------------------------------------------
# Main Computer class
# ---------------------------------------------------------------------------

class Computer:
    """
    Manages all hardware I/O on the Music Thing Modular Workshop System Computer.

    All public analog values use the CircuitPython 16-bit convention (0-65535):

      Knobs:    0-65535 (0 = fully CCW, 65535 = fully CW)
      CV in:    0-65535 (0 ~ -5V, 32768 ~ 0V, 65535 ~ +5V)
      CV out:   0-65535 (0 ~ -5V, 32768 ~ 0V, 65535 ~ +5V)
      LEDs:     0-65535 (0 = off, 65535 = full brightness)
      Switch:   0-65535 raw, or use switch_position for 0/1/2

    Calibrated output methods use physical units:
      cv_out_midi_note()  -- MIDI note 0-127
      cv_out_millivolts() -- millivolts -6000 to +6000

    Internally, the hardware uses 12-bit ADC (0-4095) and a 19-bit
    sigma-delta CV DAC (0-524287). All conversions are handled internally.
    """

    CAL_MAX_CHANNELS = 2
    CAL_MAX_POINTS = 10

    # Internal scaling constants
    _CV_DAC_MAX = 524287   # 19-bit sigma-delta range
    _CV_DAC_MID = 262144   # ~0V in 19-bit space

    def __init__(self, read_calibration=True):
        # ---------------------------------------------------------------
        # Internal state (all in native hardware resolution)
        # ---------------------------------------------------------------
        self._knobs_12 = [0, 0, 0, 0]      # 12-bit knob values
        self._knobs_smooth = [0, 0, 0, 0]   # IIR accumulators (scaled)
        self._cv_12 = [0, 0]                 # Signed 12-bit CV input
        self._cv_smooth = [0, 0]             # IIR accumulators for CV
        self._pulse = [False, False]
        self._last_pulse = [False, False]
        self._mux_state = 0

        # CV output: 19-bit sigma-delta target values (0-524287, mid ~262144)
        self._cv_value = [self._CV_DAC_MID, self._CV_DAC_MID]

        # Calibration
        self._cal_points = [[_CalPoint() for _ in range(self.CAL_MAX_POINTS)]
                            for _ in range(self.CAL_MAX_CHANNELS)]
        self._num_cal_points = [0] * self.CAL_MAX_CHANNELS
        self._cal_coeffs = [_CalCoeffs() for _ in range(self.CAL_MAX_CHANNELS)]
        self._cv_outs_calibrated = False

        # Hardware version
        self._hw_version = HARDWARE_UNKNOWN

        self.leds = []
        for pin in LED_PINS:
            led = pwmio.PWMOut(pin, frequency=60_000, duty_cycle=0)
            self.leds.append(led)

        self._mux_a = digitalio.DigitalInOut(MUX_LOGIC_A)
        self._mux_b = digitalio.DigitalInOut(MUX_LOGIC_B)
        self._mux_a.switch_to_output()
        self._mux_b.switch_to_output()

        self._analog_mux1 = analogio.AnalogIn(MUX_IO_1)
        self._analog_mux2 = analogio.AnalogIn(MUX_IO_2)

        self._pulse_1_in = digitalio.DigitalInOut(PULSE_1_INPUT)
        self._pulse_2_in = digitalio.DigitalInOut(PULSE_2_INPUT)
        self._pulse_1_in.switch_to_input(pull=digitalio.Pull.UP)
        self._pulse_2_in.switch_to_input(pull=digitalio.Pull.UP)

        self._pulse_1_out = digitalio.DigitalInOut(PULSE_1_RAW_OUT)
        self._pulse_2_out = digitalio.DigitalInOut(PULSE_2_RAW_OUT)
        self._pulse_1_out.switch_to_output(value=True)
        self._pulse_2_out.switch_to_output(value=True)

        # ---------------------------------------------------------------
        # CV outputs (PWM-based sigma-delta, using 16-bit CircuitPython PWM)
        # The C++ lib uses an IRQ-driven sigma-delta modulator over an
        # 11-bit (2048-wrap) PWM; here we approximate with high-frequency
        # 16-bit PWM mapped from the internal 19-bit target.
        # ---------------------------------------------------------------
        self._cv_1_pwm = pwmio.PWMOut(CV_OUT_1, frequency=125_000, duty_cycle=32768)
        self._cv_2_pwm = pwmio.PWMOut(CV_OUT_2, frequency=125_000, duty_cycle=32768)

        # EEPROM (I2C) and calibration
        self._i2c = None
        if read_calibration:
            try:
                self._i2c = busio.I2C(scl=EEPROM_SCL, sda=EEPROM_SDA,
                                       frequency=100_000)
                self._read_eeprom()
            except Exception as e:
                print("EEPROM init warning:", e)
                self._set_default_calibration()
        else:
            self._set_default_calibration()

        self._hw_version = self._probe_hardware_version()

        # Pre-read all mux inputs by cycling through 4 states twice
        for _ in range(8):
            self.update()


    def update(self):
        """
        Read one mux channel's worth of inputs.

        Call this frequently in your main loop. Each call reads one of the
        four mux positions (knob + CV pair). After four calls, all inputs
        have been refreshed once.
        """
        mux = self._mux_state

        # Read current mux position (CircuitPython returns 16-bit)
        raw1 = self._analog_mux1.value
        raw2 = self._analog_mux2.value

        # Convert 16-bit to 12-bit for internal processing
        adc1 = raw1 >> 4
        adc2 = raw2 >> 4
        
        # Advance mux to next state for the NEXT read (settling time)
        next_mux = (mux + 1) & 3
        self._mux_a.value = bool(next_mux & 1)
        self._mux_b.value = bool(next_mux & 2)

        # Knob smoothing #(~60Hz LPF: (127*acc + 16*sample) >> 7)
        #self._knobs_smooth[mux] = (127 * self._knobs_smooth[mux] + 16 * adc1) >> 7
        self._knobs_smooth[mux] = (3 * self._knobs_smooth[mux] + 16 * adc1) >> 2
        self._knobs_12[mux] = self._knobs_smooth[mux] >> 4

        # CV smoothing (~240Hz LPF: (15*acc + 16*sample) >> 4)
        cvi = mux & 1  # CV channels alternate: mux 0,2 -> CV0; mux 1,3 -> CV1
        self._cv_smooth[cvi] = (3 * self._cv_smooth[cvi] + raw2) >> 2
        # Internal signed 12-bit: 2048 = 0V, inverted from hardware
        self._cv_12[cvi] = 2048 - (self._cv_smooth[cvi] >> 4)

        # Pulse inputs (inverted: hardware is active-low)
        self._last_pulse[0] = self._pulse[0]
        self._last_pulse[1] = self._pulse[1]
        self._pulse[0] = not self._pulse_1_in.value
        self._pulse[1] = not self._pulse_2_in.value

        self._mux_state = next_mux

        # Update CV output PWMs from 19-bit sigma-delta targets
        self._update_cv_pwm()

    def _update_cv_pwm(self):
        """Map internal 19-bit CV target (0-524287) to 16-bit PWM duty cycle."""
        self._cv_1_pwm.duty_cycle = clamp(
            (self._cv_value[0] * 65535) // self._CV_DAC_MAX, 0, 65535)
        self._cv_2_pwm.duty_cycle = clamp(
            (self._cv_value[1] * 65535) // self._CV_DAC_MAX, 0, 65535)

    @property
    def knob_main(self):
        """Main knob position, 0-65535."""
        return clamp(self._knobs_12[0] << 4, 0, 65535)

    @property
    def knob_x(self):
        """X knob position, 0-65535."""
        return clamp(self._knobs_12[1] << 4, 0, 65535)

    @property
    def knob_y(self):
        """Y knob position, 0-65535."""
        return clamp(self._knobs_12[2] << 4, 0, 65535)


    @property
    def switch(self):
        """Raw switch value, 0-65535."""
        return clamp(self._knobs_12[3] << 4, 0, 65535)

    @property
    def switch_position(self):
        """Switch position: 0=Down, 1=Middle, 2=Up."""
        val = self._knobs_12[3]  # 12-bit
        return (val > 1000) + (val > 3000)

    # ===================================================================
    # CV inputs -- 0-65535 (CircuitPython 16-bit convention)
    #   0 ~ -5V, 32768 ~ 0V, 65535 ~ +5V
    # ===================================================================

    @property
    def cv_1_in(self):
        """CV input 1, 0-65535 (32768 ~ 0V)."""
        # Internal: signed -2048..+2047 -> unsigned 0..65535
        return clamp((self._cv_12[0] + 2048) << 4, 0, 65535)

    @property
    def cv_2_in(self):
        """CV input 2, 0-65535 (32768 ~ 0V)."""
        return clamp((self._cv_12[1] + 2048) << 4, 0, 65535)

    def cv_in(self, channel):
        """CV input by channel index (0 or 1), 0-65535."""
        return clamp((self._cv_12[channel] + 2048) << 4, 0, 65535)

    # ===================================================================
    # CV outputs -- standard 16-bit (CircuitPython convention)
    #   0 ~ max negative voltage, 32768 ~ 0V, 65535 ~ max positive voltage
    # ===================================================================

    @property
    def cv_1_out(self):
        """Current CV output 1 setting, 0-65535."""
        return clamp(
            (self._cv_value[0] * 65535) // self._CV_DAC_MAX, 0, 65535)

    @cv_1_out.setter
    def cv_1_out(self, val):
        """Set CV output 1, 0-65535 (32768 ~ 0V)."""
        val = clamp(int(val), 0, 65535)
        self._cv_value[0] = (val * self._CV_DAC_MAX) // 65535

    @property
    def cv_2_out(self):
        """Current CV output 2 setting, 0-65535."""
        return clamp(
            (self._cv_value[1] * 65535) // self._CV_DAC_MAX, 0, 65535)

    @cv_2_out.setter
    def cv_2_out(self, val):
        """Set CV output 2, 0-65535 (32768 ~ 0V)."""
        val = clamp(int(val), 0, 65535)
        self._cv_value[1] = (val * self._CV_DAC_MAX) // 65535

    def cv_out(self, channel, val):
        """Set CV output by channel (0 or 1), value 0-65535 (32768 ~ 0V)."""
        val = clamp(int(val), 0, 65535)
        self._cv_value[channel] = (val * self._CV_DAC_MAX) // 65535


    # CV outputs -- calibrated MIDI note (matching C++ CVOutMIDINote)

    def cv_out_midi_note(self, channel, note_num):
        """
        Set CV output from MIDI note number (0-127).

        Uses EEPROM calibration data for accurate 1V/oct output.
        Note 60 (middle C) corresponds to 0V.
        """
        self._cv_value[channel] = self._midi_to_dac(note_num, channel)

    def cv_out_1_midi_note(self, note_num):
        """Set CV output 1 from MIDI note number (0-127)."""
        self.cv_out_midi_note(0, note_num)

    def cv_out_2_midi_note(self, note_num):
        """Set CV output 2 from MIDI note number (0-127)."""
        self.cv_out_midi_note(1, note_num)


    # CV outputs -- calibrated millivolts (matching C++ CVOutMillivolts)

    def cv_out_millivolts(self, channel, millivolts):
        """
        Set CV output to a voltage in millivolts (-6000 to 6000).

        Uses EEPROM calibration data. Returns True if the requested
        voltage was outside the DAC's range and was clamped.
        """
        limited = False
        coeffs = self._cal_coeffs[channel]
        dac_value = (((coeffs.mi * millivolts) >> 9) * 1573 >> 12) + coeffs.bi
        if dac_value > self._CV_DAC_MAX:
            dac_value = self._CV_DAC_MAX
            limited = True
        if dac_value < 0:
            dac_value = 0
            limited = True
        self._cv_value[channel] = dac_value
        return limited

    def cv_out_1_millivolts(self, millivolts):
        """Set CV output 1 to millivolts. Returns True if clamped."""
        return self.cv_out_millivolts(0, millivolts)

    def cv_out_2_millivolts(self, millivolts):
        """Set CV output 2 to millivolts. Returns True if clamped."""
        return self.cv_out_millivolts(1, millivolts)

    # ===================================================================

    def pulse_in(self, channel):
        """Read pulse input state (True = high/active)."""
        return self._pulse[channel]

    @property
    def pulse_1_in(self):
        """Pulse input 1 state."""
        return self._pulse[0]

    @property
    def pulse_2_in(self):
        """Pulse input 2 state."""
        return self._pulse[1]

    def pulse_in_rising_edge(self, channel):
        """True for one update cycle on pulse rising edge."""
        return self._pulse[channel] and not self._last_pulse[channel]

    def pulse_in_falling_edge(self, channel):
        """True for one update cycle on pulse falling edge."""
        return not self._pulse[channel] and self._last_pulse[channel]

    @property
    def pulse_1_rising_edge(self):
        """True for one update cycle on pulse 1 rising edge."""
        return self.pulse_in_rising_edge(0)

    @property
    def pulse_1_falling_edge(self):
        """True for one update cycle on pulse 1 falling edge."""
        return self.pulse_in_falling_edge(0)

    @property
    def pulse_2_rising_edge(self):
        """True for one update cycle on pulse 2 rising edge."""
        return self.pulse_in_rising_edge(1)

    @property
    def pulse_2_falling_edge(self):
        """True for one update cycle on pulse 2 falling edge."""
        return self.pulse_in_falling_edge(1)

    # ===================================================================

    def pulse_out(self, channel, val):
        """Set pulse output (True = on). Channel 0 or 1."""
        if channel == 0:
            self._pulse_1_out.value = not val  # active low
        else:
            self._pulse_2_out.value = not val

    def pulse_out_1(self, val):
        """Set pulse output 1 (True = on)."""
        self._pulse_1_out.value = not val

    def pulse_out_2(self, val):
        """Set pulse output 2 (True = on)."""
        self._pulse_2_out.value = not val

    # ===================================================================

    def led_brightness(self, index, value):
        """
        Set LED brightness, 0-65535.
        Applies gamma correction internally (quadratic curve).
        """
        self.leds[index].duty_cycle = gamma_correct(clamp(int(value), 0, 65535))

    def led_on(self, index, on=True):
        """Turn LED on or off."""
        self.leds[index].duty_cycle = 65535 if on else 0

    def led_off(self, index):
        """Turn LED off."""
        self.leds[index].duty_cycle = 0

    # ===================================================================

    # def audio_out(self, channel, val):
    #     """
    #     Write to MCP4822 audio DAC. Value: 0-65535 (CircuitPython convention,
    #     32768 = zero crossing). Channel: 0 (A) or 1 (B).

    #     Note: In the C++ library this is driven by DMA at 48kHz.
    #     In CircuitPython, call this manually or use pulse_outs_to_audio()
    #     for audiopwmio-based playback instead.
    #     """
    #     # Convert 16-bit unsigned to signed 12-bit (-2048..2047)
    #     signed_12 = (clamp(int(val), 0, 65535) >> 4) - 2048
    #     # Invert to counteract inverting output configuration
    #     signed_12 = -signed_12
    #     dac_channel = 0x0000 if channel == 0 else 0x8000
    #     dac_data = (dac_channel | 0x3000) | (((signed_12 & 0x0FFF) + 0x800) & 0x0FFF)
    #     self._dac_write_raw(dac_data)

    # def _dac_write_raw(self, data_16bit):
    #     """Write a raw 16-bit value to the MCP4822."""
    #     buf = bytes((data_16bit >> 8, data_16bit & 0xFF))
    #     if self._dac_spi.try_lock():
    #         self._dac_cs.value = False
    #         self._dac_spi.write(buf)
    #         self._dac_cs.value = True
    #         self._dac_spi.unlock()

    # ===================================================================
    # Audio via audiopwmio (CircuitPython-specific helper)
    # ===================================================================

    def pulse_outs_to_audio(self, sample_rate=22050, voice_count=5,
                            channel_count=2):
        """
        Repurpose pulse output pins for PWM audio playback.

        This releases the pulse output DigitalInOut objects and creates
        an audiopwmio.PWMAudioOut + audiomixer.Mixer on those pins.
        After calling this, pulse_out_1/pulse_out_2 will no longer work.
        """
        # Release pulse output pins
        if self._pulse_1_out:
            self._pulse_1_out.deinit()
            self._pulse_1_out = None
        if self._pulse_2_out:
            self._pulse_2_out.deinit()
            self._pulse_2_out = None
        if hasattr(self, 'audio') and self.audio:
            self.audio.deinit()

        self.audio_pwm = audiopwmio.PWMAudioOut(
            left_channel=PULSE_1_RAW_OUT,
            right_channel=PULSE_2_RAW_OUT)
        self.mixer_pwm = audiomixer.Mixer(
            voice_count=voice_count,
            sample_rate=sample_rate,
            channel_count=channel_count,
            bits_per_sample=16,
            samples_signed=True,
            buffer_size=2048)
        self.audio_pwm.play(self.mixer_pwm)

    # ===================================================================
    # Hardware version
    # ===================================================================

    @property
    def hardware_version(self):
        """Hardware version code (see HARDWARE_* constants)."""
        return self._hw_version

    @property
    def cv_outs_calibrated(self):
        """True if valid EEPROM calibration was loaded."""
        return self._cv_outs_calibrated

    # ===================================================================
    # EEPROM / Calibration -- private methods
    # ===================================================================

    def _set_default_calibration(self):
        """Set default calibration values (used when EEPROM is absent/invalid)."""
        for ch in range(self.CAL_MAX_CHANNELS):
            self._num_cal_points[ch] = 3
            self._cal_points[ch][0].voltage = -20   # -2.0V
            self._cal_points[ch][0].dac_setting = 347700
            self._cal_points[ch][1].voltage = 0      # 0.0V
            self._cal_points[ch][1].dac_setting = 261200
            self._cal_points[ch][2].voltage = 20     # +2.0V
            self._cal_points[ch][2].dac_setting = 174400
            self._calc_cal_coeffs(ch)
        self._cv_outs_calibrated = False

    def _read_eeprom(self):
        """Read and parse calibration data from the onboard EEPROM."""
        self._set_default_calibration()

        if self._i2c is None:
            return

        # Read magic number
        magic = self._read_int_from_eeprom(EEPROM_ADDR_ID)
        if magic != EEPROM_VAL_ID:
            print("EEPROM: no valid magic number found (got", magic, ")")
            return

        # Read entire EEPROM block
        buf = bytearray(EEPROM_NUM_BYTES)
        for i in range(EEPROM_NUM_BYTES):
            buf[i] = self._read_byte_from_eeprom(i)

        # Verify CRC
        calculated_crc = _crc_ccitt(buf[:86])
        found_crc = (buf[EEPROM_ADDR_CRC_H] << 8) | buf[EEPROM_ADDR_CRC_L]
        if calculated_crc != found_crc:
            print("EEPROM: CRC mismatch (calc:", hex(calculated_crc),
                  "found:", hex(found_crc), ")")
            return

        # Parse calibration table
        for ch in range(self.CAL_MAX_CHANNELS):
            offset = 4 + (41 * ch)  # channel 0 at byte 4, channel 1 at 45
            num_points = buf[offset]
            offset += 1
            if num_points > self.CAL_MAX_POINTS:
                num_points = self.CAL_MAX_POINTS

            self._num_cal_points[ch] = num_points
            for pt in range(num_points):
                # voltage is int8 (signed, in tenths of a volt)
                voltage = buf[offset]
                if voltage > 127:
                    voltage -= 256  # unsigned byte -> signed
                offset += 1

                # dacSetting is uint32 (4 bytes, big-endian)
                dac_setting = (
                    (buf[offset] << 24) |
                    (buf[offset + 1] << 16) |
                    (buf[offset + 2] << 8) |
                    buf[offset + 3]
                )
                offset += 4

                self._cal_points[ch][pt].voltage = voltage
                self._cal_points[ch][pt].dac_setting = dac_setting

            self._calc_cal_coeffs(ch)

        self._cv_outs_calibrated = True
        print("EEPROM: calibration loaded OK")

    def _calc_cal_coeffs(self, channel):
        """
        Compute linear regression coefficients from calibration points.
        Exactly mirrors ComputerCard::CalcCalCoeffs.
        """
        n = self._num_cal_points[channel]
        sum_v = 0.0
        sum_dac = 0.0
        sum_v2 = 0.0
        sum_v_dac = 0.0

        for i in range(n):
            v = self._cal_points[channel][i].voltage * 0.1  # tenths -> volts
            dac = float(self._cal_points[channel][i].dac_setting)
            sum_v += v
            sum_dac += dac
            sum_v2 += v * v
            sum_v_dac += v * dac

        denom = n * sum_v2 - sum_v * sum_v
        coeffs = self._cal_coeffs[channel]

        if denom != 0.0:
            coeffs.m = (n * sum_v_dac - sum_v * sum_dac) / denom
        else:
            coeffs.m = 0.0

        coeffs.b = (sum_dac - coeffs.m * sum_v) / n

        # Integer versions for fast MIDI/millivolt conversion
        coeffs.mi = int(coeffs.m * (4 / 3) + 0.5)
        coeffs.bi = int(coeffs.b + 0.5)

    def _midi_to_dac(self, midi_note, channel):
        """Convert MIDI note to 19-bit DAC value. Mirrors ComputerCard::MIDIToDAC."""
        coeffs = self._cal_coeffs[channel]
        dac_value = ((coeffs.mi * (midi_note - 60)) >> 4) + coeffs.bi
        return clamp(dac_value, 0, self._CV_DAC_MAX)

    def _read_byte_from_eeprom(self, address):
        """Read a single byte from the EEPROM via I2C."""
        device_addr = EEPROM_PAGE_ADDRESS | ((address >> 8) & 0x0F)
        addr_low = bytes([address & 0xFF])
        result = bytearray(1)

        while not self._i2c.try_lock():
            pass
        try:
            self._i2c.writeto(device_addr, addr_low)
            self._i2c.readfrom_into(device_addr, result)
        finally:
            self._i2c.unlock()
        return result[0]

    def _read_int_from_eeprom(self, address):
        """Read a 16-bit big-endian integer from the EEPROM."""
        high = self._read_byte_from_eeprom(address)
        low = self._read_byte_from_eeprom(address + 1)
        return (high << 8) | low

    def _probe_hardware_version(self):
        """
        Detect board hardware version from ID pins.
        Mirrors ComputerCard::ProbeHardwareVersion.
        """
        try:
            id0 = digitalio.DigitalInOut(BOARD_ID_0)
            id1 = digitalio.DigitalInOut(BOARD_ID_1)
            id2 = digitalio.DigitalInOut(BOARD_ID_2)

            # Pull-down and read
            id0.switch_to_input(pull=digitalio.Pull.DOWN)
            id1.switch_to_input(pull=digitalio.Pull.DOWN)
            id2.switch_to_input(pull=digitalio.Pull.DOWN)
            time.sleep(0.001)
            pd = int(id0.value) | (int(id1.value) << 2) | (int(id2.value) << 4)

            # Pull-up and read
            id0.pull = digitalio.Pull.UP
            id1.pull = digitalio.Pull.UP
            id2.pull = digitalio.Pull.UP
            time.sleep(0.001)
            pu = (int(id0.value) << 1) | (int(id1.value) << 3) | (int(id2.value) << 5)

            # Cleanup
            id0.deinit()
            id1.deinit()
            id2.deinit()

            board_id = pd | pu

            if board_id in (HARDWARE_PROTO1, HARDWARE_PROTO2_REV1,
                            HARDWARE_REV1_1):
                return board_id
            return HARDWARE_UNKNOWN
        except Exception:
            return HARDWARE_UNKNOWN

    # ===================================================================
    # Debug / info
    # ===================================================================

    def print_calibration(self):
        """Print the loaded calibration table to the console."""
        for ch in range(self.CAL_MAX_CHANNELS):
            n = self._num_cal_points[ch]
            print("CV Out %d: %d calibration points" % (ch + 1, n))
            for i in range(n):
                pt = self._cal_points[ch][i]
                print("  %+.1fV -> DAC %d" % (pt.voltage / 10, pt.dac_setting))
            c = self._cal_coeffs[ch]
            print("  Coeffs: m=%.2f b=%.2f mi=%d bi=%d" % (c.m, c.b, c.mi, c.bi))
        print("Calibrated:", self._cv_outs_calibrated)
        print("HW version:", hex(self._hw_version))
