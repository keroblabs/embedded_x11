#include <regex.h>
int wmb_system_regex(const char *pattern, const char *expression)
{
    regex_t compiled;
    int result = regcomp(&compiled, pattern, REG_EXTENDED);
    if (result) return -1;
    result = regexec(&compiled, expression, 0, 0, 0);
    regfree(&compiled);
    return result == 0;
}
