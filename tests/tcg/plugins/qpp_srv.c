#include <stdio.h>
#include <qemu-plugin.h>
#include <plugin-qpp.h>
#include <gmodule.h>
#include <assert.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;
QEMU_PLUGIN_EXPORT const char *qemu_plugin_name = "qpp_srv";
#include "qpp_srv.h"

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
  qemu_plugin_outs("qpp_srv: exit triggered, running all registered"
                  " QPP callbacks\n");

  // Trigger all callbacks registered with our custom on_exit callback
  // We expect qpp_client to set qpp_client_passed if everything worked

  bool qpp_client_passed = false;
  qemu_plugin_run_callback(id, "my_on_exit", &qpp_client_passed, NULL);

  if (qpp_client_passed) {
    qemu_plugin_outs("PASS\n");
  } else {
    qemu_plugin_outs("FAIL\n");
  }
}

QEMU_PLUGIN_EXPORT int qpp_srv_do_add(int x)
{
  return x + 1;
}

QEMU_PLUGIN_EXPORT int qpp_srv_do_sub(int x)
{
  return x - 1;
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                   const qemu_info_t *info, int argc, char **argv) {
    qemu_plugin_create_callback(id, "my_on_exit");
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
