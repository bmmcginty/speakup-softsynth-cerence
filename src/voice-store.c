#include "voice-store.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define FNV_OFFSET 2166136261u
#define FNV_PRIME 16777619u

static unsigned fingerprint_update(unsigned hash, const void *data,
                                   size_t length)
{
    const unsigned char *bytes = data;

    while (length--) {
        hash ^= *bytes++;
        hash *= FNV_PRIME;
    }
    return hash;
}

/* The header name is the only index of installed packages.  The engine stores
 * one file per voice/operating-point pair: the language, voice, sample
 * frequency, operating point and version, separated by underscores. */
static int parse_header(const char *filename, struct installed_voice *voice)
{
    static const char prefix[] = "ve_pipeline_";
    static const char suffix[] = ".hdr";
    const size_t prefix_length = sizeof prefix - 1;
    const size_t suffix_length = sizeof suffix - 1;
    size_t length = strlen(filename);
    char buffer[512];
    char *fields[6];
    size_t count = 0;
    char *save = NULL;
    char *token;

    if (length <= prefix_length + suffix_length)
        return -1;
    if (strncmp(filename, prefix, prefix_length) != 0)
        return -1;
    if (strcmp(filename + length - suffix_length, suffix) != 0)
        return -1;
    length -= prefix_length + suffix_length;
    if (length >= sizeof buffer)
        return -1;
    memcpy(buffer, filename + prefix_length, length);
    buffer[length] = '\0';

    for (token = strtok_r(buffer, "_", &save);
         token && count < sizeof fields / sizeof fields[0];
         token = strtok_r(NULL, "_", &save))
        fields[count++] = token;
    if (count < 4 || !fields[0][0] || !fields[1][0] || !fields[3][0])
        return -1;
    snprintf(voice->language, sizeof voice->language, "%s", fields[0]);
    snprintf(voice->name, sizeof voice->name, "%s", fields[1]);
    snprintf(voice->operating_point, sizeof voice->operating_point, "%s",
             fields[3]);
    return 0;
}

static int append_voice(struct installed_voices *voices,
                        const struct installed_voice *voice)
{
    if (voices->count == voices->capacity) {
        size_t capacity = voices->capacity ? voices->capacity * 2 : 8;
        struct installed_voice *items =
            realloc(voices->items, capacity * sizeof *items);
        if (!items)
            return -1;
        voices->items = items;
        voices->capacity = capacity;
    }
    voices->items[voices->count++] = *voice;
    return 0;
}

static unsigned fingerprint_file(unsigned hash, const char *directory,
                                 const char *name)
{
    char path[4096];
    struct stat status;

    hash = fingerprint_update(hash, directory, strlen(directory));
    hash = fingerprint_update(hash, name, strlen(name));
    if (snprintf(path, sizeof path, "%s/%s", directory, name) <
            (int)sizeof path && stat(path, &status) == 0) {
        hash = fingerprint_update(hash, &status.st_size,
                                  sizeof status.st_size);
        hash = fingerprint_update(hash, &status.st_mtime,
                                  sizeof status.st_mtime);
    }
    return hash;
}

int voice_store_scan(struct installed_voices *voices, const char *store)
{
    unsigned fingerprint = FNV_OFFSET;
    DIR *store_dir;
    struct dirent *language_entry;

    memset(voices, 0, sizeof *voices);
    store_dir = opendir(store);
    if (!store_dir) {
        voices->fingerprint = fingerprint;
        return 0;
    }
    while ((language_entry = readdir(store_dir))) {
        char ve_directory[4096];
        DIR *ve_dir;
        struct dirent *file_entry;

        if (language_entry->d_name[0] == '.')
            continue;
        if (snprintf(ve_directory, sizeof ve_directory, "%s/%s/speech/ve",
                     store, language_entry->d_name) >=
            (int)sizeof ve_directory)
            continue;
        ve_dir = opendir(ve_directory);
        if (!ve_dir)
            continue;
        while ((file_entry = readdir(ve_dir))) {
            struct installed_voice voice;

            if (parse_header(file_entry->d_name, &voice) < 0)
                continue;
            fingerprint = fingerprint_file(fingerprint, ve_directory,
                                           file_entry->d_name);
            if (append_voice(voices, &voice) < 0) {
                closedir(ve_dir);
                closedir(store_dir);
                voice_store_destroy(voices);
                return -1;
            }
        }
        closedir(ve_dir);
    }
    closedir(store_dir);
    voices->fingerprint = fingerprint;
    return 0;
}

void voice_store_destroy(struct installed_voices *voices)
{
    free(voices->items);
    memset(voices, 0, sizeof *voices);
}
