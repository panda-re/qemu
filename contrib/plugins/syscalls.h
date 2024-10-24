#ifndef SYSCALLS_H
#define SYSCALLS_H

#define SYSCALLS_MAX_ARGS 6

 typedef struct {
    uint64_t pc;
    uint64_t callno;
    uint64_t args[6];
} SyscallDetails;

// Name of registers used by the syscall ABI for a given arch
typedef struct {
    const char* callno;
    const char* arg0;
    const char* arg1;
    const char* arg2;
    const char* arg3;
    const char* arg4;
    const char* arg5;
    const char* ret;
} syscall_regs_t;

// qemu opaque handles for each register
typedef struct reg_handles {
  struct qemu_plugin_register *callno;
  struct qemu_plugin_register *arg0;
  struct qemu_plugin_register *arg1;
  struct qemu_plugin_register *arg2;
  struct qemu_plugin_register *arg3;
  struct qemu_plugin_register *arg4;
  struct qemu_plugin_register *arg5;
  struct qemu_plugin_register *ret;
} reg_handles_t;

/*
 * Other, non-public functions
 */
typedef bool (*is_syscall_t)(char* buf);
typedef uint64_t (*get_callno_t)(void);

#endif
