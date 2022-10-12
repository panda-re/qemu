Plugin: VMI linux
===========

Summary
-------

`vmi_linux` provides Linux introspection information and makes it available through the OSI interface. It does so by knowing about the offsets for various Linux kernel data structures and then providing algorithms that traverse these data structures in the guest virtual machine.

Because the offsets of fields in Linux kernel data structures change frequently (and can even depend on the specific compilation flags used), `vmi_linux` uses a configuration file to specify the offsets of critical data structures. A portion of such a configuration file, which is in the [GLib key-value format](https://developer.gnome.org/glib/stable/glib-Key-value-file-parser.html) (similar to .ini files), is given below:

    [ubuntu:4.4.0-98-generic:32]
    name = 4.4.0-98-generic|#121-Ubuntu SMP Tue Oct 10 14:23:20 UTC 2017|i686
    version.a = 4
    version.b = 4
    version.c = 90
    task.init_addr = 3249445504
    #task.init_addr = 0xC1AE9A80
    #task.per_cpu_offset_0 = 0x34B42000
    task.per_cpu_offset_0 = 884219904
    #task.current_task_addr = 0xC1C852A8
    task.current_task_addr = 3251131048
    task.size = 5824
    task.tasks_offset = 624
    task.pid_offset = 776

    [... omitted ...]

    [debian:4.9.0-6-686-pae:32]
    name = 4.9.0-6-686-pae|#1 SMP Debian 4.9.82-1+deb9u3 (2018-03-02)|i686
    version.a = 4
    version.b = 9
    version.c = 88
    task.init_addr = 3245807232
    #task.init_addr = 0xC1771680
    #task.per_cpu_offset_0 = 0x36127000
    task.per_cpu_offset_0 = 907177984
    #task.current_task_addr = 0xC18C3208
    task.current_task_addr = 3247190536
    task.size = 5888
    task.tasks_offset = 708
    task.pid_offset = 864

    [... omitted ...]

Of course, generating this file by hand would be extremely painful. We refer interested readers to PANDA.re's infrastructure to generate these configurations: https://github.com/panda-re/panda/tree/dev/panda/plugins/osi_linux/utils

Arguments
---------

* `conf`: string: The location of the configuration file that gives the required offsets for different versions of Linux.
* `group`: string, The name of a configuration group to use from the kernelinfo file (multiple configurations can be stored in a single `kernelinfo.conf`).

Dependencies
------------

`vmi_linux` is an introspection provider for the `vmi` plugin.

Example
-------

Assuming you have a `kernelinfo.conf` in the current directory with a configuration named `my_kernel_info`, you can run the OSI test plugin on a Linux replay as follows:

```bash
    [qemu args] -plugin vmi_core -plugin vmi_linux,kconf_file=kernelinfo.conf,kconf_group=my_kernel_info
```

Background
---------

VMI Linux was originally developed for [PANDA.re](https://panda.re) and named `osi_linux` with OSI standing for Operating System Introspection, the name of PANDA's VMI system.
