/*
 * wmb_app_table_smoke.c - Compile-and-link proof for the wmb_app_table
 * contract (docs/45-jwm-module-plan.md section 3.1.4, I5 artefact 3).
 *
 * Instantiates a two-entry wmb_app_table[], provides a linear-walk
 * wmb_app_lookup, stubs wmb_app_launch, and verifies both a hit and a
 * miss.  When the real transport lands (I7-I9) the two implementations
 * here are retired; the entries are the seed for section 4.8.4's stub.
 */
#include "wmb_app_table.h"

#include <stdio.h>
#include <string.h>

static int log_hello(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("log_hello\n");
    return 0;
}

static int flag_toggle(int argc, char **argv)
{
    (void)argc; (void)argv;
    return 0;
}

const wmb_app_t wmb_app_table[] = {
    { "log_hello",   log_hello,   32 * 1024 },
    { "flag_toggle", flag_toggle, 32 * 1024 },
};
const size_t wmb_app_table_size =
    sizeof(wmb_app_table) / sizeof(wmb_app_table[0]);

const wmb_app_t *wmb_app_lookup(const char *name)
{
    for (size_t i = 0; i < wmb_app_table_size; i++) {
        if (strcmp(wmb_app_table[i].name, name) == 0) {
            return &wmb_app_table[i];
        }
    }
    return NULL;
}

hal_task_t *wmb_app_launch(const wmb_app_t *app, int argc, char **argv)
{
    (void)app; (void)argc; (void)argv;
    return NULL;
}

int main(void)
{
    const wmb_app_t *hit  = wmb_app_lookup("log_hello");
    const wmb_app_t *miss = wmb_app_lookup("nonexistent");

    if (!hit) {
        fprintf(stderr, "FAIL: lookup(\"log_hello\") returned NULL\n");
        return 1;
    }
    if (miss) {
        fprintf(stderr, "FAIL: lookup(\"nonexistent\") returned non-NULL\n");
        return 1;
    }
    printf("PASS: wmb_app_table_smoke - found '%s', missed 'nonexistent'\n",
           hit->name);
    return 0;
}
