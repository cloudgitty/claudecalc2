#include "libreader.h"
#include <stdio.h>

/* Placeholder until Part B2 defines LiteLibrary.bin. */

struct lr_library { int unused; };

lr_library *lr_open_memory(const void *data, size_t len, char *err, size_t err_len)
{
    (void)data;
    (void)len;
    if (err && err_len) snprintf(err, err_len, "library reader not implemented yet (Part B2)");
    return NULL;
}

void lr_close(lr_library *lib) { (void)lib; }

int lr_query(const lr_library *lib, double hero_premium, double villain_premium, int position,
             int pot_type, double spr, const uint8_t flop[3], int icm, lr_values *out)
{
    (void)lib; (void)hero_premium; (void)villain_premium; (void)position;
    (void)pot_type; (void)spr; (void)flop; (void)icm; (void)out;
    return -1;
}
