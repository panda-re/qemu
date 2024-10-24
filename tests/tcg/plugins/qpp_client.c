#include <stdio.h>
#include <qemu-plugin.h>
#include <plugin-qpp.h>
#include <glib.h>

QEMU_PLUGIN_EXPORT const char *qemu_plugin_name = "qpp_client";
QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;
QEMU_PLUGIN_EXPORT const char *qemu_plugin_uses[] = {"qpp_srv", NULL};

#include "qpp_srv.h"
static bool pass = true;

void my_cb_exit_callback(gpointer evdata, gpointer udata);

QEMU_PLUGIN_EXPORT void my_cb_exit_callback(gpointer evdata, gpointer udata)
{
    // Function is called by qpp_srv, update the evdata to be our 'pass' var
    *(bool *)evdata = pass;

    g_autoptr(GString) report = g_string_new("QPP client: my_on_exit callback triggered. ");
    g_string_append_printf(report, "Setting result=%d\n",pass);
    qemu_plugin_outs(report->str);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                   const qemu_info_t *info, int argc, char **argv) {

    // Use the QPP interface to run functions in qpp_srv

    // Target plugin is 'qpp_srv', we're calling the 'do_add' function and
    // we append '_qpp' to the function name to identify it as a QPP function.
    // This function is defined in qpp_srv.h
    // Ensure that the return value is as expected
    if (qpp_srv_do_add_qpp(3) == 4) {
        pass &= true;
    }

    // Now call the 'do_sub' method from qpp_srv and checking the result.
    if (qpp_srv_do_sub_qpp(10) == 9) {
        pass &= true;
    }
    
    // Register a callback to run on a QPP callback provided by qpp_srv
    qemu_plugin_reg_callback("qpp_srv", "my_on_exit", &my_cb_exit_callback);

    return 0;
}

