/*****************************************************************************\
 *  jobacct_gather_cgroup.c - slurm job accounting gather plugin for cgroup.
 *****************************************************************************
 *  Copyright (C) 2011 Bull.
 *  Written by Martin Perry, <martin.perry@bull.com>, who borrowed heavily
 *  from other parts of SLURM
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
 *
 *  This file is patterned after jobcomp_linux.c, written by Morris Jette and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#include <fcntl.h>
#include <signal.h>
#include "src/common/slurm_xlator.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_acct_gather_energy.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/common/xstring.h"
#include "src/plugins/jobacct_gather/cgroup/jobacct_gather_cgroup.h"
#include "src/slurmd/common/proctrack.h"
#include "../common/common_jag.h"

#define _DEBUG 0

/* These are defined here so when we link with something other than
 * the slurmd we will have these symbols defined.  They will get
 * overwritten when linking with the slurmd.
 */
#if defined (__APPLE__)
slurmd_conf_t *conf __attribute__((weak_import));
int bg_recover __attribute__((weak_import)) = NOT_FROM_CONTROLLER;
#else
slurmd_conf_t *conf;
int bg_recover = NOT_FROM_CONTROLLER;
#endif


/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "jobacct" for SLURM job completion logging) and <method>
 * is a description of how this plugin satisfies that application.  SLURM will
 * only load job completion logging plugins if the plugin_type string has a
 * prefix of "jobacct/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum version for their plugins as the job accounting API
 * matures.
 */
const char plugin_name[] = "Job accounting gather cgroup plugin";
const char plugin_type[] = "jobacct_gather/cgroup";
const uint32_t plugin_version = 200;

/* Other useful declarations */
static slurm_cgroup_conf_t slurm_cgroup_conf;

static void _prec_extra(jag_prec_t *prec)
{
	unsigned long utime, stime, total_rss, total_pgpgin;
	char *cpu_time = NULL, *memory_stat = NULL, *ptr;
	size_t cpu_time_size = 0, memory_stat_size = 0;

	//DEF_TIMERS;
	//START_TIMER;
	/* info("before"); */
	/* print_jag_prec(prec); */
	xcgroup_get_param(&task_cpuacct_cg, "cpuacct.stat",
			  &cpu_time, &cpu_time_size);
	if (cpu_time == NULL) {
		debug2("%s: failed to collect cpuacct.stat pid %d ppid %d",
		       __func__, prec->pid, prec->ppid);
	} else {
		sscanf(cpu_time, "%*s %lu %*s %lu", &utime, &stime);
		prec->usec = utime;
		prec->ssec = stime;
	}

	xcgroup_get_param(&task_memory_cg, "memory.stat",
			  &memory_stat, &memory_stat_size);
	if (memory_stat == NULL) {
		debug2("%s: failed to collect memory.stat  pid %d ppid %d",
		       __func__, prec->pid, prec->ppid);
	} else {
		/* This number represents the amount of "dirty" private memory
		   used by the cgroup.  From our experience this is slightly
		   different than what proc presents, but is probably more
		   accurate on what the user is actually using.
		*/
		ptr = strstr(memory_stat, "total_rss");
		sscanf(ptr, "total_rss %lu", &total_rss);
		prec->rss = total_rss / 1024; /* convert from bytes to KB */

		/* total_pgmajfault is what is reported in proc, so we use
		 * the same thing here. */
		if ((ptr = strstr(memory_stat, "total_pgmajfault"))) {
			sscanf(ptr, "total_pgmajfault %lu", &total_pgpgin);
			prec->pages = total_pgpgin;
		}
	}

	xfree(cpu_time);
	xfree(memory_stat);

	/* FIXME: Enable when kernel support ready.
	 *
	 * "Read" and "Write" from blkio.throttle.io_service_bytes are
	 * counts of bytes read and written for physical disk I/Os only.
	 * These counts do not include disk I/Os satisfied from cache.
	 */
	/* int dev_major; */
	/* uint64_t read_bytes, write_bytes, tot_read, tot_write; */
	/* char *blkio_bytes, *next_device; */
	/* size_t blkio_bytes_size; */
	/* xcgroup_get_param(&task_blkio_cg, "blkio.throttle.io_service_bytes", */
	/*                   &blkio_bytes, &blkio_bytes_size); */
	/* next_device = blkio_bytes; */
	/* tot_read = tot_write = 0; */
	/* while ((sscanf(next_device, "%d:", &dev_major)) > 0) { */
	/* 	if ((dev_major > 239) && (dev_major < 255)) */
	/* 		/\* skip experimental device codes *\/ */
	/* 		continue; */
	/* 	next_device = strstr(next_device, "Read"); */
	/* 	sscanf(next_device, "%*s %"PRIu64"", &read_bytes); */
	/* 	next_device = strstr(next_device, "Write"); */
	/* 	sscanf(next_device, "%*s %"PRIu64"", &write_bytes); */
	/* 	tot_read+=read_bytes; */
	/* 	tot_write+=write_bytes; */
	/* 	next_device = strstr(next_device, "Total"); */
	/* } */
	/* prec->disk_read = (double)tot_read / (double)1048576; */
	/* prec->disk_write = (double)tot_write / (double)1048576; */

	/* info("after %d %d", total_rss); */
	/* print_jag_prec(prec); */
	//END_TIMER;
	//info("took %s", TIME_STR);
	return;

}

static bool _run_in_daemon(void)
{
	static bool set = false;
	static bool run = false;

	if (!set) {
		set = 1;
		run = run_in_daemon("slurmstepd");
	}

	return run;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init (void)
{
	/* If running on the slurmctld don't do any of this since it
	   isn't needed.
	*/
	if (_run_in_daemon()) {
		jag_common_init(0);

		/* read cgroup configuration */
		if (read_slurm_cgroup_conf(&slurm_cgroup_conf))
			return SLURM_ERROR;

		/* initialize cpuinfo internal data */
		if (xcpuinfo_init() != XCPUINFO_SUCCESS) {
			free_slurm_cgroup_conf(&slurm_cgroup_conf);
			return SLURM_ERROR;
		}

		/* enable cpuacct cgroup subsystem */
		if (jobacct_gather_cgroup_cpuacct_init(&slurm_cgroup_conf) !=
		    SLURM_SUCCESS) {
			xcpuinfo_fini();
			free_slurm_cgroup_conf(&slurm_cgroup_conf);
			return SLURM_ERROR;
		}

		/* enable memory cgroup subsystem */
		if (jobacct_gather_cgroup_memory_init(&slurm_cgroup_conf) !=
		    SLURM_SUCCESS) {
			xcpuinfo_fini();
			free_slurm_cgroup_conf(&slurm_cgroup_conf);
			return SLURM_ERROR;
		}

		/* FIXME: Enable when kernel support ready.
		 *
		 * Enable blkio subsystem.
		 */
		/* if (jobacct_gather_cgroup_blkio_init(&slurm_cgroup_conf) */
		/*     != SLURM_SUCCESS) { */
		/* 	xcpuinfo_fini(); */
		/* 	free_slurm_cgroup_conf(&slurm_cgroup_conf); */
		/* 	return SLURM_ERROR; */
		/* } */
	}

	debug("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

extern int fini (void)
{
	if (_run_in_daemon()) {
		jobacct_gather_cgroup_cpuacct_fini(&slurm_cgroup_conf);
		jobacct_gather_cgroup_memory_fini(&slurm_cgroup_conf);
		/* jobacct_gather_cgroup_blkio_fini(&slurm_cgroup_conf); */
		acct_gather_energy_fini();

		/* unload configuration */
		free_slurm_cgroup_conf(&slurm_cgroup_conf);
	}
	return SLURM_SUCCESS;
}

/*
 * jobacct_gather_p_poll_data() - Build a table of all current processes
 *
 * IN/OUT: task_list - list containing current processes.
 * IN: pgid_plugin - if we are running with the pgid plugin.
 * IN: cont_id - container id of processes if not running with pgid.
 *
 * OUT:	none
 *
 * THREADSAFE! Only one thread ever gets here.  It is locked in
 * slurm_jobacct_gather.
 *
 * Assumption:
 *    Any file with a name of the form "/proc/[0-9]+/stat"
 *    is a Linux-style stat entry. We disregard the data if they look
 *    wrong.
 */
extern void jobacct_gather_p_poll_data(
	List task_list, bool pgid_plugin, uint64_t cont_id)
{
	static jag_callbacks_t callbacks;
	static bool first = 1;

	if (first) {
		memset(&callbacks, 0, sizeof(jag_callbacks_t));
		first = 0;
		callbacks.prec_extra = _prec_extra;
	}

	jag_common_poll_data(task_list, pgid_plugin, cont_id, &callbacks);

	return;
}

extern int jobacct_gather_p_endpoll(void)
{
	jag_common_fini();

	return SLURM_SUCCESS;
}

extern int jobacct_gather_p_add_task(pid_t pid, jobacct_id_t *jobacct_id)
{
	if (jobacct_gather_cgroup_cpuacct_attach_task(pid, jobacct_id) !=
	    SLURM_SUCCESS)
		return SLURM_ERROR;

	if (jobacct_gather_cgroup_memory_attach_task(pid, jobacct_id) !=
	    SLURM_SUCCESS)
		return SLURM_ERROR;

	/* if (jobacct_gather_cgroup_blkio_attach_task(pid, jobacct_id) != */
	/*     SLURM_SUCCESS) */
	/* 	return SLURM_ERROR; */

	return SLURM_SUCCESS;
}

extern char* jobacct_cgroup_create_slurm_cg(xcgroup_ns_t* ns)
 {
	/* we do it here as we do not have access to the conf structure */
	/* in libslurm (src/common/xcgroup.c) */
	xcgroup_t slurm_cg;
	char* pre = (char*) xstrdup(slurm_cgroup_conf.cgroup_prepend);
#ifdef MULTIPLE_SLURMD
	if (conf->node_name != NULL)
		xstrsubstitute(pre,"%n", conf->node_name);
	else {
		xfree(pre);
		pre = (char*) xstrdup("/slurm");
	}
#endif

	/* create slurm cgroup in the ns (it could already exist) */
	if (xcgroup_create(ns,&slurm_cg,pre,
			   getuid(), getgid()) != XCGROUP_SUCCESS) {
		return pre;
	}

	if (xcgroup_instanciate(&slurm_cg) != XCGROUP_SUCCESS) {
		error("unable to build slurm cgroup for ns %s: %m",
		      ns->subsystems);
		xcgroup_destroy(&slurm_cg);
		return pre;
	} else {
		debug3("slurm cgroup %s successfully created for ns %s: %m",
		       pre,ns->subsystems);
		xcgroup_destroy(&slurm_cg);
	}

	return pre;
}

