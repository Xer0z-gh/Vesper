#pragma once
/*  PitchFX.h — real-time pitch tools built on dual-tap crossfade shifting
    (the OctaveShifter primitive from Core.h). Zero latency reported; the
    grain window is a creative smear, not a delay.

    Modes: Micro (±cents stereo detune) · Octave Up · Octave Down · Fifth ·
    Wide Double (detuned, Haas-offset stereo double). */

#include "Core.h"

namespace vesper::dsp {

class PitchFX
{
public:
    enum Mode { micro = 0, octaveUp, octaveDown, fifth, wideDouble };

    void prepare(const Context& ctx)
    {
        fs = ctx.sampleRate;
        shiftL.prepare(fs);
        shiftR.prepare(fs);
        haas.prepare(fs, 0.05);
        mixSm.reset(fs, 0.03);
        reset();
    }

    /* amountPct: Micro/WideDouble -> detune 0..50 cents; other modes -> extra
       thickness detune 0..12 cents around the fixed interval. */
    void set(int modeIn, float amountPct, float mixPct) noexcept
    {
        mode = modeIn;
        const double amt = juce::jlimit(0.0f, 100.0f, amountPct) * 0.01;
        double base = 1.0, spreadCents = 0.0;
        switch (mode)
        {
            case micro:      base = 1.0; spreadCents = amt * 50.0;              break;
            case octaveUp:   base = 2.0; spreadCents = amt * 12.0;              break;
            case octaveDown: base = 0.5; spreadCents = amt * 12.0;              break;
            case fifth:      base = std::pow(2.0, 7.0 / 12.0); spreadCents = amt * 12.0; break;
            case wideDouble: base = 1.0; spreadCents = 8.0 + amt * 42.0;        break;
        }
        shiftL.setRatio(base * centsToRatio(-spreadCents));
        shiftR.setRatio(base * centsToRatio(+spreadCents));
        mixSm.setTargetValue(mixPct * 0.01f);
    }

    void process(juce::AudioBuffer<float>& buf)
    {
        const int n = buf.getNumSamples();
        auto* l = buf.getWritePointer(0);
        auto* r = buf.getWritePointer(1);

        for (int i = 0; i < n; ++i)
        {
            float wetL = shiftL.process(l[i]);
            float wetR = shiftR.process(r[i]);

            if (mode == wideDouble) // Haas-offset the right double for width
            {
                haas.write(wetR);
                wetR = haas.readHermite(0.012 * fs);
            }

            const float mix = mixSm.getNextValue();
            l[i] = equalPowerMix(l[i], wetL, mix);
            r[i] = equalPowerMix(r[i], wetR, mix);
        }
    }

    void reset()
    {
        shiftL.reset();
        shiftR.reset();
        haas.clear();
    }

private:
    static double centsToRatio(double cents) noexcept
    {
        return std::pow(2.0, cents / 1200.0);
    }

    double fs = 48000.0;
    int    mode = 0;
    OctaveShifter shiftL, shiftR;
    DelayLine haas;
    SmoothLin mixSm;
};

} // namespace vesper::dsp
