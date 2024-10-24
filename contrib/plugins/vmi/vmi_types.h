#pragma once
#include <gmodule.h>
#include <stdio.h>
#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif
#include <qemu-plugin.h>
#ifdef __cplusplus
}
#endif

// XXX are these right?
typedef uint64_t target_ulong;
typedef uint64_t target_ptr_t; 
typedef uint32_t target_pid_t;

/**
 * Minimal handle for a process. Contains a unique identifier \p asid
 * and a task descriptor pointer \p taskd that can be used to retrieve the full
 * details of the process.
 */
typedef struct vmi_proc_handle_struct {
    target_ptr_t taskd;
    target_ptr_t asid;
} VmiProcHandle;

/**
 * Minimal information about a process thread.
 * Address space and open resources are shared between threads
 * of the same process. This information is stored in VmiProc.
 */
typedef struct vmi_thread_struct {
    target_pid_t pid;
    target_pid_t tid;
} VmiThread;

/**
 * @brief Represents a page in the address space of a process.
 *
 * @note This has not been implemented/used so far.
 */
typedef struct vmi_page_struct {
    target_ptr_t start;
    target_ptr_t len;
} VmiPage;


/**
 * Represents information about a guest OS module (kernel module
 * or shared library).
 */
typedef struct vmi_module_struct {
    target_ptr_t modd;
    target_ptr_t base;
    target_ptr_t size;
    char *file;
    char *name;
} VmiModule;

/**
 * Detailed information for a process.
 */
typedef struct vmi_proc_struct {
    target_ptr_t taskd;
    target_ptr_t asid;
    target_pid_t pid;
    target_pid_t ppid;
    char *name;
    VmiPage *pages;
    uint64_t create_time;
} VmiProc;

/* ******************************************************************
 * Helper functions for freeing/copying vmi structs.
 ******************************************************************* */

/**
 * Frees an VmiProcHandle struct and its contents.
 * To be used for freeing standalone VmiProcHandle structs.
 */
static inline void free_vmiprochandle(VmiProcHandle *h) {
    g_free(h);
}

/**
 * Frees an VmiThread struct and its contents.
 * To be used for freeing standalone VmiThread structs.
 */
static inline void free_vmithread(VmiThread *t) {
    g_free(t);
}

/**
 * Frees an VmiPage struct and its contents.
 * To be used for freeing standalone VmiPage structs.
 */
static inline void free_vmipage(VmiPage *p) {
    g_free(p);
}

/**
 * Frees the contents of an VmiModule struct.
 * Meant to be passed to g_array_set_clear_func.
 */
static inline void free_vmimodule_contents(VmiModule *m) {
    if (m == NULL) return;
    g_free(m->file);
    g_free(m->name);
}

/**
 * Frees an VmiModule struct and its contents.
 * To be used for freeing standalone VmiModule structs.
 */
static inline void free_vmimodule(VmiModule *m) {
    free_vmimodule_contents(m);
    g_free(m);
}

/**
 * Frees the contents of an VmiProc struct.
 * Meant to be passed to g_array_set_clear_func.
 */
static inline void free_vmiproc_contents(VmiProc *p) {
    if (p == NULL) return;
    g_free(p->name);
    g_free(p->pages);
}

/**
 * Frees an VmiProc struct and its contents.
 * To be used for freeing standalone VmiProc structs.
 */
static inline void free_vmiproc(VmiProc *p) {
    free_vmiproc_contents(p);
    g_free(p);
}

/**
 * Copies an VmiProcHandle struct.
 * Returns a pointer to the destination location.
 */
static inline VmiProcHandle *copy_vmiprochandle(VmiProcHandle *from, VmiProcHandle *to) {
    if (from == NULL) return NULL;
    if (to == NULL) {
        to = (VmiProcHandle *)g_malloc0(sizeof(VmiProc));
    }
    memcpy(to, from, sizeof(VmiProc));
    return to;
}

/**
 * Copies an VmiProc struct.
 * Returns a pointer to the destination location.
 */
static inline VmiProc *copy_vmiproc(VmiProc *from, VmiProc *to) {
    if (from == NULL) return NULL;
    if (to == NULL) {
        to = (VmiProc *)g_malloc0(sizeof(VmiProc));
    } else {
        free_vmiproc_contents(to);
    }
    memcpy(to, from, sizeof(VmiProc));
    to->name = g_strdup(from->name);
    to->pages = NULL;  // VmiPage - TODO
    return to;
}

/**
 * Copies an VmiModule struct.
 * Returns a pointer to the destination location.
 */
static inline VmiModule *copy_vmimod(VmiModule *from, VmiModule *to) {
    if (from == NULL) return NULL;
    if (to == NULL) {
        to = (VmiModule *)g_malloc0(sizeof(VmiModule));
    } else {
        free_vmimodule_contents(to);
    }
    memcpy(to, from, sizeof(VmiModule));
    to->name = g_strdup(from->name);
    to->file = g_strdup(from->file);
    return to;
}
