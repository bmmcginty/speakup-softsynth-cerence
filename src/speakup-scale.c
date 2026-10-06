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
 * Speakup digit.
 *
 * Speakup volume 0 maps to engine silence if it is scaled from zero.  espeakup,
 * which this driver replaces, deliberately never passes 0 to espeak: it uses
 * (volume + 1) * 22 on espeak's 0-200 scale.  Mirror that on the engine's
 * 0-100 scale as (digit + 1) * 11, so the bottom digit stays audible and the
 * default digit 5 still gives about two thirds of full volume. */
#define ENGINE_NEUTRAL 100
#define RATE_DEFAULT_DIGIT 2
#define RATE_STEP 25
#define PITCH_DEFAULT_DIGIT 5
#define PITCH_STEP 10
#define VOLUME_STEP 11
#define VOLUME_MAX 100

static int clamp_digit(int digit)
{
    if (digit < 0)
        return 0;
    if (digit > 9)
        return 9;
    return digit;
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
    int volume = (clamp_digit(digit) + 1) * VOLUME_STEP;

    if (volume > VOLUME_MAX)
        volume = VOLUME_MAX;
    return volume;
}
