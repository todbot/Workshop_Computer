#include "ComputerCard.h"
#include "quantiser.h"
#include "divider.h"

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
        startPosL = KnobVal(Knob::X) * bufSize >> 4;
        startPosR = KnobVal(Knob::Y) * bufSize >> 4;

        for (int i = 0; i < bufSize; i++)
        {
            delaybuf[i] = 0;
        }
        writeInd = 0;
        readIndL = 0;
        readIndR = 0;
        cvsL = 0;
        cvsR = 0;
        halftime = false;
        startupCounter = 400;
        divisor = 1;
        internalClockRate = 1;
    };

    static constexpr uint32_t bufSize = 64000;
    int16_t delaybuf[bufSize];
    int16_t cvBuf[bufSize];
    unsigned writeInd, readIndL, readIndR, readIndCV, cvsL, cvsR;
    int32_t ledtimer = 0;
    int32_t hpf = 0;
    bool checkZero = false;
    int phaseL = 0;
    int phaseR = 0;
    bool halftime;
    bool bufferFull = false;
    Divider clockDivider;
    int divisor;
    int internalClockCounter = 0;
    int internalClockRate;
    bool lastRisingEdge1 = false;
    bool lastRisingEdge2 = false;
    int audioL1 = 0;
    int audioL2 = 0;
    int audioR1 = 0;
    int audioR2 = 0;
    int audioLf1 = 0;
    int audioLf2 = 0;
    int audioRf1 = 0;
    int audioRf2 = 0;

    virtual void ProcessSample()
    {
        halftime = !halftime;
        if (startupCounter)
            startupCounter--;

        // Hainbach says half time is the best time (no really I'm just buying more delay time)
        if (startupCounter == 0)
        {

            // pulse read can't happen at halftime
            bool risingEdge1 = PulseIn1RisingEdge();
            bool risingEdge2 = PulseIn2RisingEdge();

            int16_t qSample;

            if (halftime)
            {
                int16_t noise = rnd12() - 2048;
                Switch s = SwitchVal();

                bool pulseL = false;
                bool pulseR = false;

                x = KnobVal(Knob::X);
                y = KnobVal(Knob::Y);
                main = KnobVal(Knob::Main);

                lowPassMain = (lastLowPassMain * 4000 + (main) * 95) >> 12;
                lastLowPassMain = lowPassMain;

                // Read inputs
                cv1 = CVIn1();               // -2048 to 2047
                cv2 = CVIn2();               // -2048 to 2047
                int16_t audioL = AudioIn1(); // -2048 to 2047
                int16_t audioR = AudioIn2(); // -2048 to 2047

                // 12kHz notch filter, to remove interference from mux lines (only left channel)

                audioL <<= 2;

                int32_t ooa0 = 16302, a2oa0 = 16221; // Q=100;
                //	int32_t ooa0=15603, a2oa0=14823;//Q=10;
                int32_t audioLf = (ooa0 * (audioL + audioL2) - a2oa0 * audioLf2) >> 14;
                audioL2 = audioL1;
                audioL1 = audioL;
                audioLf2 = audioLf1;
                audioLf1 = audioLf;

                audioL >>= 2;
                audioLf >>= 2;


                int16_t lastSampleL = 0;
                int16_t lastSampleR = 0;

                int16_t fromBufferL = 0;
                int16_t fromBufferR = 0;

                internalClockRate = cabs(x - 2048) * 50 >> 12 + 1;
                divisor = (y * 16 >> 12) + 1;

                // BUFFERS LOOPS/DELAYSSS

                if ((s == Switch::Down) && (lastSwitchVal != Switch::Down))
                {
                    runMode = RECORD;
                    loopLength = 0;
                    writeInd = 0;
                    internalClockCounter = 0;
                    clockDivider.SetResetPhase(divisor);
                    pulseL = true;
                    pulseR = true;
                }
                else if ((s == Switch::Up) && (lastSwitchVal != Switch::Up))
                {
                    runMode = DELAY;
                    internalClockCounter = 0;
                    clockDivider.SetResetPhase(divisor);
                    pulseL = true;
                    pulseR = true;
                }
                else if (((s == Switch::Middle) && (lastSwitchVal != Switch::Middle)) || (runMode == RECORD && bufferFull))
                {
                    bufferFull = false;
                    runMode = PLAY;
                    calculateStartPos();
                    phaseL = startPosL;
                    phaseR = startPosR;
                    internalClockCounter = 0;
                    clockDivider.SetResetPhase(divisor);
                    pulseL = true;
                    pulseR = true;
                };

                cvMix = calcCVMix(noise);

                internalClockCounter += internalClockRate;

                if (internalClockCounter >= bufSize >> 2)
                {
                    internalClockCounter = 0;

                    if (!Connected(Input::Pulse1))
                    {
                        pulseL = true;
                        pulseR = clockDivider.Step(true);
                        if (pulseR)
                        {
                            clockDivider.SetResetPhase(divisor);
                        }
                    }
                }

                lastSwitchVal = s;

                switch (runMode)
                {
                case DELAY:
                {
                    // record the loop, output audio delayed (patch your own feedback) and output normal cv mix signals

                    qSample = quantSample(cvMix);

                    // Delay code from Chris's amazing Utility Pair modfified slightly to remove internal feedback and add stereo

                    int32_t kL, kR;

                    if (Connected(Input::Audio2))
                    {
                        kL = (2048 - audioR) * lowPassMain >> 12;
                        kR = (2048 - audioR) * lowPassMain >> 12;
                    }
                    else
                    {
                        kL = 4095 - lowPassMain;
                        kR = lowPassMain;
                    }

                    int32_t cvL = kL;
                    int32_t cvR = kR;

                    if (cvL > 4095)
                        cvL = 4095;
                    if (cvL < 0)
                        cvL = 0;

                    if (cvR > 4095)
                        cvR = 4095;
                    if (cvR < 0)
                        cvR = 0;

                    int cvtargL = cvL * cvL / 178;
                    int cvtargR = cvR * cvR / 178;
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

                    int32_t buf_write = highpass_process(&hpf, 200, audioLf);

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
                    delaybuf[writeInd] = audioLf;

                    outL = audioLf;
                    outR = audioLf;

                    outCV = cvMix;

                    writeInd++;
                    if (writeInd >= bufSize)
                    {
                        bufferFull = true;
                    }

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
                    int32_t k = lowPassMain >> 1;
                    int32_t dphaseL;
                    int32_t dphaseR;

                    if (Connected(Input::Audio2))
                    {
                        dphaseL = k + (audioR * (2048 - k) >> 11);
                        dphaseR = k + (audioR * (2048 - k) >> 11);
                    }
                    else
                    {
                        dphaseL = k;
                        dphaseR = k;
                    }

                    dphaseL -= 1024;
                    dphaseR -= 1024;

                    phaseL += dphaseL >> 1;
                    phaseR += dphaseR >> 1;

                    if (loopLength < 1)
                    {
                        loopLength = bufSize;
                    }

                    calculateStartPos();

                    checkZero = zeroCrossing(outL, lastSampleL) && zeroCrossing(outR, lastSampleR);

                    lastSampleL = fromBufferL;
                    lastSampleR = fromBufferR;

                    if (reset && ((checkZero && Connected(Input::Audio1)) || !Connected(Input::Audio1)))
                    {
                        reset = false;
                        phaseL = startPosL;
                        phaseR = startPosR;
                        pulseL = true;
                        pulseR = true;
                        clockDivider.SetResetPhase(divisor);
                        internalClockCounter = 0;
                    };

                    if (phaseL < 0)
                    {
                        phaseL += loopLength << 8;
                        clockDivider.SetResetPhase(divisor);
                        pulseL = true;
                        pulseR = clockDivider.Step(true);
                    }
                    if (phaseL > (loopLength << 8))
                    {
                        phaseL -= loopLength << 8;
                        pulseL = true;
                        clockDivider.SetResetPhase(divisor);
                        pulseR = clockDivider.Step(true);
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
                        // outCV = ((1000 * lastCV) + (3095 * outCV)) >> 12;
                        // lastCV = outCV;

                        qSample = quantSample(outCV);
                    }

                    outL >>= 12;
                    outR >>= 12;

                    break;
                }
                };

                clip(outL);
                clip(outR);
                AudioOut1(outL);
                AudioOut2(outR);
                CVOut1(outCV);

                LedBrightness(0, cabs(outL));
                LedBrightness(1, cabs(outR));

                if (risingEdge2 || lastRisingEdge2)
                {
                    reset = true;
                };

                if (risingEdge1 || lastRisingEdge1)
                {
                    pulseL = true;
                    pulseR = clockDivider.Step(true);
                    if (pulseR)
                    {
                        clockDivider.SetResetPhase(divisor);
                    }
                }

                if (pulseL)
                {
                    pulseL = false;
                    pulseTimer1 = 200;
                    PulseOut1(true);
                    LedOn(4);
                    CVOut2MIDINote(qSample);
                };

                if (pulseR)
                {
                    pulseR = false;
                    pulseTimer2 = 200;
                    PulseOut2(true);
                    LedOn(5);
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
            }

            lastRisingEdge1 = risingEdge1;
            lastRisingEdge2 = risingEdge2;
        }
    };

private:
    int pulseTimer1 = 200;
    int pulseTimer2;
    bool clockPulse = false;
    uint32_t startPosL;
    uint32_t startPosR;
    int lowPassMain = 0;
    int lastLowPassMain = 0;
    int loopLength = 0;
    bool reset = false;
    int32_t outL;
    int32_t outR;
    int32_t outCV;
    int startupCounter;
    int lastCV;

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

        LedBrightness(2, cabs(thing1));
        LedBrightness(3, cabs(thing2));

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

    void calculateStartPos()
    {
        if (Connected(Input::CV1) && Connected(Input::CV2))
        {
            startPosL = (x * (cv1 + 2048) >> 12) * loopLength >> 4;
            startPosR = (y * (cv2 + 2048) >> 12) * loopLength >> 4;
        }
        else if (Connected(Input::CV1))
        {
            startPosL = (x * (cv1 + 2048) >> 12) * loopLength >> 4;
            startPosR = y * loopLength >> 4;
        }
        else if (Connected(Input::CV2))
        {
            startPosL = x * loopLength >> 4;
            startPosR = (y * (cv2 + 2048) >> 12) * loopLength >> 4;
        }
        else
        {
            startPosL = x * loopLength >> 4;
            startPosR = y * loopLength >> 4;
        }
    }
};

int main()
{
    Goldfish gf;
    gf.EnableNormalisationProbe();
    gf.Run();
}
