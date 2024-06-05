#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <qemu-plugin.h>
#include <plugin-qpp.h>
#include "syscalls.h"

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;
QEMU_PLUGIN_EXPORT const char *qemu_plugin_name = "syscalls_logger";

typedef enum { CSV, TEXT } LogFormat;

static FILE *log_file = NULL;
static LogFormat log_format = TEXT;

static void log_syscall(gpointer evdata, gpointer udata)
{
    SyscallDetails *details = (SyscallDetails*)evdata;
    g_autoptr(GString) report = g_string_new(NULL);

    if (log_format == CSV) {
        g_string_append_printf(report, "%lx,%lx", details->pc, details->callno);
        for (int i = 0; i < SYSCALLS_MAX_ARGS; i++) {
            g_string_append_printf(report, ",%lx", details->args[i]);
        }
        g_string_append_c(report, '\n');
    } else {
        g_string_append_printf(report, "PC %lx: syscall %lx(", details->pc, details->callno);
        for (int i = 0; i < SYSCALLS_MAX_ARGS - 1; i++) {
            g_string_append_printf(report, "%lx, ", details->args[i]);
        }
        g_string_append_printf(report, "%lx)\n", details->args[SYSCALLS_MAX_ARGS - 1]);
    }

    fprintf(log_file, "%s", report->str);
    fflush(log_file);
}

static bool parse_arguments(int argc, char **argv)
{
    for (int i = 0; i < argc; i++) {
        char *opt = argv[i];
        g_autofree char **tokens = g_strsplit(opt, "=", 2);
        if (tokens[0] == NULL || tokens[1] == NULL) {
            fprintf(stderr, "Invalid argument format: %s\n", opt);
            return false;
        }

        if (g_strcmp0(tokens[0], "filename") == 0) {
            if (log_file != NULL && log_file != stdout) {
                fclose(log_file);
            }
            log_file = fopen(tokens[1], "w");
            if (log_file == NULL) {
                fprintf(stderr, "Failed to open log file: %s\n", tokens[1]);
                return false;
            }
        } else if (g_strcmp0(tokens[0], "format") == 0) {
            if (g_strcmp0(tokens[1], "csv") == 0) {
                log_format = CSV;
            } else if (g_strcmp0(tokens[1], "text") == 0) {
                log_format = TEXT;
            } else {
                fprintf(stderr, "Invalid format option: %s\n", tokens[1]);
                return false;
            }
        } else {
            fprintf(stderr, "Unknown option: %s\n", tokens[0]);
            fprintf(stderr, "Available options: filename, format\n");
            return false;
        }
    }

    if (log_file == NULL) {
        log_file = stdout;
    }

    return true;
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    if (!parse_arguments(argc, argv)) {
        return -1;
    }

    qemu_plugin_reg_callback("syscalls", "on_all_sys_enter", log_syscall);
    return 0;
}