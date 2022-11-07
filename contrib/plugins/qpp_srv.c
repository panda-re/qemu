#include <stdio.h>
#include <qemu-plugin.h>
#include <plugin-qpp.h>
#include <gmodule.h>
// #include "qpp_srv.h"

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;
QEMU_PLUGIN_EXPORT const char *qemu_plugin_name = "qpp_srv";

int qpp_srv_do_add(int x);
int qpp_srv_do_sub(int x);

//QPP_CREATE_CB(on_exit);

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
  qemu_plugin_outs(CURRENT_PLUGIN "exit triggered, running all registered"
                  " QPP callbacks\n");
  //QPP_RUN_CB(on_exit, 0, true);
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
    qemu_plugin_outs("qpp_srv loaded\n");
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    //qemu_plugin_create_callback(id, "my_on_exit");
    return 0;
}
