#include "ComputerCard.h"
#include <cmath>

/// SlowMod

// 256 values + 1 for interpolation
// see dev/knobmapping.py
int32_t knob_vals[257] = {
  12, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 27, 28, 30, 31, 33, 35, 36, 38, 40, 42, 44, 46, 48, 50, 53, 55, 57, 60, 62, 65, 68, 70, 73, 76, 79, 82, 85, 88, 91, 94, 97, 101, 104, 107, 111, 114, 118, 122, 125, 129, 133, 137, 141, 145, 149, 153, 157, 161, 166, 170, 175, 179, 184, 188, 193, 198, 202, 193, 195, 197, 199, 201, 203, 205, 208, 210, 213, 215, 218, 221, 224, 227, 230, 234, 237, 241, 245, 248, 252, 256, 261, 265, 270, 274, 279, 284, 289, 295, 300, 306, 312, 318, 324, 330, 337, 343, 350, 358, 365, 372, 380, 388, 396, 405, 414, 422, 432, 441, 451, 460, 471, 481, 492, 503, 514, 525, 537, 549, 562, 574, 587, 600, 614, 628, 642, 657, 672, 687, 702, 718, 735, 751, 768, 786, 803, 822, 840, 859, 878, 898, 918, 939, 960, 981, 1003, 1025, 1048, 1071, 1095, 1119, 1144, 1169, 1195, 1221, 1247, 1275, 1302, 1330, 1359, 1388, 1418, 1448, 1479, 1511, 1543, 1575, 1608, 1642, 1677, 1712, 1747, 1783, 1820, 1858, 1896, 1935, 1974, 2014, 2055, 2097, 2139, 2182, 2225, 2270, 2315, 3580, 3786, 4008, 4246, 4502, 4777, 5071, 5387, 5726, 6089, 6478, 6895, 7341, 7819, 8330, 8877, 9461, 10087, 10755, 11469, 12231, 13046, 13915, 14843, 15833, 16889, 18014, 19215, 20494, 21857, 23308, 24854, 26500, 28252, 30115, 32098, 34206, 36448, 38831, 41363, 44053, 46910, 49944, 53165, 56584, 60212, 64061, 68142, 72471, 77059, 81922, 81922
};

// Takes 0-4095 from CV returns target frequency as Hz (Q12)
int32_t KnobToHzQ12(int32_t in) {
  int32_t r = in & 0x0f;  // lower 4-bit
  in >>= 4;               // x now 8-bit number, 0-255
  int32_t s1 = knob_vals[in];
  int32_t s2 = knob_vals[in + 1];
  return (s2 * r + s1 * (16 - r)) >> 4;
}

uint32_t __not_in_flash_func(rnd)() {
  static uint32_t lcg_seed = 1;
  lcg_seed = 1664525 * lcg_seed + 1013904223;
  return lcg_seed;
}

class SlowMod : public ComputerCard {
  static constexpr int npts = 8192;
  int32_t sinevals[npts];

  bool led_show_phase = true;
  bool switch_is_down = false;

  // randomize phases
  void rndPhase() {
    phase1 = rnd();
    phase2 = rnd();
    phase3 = rnd();
    phase4 = rnd();
    phase5 = rnd();
    phase6 = rnd();
  }

  // Crossfade between a and b by f from 0 to 4095.
  int32_t crossfade(int32_t a, int32_t b, uint32_t f) {
    if (f > 4095) {
      f = 4095;
    }

    int64_t diff = (int64_t)(b - a);
    int64_t crossfade_val = (int64_t)a * (4095) + diff * f;

    return (int32_t)(crossfade_val >> 12);
  }

  uint32_t phase1, phase2, phase3, phase4, phase5, phase6;
  int32_t val1, val2, val3, val4, val5, val6;

public:
  SlowMod() {
    for (int i = 0; i < npts; i++) {
      // just shy of 2^22 * sin
      sinevals[i] = 2048 * 2040 * sin(2 * i * M_PI / double(npts));
    }

    rndPhase();
  }
  // Return sin given 32 bit index x, return 2^19 * sin(y)
  int32_t sinval(uint32_t x) {
    // shift from 32 bit to 13+8
    x >>= 11;
    x &= 0x001FFFFF;       // wrap at 21 bits = 13+8 bits
    int32_t r = x & 0xFF;  //
    x >>= 8;               // x now 13-bit number, 0-8191
    int32_t s1 = sinevals[x];
    int32_t s2 = sinevals[(x + 1) & 0x1FFF];
    return (s2 * r + s1 * (256 - r)) >> 8 >> 3;
  }

  void SetAudio1(int32_t cv_val) {
    static int32_t error = 0;
    cv_val += 524288;
    uint32_t truncated_cv_val = (cv_val - error) & 0xFFFFFF00;
    error += truncated_cv_val - cv_val;
    int16_t val = int32_t(truncated_cv_val >> 8) - 2048;
    if (led_show_phase) {
      LedBrightness(0, 4095 - (int32_t(truncated_cv_val >> 8) * int32_t(truncated_cv_val >> 8)) / 4096);
    }
    if (Connected(Input::Audio1)) {
      AudioOut1((val * AudioIn1()) >> 12);
    } else {
      AudioOut1(val);
    }
  }
  void SetAudio2(int32_t cv_val) {
    static int32_t error = 0;
    cv_val += 524288;
    uint32_t truncated_cv_val = (cv_val - error) & 0xFFFFFF00;
    error += truncated_cv_val - cv_val;
    int16_t val = int32_t(truncated_cv_val >> 8) - 2048;
    if (led_show_phase) {
      LedBrightness(1, 4095 - (int32_t(truncated_cv_val >> 8) * int32_t(truncated_cv_val >> 8)) / 4096);
    }
    if (Connected(Input::Audio2)) {
      AudioOut2((val * AudioIn2()) >> 12);
    } else {
      AudioOut2(val);
    }
  }
  void SetCV1(int32_t cv_val) {
    static int32_t error = 0;
    cv_val += 524288;
    uint32_t truncated_cv_val = (cv_val - error) & 0xFFFFFF00;
    error += truncated_cv_val - cv_val;
    int16_t val = 2048 - int32_t(truncated_cv_val >> 8);
    if (led_show_phase) {
      LedBrightness(2, 4095 - (int32_t(truncated_cv_val >> 8) * int32_t(truncated_cv_val >> 8)) / 4096);
    }
    if (Connected(Input::CV1)) {
      CVOut1((val * CVIn1()) >> 12);
    } else {
      CVOut1(val);
    }
  }
  void SetCV2(int32_t cv_val) {
    static int32_t error = 0;
    cv_val += 524288;
    uint32_t truncated_cv_val = (cv_val - error) & 0xFFFFFF00;
    error += truncated_cv_val - cv_val;
    int16_t val = 2048 - int32_t(truncated_cv_val >> 8);
    if (led_show_phase) {
      LedBrightness(3, 4095 - (int32_t(truncated_cv_val >> 8) * int32_t(truncated_cv_val >> 8)) / 4096);
    }
    if (Connected(Input::CV2)) {
      CVOut2((val * CVIn2()) >> 12);
    } else {
      CVOut2(val);
    }
  }
  virtual void __not_in_flash_func(ProcessSample)() {
    // uint64_t start = rp2040.getCycleCount64(); // Arduino only
    bool pause = false;

    if (SwitchVal() == Switch::Up || PulseIn1()) {
      pause = true;
    }

    if (SwitchVal() == Switch::Down) {
      if (!switch_is_down) {
        rndPhase();
      }
      switch_is_down = true;
    } else {
      switch_is_down = false;
    }

    if (PulseIn2RisingEdge()) {
      rndPhase();
    }

    // modValX is a unipolar version of valX scaled by modDepth. The result is 0-4095.
    int32_t modDepth = KnobVal(Knob::X);
    uint32_t modVal1 = (modDepth * (2047 - (val1 >> 8))) >> 12;
    uint32_t modVal2 = (modDepth * (2047 - (val2 >> 8))) >> 12;
    uint32_t modVal3 = (modDepth * (2047 - (val3 >> 8))) >> 12;
    uint32_t modVal4 = (modDepth * (2047 - (val4 >> 8))) >> 12;
    uint32_t modVal5 = (modDepth * (2047 - (val5 >> 8))) >> 12;
    uint32_t modVal6 = (modDepth * (2047 - (val6 >> 8))) >> 12;

    if (!pause) {
      // Calculate frequencies for audio, CV and mod LFO. Cross modulate frequency by one or more
      uint32_t hz1 = (KnobToHzQ12(KnobVal(Knob::Main)) * ((2 << 12) + ((50 * modVal5 * modVal5) >> 12) + 2 * modVal2)) >> 12;
      phase1 += phaseStep(hz1);
      uint32_t hz2 = (KnobToHzQ12(KnobVal(Knob::Main)) * ((1 << 12) + 3 * modVal6 + 2 * modVal1)) >> 12;
      phase2 += phaseStep(hz2);
      uint32_t hz3 = (KnobToHzQ12(KnobVal(Knob::Main)) * ((1 << 12) + modVal5 + modVal6 + modVal4)) >> 12;
      phase3 += phaseStep(hz3);
      uint32_t hz4 = ((KnobToHzQ12(KnobVal(Knob::Main)) >> 1) * ((1 << 12) + 2 * modVal6 + modVal3)) >> 12;
      phase4 += phaseStep(hz4);
      uint32_t hz5 = ((KnobToHzQ12(KnobVal(Knob::Main)) >> 2) * ((1 << 12) + (modVal6 >> 2))) >> 12;
      phase5 += phaseStep(hz5);
      uint32_t hz6 = ((KnobToHzQ12(KnobVal(Knob::Main)) >> 3) * ((1 << 12) + (modVal5 >> 2))) >> 12;
      phase6 += phaseStep(hz6);
    }

    val1 = sinval(phase1);
    val2 = sinval(phase2);
    val3 = sinval(phase3);
    val4 = sinval(phase4);
    val5 = sinval(phase5);
    val6 = sinval(phase6);

    SetAudio1(crossfade(val1, -val2, KnobVal(Knob::Y)));
    SetAudio2(crossfade(val2, -val1, KnobVal(Knob::Y)));
    SetCV1(crossfade(val3, -val4, KnobVal(Knob::Y)));
    SetCV2(crossfade(val4, -val3, KnobVal(Knob::Y)));

    bool pulse1 = (((val1 >> 8) & 0x0100) > ((val2 >> 8) & 0x0100));
    bool pulse2 = (((val3 >> 8) & 0x0100) > ((val4 >> 8) & 0x0100));
    PulseOut1(pulse1);
    PulseOut2(pulse2);
    if (led_show_phase) {
      LedOn(4, pulse1);
      LedOn(5, pulse2);
    }

    // led_show_phase = false;
    // debugVal(abs(val4 >> 9), -2000, -1000, -500, 500, 1000, 2000);
    // debugVal(rp2040.getCycleCount64() - start, 500, 1000, 1250, 1500, 1750, 2000);
  }

  uint32_t phaseStep(uint32_t hzQ12) {
    // Steps for full phase = 0xffffffff = (1<<32)-1 = 4.294.967.296
    // (2<<32)-1 / 48000hz = steps for each sample @1hz
    const uint32_t phase_steps_per_sample = 0xffffffff / 48000;
    return (phase_steps_per_sample * hzQ12) >> 12;
  }

  void debugVal(int64_t val, int32_t t0, int32_t t1, int32_t t2, int32_t t3, int32_t t4, int32_t t5) {
    LedOn(0, val > t0);
    LedOn(1, val > t1);
    LedOn(2, val > t2);
    LedOn(3, val > t3);
    LedOn(4, val > t4);
    LedOn(5, val > t5);
  }
};
