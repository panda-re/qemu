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

#define MAX_STRINGS 100
#define MAX_CALLERS 128
#define MAX_STRLEN  1024

static bool verbose;

typedef struct {
    int32_t val[MAX_STRINGS];
} match_strings;

typedef struct {
    uint32_t val[MAX_STRINGS];
} string_pos;

GHashTable *matches = NULL;

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

static void mem_callback(uint64_t pc, uint64_t addr, size_t size, bool is_write,
                         GHashTable *text_tracker)
{
    // Buffer for checking full string presence in memory
    static GByteArray *big_buf = NULL;
    // Buffer for current memory access
    GByteArray *buf = g_byte_array_sized_new(size);

    // Initialize big_buf if it hasn't been created yet
    if (big_buf == NULL) {
        big_buf = g_byte_array_new();
        g_byte_array_set_size(big_buf, MAX_STRLEN);
    }

    // Get or create the string position tracker for this PC
    string_pos *sp = g_hash_table_lookup(text_tracker, GUINT_TO_POINTER(pc));
    if (sp == NULL) {
        sp = g_new0(string_pos, 1);
        g_hash_table_insert(text_tracker, GUINT_TO_POINTER(pc), sp);
    }

    // Read the memory at the current address
    g_byte_array_set_size(buf, size);
    qemu_plugin_read_memory_vaddr(addr, buf, size);

    // Process each byte in the current memory access
    for (size_t i = 0; i < size; i++) {
        guint8 val = buf->data[i];

        // Check each string we're searching for
        for (size_t str_idx = 0; str_idx < num_strings && str_idx < MAX_STRINGS; str_idx++) {
            // If the current byte matches the next expected byte in the string
            if (tofind[str_idx][sp->val[str_idx]] == val) {
                sp->val[str_idx]++;  // Move to the next character in the string
            } else {
                sp->val[str_idx] = 0;  // Reset if there's a mismatch
            }

            // If we've matched an entire string
            if (sp->val[str_idx] == strlens[str_idx]) {
                // Record this match
                match_strings *match = g_hash_table_lookup(matches, GUINT_TO_POINTER(pc));
                if (match == NULL) {
                    match = g_new0(match_strings, 1);
                    g_hash_table_insert(matches, GUINT_TO_POINTER(pc), match);
                }
                match->val[str_idx]++;

                size_t sl = strlens[str_idx];
                // Calculate the starting address of the matched string
                uint64_t match_addr = (addr + i) - (sl - 1);

                // Check if the full string is available in memory at this moment
                g_byte_array_set_size(big_buf, sl);
                qemu_plugin_read_memory_vaddr(match_addr, big_buf, sl);
                bool full_string_in_memory = (memcmp(big_buf->data, tofind[str_idx], sl) == 0);

                // Call the callback function, passing the address only if the full string is in memory
                qemu_plugin_run_callback(plugin_id, "on_string_found", full_string_in_memory ? &match_addr : NULL, NULL);

                // If verbose mode is on, print details about the match
                if (verbose) {
                    g_autofree char *output = g_strdup_printf("%-6s match of str %03zu at pc=0x%016" PRIx64 " (%s)\n",
                                                              is_write ? "WRITE" : "READ", str_idx, pc,
                                                              full_string_in_memory ? "pointer in memory" : "loop over characters");
                    qemu_plugin_outs(output);
                }

                // Reset the string position for this string index
                sp->val[str_idx] = 0;
            }
        }
    }

    // Clean up the temporary buffer
    g_byte_array_unref(buf);
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
      size_t output_len = snprintf(NULL, 0, "[stringsearch] Adding string '%s'\n\n", arg_str);
      char *output = (char*)g_malloc0(output_len + 1);

      snprintf(output, output_len, "[stringsearch] Adding string '%s'\n\n", arg_str);
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
    //uint64_t pc = qemu_plugin_get_pc();
    uint64_t pc = (uint64_t)udata;

    if (qemu_plugin_mem_is_store(info)) {
        mem_callback(pc, vaddr, sz, /*is_write=*/true, read_text_tracker);
    } else {
        mem_callback(pc, vaddr, sz, /*is_write=*/false, write_text_tracker);
    }
}


static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    struct qemu_plugin_insn *insn;
    size_t n = qemu_plugin_tb_n_insns(tb);

    for (size_t i = 0; i < n; i++) {
        insn = qemu_plugin_tb_get_insn(tb, i);
        uint64_t addr = qemu_plugin_insn_vaddr(insn);

        /* Register callback on memory read or write */
        qemu_plugin_register_vcpu_mem_cb(insn, vcpu_mem,
                                         QEMU_PLUGIN_CB_NO_REGS,
                                         QEMU_PLUGIN_MEM_RW, (void*)addr);
    }
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info, int argc,
                                           char **argv)
{
    plugin_id = id;

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
        } else if (g_strcmp0(tokens[0], "verbose") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &verbose)) {
                fprintf(stderr, "boolean argument parsing failed: %s\n", opt);
                return -1;
            }
        }
    }

    return 0;
}
