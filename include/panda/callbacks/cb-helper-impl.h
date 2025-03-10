/* PANDABEGINCOMMENT
 * 
 * Authors:
 *  Tim Leek               tleek@ll.mit.edu
 *  Ryan Whelan            rwhelan@ll.mit.edu
 *  Joshua Hodosh          josh.hodosh@ll.mit.edu
 *  Michael Zhivich        mzhivich@ll.mit.edu
 *  Brendan Dolan-Gavitt   brendandg@gatech.edu
 * 
 * This work is licensed under the terms of the GNU GPL, version 2. 
 * See the COPYING file in the top-level directory. 
 * 
PANDAENDCOMMENT */
#pragma once
#include "panda/callbacks/cb-support.h"
// #include "panda/callbacks/cb-helper-defs.h"
#include "panda/plugin.h"

// void helper_panda_insn_exec(target_ulong pc) {
//     // PANDA instrumentation: before basic block
//     panda_cb_list *plist;
//     for(plist = panda_cbs[PANDA_CB_INSN_EXEC]; plist != NULL; plist = panda_cb_list_next(plist)) {
//         if (plist->enabled) {
//             plist->entry.insn_exec(plist->context, first_cpu, pc);
//         }
//     }
// }

// void helper_panda_after_insn_exec(target_ulong pc) {
//     // PANDA instrumentation: after basic block
//     panda_cb_list *plist;
//     for(plist = panda_cbs[PANDA_CB_AFTER_INSN_EXEC]; plist != NULL; plist = panda_cb_list_next(plist)) {
//         if (plist->enabled){
//             plist->entry.after_insn_exec(plist->context, first_cpu, pc);
//         }
//     }
// }

void helper_panda_guest_hypercall(void) {
    panda_callbacks_guest_hypercall(current_cpu);
}
