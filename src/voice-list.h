#ifndef VOICE_LIST_H
#define VOICE_LIST_H

#include <stddef.h>

#define VOICE_FIELD_SIZE 128

struct voice_choice {
    char language[VOICE_FIELD_SIZE];
    char name[VOICE_FIELD_SIZE];
    char operating_point[VOICE_FIELD_SIZE];
    int quality_rank;
};

struct voice_list {
    struct voice_choice *items;
    size_t count;
    size_t capacity;
    size_t page_start;
};

int voice_list_add(struct voice_list *list, const char *language,
                   const char *name, const char *operating_point,
                   int quality_rank);
void voice_list_sort(struct voice_list *list);
const struct voice_choice *voice_list_select(struct voice_list *list,
                                             int voice_number);
void voice_list_destroy(struct voice_list *list);

#endif
