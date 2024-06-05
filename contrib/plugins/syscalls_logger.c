#include <stdio.h>
#include <qemu-plugin.h>
#include <plugin-qpp.h>
#include "syscalls.h"

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;
QEMU_PLUGIN_EXPORT const char *qemu_plugin_name = "syscalls_logger";
 
void log_syscall(gpointer evdata, gpointer udata);

void log_syscall(gpointer evdata, gpointer udata)
{
    SyscallDetails *details = (SyscallDetails*)evdata;
    g_autoptr(GString) report = g_string_new("");
    g_string_append_printf(report, "PC %lx: syscall %lx(", details->pc, details->callno);
    for (int i = 0; i < 5; i++) {
        g_string_append_printf(report, "%lx, ", details->args[i]);
    }
    g_string_append_printf(report, "%lx)\n", details->args[5]);
    qemu_plugin_outs(report->str);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                   const qemu_info_t *info, int argc, char **argv)
{
  qemu_plugin_reg_callback("syscalls", "on_all_sys_enter", log_syscall);
  return 0;
}