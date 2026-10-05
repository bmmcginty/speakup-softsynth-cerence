#include "speakup-scale.h"

/* Engine parameter ranges, matching PITCH_MIN/PITCH_MAX, RATE_MIN/RATE_MAX and
 * VOLUME_MIN/VOLUME_MAX in the Wine bridge. */
#define RATE_LOW 50
#define RATE_HIGH 400
#define PITCH_LOW 50
#define PITCH_HIGH 200
#define VOLUME_LOW 0
#define VOLUME_HIGH 100

static int scale_digit(int digit, int low, int high)
{
    if (digit < 0)
        digit = 0;
    if (digit > 9)
        digit = 9;
    /* Round to nearest so the nine steps divide the span as evenly as the
     * integer engine parameters allow. */
    return low + (digit * (high - low) + 4) / 9;
}

int speakup_scale_rate(int digit)
{
    return scale_digit(digit, RATE_LOW, RATE_HIGH);
}

int speakup_scale_pitch(int digit)
{
    return scale_digit(digit, PITCH_LOW, PITCH_HIGH);
}

int speakup_scale_volume(int digit)
{
    return scale_digit(digit, VOLUME_LOW, VOLUME_HIGH);
}
