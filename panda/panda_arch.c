#include "panda/common.h"

/* (not kernel-doc)
 * panda_in_kernel_mode() - Determine if guest is in kernel.
 * @cpu: Cpu state.
 *
 * Determines if guest is currently executing in kernel mode, e.g. execution privilege level.
 *
 * Return: True if in kernel, false otherwise.
 */
bool panda_in_kernel_mode(const CPUState *cpu) {
    CPUArchState *env = cpu_env((CPUState*) cpu);
#if defined(TARGET_I386)
    return ((env->hflags & HF_CPL_MASK) == 0);
#elif defined(TARGET_ARM)
    // See target/arm/cpu.h arm_current_el
    if (env->aarch64) {
        return extract32(env->pstate, 2, 2) > 0;
    }
    // Note: returns true for non-SVC modes (hypervisor, monitor, system, etc).
    // See: https://www.keil.com/pack/doc/cmsis/Core_A/html/group__CMSIS__CPSR__M.html
    return ((env->uncached_cpsr & CPSR_M) > ARM_CPU_MODE_USR);
#elif defined(TARGET_PPC)
    return ((env->msr >> MSR_PR) & 1);
#elif defined(TARGET_MIPS)
    return (env->hflags & MIPS_HFLAG_KSU) == MIPS_HFLAG_KM;
#else
#error "panda_in_kernel_mode() not implemented for target architecture."
    return false;
#endif
}


/* (not kernel-doc)
 * panda_in_kernel() - Determine if guest is in kernel.
 * @cpu: Cpu state.
 *
 * Determines if guest is currently executing in kernel mode, e.g. execution privilege level.
 * DEPRECATED.
 * 
 * Return: True if in kernel, false otherwise.
 */
bool panda_in_kernel(const CPUState *cpu) {
    return panda_in_kernel_mode(cpu);
}


/* (not kernel-doc)
 * address_in_kernel_code_linux() - Determine if virtual address is in kernel.                                                                           *                                                                                                                       
 * @addr: Virtual address to check.
 *
 * Checks the top bit of the address to determine if the address is in
 * kernel space. Checking the MSB means this should work even if KASLR
 * is enabled.
 *
 * Return: True if address is in kernel, false otherwise.
 */
bool address_in_kernel_code_linux(target_ulong addr){
    // TODO: Find a way to ask QEMU what the permissions are on an area.
    #if (defined(TARGET_ARM) && !defined(TARGET_AARCH64)) || (defined(TARGET_I386) && !defined(TARGET_X86_64))
    // I386: https://elixir.bootlin.com/linux/latest/source/arch/x86/include/asm/page_32_types.h#L18
    // ARM32: https://people.kernel.org/linusw/how-the-arm32-kernel-starts
    // ARM has a variable VMSPLIT. Technically this can be several values,
    // but the most common offset is 0xc0000000. 
    target_ulong vmsplit =  0xc0000000;
    return addr >= vmsplit;
    #else
    // MIPS32: https://elixir.bootlin.com/linux/latest/source/arch/mips/include/asm/mach-malta/spaces.h#L36
    // https://techpubs.jurassic.nl/manuals/0620/developer/DevDriver_PG/sgi_html/ch01.html
    // https://www.kernel.org/doc/html/latest/vm/highmem.html
    // x86_64: https://github.com/torvalds/linux/blob/master/Documentation/x86/x86_64/mm.rst
    // If addr MSB set -> kernelspace!
    // AARCH64: https://elixir.bootlin.com/linux/latest/source/arch/arm64/include/asm/memory.h#L45
    target_ulong msb_mask = ((target_ulong)1 << ((sizeof(target_long) * 8) - 1));
    return msb_mask & addr;
    #endif
}


/* (not kernel-doc)
 * panda_in_kernel_code_linux() - Determine if current pc is kernel code.
 * @cpu: Cpu state.
 *
 * Determines if guest is currently executing kernelspace code,
 * regardless of privilege level.  Necessary because there's a small
 * bit of kernelspace code that runs AFTER a switch to usermode
 * privileges.  Therefore, certain analysis logic can't rely on
 * panda_in_kernel_mode() alone. 
 *
 * Return: true if pc is in kernel, false otherwise.
 */
bool panda_in_kernel_code_linux(CPUState *cpu) {
    return address_in_kernel_code_linux(panda_current_pc(cpu));
}


/* (not kernel-doc)
 * panda_current_ksp() - Get guest kernel stack pointer.
 * @cpu: Cpu state.
 * 
 * Return: Guest pointer value.
 */
target_ulong panda_current_ksp(CPUState *cpu) {
    CPUArchState *env = cpu_env(cpu);
#if defined(TARGET_I386)
    if (panda_in_kernel(cpu)) {
        // Return directly the ESP register value.
        return env->regs[R_ESP];
    } else {
        // Returned kernel ESP stored in the TSS.
        // Related reading: https://css.csail.mit.edu/6.858/2018/readings/i386/c07.htm
        const uint32_t esp0 = 4;
        const target_ulong tss_base = ((CPUX86State *)env)->tr.base + esp0;
        target_ulong kernel_esp = 0;
        if (panda_virtual_memory_rw(cpu, tss_base, (uint8_t *)&kernel_esp, sizeof(kernel_esp), false ) < 0) {
            return 0;
        }
        return kernel_esp;
    }
#elif defined(TARGET_ARM)
    if(env->aarch64) {
        return env->sp_el[1];
    } else {
        if ((env->uncached_cpsr & CPSR_M) == ARM_CPU_MODE_SVC) {
            return env->regs[13];
        }else {
            // Read banked R13 for SVC mode to get the kernel SP (1=>SVC bank from target/arm/internals.h)
            return env->banked_r13[1];
        }
    }
#elif defined(TARGET_PPC)
    // R1 on PPC.
    return env->gpr[1];
#elif defined(TARGET_MIPS)
    return env->active_tc.gpr[MIPS_SP];
#else
#error "panda_current_ksp() not implemented for target architecture."
    return 0;
#endif
}


/* (not kernel-doc)
 * panda_current_sp() - Get current guest stack pointer.
 * @cpu: Cpu state.
 * 
 * Return: Returns guest pointer.
 */
target_ulong panda_current_sp(const CPUState *cpu) {
    CPUArchState *env = cpu_env((CPUState *)cpu);
#if defined(TARGET_I386)
    // valid on x86 and x86_64
    return env->regs[R_ESP];
#elif defined(TARGET_ARM)
    if (env->aarch64) {
        // X31 on AARCH64.
        return env->xregs[31];
    } else {
        // R13 on ARM.
        return env->regs[13];
    }
#elif defined(TARGET_PPC)
    // R1 on PPC.
    return env->gpr[1];
#elif defined(TARGET_MIPS)
    return env->active_tc.gpr[MIPS_SP];
#else
#error "panda_current_sp() not implemented for target architecture."
    return 0;
#endif
}


/* (not kernel-doc)
 * panda_get_retval() - Get return value for function.
 * @cpu: Cpu state.
 *
 * This function provides a platform-independent abstraction for
 * retrieving a call return value. It still has to be used in the
 * proper context to retrieve a meaningful value, such as just after a
 * RET instruction under x86.
 *
 * Return: Guest ulong value.
 */
target_ulong panda_get_retval(const CPUState *cpu) {
    CPUArchState *env = cpu_env((CPUState *)cpu);
#if defined(TARGET_I386)
    // EAX for x86.
    return env->regs[R_EAX];
#elif defined(TARGET_ARM)
    // R0 on ARM.
    return env->regs[0];
#elif defined(TARGET_PPC)
    // R3 on PPC.
    return env->gpr[3];
#elif defined(TARGET_MIPS)
    // MIPS has 2 return registers v0 and v1. Here we choose v0.
    return env->active_tc.gpr[MIPS_V0];
#else
#error "panda_get_retval() not implemented for target architecture."
    return 0;
#endif
}