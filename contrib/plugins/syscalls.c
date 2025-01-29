/*
 * Copyright (C) 2025, Luke Craig <luke.craig@mit.edu>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */

#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <glib.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;


bool aarch64_check_syscall(struct qemu_plugin_insn *insn){
    qemu_plugin_insn_disas(insn)
}

typedef struct {
    const char *qemu_target;
    bool (*check_syscall)(struct qemu_plugin_insn *tb);
} ProfileSelector;

static ProfileSelector profile_tables[] = {
    { "aarch64", aarch64_insn_classes, ARRAY_SIZE(aarch64_insn_classes) },
    { "arm",   sparc32_insn_classes, ARRAY_SIZE(sparc32_insn_classes) },
    { "x86_64", sparc64_insn_classes, ARRAY_SIZE(sparc64_insn_classes) },
    { "mipsel", sparc64_insn_classes, ARRAY_SIZE(sparc64_insn_classes) },
    { "mips", sparc64_insn_classes, ARRAY_SIZE(sparc64_insn_classes) },
    { NULL, default_insn_classes, ARRAY_SIZE(default_insn_classes) },
};

static ProfileSelector profile;




static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb){
    struct qemu_plugin_insn *last = qemu_plugin_tb_get_insn(tb, qemu_plugin_tb_n_insns(tb) - 1);
    target_ulong pc = qemu_plugin_tb_vaddr(tb) + qemu_plugin_tb_size(tb) - qemu_plugin_insn_size(last);
    if (profile.check_syscall(insn)){
        qemu_plugin_register_vcpu_insn_exec_cb(
            insn, , QEMU_PLUGIN_CB_NO_REGS, pc); 
    }
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info, int argc,
                                           char **argv){
    int i;
    if (info->system_emulation == false)
    {
        qemu_plugin_outs("This plugin only works with system emulation\n");
        return -1;
    }
    for (i = 0; i<ARRAY_SIZE(profile_tables); i++){
        if (g_strcmp0(profile_tables[i].qemu_target, info->target_name) == 0){
            profile = profile_tables[i];
            break;
        }
    }
    if (profile.qemu_target == NULL){
        qemu_plugin_outs("Plugin does not support architecture: %s\n", info->target_name);
        return -1;
    }
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    return 0;
}