#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <qemu-plugin.h>
#include <plugin-qpp.h>
#include "syscalls.h"

// XXX: This plugin requires qemu to be configured with --enable-capstone
// Can we check that in here and error if not?

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;
QEMU_PLUGIN_EXPORT const char *qemu_plugin_name = "syscalls";
qemu_plugin_id_t plugin_id = {0};

bool big_endian = false; // XXX TODO

is_syscall_t is_syscall_fn = NULL;
syscall_regs_t syscall_regs = {0};
reg_handles_t registers = {0};
size_t register_size = 0;

bool is_syscall_i386(char* disasm) {
  return strncmp(disasm, "sysenter", 8) == 0 ||
         strncmp(disasm, "int 0x80", 8) == 0 ||
         strncmp(disasm, "syscall", 7) == 0;
         // XXX need to test if int80 ever happens?
}

bool is_syscall_insn(char* disasm) {
  // x86_64 and mips both use the syscall instruction
  return strncmp(disasm, "syscall", 7) == 0;
}

bool is_syscall_arm(char* disasm) {
  // This is for EABI
  // TODO: OABI encodes the callno in the instruction - we don't have a way to get that
  return strncmp(disasm, "svc #0", 6) == 0;
}

bool is_syscall_other(char* disasm) {
  return false;
}

typedef struct {
  const char *qemu_target;
  size_t register_size;
  is_syscall_t is_syscall_fn;
  syscall_regs_t syscall_regs_arr;
} SyscallDetectorSelector;

static SyscallDetectorSelector syscall_selectors[] = {
  { "i386",   32, is_syscall_i386,   {"eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "eax"}},
  { "x86_64", 64, is_syscall_insn,   {"rax", "rdi", "rsi", "rdx", "r10", "r8", "r9", "rax"}},
  { "arm",    32, is_syscall_arm,    {"r7",  "r0",  "r1",  "r2",  "r3",  "r4", "r5", "r0"}},
  { "mips",   32, is_syscall_insn,   {"v0",  "a0",  "a1",  "a2",  "a3",  "a4", "a5", "v0"}},
  { NULL,     32, is_syscall_other,  {NULL, NULL,  NULL,  NULL,  NULL,  NULL, NULL}},
};

uint64_t read_register(struct qemu_plugin_register *reg) {
    uint64_t val = {0};
    GByteArray *buf = g_byte_array_new();
    int sz = qemu_plugin_read_register(reg, buf);
    // TODO: Big endian support - bytes will need to be reversed
    memcpy(&val, buf->data, MIN(sz, register_size));
    g_byte_array_free(buf, TRUE);
    return val;
}

static void on_syscall(unsigned int cpu_index, void *udata) {
    SyscallDetails details = {
      .pc = (uint64_t)udata,
      .callno = read_register(registers.callno),
      .args = {
        read_register(registers.arg0),
        read_register(registers.arg1),
        read_register(registers.arg2),
        read_register(registers.arg3),
        read_register(registers.arg4),
        read_register(registers.arg5),
      }
    };
    qemu_plugin_run_callback(plugin_id, "on_all_sys_enter", &details, NULL);
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb) {
  // Grab last instruction in the block and check if it's a syscall.
  // In PANDA we do this with raw bytes, but here we have capstone if built with
  // --enable-capstone so we'll use that.

  // XXX: with block chaining, will syscall always be the last instruction?

  size_t n = qemu_plugin_tb_n_insns(tb);
  struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, n-1);
  char* insn_disas = qemu_plugin_insn_disas(insn);
  uint64_t insn_vaddr = qemu_plugin_insn_vaddr(insn);

  if (is_syscall_fn(insn_disas)) {
    // Register our callback to run before the instruction
    qemu_plugin_register_vcpu_insn_exec_cb(insn, on_syscall,
                    QEMU_PLUGIN_CB_R_REGS, (void*)insn_vaddr);
  }
  g_free(insn_disas);
}

static void on_vcpu_init(qemu_plugin_id_t id, unsigned int vcpu_index) {
    // Setup registers according to the current architecture.
    g_autoptr(GArray) reg_list = qemu_plugin_get_registers();

    for (int r = 0; r < reg_list->len; r++) {
        qemu_plugin_reg_descriptor *rd = &g_array_index(reg_list,qemu_plugin_reg_descriptor, r);

        // Check if the register is one of the syscall registers. Ew.
        if (syscall_regs.callno && g_str_equal(rd->name, syscall_regs.callno)) {
            registers.callno = rd->handle;
        }
        if (syscall_regs.arg0 && g_str_equal(rd->name, syscall_regs.arg0)) {
            registers.arg0 = rd->handle;
        }
        if (syscall_regs.arg1 && g_str_equal(rd->name, syscall_regs.arg1)) {
            registers.arg1 = rd->handle;
        }
        if (syscall_regs.arg2 && g_str_equal(rd->name, syscall_regs.arg2)) {
            registers.arg2 = rd->handle;
        }
        if (syscall_regs.arg3 && g_str_equal(rd->name, syscall_regs.arg3)) {
            registers.arg3 = rd->handle;
        }
        if (syscall_regs.arg4 && g_str_equal(rd->name, syscall_regs.arg4)) {
            registers.arg4 = rd->handle;
        }
        if (syscall_regs.arg5 && g_str_equal(rd->name, syscall_regs.arg5)) {
            registers.arg5 = rd->handle;
        }
        if (syscall_regs.ret && g_str_equal(rd->name, syscall_regs.ret)) {
            registers.ret = rd->handle;
        }
    }
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                   const qemu_info_t *info, int argc, char **argv) {

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    //big_endian = qemu_plugin_mem_is_big_endian(info); // TODO
    qemu_plugin_create_callback(id, "on_all_sys_enter");
    plugin_id = id;

    // Configure the architecture-specific syscall detection details
    for (int i = 0; i < ARRAY_SIZE(syscall_selectors); i++) {
        SyscallDetectorSelector *entry = &syscall_selectors[i];
        if (!entry->qemu_target ||
            strcmp(entry->qemu_target, info->target_name) == 0) {
            is_syscall_fn = entry->is_syscall_fn;
            register_size = entry->register_size;
            syscall_regs = entry->syscall_regs_arr;
            break;
        }
    }

    qemu_plugin_register_vcpu_init_cb(id, on_vcpu_init);
    return 0;
}
