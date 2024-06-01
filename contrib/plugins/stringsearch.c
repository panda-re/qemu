#include <glib.h>
#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <qemu-plugin.h>
#include <plugin-qpp.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;
QEMU_PLUGIN_EXPORT const char *qemu_plugin_name = "stringsearch";

#include "stringsearch.h"

qemu_plugin_id_t plugin_id;

bool verbose = true;

#define MAX_STRINGS 100
#define MAX_CALLERS 128
#define MAX_STRLEN  1024

typedef struct {
    int32_t val[MAX_STRINGS];
} match_strings;

typedef struct {
    uint32_t val[MAX_STRINGS];
} string_pos;

GHashTable *matches = NULL;

struct qemu_plugin_register *pc_handle = NULL;
GByteArray *pc_array = NULL;

/* table of recent reads that contained a substring of a match */
GHashTable *read_text_tracker = NULL;

/* table of recent writes that contained a substring of a match */
GHashTable *write_text_tracker = NULL;

/* list of strings to search for */
char tofind[MAX_STRINGS][MAX_STRLEN];

/* how long each string in tofind is */
uint32_t strlens[MAX_STRINGS];

/* number of strings in tofind */
size_t num_strings = 0;

static guint u64_hash(gconstpointer key)
{
    uint64_t k = (uint64_t)key;

    return ((guint)k) ^ ((guint)(k >> 32));
}

static int u64_equality(gconstpointer left, gconstpointer right)
{
    uint64_t l = (uint64_t)left;
    uint64_t r = (uint64_t)right;

    return l == r;
}

uint64_t get_pc() {
    if (pc_handle == NULL) {
        printf("Error: PC handle is NULL\n");
        return (uint64_t)-1;
    }
    qemu_plugin_read_register(pc_handle, pc_array);

    // Convert the byte array data to uint64_t
    uint64_t int_pc;
    memcpy(&int_pc, pc_array->data, sizeof(uint64_t));
    return int_pc;
}

#define VERBOSE_MSG "%s Match of str \"%s\" at pc=0x%lx in buffer \"%s\"+%lx\n"
static void mem_callback(uint64_t addr, size_t size, bool is_write) {
    char buf[64] = { 0 };
    if (size > 64) { 
        return;
    }

    // The guest is reading data at/writing data from addr, copy it into buf.
    qemu_plugin_read_guest_virt_mem(addr, buf, size);
    buf[size] = '\0';

    for (size_t str_idx = 0; str_idx < num_strings; str_idx++) {
        char *match = strstr(buf, tofind[str_idx]);
        if (match != NULL) {
            printf("MATCH\n");
            /* Match!! */
            uint64_t pc = get_pc();
            qemu_plugin_run_callback(plugin_id, "on_string_found", &addr, NULL);
            if (verbose) {
                size_t match_offset = (char *)match - buf;
                /* Log the discovery of a match */
                size_t output_len = 1 + snprintf(NULL, 0, VERBOSE_MSG, is_write ? "WRITE" : "READ", tofind[str_idx], pc, buf, match_offset);
                char *output = (char*)g_malloc0(output_len);
                snprintf(output, output_len, VERBOSE_MSG, is_write ? "WRITE" : "READ", tofind[str_idx], pc, buf, match_offset);
                qemu_plugin_outs(output);

                g_free((gpointer)output);
            }
        }
    }
    return;
}


/* Add a string to the list of strings we're searching for. */
QEMU_PLUGIN_EXPORT bool stringsearch_add_string(const char* arg_str)
{
  size_t arg_len = strlen(arg_str);
  if (arg_len <= 0) {
      qemu_plugin_outs("WARNING [stringsearch]: not adding empty string\n");
      return false;
  }

  if (arg_len >= MAX_STRLEN-1) {
      qemu_plugin_outs("WARNING [stringsearch]: string too long\n");
      return false;
  }

  /* If string already present it's okay */
  for (size_t str_idx = 0; str_idx < num_strings; str_idx++) {
      if (strncmp(arg_str, (char*)tofind[str_idx], strlens[str_idx]) == 0) {
        return true;
      }
  }

  if (num_strings >= MAX_STRINGS-1) {
    qemu_plugin_outs("Warning [stringsearch]: out of string slots\n");
    return false;
  }

  memcpy(tofind[num_strings], arg_str, arg_len);
  strlens[num_strings] = arg_len;
  num_strings++;

  if (verbose) {
      size_t output_len = snprintf(NULL, 0, "[stringsearch] Adding string %s\n\n", arg_str);
      char *output = (char*)g_malloc0(output_len + 1);

      snprintf(output, output_len, "[stringsearch] Adding string %s\n\n", arg_str);
      qemu_plugin_outs(output);

      g_free((gpointer)output);
  }

  return true;
}

/* Remove the first match from our list */
QEMU_PLUGIN_EXPORT bool stringsearch_remove_string(const char* arg_str)
{
    for (size_t str_idx = 0; str_idx < num_strings; str_idx++) {
        if (strncmp(arg_str, (char*)tofind[str_idx], strlens[str_idx]) == 0) {
            memmove( &tofind[str_idx],  &(tofind[str_idx+1]), (sizeof(uint8_t ) * (num_strings - str_idx)));
            memmove(&strlens[str_idx], &(strlens[str_idx+1]), (sizeof(uint32_t) * (num_strings - str_idx)));
            num_strings--;

            return true;
        }
    }

    return false;
}

/* Remove all strings from search list */
QEMU_PLUGIN_EXPORT void stringsearch_reset_strings(void)
{
    num_strings = 0;
}

static void vcpu_mem(unsigned int cpu_index, qemu_plugin_meminfo_t info,
                     uint64_t vaddr, void *udata)
{
    size_t sz = pow(2, qemu_plugin_mem_size_shift(info));
    mem_callback(vaddr, sz, qemu_plugin_mem_is_store(info));
}


static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    struct qemu_plugin_insn *insn;
    size_t n = qemu_plugin_tb_n_insns(tb);

    for (size_t i = 0; i < n; i++) {
        insn = qemu_plugin_tb_get_insn(tb, i);

        /* Register callback on memory read or write */
        qemu_plugin_register_vcpu_mem_cb(insn, vcpu_mem,
                                         QEMU_PLUGIN_CB_NO_REGS,
                                         QEMU_PLUGIN_MEM_RW, NULL);        
    }
}

static void vcpu_init(qemu_plugin_id_t id, unsigned int vcpu_index) {
    // Find PC register
    g_autoptr(GPtrArray) registers = g_ptr_array_new();
    g_autoptr(GArray) reg_list = qemu_plugin_get_registers();

    /* Find PC register?? */
    for (int r = 0; r < reg_list->len; r++) {
        qemu_plugin_reg_descriptor *rd = &g_array_index(reg_list, qemu_plugin_reg_descriptor, r);

        // TODO: support other arches - can we get these from the API somewhere?
        if (g_str_equal(rd->name, "rip")) {
            pc_handle = rd->handle;
        }
    }
    if (pc_handle == NULL) {
        printf("Fatal: could not find program counter in CPU\n");
        exit(0); // Should we unload ourself instead of exiting?
    }

    // Allocate the pc array
    pc_array = g_byte_array_new();
    g_byte_array_set_size(pc_array, sizeof(uint64_t)); // Allocate enough space for a 64-bit value

}


QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info, int argc,
                                           char **argv)
{
    plugin_id = id;

    qemu_plugin_register_vcpu_init_cb(id, vcpu_init);
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_create_callback(id, "on_string_found");

    matches = g_hash_table_new_full(u64_hash, u64_equality, NULL, g_free);
    read_text_tracker = g_hash_table_new_full(u64_hash, u64_equality, NULL, g_free);
    write_text_tracker = g_hash_table_new_full(u64_hash, u64_equality, NULL, g_free);
    
    /* parse arguments for passing strings to look for */
    for (int i = 0; i < argc; i++) {
        char *opt = argv[i];
        g_autofree char **tokens = g_strsplit(opt, "=", 2);

        if (g_strcmp0(tokens[0], "str") == 0) {
            stringsearch_add_string(tokens[1]);
        }
    }

    return 0;
}

