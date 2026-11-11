#include "client.h"
#include <stdio.h>
#include <stdlib.h>

int wmb_hello_main(unsigned iterations);
int main(int argc, char **argv)
{
    unsigned iterations = 1;
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: %s SOCKET [ITERATIONS]\n", argv[0]);
        return 2;
    }
    if (argc == 3) {
        char *end;
        unsigned long value = strtoul(argv[2], &end, 10);
        if (!*argv[2] || *end || !value || value > 10000) return 2;
        iterations = (unsigned)value;
    }
    if (wmb_bridge_bind(argv[1]) != 0) return 1;
    int result = wmb_hello_main(iterations);
    if (wmb_bridge_disconnect() != 0) result = 1;
    return result;
}
