/*****************************************************************************\
 *  event.c - Moab event notification
 *****************************************************************************
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "./msg.h"
#include "src/common/fd.h"

static pthread_mutex_t	event_mutex = PTHREAD_MUTEX_INITIALIZER;
static time_t		last_notify_time = (time_t) 0;
static slurm_addr_t	moab_event_addr,  moab_event_addr_bu;
static int		event_addr_set = 0;
static slurm_fd_t	event_fd = (slurm_fd_t) -1;

/* Open event_fd as needed
 * RET 0 on success, -1 on failure */
static int _open_fd(time_t now)
{
	if (event_fd != -1)
		return 0;

	/* Identify address for socket connection.
	 * Done only on first call, then cached. */
	if (event_addr_set == 0) {
		slurm_set_addr(&moab_event_addr, e_port, e_host);
		event_addr_set = 1;
		if (e_host_bu[0] != '\0') {
			slurm_set_addr(&moab_event_addr_bu, e_port,
				e_host_bu);
			event_addr_set = 2;
		}
	}

	/* Open the event port on moab as needed */
	if (event_fd == -1) {
		event_fd = slurm_open_msg_conn(&moab_event_addr);
		if (event_fd == -1) {
			error("Unable to open primary wiki "
				"event port %s:%u: %m",
				e_host, e_port);
		}
	}
	if ((event_fd == -1) && (event_addr_set == 2)) {
		event_fd = slurm_open_msg_conn(&moab_event_addr_bu);
		if (event_fd == -1) {
			error("Unable to open backup wiki "
				"event port %s:%u: %m",
				e_host_bu, e_port);
		}
	}
	if (event_fd == -1)
		return -1;

	/* We can't have the controller block on the following write() */
	fd_set_nonblocking(event_fd);
	return 0;
}

static void _close_fd(void)
{
	if (event_fd == -1)
		return;

	(void) slurm_shutdown_msg_engine(event_fd);
	event_fd = -1;
}

/*
 * event_notify - Notify Moab of some event
 * event_code IN - message code to send Moab
 *          1234 - job state change
 *          1235  - partition state change
 * desc IN - event description
 * RET 0 on success, -1 on failure
 */
extern int	event_notify(int event_code, char *desc)
{
	time_t now = time(NULL);
	int rc = 0, retry = 2;
	char *event_msg;
	DEF_TIMERS;

	START_TIMER;
	if (e_port == 0) {
		/* Event notification disabled */
		return 0;
	}

	if (event_code == 1234) {		/* job change */
		if (job_aggregation_time
		&&  (difftime(now, last_notify_time) < job_aggregation_time)) {
			debug("wiki event notification already sent recently");
			return 0;
		}
		event_msg = "1234";
	} else if (event_code == 1235) {	/* configuration change */
		event_msg = "1235";
	} else {
		error("event_notify: invalid event code: %d", event_code);
		return -1;
	}

	pthread_mutex_lock(&event_mutex);
	while (retry) {
		if ((event_fd == -1) && ((rc = _open_fd(now)) == -1)) {
			/* Can't even open socket.
			 * Don't retry again for a while (2 mins)
			 * to avoid long delays from ETIMEDOUT */
			last_notify_time = now + 120;
			break;
		}

		if (write(event_fd, event_msg, (strlen(event_msg) + 1)) > 0) {
			verbose("wiki event_notification sent: %s", desc);
			last_notify_time = now;
			rc = 0;
			/* Dave Jackson says to leave the connection
			 * open, but Moab isn't. Without the _close_fd()
			 * here, the next write() generates a broken pipe
			 * error. Just remove the _close_fd() and this
			 * comment when Moab maintains the connection. */
			_close_fd();
			break;	/* success */
		}

		error("wiki event notification failure: %m");
		rc = -1;
		retry--;
		if ((errno == EAGAIN) || (errno == EINTR))
			continue;

		_close_fd();
		if (errno == EPIPE) {
			/* If Moab closed the socket we get an EPIPE,
			 * retry once */
			continue;
		} else {
			break;
		}
	}
	pthread_mutex_unlock(&event_mutex);
	END_TIMER2("event_notify");

	return rc;
}
