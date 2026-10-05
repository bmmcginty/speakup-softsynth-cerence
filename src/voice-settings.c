#include "voice-settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

struct voice_setting *voice_settings_find(struct voice_settings *settings,
                                          const char *name)
{
    size_t i;

    for (i = 0; i < settings->count; i++)
        if (!strcasecmp(settings->items[i].name, name))
            return &settings->items[i];
    return NULL;
}

struct voice_setting *voice_settings_set(struct voice_settings *settings,
                                         const char *name, int rate,
                                         int volume)
{
    struct voice_setting *setting = voice_settings_find(settings, name);

    if (!setting) {
        if (settings->count == VOICE_SETTINGS_MAX || !name[0])
            return NULL;
        setting = &settings->items[settings->count++];
        snprintf(setting->name, sizeof setting->name, "%s", name);
    }
    setting->rate = rate;
    setting->volume = volume;
    return setting;
}

/* Accept a bare digit with optional trailing whitespace; reject empty fields
 * and anything outside Speakup's 0-9 range so a truncated write cannot
 * silently become volume zero. */
static int parse_digit(const char *text, int *value)
{
    char *end;
    long parsed;

    if (!text || !*text)
        return -1;
    parsed = strtol(text, &end, 10);
    if (end == text)
        return -1;
    while (*end == '\r' || *end == '\n' || *end == ' ' || *end == '\t')
        end++;
    if (*end != '\0' || parsed < 0 || parsed > 9)
        return -1;
    *value = (int)parsed;
    return 0;
}

void voice_settings_load(struct voice_settings *settings, const char *path)
{
    FILE *file = fopen(path, "r");
    char line[VOICE_SETTINGS_NAME + 32];

    if (!file)
        return;
    while (fgets(line, sizeof line, file)) {
        char *rate_text = strchr(line, '\t');
        char *volume_text;
        int rate, volume;

        if (!rate_text)
            continue;
        *rate_text++ = '\0';
        volume_text = strchr(rate_text, '\t');
        if (!volume_text)
            continue;
        *volume_text++ = '\0';
        if (parse_digit(rate_text, &rate) < 0 ||
            parse_digit(volume_text, &volume) < 0)
            continue;
        voice_settings_set(settings, line, rate, volume);
    }
    fclose(file);
}

int voice_settings_save(struct voice_settings *settings, const char *path)
{
    size_t length = strlen(path) + 5;
    char *temporary = malloc(length);
    FILE *file;
    size_t i;
    int failed = 0;

    if (!temporary)
        return -1;
    snprintf(temporary, length, "%s.tmp", path);
    file = fopen(temporary, "w");
    if (!file) {
        free(temporary);
        return -1;
    }
    for (i = 0; i < settings->count; i++) {
        if (strchr(settings->items[i].name, '\t') ||
            strchr(settings->items[i].name, '\n'))
            continue;
        if (fprintf(file, "%s\t%d\t%d\n", settings->items[i].name,
                    settings->items[i].rate, settings->items[i].volume) < 0)
            failed = 1;
    }
    if (fclose(file) != 0)
        failed = 1;
    if (failed || rename(temporary, path) != 0) {
        remove(temporary);
        free(temporary);
        return -1;
    }
    free(temporary);
    return 0;
}
