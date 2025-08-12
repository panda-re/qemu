#include "qemu/osdep.h"
#include "block/qapi.h"
#include "migration/snapshot.h"
#include "monitor/hmp.h"
#include "monitor/monitor.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-migration.h"
#include "qapi/qapi-visit-migration.h"
#include "qobject/qdict.h"
#include "qapi/string-input-visitor.h"
#include "qapi/string-output-visitor.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qemu/sockets.h"
#include "system/runstate.h"
#include "ui/qemu-spice.h"
#include "system/system.h"
// #include "options.h"
#include "migration/migration.h"
#include "chardev/char.h"
#include "system/replay.h"
#include "replay/replay-internal.h"
#include "system/cpu-timers.h"
#include "chardev/chardev-internal.h"
#include "qapi/qapi-commands-replay.h"

#include "exec/icount.h"

#define HEADER_SIZE                 (sizeof(uint32_t) + sizeof(uint64_t))

static int chardev_foreach_start_record(Object* obj, void* data)
{
	Chardev* chr = CHARDEV(obj);
	Error** errp = data;
	Error* local_err = NULL;

	if (!qemu_chr_has_feature(chr, QEMU_CHAR_FEATURE_REPLAY))
		return 0;

	/*
		Any character device that has the replay bit set
		must be mapped and tested for compatibility
	*/
	if (CHARDEV_GET_CLASS(chr)->chr_ioctl) {
		error_setg(&local_err, "Replay: ioctl is not supported for serial devices yet");
		error_propagate(errp, local_err);
		return -1;
	}
	replay_register_char_driver(chr);

	return 0;
}

void qmp_end_record(Error** errp)
{
	Error* local_err = NULL;

	switch (replay_mode)
	{
		case REPLAY_MODE_NONE: {
			error_setg(&local_err, "No recording is in progress...");
			error_propagate(errp, local_err);
			return;
		}
		case REPLAY_MODE_PLAY: {
			error_setg(&local_err, "No recording is in progress...");
			error_propagate(errp, local_err);
			return;
		}
		case REPLAY_MODE_RECORD: {
			break;
		}
		default: {
			error_setg(&local_err, "Unknown record/replay state! Cannot end recording.");
			error_propagate(errp, local_err);
			return;
		}
	}

	/* We're in record mode, so no point in checking all the other reasons that shouldn't be true */
	replay_finish();
	replay_vmstate_unregister();
	replay_clear_char_drivers();
	error_free(local_err);
}

void hmp_end_record(Monitor *mon, const QDict *qdict)
{
	Error* err = NULL;

	qmp_end_record(&err);
	hmp_handle_error(mon, err);
}

void qmp_begin_record(const char* snapshot_name, const char* logfile_name, Error** errp)
{
	Error* local_err = NULL;

	switch (replay_mode)
	{
		case REPLAY_MODE_NONE: {
			break;
		}
		case REPLAY_MODE_PLAY: {
			error_setg(&local_err, "Cannot begin recording during replay.");
			error_propagate(errp, local_err);
			return;
		}
		case REPLAY_MODE_RECORD: {
			error_setg(&local_err, "Recording already in progress...");
			error_propagate(errp, local_err);
			return;
		}
		default: {
			error_setg(&local_err, "Unknown record/replay state! Cannot start recording.");
			error_propagate(errp, local_err);
			return;
		}
	}

	if (!icount_enabled())
	{
		error_setg(&local_err, "icount mode is not enabled, cannot begin recording.");
		error_propagate(errp, local_err);
		return;
	}

	if (replay_get_blockers())
	{
		error_setg(&local_err, ".");
		for (const GSList* entry = replay_get_blockers(); entry; entry = entry->next)
		{
			Error* suberr = entry->data;
			error_prepend(&local_err, "\n\t%s", error_get_pretty(suberr));
		}
		error_prepend(&local_err, "Record/replay blocked by features:");
		error_propagate(errp, local_err);
		return;
	}

	if (!snapshot_name)
	{
		error_setg(&local_err, "Missing required rrsnapshot parameter!");
		error_propagate(errp, local_err);
		return;
	}

	if (!logfile_name)
	{
		error_setg(&local_err, "Missing required rrfile parameter!");
		error_propagate(errp, local_err);
		return;
	}

	object_child_foreach(get_chardevs_root(), chardev_foreach_start_record, &local_err);
	if (local_err)
	{
		replay_clear_char_drivers();
		error_propagate(errp, local_err);
		return;
	}

	if ((replay_file = fopen(logfile_name, "wb")) == NULL)
	{
		replay_clear_char_drivers();
		error_setg(&local_err, "Record: open %s: %s\n", logfile_name, strerror(errno));
		error_propagate(errp, local_err);
		return;
	}

	const RunState oldstate = runstate_get();
	vm_stop(RUN_STATE_SAVE_VM);

	replay_vmstate_register();

	atexit(replay_finish);

	replay_mode = REPLAY_MODE_RECORD;

	replay_state.data_kind = -1;
	replay_state.instruction_count = 0;
	replay_state.current_icount = replay_get_current_icount();
	replay_state.current_event = 0;
	replay_state.has_unread_data = 0;

	/* skip file header for RECORD */
	fseek(replay_file, HEADER_SIZE, SEEK_SET);

	replay_init_events();

	if (!save_snapshot(snapshot_name, /* overwrite */ true, /* vmstate */ NULL, /* has_devices */ false, /* devices */ NULL, &local_err))
	{
		replay_clear_char_drivers();
		fclose(replay_file);
		replay_file = NULL;
		replay_vmstate_unregister();
		vm_resume(oldstate);
		error_propagate(errp, local_err);
		return;
	}

	/* Timer for snapshotting will be set up here. */
	replay_enable_events();

	vm_resume(oldstate);
	error_free(local_err);
}

void hmp_begin_record(Monitor *mon, const QDict *qdict)
{
	Error* err = NULL;

	const char* rrsnapshot = qdict_get_str(qdict, "snapshot");
	const char* rrfile = qdict_get_str(qdict, "logfile");

	qmp_begin_record(rrsnapshot, rrfile, &err);
	if (!err)
		monitor_printf(mon, "Recording started @ snapshot \"%s\" (icount %#" PRIx64 ") logging to file \"%s\"", rrsnapshot, replay_state.current_icount, rrfile);
	hmp_handle_error(mon, err);
}
