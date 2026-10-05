#include <assert.h>

#include "speakup-scale.h"

int main(void)
{
    int digit;
    int previous;

    /* The endpoints of Speakup's 0-9 range reach the engine's endpoints. */
    assert(speakup_scale_rate(0) == 50);
    assert(speakup_scale_rate(9) == 400);
    assert(speakup_scale_pitch(0) == 50);
    assert(speakup_scale_pitch(9) == 200);
    assert(speakup_scale_volume(0) == 0);
    assert(speakup_scale_volume(9) == 100);

    /* Values stay inside the engine's range and never move backwards. */
    previous = speakup_scale_rate(0);
    for (digit = 1; digit <= 9; digit++) {
        int value = speakup_scale_rate(digit);
        assert(value >= 50 && value <= 400);
        assert(value >= previous);
        previous = value;
    }
    previous = speakup_scale_pitch(0);
    for (digit = 1; digit <= 9; digit++) {
        int value = speakup_scale_pitch(digit);
        assert(value >= 50 && value <= 200);
        assert(value >= previous);
        previous = value;
    }
    previous = speakup_scale_volume(0);
    for (digit = 1; digit <= 9; digit++) {
        int value = speakup_scale_volume(digit);
        assert(value >= 0 && value <= 100);
        assert(value >= previous);
        previous = value;
    }

    /* Out-of-range digits are clamped rather than passed through. */
    assert(speakup_scale_volume(-3) == 0);
    assert(speakup_scale_volume(42) == 100);
    return 0;
}
