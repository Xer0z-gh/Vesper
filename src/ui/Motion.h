#pragma once
/*  Motion.h — the one clock and the one easing (docs/design/04).
    Every animation in Vesper runs through a Tween: display-refresh driven
    (VBlank), vesperEase only, always interruptible, reduced-motion aware.
    There is no other sanctioned way to animate. */

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

namespace vesper::motion {

// Durations (04)
constexpr int instant = 80;
constexpr int move    = 170;
constexpr int settle  = 240;

inline bool& reducedMotion()
{
    static bool rm = false;
    return rm;
}

/* cubic-bezier(0.4, 0, 0.2, 1): solve x(s) = t for s (Newton), return y(s). */
inline float vesperEase(float t) noexcept
{
    t = juce::jlimit(0.0f, 1.0f, t);
    auto bx = [] (float s) { return 3.0f * (1 - s) * (1 - s) * s * 0.4f
                                  + 3.0f * (1 - s) * s * s * 0.2f + s * s * s; };
    auto by = [] (float s) { return 3.0f * (1 - s) * s * s + s * s * s; };
    float s = t;
    for (int i = 0; i < 5; ++i)
    {
        const float x  = bx(s) - t;
        const float dx = 3.0f * (1 - s) * (1 - s) * 0.4f
                       + 6.0f * (1 - s) * s * (0.2f - 0.4f) + 3.0f * s * s * (1.0f - 0.2f);
        if (std::abs(dx) < 1.0e-6f) break;
        s = juce::jlimit(0.0f, 1.0f, s - x / dx);
    }
    return by(s);
}

/* Animates one component's bounds + alpha. Interruptible: start() replaces
   any running animation from the current pose (04 law 2). */
class Tween
{
public:
    void start(juce::Component& comp, juce::Rectangle<int> from, juce::Rectangle<int> to,
               float alphaFrom, float alphaTo, int ms, std::function<void()> onDone = {})
    {
        cancel();
        if (reducedMotion() || ms <= 0)
        {
            comp.setBounds(to);
            comp.setAlpha(alphaTo);
            if (onDone) onDone();
            return;
        }
        target    = &comp;
        fromB     = from;  toB = to;
        fromA     = alphaFrom; toA = alphaTo;
        durationMs = ms;
        done      = std::move(onDone);
        startTime = juce::Time::getMillisecondCounterHiRes();
        comp.setBounds(from);
        comp.setAlpha(alphaFrom);
        vblank = std::make_unique<juce::VBlankAttachment>(&comp, [this] { tick(); });
    }

    void cancel() // leaves the component at its current pose
    {
        vblank.reset();
        target = nullptr;
        done = nullptr;
    }

    bool isRunning() const noexcept { return target != nullptr; }

private:
    void tick()
    {
        if (target == nullptr) return;
        const float t = (float) ((juce::Time::getMillisecondCounterHiRes() - startTime)
                                 / (double) durationMs);
        const float e = vesperEase(t);
        auto lerp = [e] (int a, int b) { return a + juce::roundToInt((b - a) * e); };
        target->setBounds(lerp(fromB.getX(), toB.getX()), lerp(fromB.getY(), toB.getY()),
                          lerp(fromB.getWidth(), toB.getWidth()),
                          lerp(fromB.getHeight(), toB.getHeight()));
        target->setAlpha(fromA + (toA - fromA) * e);
        if (t >= 1.0f)
        {
            auto finished = std::move(done);
            cancel();
            if (finished) finished();
        }
    }

    juce::Component* target = nullptr;
    juce::Rectangle<int> fromB, toB;
    float fromA = 0.0f, toA = 1.0f;
    int durationMs = move;
    double startTime = 0.0;
    std::function<void()> done;
    std::unique_ptr<juce::VBlankAttachment> vblank;
};

} // namespace vesper::motion
