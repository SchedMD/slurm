/*****************************************************************************\
 *  sicp.c - Inter-cluster job management functions
 *****************************************************************************
 *  Copyright (C) SchedMD LLC (http://www.schedmd.com).
 *  Written by Morris Jette
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>

#include "src/common/macros.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/sicp.h"
#include "src/slurmctld/slurmctld.h"

List sicp_job_list;

static bool sicp_stop = false;
static pthread_t sicp_thread = 0;
static pthread_mutex_t sicp_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  sicp_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t thread_lock = PTHREAD_MUTEX_INITIALIZER;
static int sicp_interval = 10;

static void _my_sleep(int add_secs);

static void _my_sleep(int add_secs)
{
	struct timespec ts = {0, 0};
	struct timeval  tv = {0, 0};

	if (gettimeofday(&tv, NULL)) {		/* Some error */
		sleep(1);
		return;
	}

	ts.tv_sec  = tv.tv_sec + add_secs;
	ts.tv_nsec = tv.tv_usec * 1000;
	pthread_mutex_lock(&sicp_lock);
	if (!sicp_stop)
		pthread_cond_timedwait(&sicp_cond, &sicp_lock, &ts);
	pthread_mutex_unlock(&sicp_lock);
}

extern void *_sicp_agent(void *args)
{
	static time_t last_sicp_time = 0;
	time_t now;
	double wait_time;

	while (!sicp_stop) {
		_my_sleep(1);
		if (sicp_stop)
			break;

		now = time(NULL);
		wait_time = difftime(now, last_sicp_time);
		if (wait_time < sicp_interval)
			continue;
		last_sicp_time = now;

		/* Load SICP job state from evey cluster here */
		//info("SICP sync here");
	}
	return NULL;
}

/* Start a thread to poll other clusters for inter-cluster job status */
extern void sicp_init(void)
{
	pthread_attr_t attr;

	pthread_mutex_lock(&thread_lock);
	if (sicp_thread) {
		error("%s: sicp thread already running", __func__);
		pthread_mutex_unlock(&thread_lock);
	}

	sicp_stop = false;
	slurm_attr_init(&attr);
	/* Since we do a join on thread later, don't make it detached */
	if (pthread_create(&sicp_thread, &attr, _sicp_agent, NULL))
		error("Unable to start power thread: %m");
	slurm_attr_destroy(&attr);
	pthread_mutex_unlock(&thread_lock);
}

/* Shutdown the inter-cluster job status thread */
extern void sicp_fini(void)
{
	pthread_mutex_lock(&thread_lock);
	pthread_mutex_lock(&sicp_lock);
	sicp_stop = true;
	pthread_cond_signal(&sicp_cond);
	pthread_mutex_unlock(&sicp_lock);

	pthread_join(sicp_thread, NULL);
	sicp_thread = 0;
	pthread_mutex_unlock(&thread_lock);
}
