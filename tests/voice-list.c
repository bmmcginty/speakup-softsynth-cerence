#include <assert.h>
#include <string.h>

#include "voice-list.h"

static const char *select_name(struct voice_list *list, int number)
{
    const struct voice_choice *choice = voice_list_select(list, number);
    return choice ? choice->name : NULL;
}

int main(void)
{
    struct voice_list list = {0};
    char name[2] = "A";
    int i;

    /* Insert in reverse order to exercise alphabetic sorting. */
    for (i = 13; i >= 0; i--) {
        name[0] = (char)('A' + i);
        assert(voice_list_add(&list, "ENG", name, "embedded-compact", 0) == 0);
    }
    /* Only the best installed operating point for a named voice is exposed. */
    assert(voice_list_add(&list, "ENG", "N", "embedded-premium", 3) == 0);
    assert(list.count == 14);

    voice_list_sort(&list);
    assert(strcmp(select_name(&list, 1), "A") == 0);
    assert(strcmp(select_name(&list, 6), "F") == 0);

    /* Seven opens the next six-voice page and zero opens the previous one. */
    assert(strcmp(select_name(&list, 7), "G") == 0);
    assert(strcmp(select_name(&list, 1), "G") == 0);
    assert(strcmp(select_name(&list, 6), "L") == 0);
    assert(strcmp(select_name(&list, 7), "M") == 0);

    /* A short final page and attempts to pass either end are clamped. */
    assert(strcmp(select_name(&list, 3), "N") == 0);
    assert(strcmp(select_name(&list, 7), "N") == 0);
    assert(strcmp(select_name(&list, 0), "G") == 0);
    assert(strcmp(select_name(&list, 0), "A") == 0);
    assert(strcmp(select_name(&list, 0), "A") == 0);
    assert(strcmp(list.items[13].operating_point, "embedded-premium") == 0);

    voice_list_destroy(&list);
    assert(voice_list_select(&list, 1) == NULL);
    return 0;
}
