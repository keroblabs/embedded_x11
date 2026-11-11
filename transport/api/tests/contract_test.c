#include "wmb_api.h"
#include "wmb_xlib_compat.h"
#include <assert.h>
#include <stdio.h>

void wmb_fake_table(wmb_api_t *table);
int wmb_legacy_contract(const void *table);
int main(void)
{
    wmb_api_t table;
    _Alignas(wmb_api_t) wmb_api_header_t short_table = {
        WMB_API_MAGIC, 1, WMB_API_SLOT_COUNT - 1, WMB_API_LAYOUT
    };
    assert(wmb_bind((const wmb_api_t *)(const void *)&short_table) == -3);
    short_table.version = WMB_API_VERSION;
    assert(wmb_bind((const wmb_api_t *)(const void *)&short_table) == -4);
    wmb_fake_table(&table);
    assert(wmb_bind(NULL) == -1);
    table.magic ^= 1;
    assert(wmb_bind(&table) == -2);
    table.magic = WMB_API_MAGIC;
    table.version = 1;
    assert(wmb_bind(&table) == -3);
    table.version = WMB_API_VERSION;
    table.count--;
    assert(wmb_bind(&table) == -4);
    table.count++;
    table.layout ^= 1;
    assert(wmb_bind(&table) == -5);
    wmb_fake_table(&table);
    table.wmbApiRevision = NULL;
    assert(wmb_bind(&table) == -6);
    wmb_fake_table(&table);
    assert(wmb_bind(&table) == 0);
    assert(DefaultScreen(NULL) == 42);
    assert(wmb_legacy_contract(&table) == 0);
    table.version++;
    assert(wmb_bind(&table) == 0 && wmb_legacy_contract(&table) == 0);
    puts("P8 ABI: typed slots, failures, frozen v1 on v2 and future append-only versions PASS");
    return 0;
}
