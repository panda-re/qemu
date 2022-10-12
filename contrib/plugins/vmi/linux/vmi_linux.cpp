#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <glib.h>
#include <plugin-qpp.h>

// Other plugins
extern "C" {
#include <qemu-plugin.h>
QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;
QEMU_PLUGIN_EXPORT const char *qemu_plugin_name = "vmi_linux";
}

#include "vmi_linux.h"
#include "default_profile.h"
#include "../../syscalls.h"
#include "../vmi.h"

#define unlikely(x)   __builtin_expect(!!(x), 0)

#define TARGET_PID_FMT "%u"


void on_first_syscall(gpointer evdata, gpointer udata);

// Using these
void on_get_process(gpointer evdata, gpointer udata);
void on_get_current_process_handle(gpointer evdata, gpointer udata);
void on_get_current_process(gpointer evdata, gpointer udata);

// Not yet using these
//void on_get_process_handles(GArray **out);
//void on_get_processes(GArray **out);
//void on_get_mappings(VmiProc *p, GArray **out);
//void on_get_current_thread(VmiThread *t);

void init_per_cpu_offsets();
struct kernelinfo ki;
struct KernelProfile const *kernel_profile;

extern const char *qemu_file;
bool vmi_initialized;
static bool first_vmi_check = true;


/**
 * Resolves a file struct and returns its full pathname.
 */
static char *get_file_name(target_ptr_t file_struct) {
    char *name = NULL;
    target_ptr_t file_dentry, file_mnt;

    // Read addresses for dentry, vfsmnt structs.
    file_dentry = get_file_dentry(file_struct);
    file_mnt = get_file_mnt(file_struct);

    if (unlikely(file_dentry == (target_ptr_t)NULL || file_mnt == (target_ptr_t)NULL)) {
        fprintf(stderr, "failure resolving file struct %lx/%lx", file_dentry, file_mnt);
        return NULL;
    }

    char *s1, *s2;
    s1 = read_vfsmount_name(file_mnt);
    s2 = read_dentry_name(file_dentry);
    name = g_strconcat(s1, s2, NULL);
    g_free(s1);
    g_free(s2);

    return name;
}

static uint64_t get_file_pvmition(target_ptr_t file_struct) {
    return get_file_pos(file_struct);
}

static target_ptr_t get_file_struct_ptr( target_ptr_t task_struct, int fd) {
    target_ptr_t files = get_files(task_struct);
    target_ptr_t fds = kernel_profile->get_files_fds(files);
    target_ptr_t fd_file_ptr, fd_file;

    // fds is a flat array with struct file pointers.
    // Calculate the address of the nth pointer and read it.
    fd_file_ptr = fds + fd*sizeof(target_ptr_t);
    if (-1 == panda_virtual_memory_rw(fd_file_ptr, (uint8_t *)&fd_file, sizeof(target_ptr_t), 0)) {
        return (target_ptr_t)NULL;
    }
    fixupendian(fd_file);
    if (fd_file == (target_ptr_t)NULL) {
        return (target_ptr_t)NULL;
    }
    return fd_file;
}

/**
 * Resolves a file struct and returns its full pathname.
 */
static char *get_fd_name( target_ptr_t task_struct, int fd) {
    target_ptr_t fd_file = get_file_struct_ptr(task_struct, fd);
    if (fd_file == (target_ptr_t)NULL) return NULL;
    return get_file_name(fd_file);
}

/**
 * Retrieves the current offset of a file descriptor.
 */
static uint64_t get_fd_pos(target_ptr_t task_struct, int fd) {
    target_ptr_t fd_file = get_file_struct_ptr(task_struct, fd);
    if (fd_file == (target_ptr_t)NULL) return ((uint64_t) INVALID_FILE_POS);
    return get_file_pvmition(fd_file);
}

/**
 * Fills an VmiProcHandle struct.
 */
static void fill_vmiprochandle(VmiProcHandle *h,
    target_ptr_t task_addr) {
    struct_get_ret_t err;

    // h->asid = taskd->mm->pgd (some kernel tasks are expected to return error)
    err = struct_get(&h->asid, task_addr, {ki.task.mm_offset, ki.mm.pgd_offset});

    if (err == struct_get_ret_t::SUCCESS) {
        // Convert asid to physical to be able to compare it with the pgd register.
        h->asid = qemu_plugin_virt_to_phys(h->asid);
        h->taskd = kernel_profile->get_group_leader(task_addr);
    } else {
        h->asid = (target_ulong)NULL;
        h->taskd = (target_ptr_t)NULL;
    }
}

/**
 * Fills an VmiProc struct. Any existing contents are overwritten.
 */
void fill_vmiproc(VmiProc *p, target_ptr_t task_addr) {
    struct_get_ret_t err;
    memset(p, 0, sizeof(VmiProc));

    // p->asid = taskd->mm->pgd (some kernel tasks are expected to return error)
    err = struct_get(&p->asid, task_addr, {ki.task.mm_offset, ki.mm.pgd_offset});
    assert(err == struct_get_ret_t::SUCCESS);

    // p->ppid = taskd->real_parent->pid
    err = struct_get( &p->ppid, task_addr,
                     {ki.task.real_parent_offset, ki.task.tgid_offset});
    assert(err == struct_get_ret_t::SUCCESS);

    // Convert asid to physical to be able to compare it with the pgd register.
    p->asid = p->asid ? qemu_plugin_virt_to_phys(p->asid) : (target_ulong) NULL;
    p->taskd = kernel_profile->get_group_leader(task_addr);

    p->name = get_name(task_addr, p->name);
    p->pid = get_tgid(task_addr);
    //p->ppid = get_real_parent_pid(task_addr);
    p->pages = NULL;  // VmiPage - TODO

    //if kernel version is < 3.17
    if(ki.version.a < 3 || (ki.version.a == 3 && ki.version.b < 17)) {
        uint64_t tmp = get_start_time(task_addr);

        //if there's an endianness mismatch TODO PORT TO Q7 XXX
        #if defined(TARGET_WORDS_BIGENDIAN) != defined(HOST_WORDS_BIGENDIAN)
            //convert the most significant half into nanoseconds, then add the rest of the nanoseconds
            p->create_time = (((tmp & 0xFFFFFFFF00000000) >> 32) * 1000000000) + (tmp & 0x00000000FFFFFFFF);
        #else
            //convert the least significant half into nanoseconds, then add the rest of the nanoseconds
            p->create_time = ((tmp & 0x00000000FFFFFFFF) * 1000000000) + ((tmp & 0xFFFFFFFF00000000) >> 32);
        #endif
       
    } else {
        p->create_time = get_start_time(task_addr);
    }
}

/**
 * Fills an VmiModule struct.
 */
static void fill_vmimodule(VmiModule *m, target_ptr_t vma_addr) {
    target_ulong vma_start, vma_end;
    target_ptr_t vma_vm_file;
    target_ptr_t vma_dentry;
    target_ptr_t mm_addr, start_brk, brk, start_stack;

    vma_start = get_vma_start(vma_addr);
    vma_end = get_vma_end(vma_addr);
    vma_vm_file = get_vma_vm_file(vma_addr);

    // Fill everything but m->name and m->file.
    m->modd = vma_addr;
    m->base = vma_start;
    m->size = vma_end - vma_start;

    if (vma_vm_file !=
        (target_ptr_t)NULL) {  // Memory area is mapped from a file.
        vma_dentry = get_vma_dentry(vma_addr);
        m->file = read_dentry_name(vma_dentry);
        m->name = g_strrstr(m->file, "/");
        if (m->name != NULL) m->name = g_strdup(m->name + 1);
    } else {  // Other memory areas.
        mm_addr = get_vma_vm_mm(vma_addr);
        start_brk = get_mm_start_brk(mm_addr);
        brk = get_mm_brk(mm_addr);
        start_stack = get_mm_start_stack(mm_addr);

        m->file = NULL;
        if (vma_start <= start_brk && vma_end >= brk) {
            m->name = g_strdup("[heap]");
        } else if (vma_start <= start_stack && vma_end >= start_stack) {
            m->name = g_strdup("[stack]");
        } else {
            m->name = g_strdup("[???]");
        }
    }
}

/**
 * Fills an VmiThread struct. Any existing contents are overwritten.
 */
void fill_vmithread(VmiThread *t,
                           target_ptr_t task_addr) {
    memset(t, 0, sizeof(*t));
    t->tid = get_pid(task_addr);
    t->pid = get_tgid(task_addr);
}

/* ******************************************************************
 Initialization logic
****************************************************************** */
/**
 * When necessary, after the first syscall ensure we can read current task
 */

void on_first_syscall(gpointer evdata, gpointer udata) {
    assert(can_read_current() && "Couldn't find current task struct at first syscall");
    if (!vmi_initialized) {
      qemu_plugin_outs("vmi_linux: initialization complete.");
    }
    vmi_initialized = true;
    qemu_plugin_unreg_callback("syscalls", "on_all_sys_enter", on_first_syscall);
}

/**
 *  Test to see if we can read the current task struct
 */
inline bool can_read_current() {
    target_ptr_t ts = kernel_profile->get_current_task_struct();
    return 0x0 != ts;
}

/**
 * Check if we've successfully initialized vmi for the guest.
 * Returns true if introspection is available.
 *
 * If introspection is unavailable at the first check, this will register a QPP-style
 * callback with syscalls to try reinitializing at the first syscall.
 *
 * If that fails, then we raise an assertion because vmi has really failed.
 */
bool vmi_guest_is_ready(void** ret) {

    if (vmi_initialized) { // If vmi_initialized is set, the guest must be ready
      return true;      // or, if it isn't, the user wants an assertion error
    }

    // If it's the very first time, try reading current, if we can't
    // wait until first sycall and try again
    if (first_vmi_check) {
        first_vmi_check = false;

        init_per_cpu_offsets();

        // Try to load current, if it works, return true
        if (can_read_current()) {
            // Disable on_first_syscall QPP callback because we're all set
            qemu_plugin_unreg_callback("syscalls", "on_all_sys_enter", on_first_syscall);
            qemu_plugin_outs("vmi_linux: initialization complete.\n");
            vmi_initialized = true;
            return true;
        }

        // We can't read the current task right now. This isn't a surprise, e.g.,
        // it could be happening because we're in boot.
        // Wait until on_first_syscall runs, everything should work then
        qemu_plugin_outs("vmi_linux: cannot find current task struct. Deferring vmi initialization until first syscall.\n");

        qemu_plugin_reg_callback("syscalls", "on_all_sys_enter", on_first_syscall);
    }
    // Not yet initialized, just set the caller's result buffer to NULL
    ret = NULL;
    return false;
}

/* ******************************************************************
QPP Callbacks
****************************************************************** */

/**
 * QPP callback to retrieve info about the currently running process.
 */
void on_get_current_process(gpointer evdata, gpointer udata) {
    VmiProc **out = (VmiProc**)evdata;
    if (!vmi_guest_is_ready((void**)out)) return;

    static target_ptr_t last_ts = 0x0;
    static target_ptr_t cached_taskd = 0x0;
    static target_ptr_t cached_asid = 0x0;
    static char *cached_name = (char *)g_malloc0(ki.task.comm_size);
    static target_ptr_t cached_pid = -1;
    static target_ptr_t cached_ppid = -1;
    static void *cached_comm_ptr = NULL;
    static uint64_t cached_start_time = 0;
    // VmiPage - TODO

    VmiProc *p = NULL;
    target_ptr_t ts = kernel_profile->get_current_task_struct();
    if (0x0 != ts) {
        p = (VmiProc *)g_malloc(sizeof(*p));
        if ((ts != last_ts) || (NULL == cached_comm_ptr) ||
            (0 != strncmp((char *)cached_comm_ptr, cached_name,
                          ki.task.comm_size))) {
            last_ts = ts;
            fill_vmiproc(p, ts);

            // update the cache
            cached_taskd = p->taskd;
            cached_asid = p->asid;
            memset(cached_name, 0, ki.task.comm_size);
            strncpy(cached_name, p->name, ki.task.comm_size);
            cached_pid = p->pid;
            cached_ppid = p->ppid;
	    cached_start_time = p->create_time;
            //cached_comm_ptr = qemu_plugin_virt_to_host(
            //    ts + ki.task.comm_offset, ki.task.comm_size);
        } else {
            p->taskd = cached_taskd;
            p->asid = cached_asid;
            p->name = g_strdup(cached_name);
            p->pid = cached_pid;
            p->ppid = cached_ppid;
            p->pages = NULL;
	    p->create_time = cached_start_time;
        }
    }
    *out = p;
}

/**
 * QPP callback to the handle of the currently running process.
 */
void on_get_current_process_handle(gpointer evdata, gpointer udata) {
    VmiProcHandle **out = (VmiProcHandle**)evdata;
    if (!vmi_guest_is_ready((void**)out)) return;

    VmiProcHandle *p = NULL;
    // Very first thing that happens. Woop
    target_ptr_t ts = kernel_profile->get_current_task_struct();
    if (ts) {
        p = (VmiProcHandle *)g_malloc(sizeof(VmiProcHandle));
        fill_vmiprochandle(p, ts);
    }
    *out = p;
}

/**
 * QPP callback to retrieve info about a running process using its
 * handle.
 */
void on_get_process(gpointer evdata, gpointer udata) {
    struct get_process_data *data = (struct get_process_data*)(evdata);
    const VmiProcHandle *h = data->h;
    VmiProc **out = data->p;
    if (!vmi_guest_is_ready((void**)out)) return;

    VmiProc *p = NULL;
    if (h != NULL && h->taskd != (target_ptr_t)NULL) {
        p = (VmiProc *)g_malloc(sizeof(VmiProc));
        fill_vmiproc(p, h->taskd);
    }
    *out = p;
}

/**
 * @brief QPP callback to retrieve VmiModules from the running OS.
 *
 * Current implementation returns all the memory areas mapped by the
 * process and the files they were mapped from. Libraries that have
 * many mappings will appear multiple times.
 *
 * @todo Remove duplicates from results.
 */
void on_get_mappings(VmiProc *p, GArray **out) {
    if (!vmi_guest_is_ready((void**)out)) return;

    VmiModule m;
    target_ptr_t vma_first, vma_current;

    // Read the module info for the process.
    vma_first = vma_current = get_vma_first(p->taskd);
    if (vma_current == (target_ptr_t)NULL) goto error0;

    if (*out == NULL) {
        *out = g_array_sized_new(false, false, sizeof(VmiModule), 128);
        g_array_set_clear_func(*out, (GDestroyNotify)free_vmimodule_contents);
    }

    do {
        memset(&m, 0, sizeof(VmiModule));
        fill_vmimodule(&m, vma_current);
        g_array_append_val(*out, m);
        vma_current = get_vma_next(vma_current);
    } while(vma_current != (target_ptr_t)NULL && vma_current != vma_first);

    return;

error0:
    if(*out != NULL) {
        g_array_free(*out, true);
    }
    *out = NULL;
    return;
}

/**
 * QPP callback to retrieve the process pid from a handle.
 */
void on_get_process_pid(const VmiProcHandle *h, target_pid_t *pid) {
    if (!vmi_guest_is_ready((void**)pid)) return;

    if (h->taskd == 0 || h->taskd == (target_ptr_t)-1) {
        *pid = (target_pid_t)-1;
    } else {
        *pid = get_tgid(h->taskd);
    }
}

/**
 * QPP callback to retrieve the process parent pid from a handle.
 */
void on_get_process_ppid(const VmiProcHandle *h, target_pid_t *ppid) {
    struct_get_ret_t err;
    if (!vmi_guest_is_ready((void**)ppid)) return;

    if (h->taskd == (target_ptr_t)-1) {
        *ppid = (target_pid_t)-1;
    } else {
        // ppid = taskd->real_parent->pid
        err = struct_get(ppid, h->taskd,
                         {ki.task.real_parent_offset, ki.task.pid_offset});
        if (err != struct_get_ret_t::SUCCESS) {
            *ppid = (target_pid_t)-1;
        }
    }
}

/* ******************************************************************
 vmi_linux extra API
****************************************************************** */

char *vmi_linux_fd_to_filename(VmiProc *p, int fd) {
    char *filename = NULL;
    target_ptr_t ts_current;
    //const char *err = NULL;

    if (p == NULL) {
        //err = "Null VmiProc argument";
        goto end;
    }

    ts_current = p->taskd;
    if (ts_current == 0) {
        //err = "can't get task";
        goto end;
    }

    filename = get_fd_name(ts_current, fd);
    if (unlikely(filename == NULL)) {
        //err = "can't get filename";
        goto end;
    }

    filename = g_strchug(filename);
    if (unlikely(g_strcmp0(filename, "") == 0)) {
        //err = "filename is empty";
        g_free(filename);
        filename = NULL;
        goto end;
    }

end:
    return filename;
}


target_ptr_t ext_get_file_dentry(target_ptr_t file_struct) {
	return get_file_dentry(file_struct);
} 

target_ptr_t ext_get_file_struct_ptr(target_ptr_t task_struct, int fd) {
	return get_file_struct_ptr(task_struct, fd);
}


unsigned long long  vmi_linux_fd_to_pos(VmiProc *p, int fd) {
    target_ptr_t ts_current = 0;
    ts_current = p->taskd;
    if (ts_current == 0) return INVALID_FILE_POS;
    return get_fd_pos(ts_current, fd);
}

/* ******************************************************************
 Plugin Initialization/Cleanup
****************************************************************** */
/**
 * Updates any per-cpu offsets we need for introspection.
 * This allows kernel profiles to be independent of boot-time configuration.
 * If ki.task.per_cpu_offsets_addr is set to 0, the values of the per-cpu
 * offsets in the profile will not be updated.
 *
 * Currently the only per-cpu offset we use in vmi_linux is
 * ki.task.per_cpu_offset_0_addr.
 */
void init_per_cpu_offsets() {
    // old kernel - no per-cpu offsets to update
    if (PROFILE_KVER_LE(ki, 2, 4, 254)) {
        return;
    }

    // skip update because there's no per_cpu_offsets_addr
    if (ki.task.per_cpu_offsets_addr == 0) {
        return;
    }

    // skip update because of failure to read from per_cpu_offsets_addr
    target_ptr_t per_cpu_offset_0_addr;
    auto r = struct_get(&per_cpu_offset_0_addr, ki.task.per_cpu_offsets_addr,
        0*(sizeof(target_ptr_t))); // final argument is 0 but like this to avoid compiler warning
    if (r != struct_get_ret_t::SUCCESS) {
        fprintf(stderr, "Unable to update value of ki.task.per_cpu_offset_0_addr.\n");
        assert(false);
        return;
    }

    ki.task.per_cpu_offset_0_addr = per_cpu_offset_0_addr;
}

/**
 * After guest has restored snapshot, reset so we can redo
 * initialization
 */
void restore_after_snapshot(qemu_plugin_id_t id, unsigned int cpu_index) {
    qemu_plugin_outs("vmi_linux: Snapshot loaded. Re-initializing\n");

    // By setting these, we'll redo our init logic which determines
    // if vmi is ready at the first time it's used, otherwise 
    // it runs at the first syscall (and asserts if it fails)
    vmi_initialized = false;
    first_vmi_check = true;
    qemu_plugin_reg_callback("syscalls", "on_all_sys_enter", on_first_syscall);
}


/**
 * Initializes plugin.
 */
extern "C" QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                   const qemu_info_t *info, int argc, char **argv) {

    gchar* kconf_file = NULL;
    gchar* kconf_group = NULL;
    vmi_initialized = false;

    //parse the arguments
    for (int i = 0; i < argc; i++) {
        g_autofree gchar** tokens = g_strsplit(argv[i], "=", 2);

        if (g_strcmp0(tokens[0], "conf") == 0) {
            kconf_file = g_strdup(tokens[1]);
        } else if (g_strcmp0(tokens[0], "group") == 0) {
            kconf_group = g_strdup(tokens[1]);
        }
    }

    if (!kconf_file || !kconf_group) {
        fprintf(stderr, "vmi_linux is missing arguments\n");
        fprintf(stderr, "USAGE: -plugin libvmi_linux.so,conf=kernel_info.conf,group=group_name\n");
        return 1;
    }

    // Load kernel offsets from the configuration file
    if (read_kernelinfo(kconf_file, kconf_group, &ki) != 0) {
        fprintf(stderr, "vmi_linux: Failed to read group %s from %s.\n", kconf_group, kconf_file);
        return 1;
    }

    if (PROFILE_KVER_LE(ki, 2, 4, 254)) {
        //kernel_profile = &KERNEL24X_PROFILE;
        fprintf(stderr, "vmi_linux: Old kernel detected. Not currently supported\n");
        return 1;
    } else {
        kernel_profile = &DEFAULT_PROFILE;
    }
    // After a snapshot is loaded, we'll need to re-initialize
    qemu_plugin_register_vcpu_loadvm_cb(id, restore_after_snapshot);

    qemu_plugin_reg_callback("vmi", "on_get_process", on_get_process);
    qemu_plugin_reg_callback("vmi", "on_get_current_process_handle", on_get_current_process_handle);
    qemu_plugin_reg_callback("vmi", "on_get_current_process", on_get_current_process);


    return 0;
}

/* vim:set tabstop=4 softtabstop=4 expandtab: */
