#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "voice-store.h"

static void make_directory(const char *path)
{
    assert(mkdir(path, 0700) == 0);
}

static void make_file(const char *path)
{
    FILE *file = fopen(path, "w");
    assert(file != NULL);
    fclose(file);
}

static const struct installed_voice *find_voice(const struct installed_voices *voices,
                                                const char *name)
{
    size_t i;
    for (i = 0; i < voices->count; i++)
        if (!strcmp(voices->items[i].name, name))
            return &voices->items[i];
    return NULL;
}

int main(void)
{
    char root[] = "/tmp/speakup-cerence-voice-store-XXXXXX";
    char path[4096];
    char command[4096];
    struct installed_voices voices = {0};
    const struct installed_voice *voice;
    unsigned first_fingerprint;

    assert(mkdtemp(root) != NULL);
    snprintf(path, sizeof path, "%s/enu/speech/ve", root);
    snprintf(command, sizeof command, "%s/enu", root);
    make_directory(command);
    snprintf(command, sizeof command, "%s/enu/speech", root);
    make_directory(command);
    make_directory(path);
    snprintf(path, sizeof path, "%s/mnc/speech/ve", root);
    snprintf(command, sizeof command, "%s/mnc", root);
    make_directory(command);
    snprintf(command, sizeof command, "%s/mnc/speech", root);
    make_directory(command);
    make_directory(path);

    snprintf(path, sizeof path,
             "%s/enu/speech/ve/ve_pipeline_enu_allison_22_embedded-high_2-0-0.hdr",
             root);
    make_file(path);
    snprintf(path, sizeof path,
             "%s/mnc/speech/ve/ve_pipeline_mnc_tiantian_22_embedded-pro_1-0-0.hdr",
             root);
    make_file(path);
    /* Wrong prefix and too few fields must both be ignored. */
    snprintf(path, sizeof path,
             "%s/enu/speech/ve/other_enu_decoy_22_embedded-high_2-0-0.hdr",
             root);
    make_file(path);
    snprintf(path, sizeof path, "%s/enu/speech/ve/ve_pipeline_short.hdr", root);
    make_file(path);

    assert(voice_store_scan(&voices, root) == 0);
    assert(voices.count == 2);
    voice = find_voice(&voices, "allison");
    assert(voice != NULL);
    assert(strcmp(voice->language, "enu") == 0);
    assert(strcmp(voice->operating_point, "embedded-high") == 0);
    voice = find_voice(&voices, "tiantian");
    assert(voice != NULL);
    assert(strcmp(voice->language, "mnc") == 0);
    assert(strcmp(voice->operating_point, "embedded-pro") == 0);
    first_fingerprint = voices.fingerprint;
    voice_store_destroy(&voices);

    /* A store with only the first package yields a different fingerprint. */
    snprintf(path, sizeof path,
             "%s/mnc/speech/ve/ve_pipeline_mnc_tiantian_22_embedded-pro_1-0-0.hdr",
             root);
    assert(remove(path) == 0);
    assert(voice_store_scan(&voices, root) == 0);
    assert(voices.count == 1);
    assert(voices.fingerprint != first_fingerprint);
    voice_store_destroy(&voices);

    /* A missing store is empty, not an error. */
    assert(voice_store_scan(&voices, "/nonexistent/voice-store") == 0);
    assert(voices.count == 0);
    voice_store_destroy(&voices);

    snprintf(command, sizeof command, "rm -rf '%s'", root);
    assert(system(command) == 0);
    return 0;
}
