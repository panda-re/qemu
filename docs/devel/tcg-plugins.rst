..
   Copyright (C) 2017, Emilio G. Cota <cota@braap.org>
   Copyright (c) 2019, Linaro Limited
   Written by Emilio Cota and Alex Bennée

.. _TCG Plugins:

QEMU TCG Plugins
================


Writing plugins
---------------

API versioning
~~~~~~~~~~~~~~

This is a new feature for QEMU and it does allow people to develop
out-of-tree plugins that can be dynamically linked into a running QEMU
process. However the project reserves the right to change or break the
API should it need to do so. The best way to avoid this is to submit
your plugin upstream so they can be updated if/when the API changes.

All plugins need to declare a symbol which exports the plugin API
version they were built against. This can be done simply by::

  QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

The core code will refuse to load a plugin that doesn't export a
``qemu_plugin_version`` symbol or if plugin version is outside of QEMU's
supported range of API versions.

Additionally the ``qemu_info_t`` structure which is passed to the
``qemu_plugin_install`` method of a plugin will detail the minimum and
current API versions supported by QEMU. The API version will be
incremented if new APIs are added. The minimum API version will be
incremented if existing APIs are changed or removed.

Lifetime of the query handle
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Each callback provides an opaque anonymous information handle which
can usually be further queried to find out information about a
translation, instruction or operation. The handles themselves are only
valid during the lifetime of the callback so it is important that any
information that is needed is extracted during the callback and saved
by the plugin.

Plugin life cycle
~~~~~~~~~~~~~~~~~

First the plugin is loaded and the public qemu_plugin_install function
is called. The plugin will then register callbacks for various plugin
events. Generally plugins will register a handler for the *atexit*
if they want to dump a summary of collected information once the
program/system has finished running.

When a registered event occurs the plugin callback is invoked. The
callbacks may provide additional information. In the case of a
translation event the plugin has an option to enumerate the
instructions in a block of instructions and optionally register
callbacks to some or all instructions when they are executed.

There is also a facility to add inline instructions doing various operations,
like adding or storing an immediate value. It is also possible to execute a
callback conditionally, with condition being evaluated inline. All those inline
operations are associated to a ``scoreboard``, which is a thread-local storage
automatically expanded when new cores/threads are created and that can be
accessed/modified in a thread-safe way without any lock needed. Combining inline
operations and conditional callbacks offer a more efficient way to instrument
binaries, compared to classic callbacks.

Finally when QEMU exits all the registered *atexit* callbacks are
invoked.

Exposure of QEMU internals
~~~~~~~~~~~~~~~~~~~~~~~~~~

The plugin architecture actively avoids leaking implementation details
about how QEMU's translation works to the plugins. While there are
conceptions such as translation time and translation blocks the
details are opaque to plugins. The plugin is able to query select
details of instructions and system configuration only through the
exported *qemu_plugin* functions.

However the following assumptions can be made:

Translation Blocks
++++++++++++++++++

All code will go through a translation phase although not all
translations will be necessarily be executed. You need to instrument
actual executions to track what is happening.

It is quite normal to see the same address translated multiple times.
If you want to track the code in system emulation you should examine
the underlying physical address (``qemu_plugin_insn_haddr``) to take
into account the effects of virtual memory although if the system does
paging this will change too.

Not all instructions in a block will always execute so if its
important to track individual instruction execution you need to
instrument them directly. However asynchronous interrupts will not
change control flow mid-block.

Instructions
++++++++++++

Instruction instrumentation runs before the instruction executes. You
can be can be sure the instruction will be dispatched, but you can't
be sure it will complete. Generally this will be because of a
synchronous exception (e.g. SIGILL) triggered by the instruction
attempting to execute. If you want to be sure you will need to
instrument the next instruction as well. See the ``execlog.c`` plugin
for examples of how to track this and finalise details after execution.

Memory Accesses
+++++++++++++++

Memory callbacks are called after a successful load or store.
Unsuccessful operations (i.e. faults) will not be visible to memory
instrumentation although the execution side effects can be observed
(e.g. entering a exception handler).

System Idle and Resume States
+++++++++++++++++++++++++++++

The ``qemu_plugin_register_vcpu_idle_cb`` and
``qemu_plugin_register_vcpu_resume_cb`` functions can be used to track
when CPUs go into and return from sleep states when waiting for
external I/O. Be aware though that these may occur less frequently
than in real HW due to the inefficiencies of emulation giving less
chance for the CPU to idle.

Internals
---------

Locking
~~~~~~~

We have to ensure we cannot deadlock, particularly under MTTCG. For
this we acquire a lock when called from plugin code. We also keep the
list of callbacks under RCU so that we do not have to hold the lock
when calling the callbacks. This is also for performance, since some
callbacks (e.g. memory access callbacks) might be called very
frequently.

  * A consequence of this is that we keep our own list of CPUs, so that
    we do not have to worry about locking order wrt cpu_list_lock.
  * Use a recursive lock, since we can get registration calls from
    callbacks.

As a result registering/unregistering callbacks is "slow", since it
takes a lock. But this is very infrequent; we want performance when
calling (or not calling) callbacks, not when registering them. Using
RCU is great for this.

We support the uninstallation of a plugin at any time (e.g. from
plugin callbacks). This allows plugins to remove themselves if they no
longer want to instrument the code. This operation is asynchronous
which means callbacks may still occur after the uninstall operation is
requested. The plugin isn't completely uninstalled until the safe work
has executed while all vCPUs are quiescent.

Plugin-to-Plugin Interactions
-----------------------------

Plugins may interact with other plugins through the QEMU Plugin-to-Plugin
("QPP") API by including ``<plugin-qpp.h>`` in addition to ``<qemu_plugin.h>``.
This API supports direct function calls between plugins. An inter-plugin
callback system is supported within the core code as long as
``qemu_plugin_version >= 5``.

Plugin names
~~~~~~~~~~~~
Plugin names must be exported as ``qemu_plugin_name`` to use the QPP API in the same way
``qemu_plugin_version`` is exported. This name can then be used by other plugins
to import functions and use callbacks belonging to that plugin.

Plugin dependencies
~~~~~~~~~~~~~~~~~~~
For a plugin to use another plugin's functions or callbacks it must declare that
dependency through exporting ``qemu_plugin_uses`` which is a string array containing
names of plugins used by that plugin. Note that this array must be null terminated, e.g.
``{plugin_a, NULL}``. Those plugins must be loaded first. QPP plugins are loaded in the
order they are listed, and each plugin's dependencies will be checked for at load time.
So if plugin_c is dependent on two other plugins, plugin_a and plugin_b, and plugin_b is
also dependent on plugin_a, then the plugins should be listed on the command line in
order of plugin_a, plugin_b, plugin_c so that each dependency is loaded prior to the
plugin which depends on it.

QPP function calls
~~~~~~~~~~~~~~~~~~
When a plugin (e.g., ``plugin_a``) wishes to make some of its functions (e.g.,
``func_1``) available to other plugins, it must:

1. Mark the function definition with the ``QEMU_PLUGIN_EXPORT`` macro. For
example : ``QEMU_PLUGIN_EXPORT int func_1(int x) {...}``
2. Provide prototypes for exported functions in a header file using the macro
``QPP_FUN_PROTOTYPE`` with arguments of the plugin's name, the function's
return type, the function's name, and any arguments the function takes. For
example: ``QPP_FUN_PROTOTYPE(my_plugin, int, do_add, int);``.
3. Import this header from the plugin.

When other plugins wish to use the functions exported by ``plugin_a``, they
must:

1. Import the header file with the function prototype(s).
2. Call the function when desired by combining the target plugin name, an
   underscore, and the target function name with ``_qpp`` on the end,
   e.g., ``plugin_a_func_1_qpp()``.

QPP callbacks
~~~~~~~~~~~~~

The QPP API also allows a plugin to define callback events and for other plugins
to request to be notified whenever these events happens. The plugin that defines
the callback is responsible for triggering the callback when it so wishes. Other
plugins that wish to be notified on these events must define a function of an
appropriate type and register it to run on this event.
In particular, these plugins must:


When a plugin (e.g., ``plugin_a``) wishes to define a callback (an event that
other plugins can request to be notified about), it must:

1. Define the callback using the ``qemu_plugin_create_callback`` function which
   takes two arguments: the unique ``qemu_plugin_id_t id`` and the callback name.
2. Call ``qemu_plugin_run_callback`` at appropriate places in the code to call registered
   callback functions. It takes four arguments: the unique ``qemu_plugin_id_t id``,
   the callback name, and the callback arguments which are standardized to be
   ``gpointer evdata, gpointer udata``. The callback arguments point to two structs
   which are defined by the plugin and can vary based on the use case of the callback.

When other plugins wish to register a function to run on such an event, they
must:

1. Define a function that matches the ``cb_func_t`` type:
   ``typedef void (*cb_func_t) (gpointer evdata, gpointer udata)``.
2. Register this function to be run on the plugin defined callback using
   ``qemu_plugin_reg_callback``. This function takes three arguments: the name of the
   plugin which defines the callback, the callback name, and a ``cb_func_t`` function
   pointer.

When other plugins wish to unregister a function which is registered to run on a plugin
defined event callback, they must:

1. Call ``qemu_plugin_unreg_callback``. This function takes the same arguments as
   ``qemu_plugin_reg_callback``. It will return true if it successfully finds and
   unregisters the function.

Plugin API
==========

The following API is generated from the inline documentation in
``include/qemu/qemu-plugin.h``. Please ensure any updates to the API
include the full kernel-doc annotations.

.. kernel-doc:: include/qemu/qemu-plugin.h
