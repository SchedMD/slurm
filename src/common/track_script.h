/*****************************************************************************\
 *  track_script.h - Track scripts running asynchronously
 *****************************************************************************
 *  Copyright (C) 2019 SchedMD LLC.
 *  Written by Felip Moll <felip@schedmd.com>
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

#ifndef __TRACK_SCRIPT_H__
#define __TRACK_SCRIPT_H__

#include "config.h"

#include <inttypes.h>

typedef struct {
	uint32_t job_id;
	pid_t cpid;
	pthread_t tid;
	pthread_mutex_t timer_mutex;
	pthread_cond_t timer_cond;
} track_script_rec_t;

/* Init track_script system */
extern void track_script_init(void);

/* Finish track_script system */
extern void track_script_fini(void);

/* Flush all scripts from track_script system */
extern void track_script_flush(void);

/* Flush tracked scripts for given job_id */
extern void track_script_flush_job(uint32_t job_id);

/*
 * create, initialize, and add a track_script_rec_t to the track_script system.
 * IN job_id - Job id we are running this script under
 * IN cpid - If non-zero this track_script_rec_t will implicitly call
 *           track_script_add
 * IN tid - thread id of thread we are tracking
 * Returns track_script_rec_t
 */
extern track_script_rec_t *track_script_rec_add(
	uint32_t job_id, pid_t cpid, pthread_t tid);

/*
 * Signal script thread to end
 */
extern bool track_script_broadcast(track_script_rec_t *track_script_rec,
				   int status);

/* Remove this thread from the track_script system */
extern void track_script_remove(pthread_t tid);

/* Set the thread's cpid (script pid) or clear with 0 */
extern void track_script_reset_cpid(pthread_t tid, pid_t cpid);

#endif // __TRACK_SCRIPT_H__
