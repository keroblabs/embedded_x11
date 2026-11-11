#include "allocation.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <regex.h>

char Jwm_Match(const char *pattern, const char *expression);
int wmb_system_regex(const char *pattern, const char *expression);
static int failure_after = -1;
void *__real_wmb_alloc(size_t size);
void *__wrap_wmb_alloc(size_t size)
{
    if (failure_after == 0) return NULL;
    if (failure_after > 0) failure_after--;
    return __real_wmb_alloc(size);
}
int main(void)
{
    static const char *patterns[] = {
        ".*", "^demo$", "^(demo|clock)[0-9]+$", "[[:alpha:]]+[0-9]{1,3}",
        "a?b*c+", "^[^x]{2,4}$", "(ab|cd)+", "a\\+b", "^$", "hello",
        "^(a)(b)\\1\\2$", "(^foo|bar$)", "x{2,}", "[[:space:]]", "(abc)?def"
    };
    static const char *values[] = {
        "", "demo", "demonstration", "clock123", "demo00", "ab12", "c",
        "ababc", "def", "abab", "a+b", "foobar", "  ", "xxxx"
    };
    allocation_stats_t before, after;
    unsigned tested = 0, oom = 0;
    assert(allocation_get_stats(1, &before) == 0);
    assert(Jwm_Match(NULL, NULL) && !Jwm_Match(NULL, "a") && !Jwm_Match("a", NULL));
    for (size_t pattern = 0; pattern < sizeof(patterns) / sizeof(patterns[0]); pattern++)
        for (size_t value = 0; value < sizeof(values) / sizeof(values[0]); value++) {
            assert((Jwm_Match(patterns[pattern], values[value]) != 0) ==
                   (wmb_system_regex(patterns[pattern], values[value]) == 1));
            tested++;
        }
    assert(!Jwm_Match("[", "abc"));
    for (int fault = 0; fault < 100; fault++) {
        regex_t expression;
        failure_after = fault;
        int result = regcomp(&expression, "^([[:alpha:]]|[0-9])+example$", REG_EXTENDED);
        if (result == 0) regfree(&expression);
        else { assert(result == REG_ESPACE); oom++; }
        failure_after = -1;
        assert(allocation_get_stats(1, &after) == 0 &&
               after.current_bytes == before.current_bytes && after.live_blocks == before.live_blocks);
    }
    assert(oom);
    printf("P9a regex: native JWM matcher, %u stock-POSIX comparisons and %u tracked OOM paths PASS\n",
           tested, oom);
    return 0;
}
