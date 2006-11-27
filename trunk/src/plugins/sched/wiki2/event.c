/*****************************************************************************\
 *  event.c - Moab event notification
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-217948.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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
static slurm_addr	moab_event_addr,  moab_event_addr_bu;
static int		event_addr_set = 0;

/*
 * event_notify - Notify Moab of some event
 * msg IN - event type, NULL to close connection
 * RET 0 on success, -1 on failure
 */
extern int	event_notify(char *msg)
{
	time_t now = time(NULL);
	int rc;
	slurm_fd event_fd = (slurm_fd) -1;

	if (e_port == 0) {
		/* Event notification disabled */
		return 0;
	}

	if (job_aggregation_time
	&&  (difftime(now, last_notify_time) < job_aggregation_time)) {
		debug2("wiki event notification already sent recently");
		return 0;
	}

	pthread_mutex_lock(&event_mutex);
	if (event_addr_set == 0) {
		/* Identify address for socket connection */
		slurm_set_addr(&moab_event_addr, e_port, e_host);
		event_addr_set = 1;
		if (e_host_bu[0] != '\0') {
			slurm_set_addr(&moab_event_addr_bu, e_port, 
				e_host_bu);
			event_addr_set = 2;
		}
	}
	event_fd = slurm_open_msg_conn(&moab_event_addr);
	if ((event_fd == -1) && (event_addr_set == 2)) {
		debug("Unable to open wiki event port %s:%u: %m",
			e_host, e_port);
		event_fd = slurm_open_msg_conn(&moab_event_addr_bu);
	}
	if (event_fd == -1) {
		char *host_name;
		if (event_addr_set == 2)
			host_name = e_host_bu;
		else
			host_name = e_host;
		error("Unable to open wiki event port %s:%u: %m", 
			host_name, e_port);
		pthread_mutex_unlock(&event_mutex);
		/* Don't retry again for a while (10 mins)
		 * to avoid long delays from ETIMEDOUT */
		last_notify_time = now + 600;
		return -1;
	}

	/* We can't have the controller block on the following write() */
	fd_set_nonblocking(event_fd);

	/* Always send "1234\0" as the message
	 * (we do not care if all of the message is sent, just that
	 * some of it went through to wake up Moab)
	 */
	if (write(event_fd, "1234", 5) > 0) {
		info("wiki event_notification sent: %s", msg);
		last_notify_time = now;
		rc = 0;
	} else {
		error("wiki event notification failure: %m");
		/* close socket, re-open later */
		(void) slurm_shutdown_msg_engine(event_fd);
		event_fd = -1;
		rc = -1;
	}

	/* We disconnect and reconnect on every message to
	 * gracefully handle some failure modes of Moab */
	(void) slurm_shutdown_msg_conn(event_fd);
	pthread_mutex_unlock(&event_mutex);

	return rc;
}
