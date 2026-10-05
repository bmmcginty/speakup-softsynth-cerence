#ifndef VOICE_SETTINGS_H
#define VOICE_SETTINGS_H

#include <stddef.h>

#define VOICE_SETTINGS_NAME 128
#define VOICE_SETTINGS_MAX 256

struct voice_setting {
    char name[VOICE_SETTINGS_NAME];
    int rate;
    int volume;
};

struct voice_settings {
    struct voice_setting items[VOICE_SETTINGS_MAX];
    size_t count;
};

struct voice_setting *voice_settings_find(struct voice_settings *settings,
                                          const char *name);
struct voice_setting *voice_settings_set(struct voice_settings *settings,
                                         const char *name, int rate,
                                         int volume);
void voice_settings_load(struct voice_settings *settings, const char *path);
int voice_settings_save(struct voice_settings *settings, const char *path);

#endif
