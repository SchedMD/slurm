/*****************************************************************************\
 *  cgroup_dbus.c - dbus utility functions for cgroup/v2.
 *****************************************************************************
 *  Copyright (C) 2022 SchedMD LLC
 *  Written by Felip Moll <felip.moll@schedmd.com>
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "src/plugins/cgroup/v2/cgroup_dbus.h"

/*
 * This is how systemd code understand you're asking for the max. of some
 * cgroup interface, e.g. pids.max, memory.[low|high|max], etc.
 */
#define SYSTEMD_CGROUP_LIMIT_MAX ((uint64_t) -1)

static int _process_and_close_abandon_reply_msg(DBusMessage *msg)
{
	DBusMessageIter itr;
	int type, rc = SLURM_SUCCESS;
	char *tmp_str;

	dbus_message_iter_init(msg, &itr);
	do {
		type = dbus_message_iter_get_arg_type(&itr);
		switch (type) {
		case DBUS_TYPE_INVALID:
			/* AbandonScope doesn't return anything on success. */
			break;
		case DBUS_TYPE_STRING:
		case DBUS_TYPE_SIGNATURE:
			rc = SLURM_ERROR;
			dbus_message_iter_get_basic(&itr, &tmp_str);
			error("Got an error an error on dbus AbandonScope: %s",
			      tmp_str);
			break;
		default:
			rc = SLURM_ERROR;
			error("%s: Invalid response type %c not supported by Slurm",
			      __func__, type);
			break;
		}
	} while (dbus_message_iter_next(&itr));

	dbus_message_unref(msg);

	if (rc == SLURM_SUCCESS)
		log_flag(CGROUP, "Successfully abandoned scope.");

	return rc;
}

static int _process_and_close_reply_msg(DBusMessage *msg)
{
	DBusMessageIter itr;
	int type, rc = SLURM_SUCCESS;
	char *tmp_str;

	dbus_message_iter_init(msg, &itr);
	do {
		type = dbus_message_iter_get_arg_type(&itr);
		switch (type) {
		case DBUS_TYPE_OBJECT_PATH:
			dbus_message_iter_get_basic(&itr, &tmp_str);
			log_flag(CGROUP, "Possibly created new scope: %s",
				 tmp_str);
			break;
		case DBUS_TYPE_STRING:
		case DBUS_TYPE_SIGNATURE:
			rc = SLURM_ERROR;
			dbus_message_iter_get_basic(&itr, &tmp_str);
			log_flag(CGROUP, "The unit may already exist or we got an error: %s",
				 tmp_str);
			break;
		default:
			rc = SLURM_ERROR;
			error("%s: Invalid response type %c not supported by Slurm",
			      __func__, type);
			break;
		}
	} while (dbus_message_iter_next(&itr));

	dbus_message_unref(msg);
	return rc;
}

static bool _set_scope_properties(DBusMessageIter *main_itr, pid_t *p,
				  int npids, bool delegate)
{
	DBusMessageIter it[4] = { DBUS_MESSAGE_ITER_INIT_CLOSED };
	const char *pid_prop_name = "PIDs";
	const char *dlg_prop_name = "Delegate";
	const char *tasksmax_prop_name = "TasksMax";
	const char pid_prop_sig[] = { DBUS_TYPE_ARRAY, DBUS_TYPE_UINT32, '\0' };
	const char dlg_prop_sig [] = { DBUS_TYPE_BOOLEAN, '\0' };
	const char tasksmax_prop_sig [] = { DBUS_TYPE_UINT64, '\0' };
	char sig[5];
	int dlg = delegate ? 1 : 0;
	uint64_t tasksmax_val = SYSTEMD_CGROUP_LIMIT_MAX;

	/* Signature for the container - (sv) part */
	sig[0] = DBUS_STRUCT_BEGIN_CHAR;
	sig[1] = DBUS_TYPE_STRING;
	sig[2] = DBUS_TYPE_VARIANT;
	sig[3] = DBUS_STRUCT_END_CHAR;
	sig[4] = '\0';

	/* Open array - a(sv) */
	if (!dbus_message_iter_open_container(main_itr, DBUS_TYPE_ARRAY, sig,
					      &it[0]))
		goto oom;

	/*
	 * Add PIDs property - PIDs <pid1, pid2, ...>
	 */
	/* Open struct */
	if (!dbus_message_iter_open_container(&it[0], DBUS_TYPE_STRUCT, NULL,
					      &it[1]))
		goto abandon;

	/* Insert string */
	if (!dbus_message_iter_append_basic(&it[1], DBUS_TYPE_STRING,
					    &pid_prop_name))
		goto abandon;

	/* Open variant */
	if (!dbus_message_iter_open_container(&it[1], DBUS_TYPE_VARIANT,
					      pid_prop_sig, &it[2]))
		goto abandon;

	/* Open array of uint32 */
	if (!dbus_message_iter_open_container(&it[2], *(pid_prop_sig),
					      (pid_prop_sig + 1), &it[3]))
		goto abandon;

	/* Insert elements */
	if (!dbus_message_iter_append_fixed_array(&it[3], *(pid_prop_sig + 1),
						  &p, npids))
		goto abandon;

	/*
	 * At this point we have the array of pids inserted, let's close this
	 * block: Close array, Close variant, Close struct.
	 */
	if (!dbus_message_iter_close_container(&it[2], &it[3])
	    || !dbus_message_iter_close_container(&it[1], &it[2])
	    || !dbus_message_iter_close_container(&it[0], &it[1]))
		goto abandon;

	/*
	 * Add the property of Delegate = yes. We are into the array (it1) and
	 * we need to open a new struct (it2) to put the string and the boolean.
	 */
	if (!dbus_message_iter_open_container(&it[0], DBUS_TYPE_STRUCT, NULL,
					      &it[1]))
		goto abandon;
	if (!dbus_message_iter_append_basic(&it[1], DBUS_TYPE_STRING,
					    &dlg_prop_name))
		goto abandon;
	if (!dbus_message_iter_open_container(&it[1], DBUS_TYPE_VARIANT,
					      dlg_prop_sig, &it[2]))
		goto abandon;
	if (!dbus_message_iter_append_basic(&it[2], *(dlg_prop_sig), &dlg))
		goto abandon;

	/*
	 * At this point we have the Delegate=yes inserted, let's close this
	 * block: Close variant, Close struct.
	 */
	if (!dbus_message_iter_close_container(&it[1], &it[2])
	    || !dbus_message_iter_close_container(&it[0], &it[1]))
		goto abandon;

	/*
	 * Add the property of TasksMax = infinity. We are into the array (it1)
	 * and we need to open a new struct (it2) to put the string and the
	 * variant which is a uint64_t.
	 */
	if (!dbus_message_iter_open_container(&it[0], DBUS_TYPE_STRUCT, NULL,
					      &it[1]))
		goto abandon;
	if (!dbus_message_iter_append_basic(&it[1], DBUS_TYPE_STRING,
					    &tasksmax_prop_name))
		goto abandon;
	if (!dbus_message_iter_open_container(&it[1], DBUS_TYPE_VARIANT,
					      tasksmax_prop_sig, &it[2]))
		goto abandon;
	if (!dbus_message_iter_append_basic(&it[2], *(tasksmax_prop_sig),
					    &tasksmax_val))
		goto abandon;
	/*
	 * At this point we have the TasksMax=infinity inserted, let's close all
	 * block: Close variant, Close struct, Close array.
	 */
	if (!dbus_message_iter_close_container(&it[1], &it[2])
	    || !dbus_message_iter_close_container(&it[0], &it[1])
	    || !dbus_message_iter_close_container(main_itr, &it[0]))
		goto abandon;

	return true;

abandon:
	dbus_message_iter_abandon_container_if_open(&it[2], &it[3]);
	dbus_message_iter_abandon_container_if_open(&it[1], &it[2]);
	dbus_message_iter_abandon_container_if_open(&it[0], &it[1]);
	dbus_message_iter_abandon_container_if_open(main_itr, &it[0]);
oom:
	error("%s: not enough memory setting dbus msg.", __func__);
	return false;
}

static bool _set_scope_aux(DBusMessageIter *main_itr)
{
	char sig[9];
	DBusMessageIter it1 = DBUS_MESSAGE_ITER_INIT_CLOSED;

	/*
	 * Systemd's StartTransientUnit method requires to setup this signature
	 * but at the same time requires it to be NULL. This is the last part:
	 * 'a(sa(sv))'
	 */
	sig[0] = DBUS_STRUCT_BEGIN_CHAR;
	sig[1] = DBUS_TYPE_STRING;
	sig[2] = DBUS_TYPE_ARRAY;
	sig[3] = DBUS_STRUCT_BEGIN_CHAR;
	sig[4] = DBUS_TYPE_STRING;
	sig[5] = DBUS_TYPE_VARIANT;
	sig[6] = DBUS_STRUCT_END_CHAR;
	sig[7] = DBUS_STRUCT_END_CHAR;
	sig[8] = '\0';

	/* The array, which will contain the signature but have 0 elements. */
	if (!dbus_message_iter_open_container(main_itr, DBUS_TYPE_ARRAY,
					      sig, &it1))
		goto oom;

	/* Close the array. */
	if (!dbus_message_iter_close_container(main_itr, &it1)) {
		dbus_message_iter_abandon_container_if_open(main_itr, &it1);
		goto oom;
	}

	return true;
oom:
	error("%s: not enough memory setting dbus msg.", __func__);
	return false;
}

static int _abandon_scope(char *scope_name)
{
	DBusMessage *msg;
	DBusMessageIter args_itr = DBUS_MESSAGE_ITER_INIT_CLOSED;
	DBusConnection *conn = NULL;
	DBusPendingCall *pending;
	DBusError err;

	log_flag(CGROUP, "Abandoning Slurm scope %s", scope_name);

	dbus_error_init(&err);
	conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);

	if (dbus_error_is_set(&err)) {
		error("%s: cannot connect to dbus system daemon: %s",
		      __func__, err.message);
		dbus_error_free(&err);
	}
	if (!conn)
		return SLURM_ERROR;

	msg = dbus_message_new_method_call("org.freedesktop.systemd1",
					   "/org/freedesktop/systemd1",
					   "org.freedesktop.systemd1.Manager",
					   "AbandonScope");
	if (!msg) {
		error("%s: not enough memory setting dbus msg.", __func__);
		return SLURM_ERROR;
	}

	dbus_message_iter_init_append(msg, &args_itr);
	if (!dbus_message_iter_append_basic(&args_itr, DBUS_TYPE_STRING,
					    &scope_name)) {
		error("%s: memory couldn't be allocated while appending argument.",
		      __func__);
		return SLURM_ERROR;
	}
	log_flag(CGROUP,"dbus AbandonScope msg signature: %s",
		 dbus_message_get_signature(msg));

	if (!dbus_connection_send_with_reply(conn, msg, &pending, -1)) {
		error("%s: failed to send dbus message.", __func__);
		return SLURM_ERROR;
	}
	if (!pending) {
		error("%s: could not get a handle for dbus reply.", __func__);
		return SLURM_ERROR;
	}

	dbus_connection_flush(conn);
	dbus_message_unref(msg);
	dbus_pending_call_block(pending);
	if (!(msg = dbus_pending_call_steal_reply(pending))) {
		error("%s: cannot abandon scope, dbus reply msg is null.",
		      __func__);
		return SLURM_ERROR;
	}
	dbus_pending_call_unref(pending);

	if (_process_and_close_abandon_reply_msg(msg) != SLURM_SUCCESS)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

/*
 * Slurm function to attach stepd to a systemd scope, using dbus.
 */
extern int cgroup_dbus_attach_to_scope(pid_t stepd_pid, char *full_path)
{
	const char *mode = "fail";
	char *scope_name = xbasename(full_path);
	DBusMessage *msg;
	DBusMessageIter args_itr = DBUS_MESSAGE_ITER_INIT_CLOSED;
	DBusConnection *conn = NULL;
	DBusPendingCall *pending;
	DBusError err;
	pid_t pids[] = { stepd_pid };
	int npids = 1;

	log_flag(CGROUP, "Creating Slurm scope %s into system slice and adding pid %d.",
		 scope_name, stepd_pid);

	dbus_error_init(&err);

	/*
	 * Connect to the system bus daemon and register our connection.
	 * This function may block until auth. and bus registration are
	 * complete.
	 */
	conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);

	if (dbus_error_is_set(&err)) {
		error("%s: cannot connect to dbus system daemon: %s",
		      __func__, err.message);
		dbus_error_free(&err);
	}
	if (!conn)
		return SLURM_ERROR;

	/* Create the new method call. */
	msg = dbus_message_new_method_call("org.freedesktop.systemd1",
					   "/org/freedesktop/systemd1",
					   "org.freedesktop.systemd1.Manager",
					   "StartTransientUnit");
	if (!msg) {
		error("%s: not enough memory setting dbus msg.", __func__);
		return SLURM_ERROR;
	}

	/*
	 * Initialize the iterator for appending arguments to the end of the
	 * message.
	 */
	dbus_message_iter_init_append(msg, &args_itr);

	/* Append our scope name to the arguments. */
	if (!dbus_message_iter_append_basic(&args_itr, DBUS_TYPE_STRING,
					    &scope_name)) {
		error("%s: memory couldn't be allocated while appending argument.",
		      __func__);
		return SLURM_ERROR;
	}

	/*
	 * Append the scope mode. Normally it is 'fail' or 'replace'.
	 * Check systemd docs fore more info.
	 */
	if (!dbus_message_iter_append_basic(&args_itr, DBUS_TYPE_STRING,
					    &mode)) {
		error("%s: memory couldn't be allocated while appending argument.",
		      __func__);
		return SLURM_ERROR;
	}

	/*
	 * Start adding specific 'properties' as arguments to our message.
	 * Properties in this context are systemd unit properties. We're
	 * interested in adding Delegate=yes, and the PIDs list (stepd's pid)
	 * which will be moved to this scope container at startup.
	 */

	if (!_set_scope_properties(&args_itr, pids, npids, true)) {
		error("%s: cannot set scope properties, scope not started.",
		      __func__);
		return SLURM_ERROR;
	}

	/*
	 * 'Auxiliary units'
	 * Systemd's StartTransientUnit method signature requires to set this
	 * and to be null. These are useless parameters but need to be defined.
	 */
	if (!_set_scope_aux(&args_itr)) {
		error("%s: cannot set scope auxiliary units, scope not started.",
		      __func__);
		return SLURM_ERROR;
	}

	log_flag(CGROUP,"dbus StartTransientUnit msg signature: %s",
		 dbus_message_get_signature(msg));

	/*
	 * Queue the msg to send and get a handle for the reply.
	 * -1 is infinite timeout.
	 */
	if (!dbus_connection_send_with_reply(conn, msg, &pending, -1)) {
		error("%s: failed to send dbus message.", __func__);
		return SLURM_ERROR;
	}
	if (!pending) {
		error("%s: could not get a handle for dbus reply.", __func__);
		return SLURM_ERROR;
	}

	/* Block until the outgoing message queue is empty. */
	dbus_connection_flush(conn);

	/* Decrement the ref. count of msg and free it if the cnt is 0. */
	dbus_message_unref(msg);

	/* Wait for the reply. */
	dbus_pending_call_block(pending);
	if (!(msg = dbus_pending_call_steal_reply(pending))) {
		dbus_connection_unref(conn);
		error("%s: cannot start scope, dbus reply msg is null.",
		      __func__);
		return SLURM_ERROR;
	}
	dbus_pending_call_unref(pending);
	dbus_connection_unref(conn);

	if (_process_and_close_reply_msg(msg) != SLURM_SUCCESS)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

extern int cgroup_dbus_abandon_scope(char *full_path)
{
	return _abandon_scope(xbasename(full_path));
}