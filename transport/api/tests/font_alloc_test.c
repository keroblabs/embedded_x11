#include "allocation.h"
#include "hal_alloc.h"
#include <X11/fonts/bufio.h>
#include <assert.h>
#include <stdio.h>

typedef struct { const unsigned char *bytes; size_t length, position; } Input;
static int fail_after = -1;
void *__real_wmb_alloc(size_t size);
void *__wrap_wmb_alloc(size_t size)
{
    if (fail_after == 0) return NULL;
    if (fail_after > 0) fail_after--;
    return __real_wmb_alloc(size);
}
static int input(BufFilePtr file)
{
    Input *source = (Input *)(void *)file->private;
    file->left = 0;
    return source->position == source->length ? BUFFILEEOF : source->bytes[source->position++];
}
static int close_input(BufFilePtr file, int flag)
{
    (void)file; (void)flag;
    return 1;
}
int main(void)
{
    static const unsigned char header[] = {0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 255};
    allocation_stats_t initial, final;
    unsigned successes = 0, failures = 0;
    assert(allocation_get_stats(1, &initial) == 0);
    for (unsigned malformed = 0; malformed < 2; malformed++) {
        for (int fault = -1; fault < 12; fault++) {
            Input source = {header, malformed ? 0 : sizeof(header), 0};
            BufFilePtr base = BufFileCreate((char *)(void *)&source, input, NULL, NULL, close_input);
            assert(base);
            fail_after = fault;
            BufFilePtr zip = BufFilePushZIP(base);
            fail_after = -1;
            if (zip) {
                assert(!malformed);
                assert(BufFileClose(zip, 0));
                successes++;
            } else {
                assert(BufFileClose(base, 0));
                failures++;
            }
            assert(allocation_get_stats(1, &final) == 0);
            assert(final.current_bytes == initial.current_bytes &&
                   final.live_blocks == initial.live_blocks);
        }
    }
    assert(successes && failures);
    printf("P8 font allocation: zlib callbacks, malformed input and every backing OOM seam "
           "(%u successes/%u failures), no retained bytes PASS\n", successes, failures);
    return 0;
}
