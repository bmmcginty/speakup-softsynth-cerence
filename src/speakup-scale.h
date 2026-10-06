#ifndef SPEAKUP_SCALE_H
#define SPEAKUP_SCALE_H

/*
 * Speakup sends the soft-synth rate, pitch and volume as single digits in the
 * range 0-9 (see the RATE_ID, PITCH_ID and VOL_ID variables in the kernel's
 * speakup_soft driver).  The Cerence engine takes percentages instead, so each
 * digit is scaled onto the engine's parameter scale.  Rate and pitch are
 * anchored so that Speakup's default digits select the engine's neutral value.
 */
int speakup_scale_rate(int digit);
int speakup_scale_pitch(int digit);
int speakup_scale_volume(int digit);

#endif
