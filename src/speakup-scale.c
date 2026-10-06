#include "speakup-scale.h"

/* The engine takes rate, pitch and volume as integer percentages.  Rate and
 * pitch are relative to the voice's own neutral value of 100 (the defaults the
 * engine reports through getParamList); rate accepts 50-400 and pitch accepts
 * 50-200.  Volume is an absolute percentage from 0 to 100.
 *
 * Speakup sends its settings as single digits 0-9 and its own defaults are
 * rate 2, pitch 5 and volume 5 (see the RATE_ID, PITCH_ID and VOL_ID variables
 * in the kernel's speakup_soft driver).  Spreading a digit evenly from the
 * bottom of an engine range put those defaults well above the engine's neutral
 * value: Speakup rate 2 became 128 and pitch 5 became 133, which is heard as
 * fast, chipmunk-like speech.  Anchor the engine's neutral value at the
 * Speakup default and move a fixed number of percentage points per step
 * instead.  The engine's extremes remain available to callers that set the
 * parameters directly, but they are deliberately not reachable from a single
 * Speakup digit. */
#define ENGINE_NEUTRAL 100
#define RATE_DEFAULT_DIGIT 2
#define RATE_STEP 25
#define PITCH_DEFAULT_DIGIT 5
#define PITCH_STEP 10
#define VOLUME_LOW 0
#define VOLUME_HIGH 100

static int clamp_digit(int digit)
{
    if (digit < 0)
        return 0;
    if (digit > 9)
        return 9;
    return digit;
}

static int scale_digit(int digit, int low, int high)
{
    digit = clamp_digit(digit);
    /* Round to nearest so the nine steps divide the span as evenly as the
     * integer engine parameters allow. */
    return low + (digit * (high - low) + 4) / 9;
}

int speakup_scale_rate(int digit)
{
    return ENGINE_NEUTRAL +
           (clamp_digit(digit) - RATE_DEFAULT_DIGIT) * RATE_STEP;
}

int speakup_scale_pitch(int digit)
{
    return ENGINE_NEUTRAL +
           (clamp_digit(digit) - PITCH_DEFAULT_DIGIT) * PITCH_STEP;
}

int speakup_scale_volume(int digit)
{
    return scale_digit(digit, VOLUME_LOW, VOLUME_HIGH);
}
