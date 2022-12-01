#ifndef PLUGIN_QPP_H
#define PLUGIN_QPP_H

/*
 * Facilities for "Plugin to plugin" (QPP) interactions between tcg plugins.
 * These allow both direct function calls between loaded plugins as well as
 * an inter-plugin callback-system. For more details see docs/devel/plugin.rst.
 */

#include <dlfcn.h>
#include <gmodule.h>
#include <assert.h>
#ifdef __cplusplus
extern "C"
#endif
GModule * qemu_plugin_name_to_handle(const char *);

/*
 ****************************************************************
 * These are internal helper macros, the public interface is after
 * this section
 ****************************************************************
 */
#define PLUGIN_CONCAT(x, y) _PLUGIN_CONCAT(x, y)
#define _PLUGIN_CONCAT(x, y) x##y
#define PLUGIN_STR(s) _PLUGIN_STR(s)
#define _PLUGIN_STR(s) #s
#define QPP_NAME(plugin, fn) PLUGIN_CONCAT(plugin, PLUGIN_CONCAT(_, fn))


#define QPP_MAX_CB 256

#define _QPP_SETUP_NAME(fn) PLUGIN_CONCAT(_qpp_setup_, \
                                    fn)

/*
 **************************************************************************
 * The following macros are to be used in plugins that wish to expose
 * functions or callback that can be called or registered by other plugins.
 **************************************************************************
 */

/*
 * QPP_CREATE_CB(name) defines vars and functions based off the callback name.
 * THis macro should be called before a plugin uses or defines a callback
 * function. The variables and functions created are:
 * qpp_[callback_name]_cb is an array of function pointers storing the
 * registered
 * callbacks. qpp_[callback_name]_num_cb is the number of registered callbacks.
 * qpp_add_cb_[name] adds a new callback into the _cb array and increments
 * _num_cb.
 * qpp_remove_cb_[name] finds a registered callback, deletes it, decrements
 * _num_cb and shifts the _cb array appropriately.
 *
 * Note that we do not support any customization of the ordering of this list,
 * when multiple callbacks are registered for the same event, consumers should
 * not make asumptions about the order in which they will be called.
 *
 * Also note that qpp_[cb_name]_num_cb will be initialized to 0 per the C spec.
 */

#ifdef __cplusplus
#define QPP_CREATE_CB(cb_name)                              \
extern "C" { \
void qpp_add_cb_##cb_name(cb_name##_t fptr);                \
int qpp_remove_cb_##cb_name(cb_name##_t fptr);             \
cb_name##_t * qpp_##cb_name##_cb[QPP_MAX_CB];               \
int qpp_##cb_name##_num_cb;                                 \
} \
void qpp_add_cb_##cb_name(cb_name##_t fptr)                 \
{                                                           \
  assert(qpp_##cb_name##_num_cb < QPP_MAX_CB);              \
  qpp_##cb_name##_cb[qpp_##cb_name##_num_cb] = fptr;        \
  qpp_##cb_name##_num_cb += 1;                              \
}                                                           \
                                                            \
int qpp_remove_cb_##cb_name(cb_name##_t fptr)              \
{                                                           \
  fprintf(stderr, "Trying to remove something\n"); \
  int i = 0;                                                \
  bool found = 0;                                       \
  for (; i < MIN(QPP_MAX_CB, qpp_##cb_name##_num_cb); i++) {\
    if (!found && fptr == qpp_##cb_name##_cb[i]) {          \
        found = 1;                                       \
        qpp_##cb_name##_num_cb--;                           \
    }                                                       \
    if (found && i < QPP_MAX_CB - 2) {                      \
        qpp_##cb_name##_cb[i] = qpp_##cb_name##_cb[i + 1];  \
    }                                                       \
  }                                                         \
  return found;                                             \
}
#else
#define QPP_CREATE_CB(cb_name)                              \
void qpp_add_cb_##cb_name(cb_name##_t fptr);                \
int qpp_remove_cb_##cb_name(cb_name##_t fptr);              \
cb_name##_t * qpp_##cb_name##_cb[QPP_MAX_CB];               \
int qpp_##cb_name##_num_cb;                                 \
void qpp_add_cb_##cb_name(cb_name##_t fptr)                 \
{                                                           \
  assert(qpp_##cb_name##_num_cb < QPP_MAX_CB);              \
  qpp_##cb_name##_cb[qpp_##cb_name##_num_cb] = fptr;        \
  qpp_##cb_name##_num_cb += 1;                              \
}                                                           \
                                                            \
int qpp_remove_cb_##cb_name(cb_name##_t fptr)               \
{                                                           \
  int i = 0;                                                \
  bool found = 0;                                           \
  for (; i < MIN(QPP_MAX_CB, qpp_##cb_name##_num_cb); i++) {\
    if (!found && fptr == qpp_##cb_name##_cb[i]) {          \
        found = 1;                                          \
        qpp_##cb_name##_num_cb--;                           \
    }                                                       \
    if (found && i < QPP_MAX_CB - 2) {                      \
        qpp_##cb_name##_cb[i] = qpp_##cb_name##_cb[i + 1];  \
    }                                                       \
  }                                                         \
  return found;                                             \
}
#endif


/*
 * Two forms of QPP_RUN_CB(name, args...) are provided to use in the plugin
 * that created a callback in order to run all registerd callback functions.
 * In this form, the return values of the run callbacks are ignored.
 */
#define QPP_RUN_CB(cb_name, ...) {                              \
  int cb_idx;                                                   \
  for (cb_idx = 0; cb_idx < qpp_##cb_name##_num_cb; cb_idx++) { \
    if (qpp_##cb_name##_cb[cb_idx] != NULL) {                   \
      qpp_##cb_name##_cb[cb_idx](__VA_ARGS__);                  \
    }                                                           \
  }                                                             \
}

/*
 * The second form is IF_QPP_RUN_BOOL_CB. In this form, the return values of
 * callbacks will be OR'd together and, if any return true, the callback will
 * evaluate the subsequent if-statement body.
 * Usage: IF_QPP_RUN_BOOL_CB(...) { printf("True"); }
 */
#define IF_QPP_RUN_BOOL_CB(cb_name, ...)                          \
  bool __ret = false;                                             \
  {                                                               \
    int cb_idx;                                                   \
    for (cb_idx = 0; cb_idx < qpp_##cb_name##_num_cb; cb_idx++) { \
      if (qpp_##cb_name##_cb[cb_idx] != NULL) {                   \
        __ret |= qpp_##cb_name##_cb[cb_idx](__VA_ARGS__);         \
      }                                                           \
    }                                                             \
  }; if (__ret)

/*
 * A header file that defines an exported function should use
 * the QPP_FUN_PROTOTYPE macro to create the necessary types.
 *
 * The generated function named after the output of QPP_SETUP_NAME should
 * dynamically resolve a target function in another plugin or raise a fatal
 * error on failure. In particular, it must handle the following two cases:
 * 1) When the header is loaded by the plugin that defines the function.
 *    In this case, we do not need to find the symbol externally.
 *    qemu_plugin_name_to_handle will return NULL, we see that the
 *    target plugin matches CURRENT_PLUGIN and do nothing.
 * 2) When the header is loaded by another plugin. In this case
 *    we get the function pointer from qemu_plugin_import_function
 *    and correctly cast and assign the function pointer
 */

/* this is the new one, yay */
#define QPP_FUN_PROTOTYPE(plugin_name, fn_ret, fn, args)                     \
  fn_ret fn(args); \
  typedef fn_ret(*PLUGIN_CONCAT(fn, _t))(args);                               \
  fn##_t fn##_qpp;                                           \
  void _QPP_SETUP_NAME(fn) (void);                               \
                                                                              \
  void __attribute__ ((constructor)) _QPP_SETUP_NAME(fn) (void) { \
    if (strcmp(qemu_plugin_name, #plugin_name) != 0) {        \
      fn##_qpp = qemu_plugin_import_function(PLUGIN_STR(plugin_name), PLUGIN_STR(fn)); \
    } \
  }

/*
 * A header file that defines a callback function should use
 * the QPP_CB_PROTOTYPE macro to create the necessary types.
 */
#define QPP_CB_PROTOTYPE(fn_ret, name, ...) \
  typedef fn_ret(PLUGIN_CONCAT(name, _t))(__VA_ARGS__);

/*
 *****************************************************************************
 * The following code is used in "plugin B", i.e., the plugin that wants to
 * register a function to run in response to a callback triggered by plugin A.
 *****************************************************************************
 */

/*
 * When a plugin wishes to register a function `cb_func` with a callback
 * `cb_name` provided `other_plugin`, it must use the QPP_REG_CB.
 */
#define QPP_REG_CB(other_plugin, cb_name, cb_func)      \
{                                                       \
  dlerror();                                            \
  GModule *h = qemu_plugin_name_to_handle(other_plugin);   \
  if (!h) {                                             \
    fprintf(stderr, "In trying to add plugin callback, "\
                    "couldn't load %s plugin\n",        \
                    other_plugin);                      \
    assert(h);                                          \
  }                                                     \
  void (*add_cb)(cb_name##_t fptr);                     \
  if (g_module_symbol(h, "qpp_add_cb_" #cb_name,        \
                      (gpointer *) &add_cb)) {          \
    add_cb(cb_func);                                    \
  } else {                                              \
    fprintf(stderr, "Could not find symbol " #cb_name   \
            " in " #other_plugin "\n");                 \
  }                                                     \
}

/*
 * If a plugin wishes to disable a previously-registered `cb_func` it should
 * use the QPP_REMOVE_CB macro.
 */
#define QPP_REMOVE_CB(other_plugin, cb_name, cb_func)           \
{                                                               \
  GModule *h = qemu_plugin_name_to_handle(other_plugin);        \
  if (!h) {                                                     \
    fprintf(stderr, "In trying to remove plugin callback, "     \
                    "couldn't find %s plugin\n",                \
                    other_plugin);                              \
    assert(h);                                                  \
  }                                                             \
  void (*rm_cb)(cb_name##_t fptr);                              \
  if (g_module_symbol(h, "qpp_remove_cb_" #cb_name,             \
                      (gpointer *) &rm_cb)) {                   \
  rm_cb(cb_func);                                               \
  } else {                                                      \
    fprintf(stderr, "Could not find internal qpp_remove_cb "    \
            " function in " #other_plugin "\n");                \
  }                                                             \
}
#endif /* PLUGIN_QPP_H */
