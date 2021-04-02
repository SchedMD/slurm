/***************************************************************************** \
 *  task_cgroup_memory.c - memory cgroup subsystem for task/cgroup
 *****************************************************************************
 *  Copyright (C) 2009 CEA/DAM/DIF
 *  Written by Matthieu Hautreux <matthieu.hautreux@cea.fr>
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

#define _GNU_SOURCE		/* For POLLRDHUP, O_CLOEXEC on older glibc */
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>		/* getenv */
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"
#include "slurm/slurm.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"
#include "src/slurmd/slurmd/slurmd.h"

#include "src/common/xstring.h"

#include "task_cgroup.h"

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__)
#define POLLRDHUP POLLHUP
#else
#include <sys/eventfd.h>
#endif

typedef struct {
	stepd_step_rec_t *job;
} task_memory_create_callback_t;

extern slurmd_conf_t *conf;

static char user_cgroup_path[PATH_MAX];
static char job_cgroup_path[PATH_MAX];
static char jobstep_cgroup_path[PATH_MAX];

static xcgroup_ns_t memory_ns;

static xcgroup_t user_memory_cg;
static xcgroup_t job_memory_cg;
static xcgroup_t step_memory_cg;

static bool constrain_ram_space;
static bool constrain_kmem_space;
static bool constrain_swap_space;

static float allowed_ram_space;   /* Allowed RAM in percent       */
static float allowed_swap_space;  /* Allowed Swap percent         */
static float allowed_kmem_space;  /* Allowed Kmem number          */
static float max_kmem_percent;	   /* Allowed Kernel memory percent*/

static uint64_t max_kmem;       /* Upper bound for kmem.limit_in_bytes  */
static uint64_t max_ram;        /* Upper bound for memory.limit_in_bytes  */
static uint64_t max_swap;       /* Upper bound for swap                   */
static uint64_t totalram;       /* Total real memory available on node    */
static uint64_t min_ram_space;  /* Don't constrain RAM below this value   */
static uint64_t min_kmem_space; /* Don't constrain Kernel mem below       */

/*
 * Cgroup v1 control files to detect OOM events.
 * https://www.kernel.org/doc/Documentation/cgroup-v1/memory.txt
 */
#define OOM_CONTROL "memory.oom_control"
#define EVENT_CONTROL "cgroup.event_control"
#define STOP_OOM 0x987987987

/*
 * If we plan to support cgroup v2, we should monitor 'memory.events' file
 * modified events. That would mean that any of the available entries changed
 * its value upon notification. Entries include: low, high, max, oom, oom_kill.
 * https://www.kernel.org/doc/Documentation/cgroup-v2.txt
 */

typedef struct {
	int cfd;	/* control file fd. */
	int efd;	/* event file fd. */
	int event_fd;	/* eventfd fd. */
} oom_event_args_t;

static bool oom_thread_created = false;
static uint64_t oom_kill_count = 0;
static int oom_pipe[2] = { -1, -1 };
static pthread_t oom_thread;
static pthread_mutex_t oom_mutex = PTHREAD_MUTEX_INITIALIZER;

static uint64_t percent_in_bytes (uint64_t mb, float percent)
{
	return ((mb * 1024 * 1024) * (percent / 100.0));
}

extern int task_cgroup_memory_init(void)
{
	xcgroup_t memory_cg;
	bool set_swappiness;
	slurm_cgroup_conf_t *slurm_cgroup_conf;

	/* initialize user/job/jobstep cgroup relative paths */
	user_cgroup_path[0]='\0';
	job_cgroup_path[0]='\0';
	jobstep_cgroup_path[0]='\0';

	/* initialize memory cgroup namespace */
	if (xcgroup_ns_create(&memory_ns, "", "memory") != SLURM_SUCCESS) {
		error("unable to create memory namespace. "
			"You may need to set the Linux kernel option "
			"cgroup_enable=memory (and reboot), or disable "
			"ConstrainRAMSpace in cgroup.conf.");
		return SLURM_ERROR;
	}

	/* Enable memory.use_hierarchy in the root of the cgroup.
	 */
	if (xcgroup_create(&memory_ns, &memory_cg, "", 0, 0)
	    != SLURM_SUCCESS) {
		error("unable to create root memory cgroup: %m");
		return SLURM_ERROR;
	}
	xcgroup_set_param(&memory_cg, "memory.use_hierarchy","1");

	/* read cgroup configuration */
	slurm_mutex_lock(&xcgroup_config_read_mutex);
	slurm_cgroup_conf = xcgroup_get_slurm_cgroup_conf();

	set_swappiness = (slurm_cgroup_conf->memory_swappiness != NO_VAL64);
	if (set_swappiness)
		xcgroup_set_uint64_param(&memory_cg, "memory.swappiness",
					 slurm_cgroup_conf->memory_swappiness);

	xcgroup_destroy(&memory_cg);

	constrain_kmem_space = slurm_cgroup_conf->constrain_kmem_space;
	constrain_ram_space = slurm_cgroup_conf->constrain_ram_space;
	constrain_swap_space = slurm_cgroup_conf->constrain_swap_space;

	/*
	 * as the swap space threshold will be configured with a
	 * mem+swp parameter value, if RAM space is not monitored,
	 * set allowed RAM space to 100% of the job requested memory.
	 * It will help to construct the mem+swp value that will be
	 * used for both mem and mem+swp limit during memcg creation.
	 */
	if ( constrain_ram_space )
		allowed_ram_space = slurm_cgroup_conf->allowed_ram_space;
	else
		allowed_ram_space = 100.0;

	allowed_kmem_space = slurm_cgroup_conf->allowed_kmem_space;
	allowed_swap_space = slurm_cgroup_conf->allowed_swap_space;

	if ((totalram = (uint64_t) conf->real_memory_size) == 0)
		error ("Unable to get RealMemory size");

	max_kmem = percent_in_bytes(totalram, slurm_cgroup_conf->max_kmem_percent);
	max_ram = percent_in_bytes(totalram, slurm_cgroup_conf->max_ram_percent);
	max_swap = percent_in_bytes(totalram, slurm_cgroup_conf->max_swap_percent);
	max_swap += max_ram;
	min_ram_space = slurm_cgroup_conf->min_ram_space * 1024 * 1024;
	max_kmem_percent = slurm_cgroup_conf->max_kmem_percent;
	min_kmem_space = slurm_cgroup_conf->min_kmem_space * 1024 * 1024;

	debug("task/cgroup/memory: total:%"PRIu64"M allowed:%.4g%%(%s), "
	      "swap:%.4g%%(%s), max:%.4g%%(%"PRIu64"M) "
	      "max+swap:%.4g%%(%"PRIu64"M) min:%"PRIu64"M "
	      "kmem:%.4g%%(%"PRIu64"M %s) min:%"PRIu64"M "
	      "swappiness:%"PRIu64"(%s)",

	      totalram, allowed_ram_space,
	      constrain_ram_space ? "enforced" : "permissive",

	      allowed_swap_space,
	      constrain_swap_space ? "enforced" : "permissive",
	      slurm_cgroup_conf->max_ram_percent,
	      (uint64_t) (max_ram / (1024 * 1024)),

	      slurm_cgroup_conf->max_swap_percent,
	      (uint64_t) (max_swap / (1024 * 1024)),
	      slurm_cgroup_conf->min_ram_space,

	      slurm_cgroup_conf->max_kmem_percent,
	      (uint64_t) (max_kmem / (1024 * 1024)),
	      constrain_kmem_space ? "enforced" : "permissive",
	      slurm_cgroup_conf->min_kmem_space,

	      set_swappiness ? slurm_cgroup_conf->memory_swappiness : 0,
	      set_swappiness ? "set" : "unset");

        /*
         *  Warning: OOM Killer must be disabled for slurmstepd
         *  or it would be destroyed if the application use
         *  more memory than permitted
         *
         *  If an env value is already set for slurmstepd
         *  OOM killer behavior, keep it, otherwise set the
         *  -1000 value, wich means do not let OOM killer kill it
         *
         *  FYI, setting "export SLURMSTEPD_OOM_ADJ=-1000"
         *  in /etc/sysconfig/slurm would be the same
         */
        setenv("SLURMSTEPD_OOM_ADJ", "-1000", 0);
	slurm_mutex_unlock(&xcgroup_config_read_mutex);

	return SLURM_SUCCESS;
}

extern int task_cgroup_memory_fini(slurm_cgroup_conf_t *slurm_cgroup_conf)
{
	xcgroup_t memory_cg;

	if (user_cgroup_path[0] == '\0' ||
	     job_cgroup_path[0] == '\0' ||
	     jobstep_cgroup_path[0] == '\0') {
		xcgroup_ns_destroy(&memory_ns);
		return SLURM_SUCCESS;
	}
	/*
	 * Lock the root memcg and try to remove the different memcgs.
	 * The reason why we are locking here is that if a concurrent
	 * step is in the process of being executed, he could try to
	 * create the step memcg just after we remove the job memcg,
	 * resulting in a failure.
	 * First, delete step memcg as all the tasks have now exited.
	 * Then, try to remove the job memcg.
	 * If it fails, it is due to the fact that it is still in use by an
	 * other running step.
	 * After that, try to remove the user memcg. If it fails, it is due
	 * to jobs that are still running for the same user on the node or
	 * because of tasks attached directly to the user cg by an other
	 * component (PAM).
	 * For now, do not try to detect if only externally attached tasks
	 * are present to see if they can be be moved to an orhpan memcg.
	 * That could be done in the future, if it is necessary.
	 */
	if (xcgroup_create(&memory_ns,&memory_cg,"",0,0) == SLURM_SUCCESS) {
		if (xcgroup_lock(&memory_cg) == SLURM_SUCCESS) {
			if (xcgroup_delete(&step_memory_cg) != SLURM_SUCCESS)
				debug2("unable to remove step "
				       "memcg : %m");
			if (xcgroup_delete(&job_memory_cg) != SLURM_SUCCESS)
				debug2("not removing "
				       "job memcg : %m");
			if (xcgroup_delete(&user_memory_cg) != SLURM_SUCCESS)
				debug2("not removing "
				       "user memcg : %m");
			xcgroup_unlock(&memory_cg);
		} else
			error("unable to lock root memcg : %m");
		xcgroup_destroy(&memory_cg);
	} else
		error("unable to create root memcg : %m");

	xcgroup_destroy(&user_memory_cg);
	xcgroup_destroy(&job_memory_cg);
	xcgroup_destroy(&step_memory_cg);

	user_cgroup_path[0]='\0';
	job_cgroup_path[0]='\0';
	jobstep_cgroup_path[0]='\0';

	xcgroup_ns_destroy(&memory_ns);

	return SLURM_SUCCESS;
}

/*
 *  Return configured memory limit in bytes given a memory limit in MB.
 */
static uint64_t mem_limit_in_bytes (uint64_t mem, bool with_allowed)
{
	/*
	 *  If mem == 0 then assume there was no Slurm limit imposed
	 *   on the amount of memory for job or step. Use the total
	 *   amount of available RAM instead.
	 */
	if (mem == 0)
		mem = totalram * 1024 * 1024;
	else {
		if (with_allowed)
			mem = percent_in_bytes (mem, allowed_ram_space);
		else
			mem = percent_in_bytes(mem, 100.0);
	}
	if (mem < min_ram_space)
		return (min_ram_space);
	if (mem > max_ram)
		return (max_ram);
	return (mem);
}

/*
 *  Return configured swap limit in bytes given a memory limit in MB.
 *
 *   Swap limit is calculated as:
 *
 *     mem_limit_in_bytes + (configured_swap_percent * allocated_mem_in_bytes)
 */
static uint64_t swap_limit_in_bytes (uint64_t mem)
{
	uint64_t swap;
	/*
	 *  If mem == 0 assume "unlimited" and use totalram.
	 */
	swap = percent_in_bytes (mem ? mem : totalram, allowed_swap_space);
	mem = mem_limit_in_bytes (mem, true) + swap;
	if (mem < min_ram_space)
		return (min_ram_space);
	if (mem > max_swap)
		return (max_swap);
	return (mem);
}

/*
 * Return kmem memory limit in bytes given a memory limit in bytes.
 * If Kmem space is disabled, it set to max percent of its RAM usage.
 */
static uint64_t kmem_limit_in_bytes (uint64_t mlb)
{
	uint64_t totalKmem = mlb * (max_kmem_percent / 100.0);

	if ( allowed_kmem_space < 0 ) {	/* Initial value */
		if ( mlb > totalKmem )
			return totalKmem;
		if ( mlb < min_kmem_space )
			return min_kmem_space;
		return mlb;
	}
	if ( allowed_kmem_space > totalKmem )
		return totalKmem;
	if ( allowed_kmem_space < min_kmem_space )
		return min_kmem_space;
	return allowed_kmem_space;
}

static int _memcg_initialize(xcgroup_ns_t *ns, xcgroup_t *cg,
			     char *path, uint64_t mem_limit)
{
	uint64_t mlb = mem_limit_in_bytes (mem_limit, true);
	uint64_t mlb_soft = mem_limit_in_bytes(mem_limit, false);
	uint64_t mls = swap_limit_in_bytes  (mem_limit);

	if (mlb_soft > mlb) {
		/*
		 * NOTE: It is recommended to set the soft limit always below
		 * the hard limit, otherwise the hard one will take precedence.
		 */
		debug2("Setting memory.soft_limit_in_bytes (%"PRIu64" bytes) to the same value as memory.limit_in_bytes (%"PRIu64" bytes) for cgroup: %s",
		       mlb_soft, mlb, path);
		mlb_soft = mlb;
	}

	xcgroup_set_param (cg, "memory.use_hierarchy", "1");

	/* when RAM space has not to be constrained and we are here, it
	 * means that only Swap space has to be constrained. Thus set
	 * RAM space limit to the mem+swap limit too */
	if ( ! constrain_ram_space )
		mlb = mls;
	xcgroup_set_uint64_param (cg, "memory.limit_in_bytes", mlb);

	/* Set the soft limit to the allocated RAM.  */
	xcgroup_set_uint64_param(cg, "memory.soft_limit_in_bytes", mlb_soft);

	/*
	 * Also constrain kernel memory (if available).
	 * See https://lwn.net/Articles/516529/
	 */
	if (constrain_kmem_space)
		xcgroup_set_uint64_param (cg, "memory.kmem.limit_in_bytes",
					  kmem_limit_in_bytes(mlb));

	/* this limit has to be set only if ConstrainSwapSpace is set to yes */
	if ( constrain_swap_space ) {
		xcgroup_set_uint64_param (cg, "memory.memsw.limit_in_bytes",
					  mls);
		info ("%s: alloc=%luMB mem.limit=%luMB "
		      "memsw.limit=%luMB", path,
		      (unsigned long) mem_limit,
		      (unsigned long) mlb/(1024*1024),
		      (unsigned long) mls/(1024*1024));
	} else {
		info ("%s: alloc=%luMB mem.limit=%luMB "
		      "memsw.limit=unlimited", path,
		      (unsigned long) mem_limit,
		      (unsigned long) mlb/(1024*1024));
	}

	return 0;
}

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__)

static int _register_oom_notifications(char *ignored)
{
	error("OOM notification does not work on FreeBSD, NetBSD, or macOS");

	return SLURM_ERROR;
}

#else

static int _read_fd(int fd, uint64_t *buf)
{
	int rc = SLURM_ERROR;
	size_t len = sizeof(uint64_t);
	uint64_t *buf_ptr = buf;
	ssize_t nread;

	while (len > 0 && (nread = read(fd, buf_ptr, len)) != 0) {
		if (nread == -1) {
			if (errno == EINTR)
				continue;
			error("read(): %m");
			break;
		}
		len -= nread;
		buf_ptr += nread;
	}

	if (len == 0)
		rc = SLURM_SUCCESS;

	return rc;
}

static void *_oom_event_monitor(void *x)
{
	oom_event_args_t *args = (oom_event_args_t *) x;
	int ret = -1;
	uint64_t res;
	struct pollfd fds[2];

	debug("started.");

	/*
	 * POLLPRI should only meaningful for event_fd, since according to the
	 * poll() man page it may indicate "cgroup.events" file modified.
	 *
	 * POLLRDHUP should only be meaningful for oom_pipe[0], since it refers
	 * to stream socket peer closed connection.
	 *
	 * POLLHUP is ignored in events member, and should be set by the Kernel
	 * in revents even if not defined in events.
	 *
	 */
	fds[0].fd = args->event_fd;
	fds[0].events = POLLIN | POLLPRI;

	fds[1].fd = oom_pipe[0];
	fds[1].events = POLLIN | POLLRDHUP;

	/*
	 * Poll event_fd for oom_kill events plus oom_pipe[0] for stop msg.
	 * Specifying a negative value in timeout means an infinite timeout.
	 */
	while (1) {
		ret = poll(fds, 2, -1);

		if (ret == -1) {
			/* Error. */
			if (errno == EINTR)
				continue;

			error("poll(): %m");
			break;
		} else if (ret == 0) {
			/* Should not happen since infinite timeout. */
			error("poll() timeout.");
			break;
		} else if (ret > 0) {
			if (fds[0].revents & (POLLIN | POLLPRI)) {
				/* event_fd readable. */
				res = 0;
				ret = _read_fd(args->event_fd, &res);
				if (ret == SLURM_SUCCESS) {
					slurm_mutex_lock(&oom_mutex);
					debug3("res: %"PRIu64"", res);
					oom_kill_count += res;
					debug2("oom-kill event count: %"PRIu64"",
					       oom_kill_count);
					slurm_mutex_unlock(&oom_mutex);
				} else
					error("cannot read oom-kill counts.");
			} else if (fds[0].revents & (POLLRDHUP | POLLERR |
						     POLLHUP | POLLNVAL)) {
				error("problem with event_fd");
				break;
			}

			if (fds[1].revents & POLLIN) {
				/* oom_pipe[0] readable. */
				res = 0;
				ret = _read_fd(oom_pipe[0], &res);
				if (ret == SLURM_SUCCESS && res == STOP_OOM) {
					/* Read stop msg. */
					debug2("stop msg read.");
					break;
				}
			} else if (fds[1].revents &
				   (POLLRDHUP | POLLERR | POLLHUP | POLLNVAL)) {
				error("problem with oom_pipe[0]");
				break;
			}
		}
	}

	slurm_mutex_lock(&oom_mutex);
	if (!oom_kill_count)
		debug("No oom events detected.");
	slurm_mutex_unlock(&oom_mutex);

	if ((args->event_fd != -1) && (close(args->event_fd) == -1))
		error("close(event_fd): %m");
	if ((args->efd != -1) && (close(args->efd) == -1))
		error("close(efd): %m");
	if ((args->cfd != -1) && (close(args->cfd) == -1))
		error("close(cfd): %m");
	if ((oom_pipe[0] >= 0) && (close(oom_pipe[0]) == -1))
		error("close(oom_pipe[0]): %m");
	xfree(args);

	debug("stopping.");

	pthread_exit((void *) 0);
}

/*
 * Code based on linux tools/cgroup/cgroup_event_listener.c with adapted
 * modifications for Slurm logic and needs.
 */
static int _register_oom_notifications(char * cgpath)
{
	char *control_file = NULL, *event_file = NULL, *line = NULL;
	int rc = SLURM_SUCCESS, event_fd = -1, cfd = -1, efd = -1;
	oom_event_args_t *event_args;

	if ((cgpath == NULL) || (cgpath[0] == '\0')) {
		error("cgroup path is NULL or empty.");
		rc = SLURM_ERROR;
		goto fini;
	}

	xstrfmtcat(control_file, "%s/%s", cgpath, OOM_CONTROL);

	if ((cfd = open(control_file, O_RDONLY | O_CLOEXEC)) == -1) {
		error("Cannot open %s: %m", control_file);
		rc = SLURM_ERROR;
		goto fini;
	}

	xstrfmtcat(event_file, "%s/%s", cgpath, EVENT_CONTROL);

	if ((efd = open(event_file, O_WRONLY | O_CLOEXEC)) == -1) {
		error("Cannot open %s: %m", event_file);
		rc = SLURM_ERROR;
		goto fini;
	}

	if ((event_fd = eventfd(0, EFD_CLOEXEC)) == -1) {
		error("eventfd: %m");
		rc = SLURM_ERROR;
		goto fini;
	}

	xstrfmtcat(line, "%d %d", event_fd, cfd);

	oom_kill_count = 0;

	if (write(efd, line, strlen(line) + 1) == -1) {
		error("Cannot write to %s", event_file);
		rc = SLURM_ERROR;
		goto fini;
	}

	if (pipe2(oom_pipe, O_CLOEXEC) == -1) {
		error("pipe(): %m");
		rc = SLURM_ERROR;
		goto fini;
	}

	/*
	 * Monitoring thread should be responsible for closing the fd's and
	 * freeing the oom_event_args_t struct and members.
	 */
	event_args = xmalloc(sizeof(oom_event_args_t));
	event_args->cfd = cfd;
	event_args->efd = efd;
	event_args->event_fd = event_fd;

	slurm_mutex_init(&oom_mutex);
	slurm_thread_create(&oom_thread, _oom_event_monitor, event_args);
	oom_thread_created = true;

fini:
	xfree(line);
	if (!oom_thread_created) {
		if ((event_fd != -1) && (close(event_fd) == -1))
			error("close: %m");
		if ((efd != -1) && (close(efd) == -1))
			error("close: %m");
		if ((cfd != -1) && (close(cfd) == -1))
			error("close: %m");
		if ((oom_pipe[0] != -1) && (close(oom_pipe[0]) == -1))
			error("close oom_pipe[0]: %m");
		if ((oom_pipe[1] != -1) && (close(oom_pipe[1]) == -1))
			error("close oom_pipe[1]: %m");
	}
	xfree(event_file);
	xfree(control_file);

	return rc;
}
#endif

static int _cgroup_create_callback(const char *calling_func,
				   xcgroup_ns_t *ns,
				   void *callback_arg)
{
	task_memory_create_callback_t *cgroup_callback =
		(task_memory_create_callback_t *)callback_arg;

	stepd_step_rec_t *job = cgroup_callback->job;

	if (xcgroup_set_param(&user_memory_cg, "memory.use_hierarchy", "1") !=
	    SLURM_SUCCESS) {
		error("%s: unable to ask for hierarchical accounting of user memcg '%s'",
		      calling_func, user_memory_cg.path);
		xcgroup_destroy (&user_memory_cg);
		return SLURM_ERROR;
	}

	/*
	 * set the associated memory limits.
	 */
	if (_memcg_initialize(&memory_ns, &job_memory_cg, job_cgroup_path,
			      job->job_mem) < 0) {
		xcgroup_destroy(&user_memory_cg);
		return SLURM_ERROR;
	}

	/*
	 * set the associated memory limits for the step.
	 */
	if (_memcg_initialize(&memory_ns, &step_memory_cg, jobstep_cgroup_path,
			      job->step_mem) < 0) {
		xcgroup_destroy(&user_memory_cg);
		xcgroup_destroy(&job_memory_cg);
		return SLURM_ERROR;
	}

	if (_register_oom_notifications(step_memory_cg.path) == SLURM_ERROR) {
		error("%s: Unable to register OOM notifications for %s",
		      calling_func, step_memory_cg.path);
	}

	return SLURM_SUCCESS;
}

extern int task_cgroup_memory_create(stepd_step_rec_t *job)
{
	task_memory_create_callback_t cgroup_callback = {
		.job = job,
	};

	return xcgroup_create_hierarchy(__func__,
					job,
					&memory_ns,
					&job_memory_cg,
					&step_memory_cg,
					&user_memory_cg,
					job_cgroup_path,
					jobstep_cgroup_path,
					user_cgroup_path,
					_cgroup_create_callback,
					&cgroup_callback);
}

extern int task_cgroup_memory_attach_task(stepd_step_rec_t *job, pid_t pid)
{
	int fstatus = SLURM_ERROR;

	if (xcgroup_add_pids(&step_memory_cg, &pid, 1) != SLURM_SUCCESS) {
		error("unable to add task[pid=%u] to "
		      "memory cg '%s'",pid,step_memory_cg.path);
		fstatus = SLURM_ERROR;
	} else
		fstatus = SLURM_SUCCESS;

	return fstatus;
}

/* return 1 if failcnt file exists and is > 0 */
int failcnt_non_zero(xcgroup_t* cg, char* param)
{
	int fstatus = SLURM_ERROR;
	uint64_t value;

	fstatus = xcgroup_get_uint64_param(cg, param, &value);

	if (fstatus != SLURM_SUCCESS) {
		debug2("unable to read '%s' from '%s'", param, cg->path);
		return 0;
	}

	return value > 0;
}

extern int task_cgroup_memory_check_oom(stepd_step_rec_t *job)
{
	xcgroup_t memory_cg;
	int rc = SLURM_SUCCESS;
	uint64_t stop_msg;
	ssize_t ret;

	if (xcgroup_create(&memory_ns, &memory_cg, "", 0, 0)
	    != SLURM_SUCCESS) {
		error("task/cgroup task_cgroup_memory_check_oom: unable to create root memcg : %m");
		goto fail_xcgroup_create;
	}

	if (xcgroup_lock(&memory_cg) != SLURM_SUCCESS) {
		error("task/cgroup task_cgroup_memory_check_oom: task_cgroup_memory_check_oom: unable to lock root memcg : %m");
		goto fail_xcgroup_lock;
	}

	if (failcnt_non_zero(&step_memory_cg, "memory.memsw.failcnt")) {
		/*
		 * reports the number of times that the memory plus swap space
		 * limit has reached the value in memory.memsw.limit_in_bytes.
		 */
		info("%ps hit memory+swap limit at least once during execution. This may or may not result in some failure.",
		     &job->step_id);
	} else if (failcnt_non_zero(&step_memory_cg, "memory.failcnt")) {
		/*
		 * reports the number of times that the memory limit has reached
		 * the value set in memory.limit_in_bytes.
		 */
		info("%ps hit memory limit at least once during execution. This may or may not result in some failure.",
		     &job->step_id);
	}

	if (failcnt_non_zero(&job_memory_cg, "memory.memsw.failcnt")) {
		info("%ps hit memory+swap limit at least once during execution. This may or may not result in some failure.",
		     &job->step_id);
	} else if (failcnt_non_zero(&job_memory_cg, "memory.failcnt")) {
		info("%ps hit memory limit at least once during execution. This may or may not result in some failure.",
		     &job->step_id);
	}

	if (!oom_thread_created) {
		debug("OOM events were not monitored for %ps",
		      &job->step_id);
		goto fail_oom_results;
	}

	/*
	 * oom_thread created, but could have finished before we attempt
	 * to send the stop msg. If it finished, oom_thread should had
	 * closed the read endpoint of oom_pipe.
	 */
	stop_msg = STOP_OOM;
	while (1) {
		ret = write(oom_pipe[1], &stop_msg, sizeof(stop_msg));
		if (ret == -1) {
			if (errno == EINTR)
				continue;
			debug("oom stop msg write() failed: %m");
		} else if (ret == 0)
			debug("oom stop msg nothing written: %m");
		else if (ret == sizeof(stop_msg))
			debug2("oom stop msg write success.");
		else
			debug("oom stop msg not fully written.");
		break;
	}

	debug2("attempt to join oom_thread.");
	if (oom_thread && pthread_join(oom_thread, NULL) != 0)
		error("pthread_join(): %m");

	slurm_mutex_lock(&oom_mutex);
	if (oom_kill_count) {
		error("Detected %"PRIu64" oom-kill event(s) in %ps cgroup. Some of your processes may have been killed by the cgroup out-of-memory handler.",
		      oom_kill_count, &job->step_id);
		rc = ENOMEM;
	}
	slurm_mutex_unlock(&oom_mutex);

fail_oom_results:
	if ((oom_pipe[1] != -1) && (close(oom_pipe[1]) == -1)) {
		error("close() failed on oom_pipe[1] fd, %ps: %m",
		      &job->step_id);
	}

	slurm_mutex_destroy(&oom_mutex);

	xcgroup_unlock(&memory_cg);

fail_xcgroup_lock:
	xcgroup_destroy(&memory_cg);

fail_xcgroup_create:

	return rc;
}

extern int task_cgroup_memory_add_pid(pid_t pid)
{
	return xcgroup_add_pids(&step_memory_cg, &pid, 1);
}
