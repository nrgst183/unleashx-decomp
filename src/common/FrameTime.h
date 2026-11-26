// Frame-rate independent animation.
//
// (Fix: UnleashX advanced its animations (fades, scrolling text, the orb,
// the menus' movement, blinking cursors...) by a fixed step every frame,
// tuned for 60 frames a second. With "fast refresh" (no vsync), held left
// stick at boot, 1080i or a PAL refresh, they ran at whatever the frame rate
// was. Each step is now scaled by the frame's time in 60 Hz frames.)
//
// CXBApplicationEx::Run (src/app/XBAppRun.cpp) sets the scale before each
// FrameMove/Render; it stays 1 elsewhere (the host tests, the blocking
// startup loops before the main loop starts).
#ifndef UNLEASHX_COMMON_FRAMETIME_H
#define UNLEASHX_COMMON_FRAMETIME_H

#include <math.h>

// The rate the per-frame steps were tuned for.
#define FRAME_TUNED_HZ 60.0f

// This frame's length in 60 Hz frames (1 at 60 fps, 0.5 at 120, 1.2 at 50).
// Header-only (a function's static is shared by every file) so no build's
// file lists change. (C++98: VC7.1 has no inline variables.)
inline float &FrameScaleRef()
{
	static float s_fScale = 1.0f;
	return s_fScale;
}

inline float FrameScale()
{
	return FrameScaleRef();
}

// Sets it from the frame's elapsed seconds. A long frame (loading, a dialog
// that blocked) counts as a few frames at most, so nothing jumps.
inline void SetFrameTime(float elapsedSeconds)
{
	float scale = elapsedSeconds * FRAME_TUNED_HZ;
	if (!(scale > 0.0f))
		scale = 0.0f;
	else if (scale > 4.0f)
		scale = 4.0f;
	FrameScaleRef() = scale;
}

// A per-frame step, scaled to this frame.
inline float FrameStep(float perFrame)
{
	return perFrame * FrameScale();
}

// For integer counters stepped once a frame (fades, delays, blink phases):
// how many 60 Hz frames passed, carrying the fraction in *carry (a static or
// member next to the counter, starting at 0). Over time this sums to exactly
// the original count at 60 fps.
inline int FrameTicks(float *carry)
{
	*carry += FrameScale();
	int ticks = (int)*carry;
	*carry -= (float)ticks;
	return ticks;
}

// For easing (x += (target - x) * rate each frame): the rate that does the
// same in this frame's time.
inline float FrameRate(float perFrameRate)
{
	if (perFrameRate <= 0.0f || perFrameRate >= 1.0f)
		return perFrameRate;
	return 1.0f - (float)pow(1.0f - perFrameRate, FrameScale());
}

// For decay (x *= factor each frame).
inline float FrameFactor(float perFrameFactor)
{
	if (perFrameFactor <= 0.0f)
		return perFrameFactor;
	return (float)pow(perFrameFactor, FrameScale());
}

// For timers that stepped at most once a frame ("a pixel every <ScrollSpeed>
// ms" for scrolling text): the time a step really took at 60 fps, the first
// 60 Hz frame after ms had passed. (At 60 fps a 20 ms marquee moved every
// second frame, 33 ms; with no vsync it moved every 20 ms.)
inline float FrameInterval(unsigned long ms)
{
	return (float)((floor((double)ms * (FRAME_TUNED_HZ / 1000.0)) + 1.0) * (1000.0 / FRAME_TUNED_HZ));
}

// Whether such a step is due at now (ms): *tick is the next one's time. Steps
// keep that 60 fps pace at any frame rate, not tied to 60 Hz frames. (After
// a long stall it starts again from now rather than catching up.)
inline bool FrameDue(unsigned long *tick, unsigned long now, unsigned long ms)
{
	if ((long)(now - *tick) <= 0)
		return false;
	unsigned long interval = (unsigned long)(FrameInterval(ms) + 0.5f);
	*tick += interval;
	if ((long)(now - *tick) > (long)interval)
		*tick = now + interval;
	return true;
}

// The same pace as a smooth movement: the steps (pixels) this frame's time
// is worth, for a position kept as a float.
inline float FrameSteps(unsigned long ms)
{
	return FrameScale() * (1000.0f / FRAME_TUNED_HZ) / FrameInterval(ms);
}

#endif
