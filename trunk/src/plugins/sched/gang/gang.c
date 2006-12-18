/*****************************************************************************
 *  gang.c - Gang scheduler functions.
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-226842.
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

#include "./gang.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/locks.h"

static bool thread_running = false;
static bool thread_shutdown = false;
static pthread_mutex_t thread_flag_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t gang_thread_id;

/* Global configuration parameters */
uint16_t multi_prog_level = 2;	/* maximum multi-programming level */
uint16_t slice_time = 10;	/* seconds */

static bool	_context_switch(void);
static void *	_gang_thread(void *no_data);
static int	_gang_resume_job(uint32_t jobid);
static int	_gang_suspend_job(uint32_t jobid);
static void	_parse_gang_config(void);


/* _parse_gang_config - load gang scheduler configuration parameters.
 * To read gang.conf configuration file, see _parse_wiki_config
 * code in src/wiki/msg.c, or add parameters to main config file */
static void
_parse_gang_config(void)
{
	/* Reset multi_prog_level and slice_time as needed */
}

static int
_gang_resume_job(uint32_t jobid)
{
	int slurm_rc;
	suspend_msg_t msg;

	msg.job_id = jobid;
	msg.op = RESUME_JOB;
	slurm_rc = job_suspend(&msg, 0, -1);
	if (slurm_rc != SLURM_SUCCESS)
		error("gang: Failed to resume job %u (%m)", jobid);
	else
		info("gang: Resumed job %u", jobid);
	return slurm_rc;
}

static int
_gang_suspend_job(uint32_t jobid)
{
	int slurm_rc;
	suspend_msg_t msg;

	msg.job_id = jobid;
	msg.op = SUSPEND_JOB;
	slurm_rc = job_suspend(&msg, 0, -1);
	if (slurm_rc != SLURM_SUCCESS)
		error("gang: Failed to suspend job %u (%m)", jobid);
	else
		info("gang: Suspended job %u", jobid);
	return slurm_rc;
}

/* _context_switch - This is just a very simple proof of concept sample. 
 *	The production version needs to maintain an Ousterhout matrix and 
 *	make intelligent scheduling decisions. This version supports a 
 *	multi-programming level of 2 only. In practice we'll want a 
 *	time slice much larger than 10 seconds too, but that's fine for 
 *	testing. - Moe */
static bool
_context_switch(void)
{
	bool run_scheduler = false;
	struct job_record *job_ptr;
	ListIterator job_iterator;

	if (!job_list)	/* Not yet initialized */
		return false;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (job_ptr->job_state == JOB_RUNNING)
			_gang_suspend_job(job_ptr->job_id);
		else if (job_ptr->job_state == JOB_SUSPENDED) {
			_gang_resume_job(job_ptr->job_id);
			run_scheduler = true;
		}
	}
	list_iterator_destroy(job_iterator);
	return run_scheduler;
}

/* _gang_thread - A pthread to periodically perform gang scheduler context 
 *	switches. */
static void *
_gang_thread(void *no_data)
{
	bool run_scheduler;

	/* Locks: write job and node info */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK };

	while (!thread_shutdown) {
		lock_slurmctld(job_write_lock);
		run_scheduler = _context_switch();
		unlock_slurmctld(job_write_lock);
		if (run_scheduler)
			schedule();	/* has own locking */
		sleep(slice_time);
	}
	pthread_exit((void *) 0);

	return NULL;
}

/*
 * spawn_gang_thread - Create a pthread to perform gang scheduler actions
 *
 * NOTE: Create only one pthread in any plugin. Some systems leak memory on 
 * each pthread_create from within a plugin
 */
extern int
spawn_gang_thread(void)
{
	pthread_attr_t thread_attr_msg;

	pthread_mutex_lock( &thread_flag_mutex );
	if (thread_running) {
		error("gang thread already running, not starting another");
		pthread_mutex_unlock(&thread_flag_mutex);
		return SLURM_ERROR;
	}

	_parse_gang_config();
	slurm_attr_init(&thread_attr_msg);
	if (pthread_create(&gang_thread_id, &thread_attr_msg, 
			_gang_thread, NULL))
		fatal("pthread_create %m");

	slurm_attr_destroy(&thread_attr_msg);
	thread_running = true;
	pthread_mutex_unlock(&thread_flag_mutex);
	return SLURM_SUCCESS;
}

extern void
term_gang_thread(void)
{
	pthread_mutex_lock(&thread_flag_mutex);
	if (thread_running) {
		int i;
		thread_shutdown = true;
		for (i=0; i<4; i++) {
			if (pthread_cancel(gang_thread_id)) {
				gang_thread_id = 0;
				break;
			}
			usleep(1000);
		}
		if (gang_thread_id)
			error("Cound not kill gang pthread");
	}
	pthread_mutex_unlock(&thread_flag_mutex);
}

