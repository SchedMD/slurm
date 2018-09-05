/*****************************************************************************\
 *  heartbeat.c
 *****************************************************************************
 *  Copyright (C) 2017 SchedMD LLC.
 *  Written by Tim Wickberg <tim@schedmd.com>
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

#define _GNU_SOURCE

#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "src/common/fd.h"
#include "src/common/xstring.h"
#include "src/slurmctld/heartbeat.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/state_save.h"

/*
 * Write to a file at a frequent interval to demonstrate that the primary
 * is still alive and active, and could thus change the contents of
 * StateSaveLocation at any point in time. This is monitoried in the backup
 * and will prevent the backup controller from assuming control in periods
 * of high load (as this thread does not depend on any other locks within
 * slurmctld) or if the network path between primary <-> backup is lost but
 * the path to the StateSaveLocation storage remains intact.
 *
 * Will only run if a BackupController is setup, otherwise this is a no-op
 * and no thread will be launched.
 */

static void *_heartbeat_thread(void *no_data);

static pthread_mutex_t heartbeat_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t heartbeat_cond = PTHREAD_COND_INITIALIZER;

static bool heart_beating;

static void *_heartbeat_thread(void *no_data)
{
	/*
	 * The frequency needs to be faster than slurmctld_timeout,
	 * or the backup controller may try to assume control.
	 * One-fourth is very conservative, one-half should be sufficient.
	 * Have it happen at least every 30 seconds if the timeout is quite
	 * large.
	 */
	int beat = MIN(slurmctld_conf.slurmctld_timeout / 4, 30);
	time_t now;
	uint64_t nl;
	struct timespec ts = {0, 0};
	char *reg_file, *new_file;
	int fd;

	debug("Heartbeat thread started, beating every %d seconds.", beat);

	slurm_mutex_lock(&heartbeat_mutex);
	while (heart_beating) {
		now = time(NULL);
		ts.tv_sec = now + beat;

		debug3("Heartbeat at %ld", now);
		/*
		 * Rebuild file path each beat just in case someone changes
		 * StateSaveLocation and runs reconfigure.
		 */
		reg_file = xstrdup_printf("%s/heartbeat",
					  slurmctld_conf.state_save_location);
		new_file = xstrdup_printf("%s.new", reg_file);

		fd = open(new_file, O_CREAT|O_WRONLY|O_TRUNC|O_CLOEXEC, 0600);
		if (fd < 0) {
			error("%s: heartbeat file creation failed to %s.",
			      __func__, new_file);
			goto delay;
		}

		nl = HTON_uint64(((uint64_t) now));
		if (write(fd, &nl, sizeof(uint64_t)) != sizeof(uint64_t)) {
			error("%s: heartbeat write failed to %s.",
			      __func__, new_file);
			close(fd);
			(void) unlink(new_file);
			goto delay;
		}

		nl = HTON_uint64(((uint64_t) backup_inx));
		if (write(fd, &nl, sizeof(uint64_t)) != sizeof(uint64_t)) {
			error("%s: heartbeat write failed to %s.",
			      __func__, new_file);
			close(fd);
			(void) unlink(new_file);
			goto delay;
		}

		if (fsync_and_close(fd, "heartbeat")) {
			(void) unlink(new_file);
			goto delay;
		}

		/* shuffle files around */
		(void) unlink(reg_file);
		if (link(new_file, reg_file))
			debug("%s: unable to create link for %s -> %s, %m",
			      __func__, new_file, reg_file);
		(void) unlink(new_file);

delay:
		xfree(reg_file);
		xfree(new_file);
		slurm_cond_timedwait(&heartbeat_cond, &heartbeat_mutex, &ts);
	}
	slurm_mutex_unlock(&heartbeat_mutex);

	return NULL;
}

void heartbeat_start(void)
{
	if (slurmctld_conf.control_cnt < 2) {
		debug("No backup controllers, not launching heartbeat.");
		return;
	}

	slurm_mutex_lock(&heartbeat_mutex);
	slurm_thread_create_detached(NULL, _heartbeat_thread, NULL);
	heart_beating = true;
	slurm_mutex_unlock(&heartbeat_mutex);
}

void heartbeat_stop(void)
{
	slurm_mutex_lock(&heartbeat_mutex);
	if (heart_beating) {
		heart_beating = false;
		slurm_cond_signal(&heartbeat_cond);
	}
	slurm_mutex_unlock(&heartbeat_mutex);
}

#define OPEN_RETRIES 3

time_t get_last_heartbeat(int *server_inx)
{
	char *file;
	int fd = -1, i;
	uint64_t value;
	uint64_t inx;

	file = xstrdup_printf("%s/heartbeat",
			      slurmctld_conf.state_save_location);

	/*
	 * Retry the open() in case the primary is rearranging things
	 * at the moment. Once opened, our handle should persist during
	 * the shuffle, as the contents are left intact.
	 */
	for (i = 0; (i < OPEN_RETRIES) && (fd < 0); i++) {
		if (i) {
			debug("%s: sleeping before attempt %d to open heartbeat",
			      __func__, i);
			usleep(100000);
		}
		fd = open(file, O_RDONLY);
	}

	if (fd < 0) {
		error("%s: heartbeat open attempt failed from %s.",
		      __func__, file);
		xfree(file);
		return 0;
	}

	if (read(fd, &value, sizeof(uint64_t)) != sizeof(uint64_t)) {
		error("%s: heartbeat read failed from %s.",
		      __func__, file);
		value = 0;
	}
	if (read(fd, &inx, sizeof(uint64_t)) != sizeof(uint64_t)) {
		/* Information not available before Slurm version 18.08 */
		debug("%s: heartbeat read failed from %s.",
		      __func__, file);
	} else if (server_inx) {
		*server_inx = NTOH_uint64(inx);
	}

	close(fd);
	xfree(file);

	return (time_t) NTOH_uint64(value);
}
