#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "panda/debug.h"
#include "panda/plugin.h"
#include "panda/common.h"
#include "exec/translator.h"
#include "panda/panda_qemu_plugin_helpers.h"


CPUState* panda_cpu_by_index(int index){
    CPUState *cpu = qemu_get_cpu(index);
    return cpu;
}

CPUState* panda_cpu_in_translate(void){
    return tcg_ctx->cpu;
}

TranslationBlock * panda_get_tb(struct qemu_plugin_tb *tb){
    const DisasContextBase *db = tcg_ctx->plugin_db;
    return db->tb;
}

static bool panda_has_callback_registered(panda_cb_type type){
    return panda_cbs[type] != NULL;
}

int panda_get_memcb_status(void){
    int rv = 0;
    if (panda_has_callback_registered(PANDA_CB_PHYS_MEM_BEFORE_READ) 
    || panda_has_callback_registered(PANDA_CB_VIRT_MEM_BEFORE_READ))
    {
        rv |= QEMU_PLUGIN_BEFORE_MEM_R;
    }
    if (panda_has_callback_registered(PANDA_CB_PHYS_MEM_AFTER_READ)
    || panda_has_callback_registered(PANDA_CB_VIRT_MEM_AFTER_READ))
    {
        rv |= QEMU_PLUGIN_AFTER_MEM_R;
    }
    if (panda_has_callback_registered(PANDA_CB_PHYS_MEM_BEFORE_WRITE) 
    || panda_has_callback_registered(PANDA_CB_VIRT_MEM_BEFORE_WRITE))
    {
        rv |= QEMU_PLUGIN_BEFORE_MEM_W;
    }
    if (panda_has_callback_registered(PANDA_CB_PHYS_MEM_AFTER_WRITE)
    || panda_has_callback_registered(PANDA_CB_VIRT_MEM_AFTER_WRITE))
    {
        rv |= QEMU_PLUGIN_AFTER_MEM_W;
    }
	return rv;
}
