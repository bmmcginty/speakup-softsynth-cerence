#ifndef VOICE_STORE_H
#define VOICE_STORE_H

#include <stddef.h>

#define VOICE_STORE_LANGUAGE 8
#define VOICE_STORE_NAME 128
#define VOICE_STORE_OPERATING_POINT 128

struct installed_voice {
    char language[VOICE_STORE_LANGUAGE];
    char name[VOICE_STORE_NAME];
    char operating_point[VOICE_STORE_OPERATING_POINT];
};

struct installed_voices {
    struct installed_voice *items;
    size_t count;
    size_t capacity;
    /* A hash over the header files that were found, used to tell whether a new
     * package appeared since the engine was started. */
    unsigned fingerprint;
};

/*
 * Enumerate installed voices without starting the engine by reading the
 * pipeline headers each package installs:
 *
 *   <store>/<lang>/speech/ve/ve_pipeline_<lang>_<name>_<freq>_<op>_<version>.hdr
 *
 * A missing or unreadable store yields an empty list, not an error.  Returns
 * -1 only when memory could not be allocated.
 */
int voice_store_scan(struct installed_voices *voices, const char *store);
void voice_store_destroy(struct installed_voices *voices);

#endif
