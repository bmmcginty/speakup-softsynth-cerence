#include <assert.h>

#include "speakup-scale.h"

int main(void)
{
    int digit;
    int previous;

    /* Speakup's default digits select the engine's neutral value, so a fresh
     * install does not speak fast or high-pitched. */
    assert(speakup_scale_rate(2) == 100);
    assert(speakup_scale_pitch(5) == 100);

    /* The endpoints stay inside the engine's documented ranges. */
    assert(speakup_scale_rate(0) == 50);
    assert(speakup_scale_rate(9) == 275);
    assert(speakup_scale_pitch(0) == 50);
    assert(speakup_scale_pitch(9) == 140);
    assert(speakup_scale_volume(0) == 0);
    assert(speakup_scale_volume(9) == 100);

    /* Values stay inside the engine's range and never move backwards. */
    previous = speakup_scale_rate(0);
    for (digit = 1; digit <= 9; digit++) {
        int value = speakup_scale_rate(digit);
        assert(value >= 50 && value <= 275);
        assert(value >= previous);
        previous = value;
    }
    previous = speakup_scale_pitch(0);
    for (digit = 1; digit <= 9; digit++) {
        int value = speakup_scale_pitch(digit);
        assert(value >= 50 && value <= 140);
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
    assert(speakup_scale_pitch(-3) == 50);
    assert(speakup_scale_pitch(42) == 140);
    assert(speakup_scale_volume(-3) == 0);
    assert(speakup_scale_volume(42) == 100);
    return 0;
}
