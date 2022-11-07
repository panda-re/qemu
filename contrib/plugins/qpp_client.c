#include <stdio.h>
#include <qemu-plugin.h>
#include <plugin-qpp.h>
#include <glib.h>
#include "qpp_srv.h"


QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;
QEMU_PLUGIN_EXPORT const char *qemu_plugin_name = "qpp_client";
QEMU_PLUGIN_EXPORT const char *qemu_plugin_uses = "qpp_srv";

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                   const qemu_info_t *info, int argc, char **argv) {

    g_autoptr(GString) report = g_string_new(CURRENT_PLUGIN ": Call "
                                             "qpp_srv's do_add(0) do_sub(3) => ");
    g_string_append_printf(report, "%d %d\n", qpp_srv_do_add(0), qpp_srv_do_sub(3));
    qemu_plugin_outs(report->str);

    assert(0);
    return 0;
}

