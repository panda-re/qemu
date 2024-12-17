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
#include "panda/plugin.h"


#if defined(TARGET_MIPS) || defined(TARGET_ARM)
void helper_panda_guest_hypercall(CPUArchState *cpu_env){
    panda_callbacks_guest_hypercall(env_cpu(cpu_env));
}
#endif
