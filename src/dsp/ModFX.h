#pragma once
/*  ModFX.h — the "Motion" module (chorus / ensemble / doubler / flanger /
    phaser / vibrato / tremolo / rotary) and the Stereo tools module. */

#include "Core.h"

namespace vesper::dsp {

// ---------------------------------------------------------------- Motion ---
class ModulationFX
{
public:
    enum Mode { chorusMode = 0, ensembleMode, doublerMode, flangerMode,
                phaserMode, vibratoMode, tremoloMode, rotaryMode };

    void prepare(const Context& ctx)
    {
        fs = ctx.sampleRate;
        for (auto& d : line) d.prepare(fs, 0.08);
        for (auto& v : voiceLFO) v.prepare(fs);
        for (int ch = 0; ch < 2; ++ch)
            for (auto& ap : phaserAP[(size_t) ch])
                ap.setCoefficients(Biquad::Type::allPass, fs, 800.0, 0.5, 0.0);
        rotarySpeed.setTau(600.0, fs);
        xover[0].prepare(fs); xover[1].prepare(fs);
        xover[0].set(800.0, 0.0); xover[1].set(800.0, 0.0);
        mixSm.reset(fs, 0.03);
        reset();
    }

    void set(int modeIn, float rateHz, float depthPct, float feedback,
             float spreadPct, float mixPct) noexcept
    {
        mode   = modeIn;
        depth  = depthPct * 0.01f;
        fb     = feedback;
        spread = spreadPct * 0.01f;
        const double r = rateHz;
        for (int v = 0; v < 6; ++v)
            voiceLFO[(size_t) v].setRate(r * (1.0 + 0.07 * v * (mode == ensembleMode ? 1.0 : 0.0)));
        mixSm.setTargetValue(mixPct * 0.01f);
    }

    void process(juce::AudioBuffer<float>& buf)
    {
        const int n = buf.getNumSamples();
        auto* l = buf.getWritePointer(0);
        auto* r = buf.getWritePointer(1);

        for (int i = 0; i < n; ++i)
        {
            const float mix = mixSm.getNextValue();
            float wetL = l[i], wetR = r[i];

            switch (mode)
            {
                case chorusMode:   tickChorus(l[i], r[i], wetL, wetR, 3, 7.0, 4.5);  break;
                case ensembleMode: tickChorus(l[i], r[i], wetL, wetR, 6, 11.0, 6.0); break;
                case doublerMode:  tickChorus(l[i], r[i], wetL, wetR, 2, 18.0, 1.2); break;
                case flangerMode:
                {
                    const float m = voiceLFO[0].tick(0.0) * 0.5f + 0.5f;
                    const double d = (0.8 + m * depth * 6.0) * 0.001 * fs;
                    const float zl = line[0].readHermite(d);
                    const float zr = line[1].readHermite(d * (1.0 + 0.12 * spread));
                    line[0].write(l[i] + zl * fb);
                    line[1].write(r[i] + zr * fb);
                    wetL = zl; wetR = zr;
                    break;
                }
                case phaserMode:
                {
                    const float m = voiceLFO[0].tick() * 0.5f + 0.5f;
                    const double centre = 300.0 * std::pow(2.0, 3.2 * (double) (m * depth) );
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        const double c = centre * (ch == 1 ? 1.0 + 0.18 * spread : 1.0);
                        float x = (ch == 0 ? l[i] : r[i]) + phaserState[(size_t) ch] * fb;
                        for (int s = 0; s < 6; ++s)
                        {
                            phaserAP[(size_t) ch][(size_t) s].setCoefficients(
                                Biquad::Type::allPass, fs, c * (1.0 + 0.5 * s), 0.55, 0.0);
                            x = (float) phaserAP[(size_t) ch][(size_t) s].process(x);
                        }
                        phaserState[(size_t) ch] = flushDenorm(x);
                        (ch == 0 ? wetL : wetR) = x;
                    }
                    break;
                }
                case vibratoMode:
                {
                    const float m1 = voiceLFO[0].tick();
                    const float m2 = voiceLFO[0].tick(0.25 * spread) ;
                    const double base = 0.005 * fs;
                    line[0].write(l[i]); line[1].write(r[i]);
                    wetL = line[0].readHermite(base * (1.0 + m1 * depth * 0.55));
                    wetR = line[1].readHermite(base * (1.0 + m2 * depth * 0.55));
                    break;
                }
                case tremoloMode:
                {
                    const float m1 = voiceLFO[0].tick() * 0.5f + 0.5f;
                    const float m2 = voiceLFO[0].tick(0.5 * spread) * 0.5f + 0.5f;
                    wetL = l[i] * (1.0f - depth + depth * m1);
                    wetR = r[i] * (1.0f - depth + depth * m2);
                    break;
                }
                case rotaryMode:
                {
                    // depth crossfades slow (0.8 Hz) <-> fast (6.4 Hz) rotor speed
                    const double target = 0.8 + depth * 5.6;
                    const double spd = rotarySpeed.process(target);
                    voiceLFO[0].setRate(spd);
                    voiceLFO[1].setRate(spd * 0.87); // drum lags horn
                    const float horn = voiceLFO[0].tick();
                    const float drum = voiceLFO[1].tick(0.31);
                    double lowL, lowR, bp, hp, hiL, hiR;
                    xover[0].process(l[i], lowL, bp, hiL);
                    xover[1].process(r[i], lowR, bp, hiR);
                    juce::ignoreUnused(hp);
                    const float hornAmL = 0.6f + 0.4f * (horn * 0.5f + 0.5f);
                    const float hornAmR = 0.6f + 0.4f * (-horn * 0.5f + 0.5f);
                    const float drumAm  = 0.75f + 0.25f * (drum * 0.5f + 0.5f);
                    wetL = (float) (hiL * hornAmL + lowL * drumAm);
                    wetR = (float) (hiR * hornAmR + lowR * drumAm);
                    break;
                }
            }

            l[i] = equalPowerMix(l[i], wetL, mix);
            r[i] = equalPowerMix(r[i], wetR, mix);
        }
    }

    void reset()
    {
        for (auto& d : line) d.clear();
        for (auto& ch : phaserAP) for (auto& ap : ch) ap.reset();
        phaserState[0] = phaserState[1] = 0.0f;
        xover[0].reset(); xover[1].reset();
    }

private:
    void tickChorus(float inL, float inR, float& outL, float& outR,
                    int voices, double baseMs, double depthMs)
    {
        line[0].write(inL); line[1].write(inR);
        float sumL = 0.0f, sumR = 0.0f;
        for (int v = 0; v < voices; ++v)
        {
            const float m = voiceLFO[(size_t) v].tick(v / (double) voices);
            const double d = (baseMs + v * 2.3 + m * depthMs * depth) * 0.001 * fs;
            if ((v & 1) == 0) sumL += line[0].readHermite(d);
            else              sumR += line[1].readHermite(d * (1.0 + 0.1 * spread));
        }
        const float norm = 1.0f / (float) juce::jmax(1, voices / 2);
        outL = sumL * norm;
        outR = voices > 1 ? sumR * norm : sumL * norm;
        // Narrow if spread is low
        const float mid = (outL + outR) * 0.5f, side = (outL - outR) * 0.5f * spread;
        outL = mid + side; outR = mid - side;
    }

    double fs = 48000.0;
    int    mode = 0;
    float  depth = 0.5f, fb = 0.0f, spread = 1.0f;
    std::array<DelayLine, 2> line;
    std::array<LFO, 6> voiceLFO;
    std::array<std::array<Biquad, 6>, 2> phaserAP;
    std::array<float, 2> phaserState {};
    std::array<SVF, 2> xover;
    OnePole rotarySpeed;
    SmoothLin mixSm;
};

// ---------------------------------------------------------------- Stereo ---
class StereoTools
{
public:
    void prepare(const Context& ctx)
    {
        fs = ctx.sampleRate;
        haasLine.prepare(fs, 0.06);
        widthSm.reset(fs, 0.03); midSm.reset(fs, 0.03); sideSm.reset(fs, 0.03);
        panSm.reset(fs, 0.03);
        reset();
    }

    void set(float widthPct, float midDb, float sideDb, float haasMs,
             float pan, float monoBelowHz) noexcept
    {
        widthSm.setTargetValue(widthPct * 0.01f);
        midSm.setTargetValue(dbToGain(midDb));
        sideSm.setTargetValue(dbToGain(sideDb));
        haasSamples = haasMs * 0.001 * fs;
        panSm.setTargetValue(pan);
        if (std::abs(monoBelowHz - lastMono) > 0.5f)
        {
            lastMono = monoBelowHz;
            monoOn = monoBelowHz > 5.0f;
            if (monoOn)
                for (auto& f : sideHP)
                    f.setCoefficients(Biquad::Type::highPass, fs, monoBelowHz, 0.71, 0.0);
        }
    }

    void process(juce::AudioBuffer<float>& buf)
    {
        const int n = buf.getNumSamples();
        auto* l = buf.getWritePointer(0);
        auto* r = buf.getWritePointer(1);

        for (int i = 0; i < n; ++i)
        {
            // Haas: delay right channel only
            if (haasSamples > 0.5)
            {
                haasLine.write(r[i]);
                r[i] = haasLine.readHermite(haasSamples);
            }

            float mid  = (l[i] + r[i]) * 0.5f;
            float side = (l[i] - r[i]) * 0.5f;

            if (monoOn) // fold lows to mono: highpass the side signal (LR-ish, 2x biquad)
                side = (float) sideHP[1].process(sideHP[0].process(side));

            mid  *= midSm.getNextValue();
            side *= sideSm.getNextValue() * widthSm.getNextValue();

            float yl = mid + side, yr = mid - side;

            // Constant-power pan
            const float p = panSm.getNextValue() * 0.5f + 0.5f;
            const float gl = std::cos(p * juce::MathConstants<float>::halfPi) * 1.41421f;
            const float gr = std::sin(p * juce::MathConstants<float>::halfPi) * 1.41421f;
            l[i] = yl * gl * 0.70711f;
            r[i] = yr * gr * 0.70711f;
        }
    }

    void reset()
    {
        haasLine.clear();
        for (auto& f : sideHP) f.reset();
    }

private:
    double fs = 48000.0, haasSamples = 0.0;
    float  lastMono = -1.0f;
    bool   monoOn = false;
    DelayLine haasLine;
    std::array<Biquad, 2> sideHP;
    SmoothLin widthSm, midSm, sideSm, panSm;
};

} // namespace vesper::dsp
