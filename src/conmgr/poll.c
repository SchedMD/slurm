/*****************************************************************************\
 *  poll.c - Definitions for poll() handlers
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#include <stddef.h>

#include "slurm/slurm_errno.h"
#include "src/conmgr/polling.h"

static const char *_type_to_string(pollctl_fd_type_t type)
{
	return NULL;
}

static void _init(const int max_connections)
{
}

static void _modify_max_connections(const int max_connections)
{
}

static void _fini(void)
{
}

/* caller must hold pctl.mutex lock */
static int _link_fd(int fd, pollctl_fd_type_t type, const char *con_name,
		    const char *caller)
{
	return ESLURM_NOT_SUPPORTED;
}

static void _relink_fd(int fd, pollctl_fd_type_t type, const char *con_name, const char *caller)
{
}

static void _unlink_fd(int fd, const char *con_name, const char *caller)
{
}

static int _poll(const char *caller)
{
	return -1;
}

static int _for_each_event(pollctl_event_func_t func, void *arg,
			   const char *func_name, const char *caller)
{
	return ESLURM_NOT_SUPPORTED;
}

static void _interrupt(const char *caller)
{
}

static bool _events_can_read(pollctl_events_t events)
{
	return false;
}

static bool _events_can_write(pollctl_events_t events)
{
	return false;
}

static bool _events_has_error(pollctl_events_t events)
{
	return false;
}

static bool _events_has_hangup(pollctl_events_t events)
{
	return false;
}

const poll_funcs_t poll_funcs = {
	.mode = POLL_MODE_POLL,
	.init = _init,
	.fini = _fini,
	.type_to_string = _type_to_string,
	.modify_max_connections = _modify_max_connections,
	.link_fd = _link_fd,
	.relink_fd = _relink_fd,
	.unlink_fd = _unlink_fd,
	.poll = _poll,
	.for_each_event = _for_each_event,
	.interrupt = _interrupt,
	.events_can_read = _events_can_read,
	.events_can_write = _events_can_write,
	.events_has_error = _events_has_error,
	.events_has_hangup = _events_has_hangup,
};
