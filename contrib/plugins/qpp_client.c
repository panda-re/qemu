#include <stdio.h>
#include <qemu-plugin.h>
#include <plugin-qpp.h>
#include <glib.h>
//#include "qpp_srv.h"

typedef int(*qpp_srv_do_add_t)(int);
qpp_srv_do_add_t qpp_srv_do_add; // XXX missing colon
void _qpp_setup_qpp_srv_qpp_srv_do_add (void);
void __attribute__ ((constructor)) _qpp_setup_qpp_srv_qpp_srv_do_add (void) {
   if (strcmp("qpp_client", "qpp_srv") == 0) { } else { qpp_srv_do_add = qemu_plugin_import_function("qpp_srv", "qpp_srv_do_add");
    } };

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;
QEMU_PLUGIN_EXPORT const char *qemu_plugin_name = "qpp_client";
QEMU_PLUGIN_EXPORT const char *qemu_plugin_uses = "qpp_srv";

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                   const qemu_info_t *info, int argc, char **argv) {

    g_autoptr(GString) report = g_string_new(CURRENT_PLUGIN ": Call "
                                             "qpp_srv's do_add(0) => ");
    g_string_append_printf(report, "%d %d\n", qpp_srv_do_add(0), /*qpp_srv_do_sub(3)*/ 0);
    qemu_plugin_outs(report->str);

    assert(0);
    return 0;
}

