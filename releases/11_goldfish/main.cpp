#include "ComputerCard.h"
#include "quantiser.h"

#define BUFFER_SIZE 48000
#define SMALL_BUFFER_SIZE 24000

bool tmp;

uint32_t __not_in_flash_func(rnd12)()
{
    static uint32_t lcg_seed = 1;
    lcg_seed = 1664525 * lcg_seed + 1013904223;
    return lcg_seed >> 20;
}

bool __not_in_flash_func(zeroCrossing)(int16_t a, int16_t b)
{
    return (a < 0 && b >= 0) || (a >= 0 && b < 0);
};

int32_t __not_in_flash_func(highpass_process)(int32_t *out, int32_t b, int32_t in)
{
    *out += (((in - *out) * b) >> 16);
    return in - *out;
}

/// Goldfish
class Goldfish : public ComputerCard
{
public:
    Goldfish()
    {
        // constructor
        loopRamp = 0;
        clockRate = 0;
        runMode = SwitchVal() == Switch::Middle ? PLAY : DELAY;
        startPosAudio = 0;
        startPosControl = 0;

        for (int i = 0; i < BUFFER_SIZE; i++)
            delaybuf[i] = 0;
        writeInd = 0;
        readInd = 0;
        cvs = 0;
    };

    static constexpr uint32_t bufSize = 48000;
    int16_t delaybuf[bufSize];
    unsigned writeInd, readInd, cvs;
    int32_t ledtimer = 0;
    int32_t hpf = 0;

    virtual void ProcessSample()
    {
        int32_t k = KnobVal(Knob::X);
        int32_t cv = CVIn1() + k;
        if (cv > 4095)
            cv = 4095;
        if (cv < 0)
            cv = 0;

        int cvtarg = cv * cv / 178;
        cvs = (cvs * 255 + (cvtarg << 5)) >> 8;

        int cvs1 = cvs >> 7;
        int r = cvs & 0x7F;

        readInd = (writeInd + (bufSize << 1) - (cvs1)-1) % bufSize;
        int32_t fromBuffer1 = delaybuf[readInd];
        int readInd2 = (writeInd + (bufSize << 1) - (cvs1)-2) % bufSize;
        int32_t fromBuffer2 = delaybuf[readInd2];

        int32_t fromBuffer = (fromBuffer2 * r + fromBuffer1 * (128 - r)) >> 7;
        int32_t in = AudioIn1();

        int32_t k2;

        if (SwitchVal() == Switch::Up)
            k2 = 800;
        else if (SwitchVal() == Switch::Middle)
            k2 = 2500;
        else
            k2 = 4095; // switch down = infinite loop

        int32_t out = (((4095 - ((k2 * k2) >> 12)) * in) >> 12) + (((4095 - (((4095 - k2) * (4095 - k2)) >> 12)) * fromBuffer) >> 12);

        int32_t buf_write = highpass_process(&hpf, 200, out);

        delaybuf[writeInd] = buf_write;
        clip(out);
        AudioOut1(out);

        writeInd++;
        if (writeInd >= bufSize)
            writeInd = 0;

        ledtimer--;
        if (ledtimer <= 0)
            ledtimer = cvs1;
        LedOn(4, ledtimer < 100);
        LedBrightness(0, cabs(in << 1));
        LedBrightness(2, cabs(out << 1));

        // END CHRISG
        // int16_t noise = rnd12() - 2048;
        // Switch s = SwitchVal();

        // x = KnobVal(Knob::X);
        // y = KnobVal(Knob::Y);
        // main = KnobVal(Knob::Main);

        // int lowPassX = (lastLowPassX * 15 + x) >> 4;
        // int lowPassMain = (lastLowPassMain * 15 + main) >> 4;
        // lastLowPassX = lowPassX;
        // lastLowPassMain = lowPassMain;

        // bool clockPulse = false;
        // bool randPulse = false;

        // // Read inputs
        // cv1 = CVIn1();               // -2048 to 2047
        // cv2 = CVIn2();               // -2048 to 2047
        // int16_t audioL = AudioIn1(); // -2048 to 2047
        // int16_t audioR = AudioIn2(); // -2048 to 2047

        // int16_t qSample;

        // int16_t lastSampleL = 0;
        // int16_t lastSampleR = 0;

        // // BUFFERS LOOPS/DELAYSSS

        // if ((s == Switch::Down) && (lastSwitchVal != Switch::Down))
        // {
        //     runMode = RECORD;
        //     audioLoopLength = 0;
        //     cvLoopLength = 0;
        //     audioWriteIndexL = 0;
        //     audioWriteIndexR = 0;
        //     controlWriteIndex = 0;
        // }
        // else if ((s == Switch::Up) && (lastSwitchVal != Switch::Up))
        // {
        //     runMode = DELAY;
        // }
        // else if ((s == Switch::Middle) && (lastSwitchVal != Switch::Middle))
        // {
        //     runMode = PLAY;
        //     audioReadIndexL = 0;
        //     audioReadIndexR = 0;
        //     controlReadIndex = 0;
        // };

        // lastSwitchVal = s;

        // ////INTERNAL CLOCK AND RANDOM CLOCK SKIP / SWITCHED GATE

        // clockRate = ((4095 - lowPassX) * BUFFER_SIZE * 2 + 50) >> 12;

        // cvMix = calcCVMix(noise);

        // clock++;
        // if (clock > clockRate)
        // {
        //     clock = 0;
        //     clockPulse = true;
        // };

        // if (noise > y - 2048)
        // {
        //     randPulse = true;
        // };

        // int shrinkRange = 1600;
        // incr = ((4095 - main) * (2048 - shrinkRange) >> 12) + shrinkRange;

        // switch (runMode)
        // {
        // case DELAY:
        // {
        //     // record the loop, output audio delayed (patch your own feedback) and output normal cv mix signals

        //     qSample = quantSample(cvMix);

        //     CVOut1(cvMix);

        //     loopRamp += incr;

        //     if (loopRamp > 8192)
        //     {
        //         loopRamp -= 8192;
        //         // Calculate new sample for outputsssss
        //         audioBufferL[audioWriteIndexL] = audioL;
        //         audioBufferR[audioWriteIndexR] = audioR;
        //         audioReadIndexL = (audioWriteIndexL - clockRate + (BUFFER_SIZE * 2)) % BUFFER_SIZE;
        //         audioReadIndexR = (audioWriteIndexR - clockRate + (BUFFER_SIZE * 2)) % BUFFER_SIZE;
        //         AudioOut1(audioBufferL[audioReadIndexL]);
        //         AudioOut2(audioBufferR[audioReadIndexR]);
        //         if (audioBufferL[audioReadIndexL] > 0)
        //         {
        //             LedBrightness(0, audioBufferL[audioReadIndexL] << 1);
        //         }
        //         else
        //         {
        //             LedOff(0);
        //         };

        //         if (audioBufferR[audioReadIndexR] > 0)
        //         {
        //             LedBrightness(1, audioBufferR[audioReadIndexR] << 1);
        //         }
        //         else
        //         {
        //             LedOff(1);
        //         };
        //         audioWriteIndexL = (audioWriteIndexL + 1) % BUFFER_SIZE;
        //         audioWriteIndexR = (audioWriteIndexR + 1) % BUFFER_SIZE;
        //     };

        //     break;
        // }
        // case RECORD:
        // {
        //     // reset clock, stop recording, playback, loop at the end of the delay buffer
        //     qSample = quantSample(cvMix);

        //     if ((!Connected(Input::Pulse1) && clockPulse) || (Connected(Input::Pulse1) && PulseIn1RisingEdge()))
        //     {

        //         LedOn(4, true);
        //         pulseTimer1 = 200;

        //         if (randPulse)
        //         {
        //             PulseOut2(true);
        //             LedOn(5, true);
        //             pulseTimer2 = 200;

        //             CVOut2MIDINote(qSample);
        //         };
        //     };

        //     loopRamp += incr;

        //     if (loopRamp > 8192)
        //     {
        //         loopRamp -= 8192;
        //         cvMix = calcCVMix(noise);
        //         audioBufferL[audioWriteIndexL] = audioL;
        //         audioBufferR[audioWriteIndexR] = audioR;
        //         AudioOut1(audioL);
        //         AudioOut2(audioR);
        //         if (audioL > 0)
        //         {
        //             LedBrightness(0, audioL << 1);
        //         }
        //         else
        //         {
        //             LedOff(0);
        //         };

        //         if (audioR > 0)
        //         {
        //             LedBrightness(1, audioR << 1);
        //         }
        //         else
        //         {
        //             LedOff(1);
        //         };

        //         cvBuffer[controlWriteIndex] = cvMix;
        //         CVOut1(cvMix);

        //         audioWriteIndexL++;
        //         audioWriteIndexR++;

        //         if (audioWriteIndexL >= BUFFER_SIZE)
        //         {
        //             audioWriteIndexL = 0;
        //         }
        //         else
        //             audioLoopLength++;

        //         controlWriteIndex++;
        //         if (controlWriteIndex >= SMALL_BUFFER_SIZE)
        //         {
        //             controlWriteIndex = 0;
        //         }
        //         else
        //             cvLoopLength++;
        //     }

        //     break;
        // }
        // case PLAY:
        // {
        //     // play the loop, ignore the input

        //     qSample = quantSample(cvBuffer[controlReadIndex]);

        //     loopRamp += incr;

        //     if (loopRamp > 8192)
        //     {
        //         loopRamp -= 8192;

        //         AudioOut1(audioBufferL[audioReadIndexL]);
        //         AudioOut2(audioBufferR[audioReadIndexR]);

        //         if (audioBufferL[audioReadIndexL] > 0)
        //         {
        //             LedBrightness(0, audioBufferL[audioReadIndexL] << 1);
        //         }
        //         else
        //         {
        //             LedOff(0);
        //         };

        //         if (audioBufferR[audioReadIndexR] > 0)
        //         {
        //             LedBrightness(1, audioBufferR[audioReadIndexR] << 1);
        //         }
        //         else
        //         {
        //             LedOff(1);
        //         };

        //         CVOut1(cvBuffer[controlReadIndex]);

        //         if (Connected(Input::Pulse2))
        //         {
        //             startPosAudio = (cvMix + 2048) * (BUFFER_SIZE - 1) >> 12;
        //             startPosControl = (cvMix + 2048) * (SMALL_BUFFER_SIZE - 1) >> 12;
        //         }
        //         else
        //         {
        //             startPosAudio = 0;
        //             startPosControl = 0;
        //         };

        //         audioReadIndexL++;
        //         audioReadIndexR++;

        //         if (audioReadIndexL >= BUFFER_SIZE)
        //         {
        //             audioReadIndexL = 0;
        //         };

        //         if (audioReadIndexR >= BUFFER_SIZE)
        //         {
        //             audioReadIndexR = 0;
        //         };

        //         controlReadIndex++;
        //         if (controlReadIndex >= SMALL_BUFFER_SIZE)
        //         {
        //             controlReadIndex = 0;
        //         };

        //         tmp = zeroCrossing(audioBufferL[audioReadIndexL], lastSampleL) || zeroCrossing(audioBufferR[audioReadIndexR], lastSampleR);

        //         lastSampleL = audioBufferL[audioReadIndexL];
        //         lastSampleR = audioBufferR[audioReadIndexR];

        //         if (reset && ((Connected(Input::Audio1) && tmp) ||
        //                       (Connected(Input::Audio2) && tmp)))
        //         {
        //             reset = false;
        //             loopRamp = 0;
        //             audioReadIndexL = startPosAudio;
        //             audioReadIndexR = startPosAudio;
        //             controlReadIndex = startPosControl;
        //         };

        //         loopRamp++;

        //         if (loopRamp >= audioLoopLength)
        //         {
        //             loopRamp = 0;
        //             audioReadIndexL = startPosAudio;
        //             audioReadIndexR = startPosAudio;
        //             controlReadIndex = startPosControl;
        //         };
        //     }
        //     break;
        // }
        // };

        // if ((!Connected(Input::Pulse1) && clockPulse) || (Connected(Input::Pulse1) && PulseIn1RisingEdge()))
        // {

        //     LedOn(4, true);
        //     pulseTimer1 = 200;
        //     PulseOut1(true);
        //     if (randPulse)
        //     {
        //         PulseOut2(true);
        //         LedOn(5, true);
        //         pulseTimer2 = 200;

        //         CVOut2MIDINote(qSample);
        //     };
        // };

        // if (PulseIn2RisingEdge())
        // {
        //     reset = true;
        // };

        // // If a pulse1 is ongoing, keep counting until it ends
        // if (pulseTimer1)
        // {
        //     pulseTimer1--;
        //     if (pulseTimer1 == 0) // pulse ends
        //     {
        //         PulseOut1(false);
        //         LedOff(4);
        //     }
        // };

        // // If a pulse2 is ongoing, keep counting until it ends
        // if (pulseTimer2)
        // {
        //     pulseTimer2--;
        //     if (pulseTimer2 == 0) // pulse ends
        //     {
        //         PulseOut2(false);
        //         LedOff(5);
        //     }
        // };
    };

private:
    int16_t cvBuffer[SMALL_BUFFER_SIZE] = {0};
    int clockRate;
    int clock;
    int pulseTimer1 = 200;
    int pulseTimer2;
    bool clockPulse = false;
    int loopRamp;
    uint32_t audioReadIndexL = 0;
    uint32_t audioReadIndexR = 0;
    uint32_t controlReadIndex = 0;
    uint32_t controlWriteIndex = 0;
    uint32_t audioWriteIndexL = 0;
    uint32_t audioWriteIndexR = 0;
    uint32_t startPosAudio;
    uint32_t startPosControl;
    int lastLowPassX = 0;
    int lastLowPassMain = 0;
    int lastCVmix = 0;
    int incr;
    int audioLoopLength;
    int cvLoopLength;
    bool reset = false;

    Switch lastSwitchVal;
    int x;
    int y;
    int main;
    int16_t cv1;
    int16_t cv2;
    int16_t cvMix;

    enum RunMode
    {
        RECORD,
        DELAY,
        PLAY
    } runMode;

    int16_t calcCVMix(int16_t noise)
    {
        int16_t result = 0;
        int16_t thing1 = 0;
        int16_t thing2 = 0;

        if (Connected(Input::CV1) && Connected(Input::CV2))
        {
            thing1 = cv1 * (x - 2048) >> 11;
            thing2 = cv2 * (y - 2048) >> 11;
        }
        else if (Connected(Input::CV1))
        {
            thing1 = cv1 * (x - 2048) >> 11;
            thing2 = noise * y >> 12;
        }
        else if (Connected(Input::CV2))
        {
            thing1 = x - 2048;
            thing2 = cv2 * (y - 2048) >> 11;
        }
        else
        {
            thing1 = x - 2048;
            thing2 = y * noise >> 12;
        };

        // simple crossfade
        result = (thing1 * (4095 - main)) + (thing2 * main) >> 12;

        if (thing1 > 0)
        {
            LedBrightness(2, thing1);
        }
        else
        {
            LedBrightness(2, -1 * thing1);
        };

        if (thing2 > 0)
        {
            LedBrightness(3, thing2);
        }
        else
        {
            LedBrightness(3, -1 * thing2);
        };

        return result;
    };

    void clip(int32_t &a)
    {
        if (a < -2047)
            a = -2047;
        if (a > 2047)
            a = 2047;
    }

    int32_t cabs(int32_t a)
    {
        return (a > 0) ? a : -a;
    }
};

int main()
{
    Goldfish gf;
    gf.EnableNormalisationProbe();
    gf.Run();
}
