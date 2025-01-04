#include "ComputerCard.h"
#include "quantiser.h"

#define SMALL_BUFFER_SIZE 24000

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
        runMode = SwitchVal() == Switch::Middle ? PLAY : DELAY;
        startPos = 0;

        for (int i = 0; i < bufSize; i++)
        {
            delaybufL[i] = 0;
            delaybufR[i] = 0;
        }
        writeInd = 0;
        readInd = 0;
        cvs = 0;
    };

    static constexpr uint32_t bufSize = 48000;
    int16_t delaybufL[bufSize];
    int16_t delaybufR[bufSize];
    int16_t cvBuf[bufSize >> 1];
    unsigned writeInd, readInd, cvs;
    int32_t ledtimer = 0;
    int32_t hpf = 0;
    bool checkZero = false;

    virtual void ProcessSample()
    {

        // Delay code from Chris's amazing Utility Pair modfified slightly to remove internal feedback and add stereo
        int32_t k = 4095 - KnobVal(Knob::Main);
        int32_t cv = k;
        if (cv > 4095)
            cv = 4095;
        if (cv < 0)
            cv = 0;

        int cvtarg = cv * cv / 178;
        cvs = (cvs * 255 + (cvtarg << 5)) >> 8;

        int cvs1 = cvs >> 7;
        int r = cvs & 0x7F;

        
       

        int16_t noise = rnd12() - 2048;
        Switch s = SwitchVal();

        x = KnobVal(Knob::X);
        y = KnobVal(Knob::Y);
        main = KnobVal(Knob::Main);

        bool clockPulse = false;
        bool randPulse = false;

        // Read inputs
        cv1 = CVIn1();               // -2048 to 2047
        cv2 = CVIn2();               // -2048 to 2047
        int16_t audioL = AudioIn1(); // -2048 to 2047
        int16_t audioR = AudioIn2(); // -2048 to 2047

        int16_t qSample;

        int16_t lastSampleL = 0;
        int16_t lastSampleR = 0;

        // BUFFERS LOOPS/DELAYSSS

        if ((s == Switch::Down) && (lastSwitchVal != Switch::Down))
        {
            runMode = RECORD;
            audioLoopLength = 0;
            writeInd = 0;
        }
        else if ((s == Switch::Up) && (lastSwitchVal != Switch::Up))
        {
            runMode = DELAY;
        }
        else if ((s == Switch::Middle) && (lastSwitchVal != Switch::Middle))
        {
            runMode = PLAY;
            writeInd = startPos;
            loopRamp = 0;
        };

        lastSwitchVal = s;

        ////INTERNAL CLOCK AND RANDOM CLOCK SKIP / SWITCHED GATE

        clockRate = ((4095 - x) * bufSize * 2 + 50) >> 12;

        cvMix = calcCVMix(noise);

        clock++;
        if (clock > clockRate)
        {
            clock = 0;
            clockPulse = true;
        };

        if (noise > y - 2048)
        {
            randPulse = true;
        };

        int shrinkRange = 1600;
        incr = ((4095 - main) * (2048 - shrinkRange) >> 12) + shrinkRange;

        switch (runMode)
        {
        case DELAY:
        {
            // record the loop, output audio delayed (patch your own feedback) and output normal cv mix signals

            qSample = quantSample(cvMix);

            readInd = (writeInd + (bufSize << 1) - (cvs1)-1) % bufSize;
            int32_t fromBuffer1L = delaybufL[readInd];
            int32_t fromBuffer1R = delaybufR[readInd];
            int readInd2 = (writeInd + (bufSize << 1) - (cvs1)-2) % bufSize;
            int32_t fromBuffer2L = delaybufL[readInd2];
            int32_t fromBuffer2R = delaybufR[readInd2];

            int32_t fromBufferL = (fromBuffer2L * r + fromBuffer1L * (128 - r)) >> 7;
            int32_t fromBufferR = (fromBuffer2R * r + fromBuffer1R * (128 - r)) >> 7;


            CVOut1(cvMix);

            int32_t outL = fromBufferL;
            int32_t outR = fromBufferR;

            int32_t buf_writeL = highpass_process(&hpf, 200, audioL);
            int32_t buf_writeR = highpass_process(&hpf, 200, audioR);

            delaybufL[writeInd] = buf_writeL;
            delaybufR[writeInd] = buf_writeR;
            clip(outL);
            clip(outR);
            AudioOut1(outL);
            AudioOut2(outR);

            writeInd++;
            if (writeInd >= bufSize)
                writeInd = 0;

            break;
        }
        case RECORD:
        {
            // reset clock, stop recording, playback, loop at the end of the delay buffer
            qSample = quantSample(cvMix);

            readInd = writeInd;
            int32_t fromBuffer1L = delaybufL[readInd];
            int32_t fromBuffer1R = delaybufR[readInd];
            int readInd2 = (writeInd - 1) % bufSize;
            int32_t fromBuffer2L = delaybufL[readInd2];
            int32_t fromBuffer2R = delaybufR[readInd2];

            int32_t fromBufferL = (fromBuffer2L * r + fromBuffer1L * (128 - r)) >> 7;
            int32_t fromBufferR = (fromBuffer2R * r + fromBuffer1R * (128 - r)) >> 7;


            if ((!Connected(Input::Pulse1) && clockPulse) || (Connected(Input::Pulse1) && PulseIn1RisingEdge()))
            {

                LedOn(4, true);
                pulseTimer1 = 200;

                if (randPulse)
                {
                    PulseOut2(true);
                    LedOn(5, true);
                    pulseTimer2 = 200;

                    CVOut2MIDINote(qSample);
                };
            };

            cvMix = calcCVMix(noise);

            int32_t outL = audioL;
            int32_t outR = audioR;

            int32_t buf_writeL = highpass_process(&hpf, 200, audioL);
            int32_t buf_writeR = highpass_process(&hpf, 200, audioR);

            delaybufL[writeInd] = buf_writeL;
            delaybufR[writeInd] = buf_writeR;
            clip(outL);
            clip(outR);
            AudioOut1(outL);
            AudioOut2(outR);

            if (audioL > 0)
            {
                LedBrightness(0, audioL << 1);
            }
            else
            {
                LedOff(0);
            };

            if (audioR > 0)
            {
                LedBrightness(1, audioR << 1);
            }
            else
            {
                LedOff(1);
            };

            cvBuf[writeInd >> 1] = cvMix;
            CVOut1(cvMix);

            if (writeInd >= bufSize)
            {
                writeInd = 0;
            }
            else
                audioLoopLength++;

            break;
        }
        case PLAY:
        {
            // play the loop, ignore the input

            qSample = quantSample(cvBuf[readInd >> 1]);

            readInd = writeInd;
            int32_t fromBuffer1L = delaybufL[readInd];
            int32_t fromBuffer1R = delaybufR[readInd];
            int readInd2 = (writeInd - 1) % bufSize;
            int32_t fromBuffer2L = delaybufL[readInd2];
            int32_t fromBuffer2R = delaybufR[readInd2];

            int32_t fromBufferL = (fromBuffer2L * r + fromBuffer1L * (128 - r)) >> 7;
            int32_t fromBufferR = (fromBuffer2R * r + fromBuffer1R * (128 - r)) >> 7;

            int32_t outL = fromBufferL;
            int32_t outR = fromBufferR;

            clip(outL);
            clip(outR);
            AudioOut1(outL);
            AudioOut2(outR);

            int32_t buf_writeL = outL;
            int32_t buf_writeR = outR;

            delaybufL[writeInd] = buf_writeL;
            delaybufR[writeInd] = buf_writeR;

            writeInd++;
            if (writeInd >= bufSize)
                writeInd = 0;

            loopRamp++;
            if (loopRamp >= audioLoopLength)
            {
                loopRamp = 0;
                writeInd = startPos;
            }

            if (fromBufferL > 0)
            {
                LedBrightness(0, fromBufferL << 1);
            }
            else
            {
                LedOff(0);
            };

            if (fromBufferR > 0)
            {
                LedBrightness(1, fromBufferR << 1);
            }
            else
            {
                LedOff(1);
            };

            CVOut1(cvBuf[readInd >> 1]);

            if (Connected(Input::Pulse2))
            {
                startPos = (cvMix + 2048) * (bufSize - 1) >> 12;
            }
            else
            {
                startPos = 0;
            };

            checkZero = zeroCrossing(fromBufferL, lastSampleL) || zeroCrossing(fromBufferR, lastSampleR);

            lastSampleL = fromBufferL;
            lastSampleR = fromBufferR;

            if (reset && ((Connected(Input::Audio1) && checkZero) ||
                          (Connected(Input::Audio2) && checkZero)))
            {
                reset = false;
                loopRamp = 0;
                writeInd = startPos;
            };
            break;
        }
        };

        if ((!Connected(Input::Pulse1) && clockPulse) || (Connected(Input::Pulse1) && PulseIn1RisingEdge()))
        {

            LedOn(4, true);
            pulseTimer1 = 200;
            PulseOut1(true);
            if (randPulse)
            {
                PulseOut2(true);
                LedOn(5, true);
                pulseTimer2 = 200;

                CVOut2MIDINote(qSample);
            };
        };

        if (PulseIn2RisingEdge())
        {
            reset = true;
        };

        // If a pulse1 is ongoing, keep counting until it ends
        if (pulseTimer1)
        {
            pulseTimer1--;
            if (pulseTimer1 == 0) // pulse ends
            {
                PulseOut1(false);
                LedOff(4);
            }
        };

        // If a pulse2 is ongoing, keep counting until it ends
        if (pulseTimer2)
        {
            pulseTimer2--;
            if (pulseTimer2 == 0) // pulse ends
            {
                PulseOut2(false);
                LedOff(5);
            }
        };

        ledtimer--;
        if (ledtimer <= 0){
            ledtimer = cvs1;
            //PULSE TIMER HERE??
        }
        LedOn(4, ledtimer < 100);
    };

private:
    int clockRate;
    int clock;
    int pulseTimer1 = 200;
    int pulseTimer2;
    bool clockPulse = false;
    int loopRamp;
    uint32_t controlReadIndex = 0;
    uint32_t controlWriteIndex = 0;
    uint32_t startPos;
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
