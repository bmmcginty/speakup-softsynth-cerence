#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "voice-settings.h"

int main(void)
{
    char path[] = "/tmp/speakup-cerence-voice-settings-XXXXXX";
    char temporary[sizeof path + 8];
    struct voice_settings settings = {0};
    struct voice_setting *found;
    FILE *file;
    int fd;

    fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);

    /* A missing file simply leaves the table empty. */
    voice_settings_load(&settings, "/nonexistent/voice-settings");
    assert(settings.count == 0);

    /* Updating a voice replaces its entry instead of adding another. */
    assert(voice_settings_set(&settings, "Alpha", 2, 5) != NULL);
    assert(voice_settings_set(&settings, "Beta", 8, 9) != NULL);
    assert(voice_settings_set(&settings, "alpha", 3, 6) != NULL);
    assert(settings.count == 2);

    assert(voice_settings_save(&settings, path) == 0);
    {
        struct voice_settings reloaded = {0};
        voice_settings_load(&reloaded, path);
        assert(reloaded.count == 2);
        found = voice_settings_find(&reloaded, "ALPHA");
        assert(found && found->rate == 3 && found->volume == 6);
        found = voice_settings_find(&reloaded, "beta");
        assert(found && found->rate == 8 && found->volume == 9);
    }

    /* Truncated and out-of-range records must not become real settings. */
    file = fopen(path, "w");
    assert(file);
    fputs("Gamma\t4\t\n", file);
    fputs("Delta\t10\t2\n", file);
    fputs("no-tabs\n", file);
    fputs("Epsilon\t1\n", file);
    fputs("Zeta\t4\t7\n", file);
    fclose(file);
    {
        struct voice_settings reloaded = {0};
        voice_settings_load(&reloaded, path);
        assert(reloaded.count == 1);
        found = voice_settings_find(&reloaded, "Zeta");
        assert(found && found->rate == 4 && found->volume == 7);
    }

    snprintf(temporary, sizeof temporary, "%s.tmp", path);
    remove(temporary);
    remove(path);
    return 0;
}
