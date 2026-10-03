#include "voice-list.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static int choice_order(const void *left, const void *right)
{
    const struct voice_choice *a = left;
    const struct voice_choice *b = right;
    int order = strcasecmp(a->name, b->name);

    if (!order)
        order = strcmp(a->name, b->name);
    return order;
}

static int variant_order(const struct voice_choice *choice,
                         const char *language, const char *operating_point,
                         int quality_rank)
{
    int order;

    if (quality_rank != choice->quality_rank)
        return quality_rank > choice->quality_rank ? 1 : -1;
    order = strcasecmp(choice->language, language);
    if (order)
        return order > 0 ? 1 : -1;
    order = strcasecmp(choice->operating_point, operating_point);
    return order > 0 ? 1 : order < 0 ? -1 : 0;
}

static void set_choice(struct voice_choice *choice, const char *language,
                       const char *name, const char *operating_point,
                       int quality_rank)
{
    snprintf(choice->language, sizeof choice->language, "%s", language);
    snprintf(choice->name, sizeof choice->name, "%s", name);
    snprintf(choice->operating_point, sizeof choice->operating_point, "%s",
             operating_point);
    choice->quality_rank = quality_rank;
}

int voice_list_add(struct voice_list *list, const char *language,
                   const char *name, const char *operating_point,
                   int quality_rank)
{
    size_t i;
    struct voice_choice *items;

    for (i = 0; i < list->count; i++) {
        if (!strcasecmp(list->items[i].name, name)) {
            if (variant_order(&list->items[i], language, operating_point,
                              quality_rank) > 0)
                set_choice(&list->items[i], language, name, operating_point,
                           quality_rank);
            return 0;
        }
    }

    if (list->count == list->capacity) {
        size_t capacity = list->capacity ? list->capacity * 2 : 8;
        items = realloc(list->items, capacity * sizeof *items);
        if (!items)
            return -1;
        list->items = items;
        list->capacity = capacity;
    }
    set_choice(&list->items[list->count++], language, name, operating_point,
               quality_rank);
    return 0;
}

void voice_list_sort(struct voice_list *list)
{
    qsort(list->items, list->count, sizeof *list->items, choice_order);
}

const struct voice_choice *voice_list_select(struct voice_list *list,
                                             int voice_number)
{
    const size_t page_size = 6;
    size_t last_page;
    size_t index;

    if (!list->count)
        return NULL;
    last_page = ((list->count - 1) / page_size) * page_size;
    if (voice_number <= 0) {
        if (list->page_start >= page_size)
            list->page_start -= page_size;
        index = list->page_start;
    } else if (voice_number >= 7) {
        if (list->page_start < last_page) {
            list->page_start += page_size;
            index = list->page_start;
        } else {
            index = list->count - 1;
        }
    } else {
        index = list->page_start + (size_t)voice_number - 1;
        if (index >= list->count)
            index = list->count - 1;
    }
    return &list->items[index];
}

void voice_list_destroy(struct voice_list *list)
{
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
    list->page_start = 0;
}
