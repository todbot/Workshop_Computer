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
        runMode = SwitchVal() == Switch::Middle ? PLAY : DELAY;
        startPos = 0;

        for (int i = 0; i < bufSize; i++)
        {
            delaybuf[i] = 0;
        }
        writeInd = 0;
        readIndL = 0;
        readIndR = 0;
        cvsL = 0;
        cvsR = 0;
    };

    static constexpr uint32_t bufSize = 48000;
    int16_t delaybuf[bufSize];
    int16_t cvBuf[bufSize];
    unsigned writeInd, readIndL, readIndR, readIndCV, cvsL, cvsR;
    int32_t ledtimer = 0;
    int32_t hpf = 0;
    bool checkZero = false;
    int phaseL = 0;
    int phaseR = 0;

    virtual void ProcessSample()
    {

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

        int16_t fromBufferL = 0;
        int16_t fromBufferR = 0;

        // BUFFERS LOOPS/DELAYSSS

        if ((s == Switch::Down) && (lastSwitchVal != Switch::Down))
        {
            runMode = RECORD;
            loopLength = 0;
            writeInd = 0;
        }
        else if ((s == Switch::Up) && (lastSwitchVal != Switch::Up))
        {
            runMode = DELAY;
        }
        else if ((s == Switch::Middle) && (lastSwitchVal != Switch::Middle))
        {
            runMode = PLAY;
            readIndL = startPos;
            readIndR = startPos;
        };

        cvMix = calcCVMix(noise);

        lastSwitchVal = s;

        switch (runMode)
        {
        case DELAY:
        {
            // record the loop, output audio delayed (patch your own feedback) and output normal cv mix signals

            qSample = quantSample(cvMix);

            // Delay code from Chris's amazing Utility Pair modfified slightly to remove internal feedback and add stereo
            int32_t kL = 4095 - KnobVal(Knob::Main);
            int32_t kR = KnobVal(Knob::Main);
            int32_t cvL = kL + AudioIn2();
            int32_t cvR = kR + AudioIn2();
            if (cvL > 4095)
                cvL = 4095;
            if (cvL < 0)
                cvL = 0;

            if (cvR > 4095)
                cvR = 4095;
            if (cvR < 0)
                cvR = 0;

            int cvtargL = cvL * cvL / 100;
            int cvtargR = cvR * cvR / 100;
            cvsL = (cvsL * 255 + (cvtargL << 5)) >> 8;
            cvsR = (cvsR * 255 + (cvtargR << 5)) >> 8;

            int cvs1L = cvsL >> 7;
            int cvs1R = cvsR >> 7;

            int rL = cvsL & 0x7F;
            int rR = cvsR & 0x7F;

            readIndL = (writeInd + (bufSize << 1) - (cvs1L)-1) % bufSize;
            readIndR = (writeInd + (bufSize << 1) - (cvs1R)-1) % bufSize;
            int32_t fromBuffer1L = delaybuf[readIndL];
            int32_t fromBuffer1R = delaybuf[readIndR];
            int readInd2L = (writeInd + (bufSize << 1) - (cvs1L)-2) % bufSize;
            int readInd2R = (writeInd + (bufSize << 1) - (cvs1R)-2) % bufSize;
            int32_t fromBuffer2L = delaybuf[readInd2L];
            int32_t fromBuffer2R = delaybuf[readInd2R];

            int32_t fromBufferL = (fromBuffer2L * rL + fromBuffer1L * (128 - rL)) >> 7;
            int32_t fromBufferR = (fromBuffer2R * rR + fromBuffer1R * (128 - rR)) >> 7;

            outL = fromBufferL;
            outR = fromBufferR;

            outCV = cvMix;

            int32_t buf_write = highpass_process(&hpf, 200, audioL);

            delaybuf[writeInd] = buf_write;

            writeInd++;
            if (writeInd >= bufSize)
                writeInd = 0;

            break;
        }
        case RECORD:
        {
            // reset clock, stop recording, playback, loop at the end of the delay buffer
            qSample = quantSample(cvMix);

            cvBuf[writeInd] = cvMix;
            delaybuf[writeInd] = audioL;

            outL = audioL;
            outR = audioL;

            outCV = cvMix;

            writeInd++;
            if (writeInd >= bufSize)
                writeInd = 0;

            loopLength++;
            if (loopLength > bufSize)
            {
                loopLength = bufSize;
            }

            break;
        }
        case PLAY:
        {
            // play the loop, ignore the input

            qSample = quantSample(cvBuf[readIndCV]);

            int32_t k = KnobVal(Knob::Main) >> 1;
            int32_t dphaseL = k + AudioIn2();
            int32_t dphaseR = (2047 - k) + AudioIn2();
            dphaseL -= 1024;
            dphaseR -= 1024;

            phaseL += dphaseL >> 1;
            phaseR += dphaseR >> 1;

            if (loopLength < 1)
            {
                loopLength = bufSize;
            }


            if (phaseL < 0)
            {
                phaseL += loopLength << 8;
            }
            if (phaseL > (loopLength << 8))
            {
                phaseL -= loopLength << 8;
            }

            if (phaseR < 0)
            {
                phaseR += loopLength << 8;
            }
            if (phaseR > (loopLength << 8))
            {
                phaseR -= loopLength << 8;
            }

            int32_t rL = phaseL & 0xFF;
            int32_t readIndL = phaseL >> 8;
            int32_t rR = phaseR & 0xFF;
            int32_t readIndR = phaseR >> 8;

            outL = (delaybuf[readIndL] << 3) * (265 - rL) + (delaybuf[(readIndL + 1) % loopLength] << 3) * (rL);
            outR = (delaybuf[readIndR] << 3) * (265 - rR) + (delaybuf[(readIndR + 1) % loopLength] << 3) * (rR);

            if (loopLength > 0)
            {
                outCV = (cvBuf[readIndL] * (265 - rL) + cvBuf[(readIndL + 1) % loopLength] * rL) >> 8;
            }

            outL >>= 12;
            outR >>= 12;


            // if (Connected(Input::Pulse2))
            // {
            //     startPos = (cvMix + 2048) * (bufSize - 1) >> 12;
            // }
            // else
            // {
            //     startPos = 0;
            // };

            // checkZero = zeroCrossing(fromBufferL, lastSampleL) || zeroCrossing(fromBufferR, lastSampleR);

            // lastSampleL = fromBufferL;
            // lastSampleR = fromBufferR;

            // if (reset && ((Connected(Input::Audio1) && checkZero) ||
            //               (Connected(Input::Audio2) && checkZero)))
            // {
            //     reset = false;
            //     loopRamp = 0;
            //     writeInd = startPos;
            // };
            break;
        }
        };

        clip(outL);
        clip(outR);
        AudioOut1(outL);
        AudioOut2(outR);
        CVOut1(outCV);

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

        // ledtimer--;
        // if (ledtimer <= 0){
        //     ledtimer = cvs1;
        //     //PULSE TIMER HERE??
        // }
        // LedOn(4, ledtimer < 100);
    };

private:
    int clockRate;
    int clock;
    int pulseTimer1 = 200;
    int pulseTimer2;
    bool clockPulse = false;
    uint32_t controlReadIndex = 0;
    uint32_t controlWriteIndex = 0;
    uint32_t startPos;
    int lastLowPassX = 0;
    int lastLowPassMain = 0;
    int lastCVmix = 0;
    int incr;
    int loopLength = 0;
    bool reset = false;
    int32_t outL;
    int32_t outR;
    int32_t outCV;

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
