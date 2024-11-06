/*****************************************************************************\
 *  polling.c - Definitions for polling handlers
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

#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/macros.h"
#include "src/common/xassert.h"

#include "src/conmgr/polling.h"

#ifdef HAVE_EPOLL
#define DEFAULT_POLLING_MODE POLL_MODE_EPOLL
extern const poll_funcs_t epoll_funcs;
#else
#define DEFAULT_POLLING_MODE POLL_MODE_POLL
#endif /* HAVE_EPOLL */

#define T(mode) { mode, XSTRINGIFY(mode) }
static const struct {
	poll_mode_t mode;
	const char *string;
} modes[] = {
	T(POLL_MODE_INVALID),
	T(POLL_MODE_EPOLL),
	T(POLL_MODE_POLL),
	T(POLL_MODE_INVALID_MAX),
};

extern const poll_funcs_t poll_funcs;

static poll_mode_t mode = POLL_MODE_INVALID;
static const poll_funcs_t *polling_funcs[] = {
#ifdef HAVE_EPOLL
	&epoll_funcs,
#endif /* HAVE_EPOLL */
	&poll_funcs,
};

static const char *_mode_string(poll_mode_t find_mode)
{
	for (int i = 0; i < ARRAY_SIZE(modes); i++)
		if (modes[i].mode == find_mode)
			return modes[i].string;

	fatal_abort("should never happen");
}

static const poll_funcs_t *_get_funcs(void)
{
	for (int i = 0; i < ARRAY_SIZE(polling_funcs); i++)
		if (polling_funcs[i]->mode == mode)
			return polling_funcs[i];

	fatal_abort("should never happen");
}

extern const char *pollctl_type_to_string(pollctl_fd_type_t type)
{
	return _get_funcs()->type_to_string(type);
}

extern void pollctl_init(const int max_connections)
{
	if (mode == POLL_MODE_INVALID)
		mode = DEFAULT_POLLING_MODE;
	log_flag(CONMGR, "%s: [%s] Initializing with connection count %d",
		 __func__, _mode_string(mode), max_connections);
	_get_funcs()->init(max_connections);
}

extern void pollctl_set_mode(poll_mode_t new_mode)
{
	/* This should only be called before polling has been initialized */
	xassert(mode == POLL_MODE_INVALID);
	xassert(new_mode > POLL_MODE_INVALID);
	xassert(new_mode < POLL_MODE_INVALID_MAX);

	mode = new_mode;
	if (mode == DEFAULT_POLLING_MODE)
		return;

	log_flag(CONMGR, "%s: Changing polling type: %s -> %s",
		 __func__, _mode_string(DEFAULT_POLLING_MODE),
		 _mode_string(mode));
}

extern void pollctl_fini(void)
{
	log_flag(CONMGR, "%s: [%s] cleanup", __func__, _mode_string(mode));
	_get_funcs()->fini();
}

extern int pollctl_link_fd(int fd, pollctl_fd_type_t type, const char *con_name,
			   const char *caller)
{
	return _get_funcs()->link_fd(fd, type, con_name, caller);
}

extern void pollctl_relink_fd(int fd, pollctl_fd_type_t type,
			      const char *con_name, const char *caller)
{
	_get_funcs()->relink_fd(fd, type, con_name, caller);
}

extern void pollctl_unlink_fd(int fd, const char *con_name, const char *caller)
{
	_get_funcs()->unlink_fd(fd, con_name, caller);
}

extern int pollctl_poll(const char *caller)
{
	return _get_funcs()->poll(caller);
}

extern int pollctl_for_each_event(pollctl_event_func_t func, void *arg,
				  const char *func_name, const char *caller)
{
	return _get_funcs()->for_each_event(func, arg, func_name, caller);
}

extern void pollctl_interrupt(const char *caller)
{
	_get_funcs()->interrupt(caller);
}

extern bool pollctl_events_can_read(pollctl_events_t events)
{
	return _get_funcs()->events_can_read(events);
}

extern bool pollctl_events_can_write(pollctl_events_t events)
{
	return _get_funcs()->events_can_write(events);
}

extern bool pollctl_events_has_error(pollctl_events_t events)
{
	return _get_funcs()->events_has_error(events);
}

extern bool pollctl_events_has_hangup(pollctl_events_t events)
{
	return _get_funcs()->events_has_hangup(events);
}
