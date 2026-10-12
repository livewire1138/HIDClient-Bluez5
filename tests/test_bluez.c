#include "../bluez.h"
#include "../hid.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    GVariant *options=g_variant_ref_sink(bluez_profile_options("Keyboard & \"Mouse\"",HID_MODE_COMBO));
    const char *name,*role,*record;
    assert(g_variant_lookup(options,"Name","&s",&name) && !strcmp(name,"Keyboard & \"Mouse\""));
    assert(g_variant_lookup(options,"Role","&s",&role) && !strcmp(role,"server"));
    assert(g_variant_lookup(options,"ServiceRecord","&s",&record));
    assert(strstr(record,"Keyboard &amp; &quot;Mouse&quot;"));
    assert(strstr(record,"0x0011") && strstr(record,"0x0013"));
    assert(!g_variant_lookup_value(options,"PSM",NULL));
    assert(!g_variant_lookup_value(options,"Channel",NULL));
    g_variant_unref(options);
    puts("D-Bus option types and record-only registration checks passed");
    return 0;
}
