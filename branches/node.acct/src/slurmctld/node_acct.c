/*****************************************************************************\
 *  node_acct.c - node accounting plugin stub.
 *****************************************************************************
 *  Copyright (C) 2007-2008 The Regents of the University of California.
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

#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <src/common/parse_time.h>
#include <src/common/xstring.h>
#include <src/slurmctld/slurmctld.h>

#define _DEBUG 1

/* Note that all nodes entered a DOWN state after a cold-start of SLURM */
extern void node_acct_all_down(char *reason)
{
	char *state_file, tmp[32];
	struct stat stat_buf;
	uint16_t cpus;
	struct node_record *node_ptr;
	int i;

	state_file = xstrdup (slurmctld_conf.state_save_location);
	xstrcat (state_file, "/node_state");
	if (stat(state_file, &stat_buf)) {
		error("node_acct_all_down: could not stat(%s) to record "
		      "node down time", state_file);
		xfree(state_file);
		return;
	}
	xfree(state_file);

	slurm_make_time_str(&stat_buf.st_mtime, tmp, sizeof(tmp));
	node_ptr = node_record_table_ptr;
	for (i = 0; i < node_record_count; i++, node_ptr++) {
		if (node_ptr->name == '\0')
			continue;
		if (slurmctld_conf.fast_schedule)
			cpus = node_ptr->config_ptr->cpus;
		else
			cpus = node_ptr->cpus;
#if _DEBUG
		info("Node_acct_down: %s at %s with %u cpus due to %s",
		     node_ptr->name, tmp, cpus, reason);
#endif
/* FIXME: WRITE TO DATABASE HERE */
	}
}

/* Note that a node has entered a DOWN or DRAINED state */
extern void node_acct_down(struct node_record *node_ptr)
{
	char tmp[32];
	time_t now = time(NULL);
	uint16_t cpus;

	if (slurmctld_conf.fast_schedule)
		cpus = node_ptr->config_ptr->cpus;
	else
		cpus = node_ptr->cpus;

	slurm_make_time_str(&now, tmp, sizeof(tmp));
#if _DEBUG
	info("Node_acct_down: %s at %s with %u cpus due to %s", 
	     node_ptr->name, tmp, cpus, node_ptr->reason);
#endif
/* FIXME: WRITE TO DATABASE HERE */
}

/* Note that a node has exited from a DOWN or DRAINED state */
extern void node_acct_up(struct node_record *node_ptr)
{
	char tmp[32];
	time_t now = time(NULL);

	slurm_make_time_str(&now, tmp, sizeof(tmp));
#if _DEBUG
	info("Node_acct_up: %s at %s", node_ptr->name, tmp);
#endif
	/* FIXME: WRITE TO DATABASE HERE */
}

/* Note the total processor count in a cluster */
extern void node_acct_procs(char *cluster_name, uint32_t procs)
{
	static uint32_t last_procs = 0;
	char tmp[32];
	time_t now = time(NULL);

	if (procs == last_procs)
		return;
	last_procs = procs;

	/* Record the processor count */
	slurm_make_time_str(&now, tmp, sizeof(tmp));
#if _DEBUG
	info("Node_acct_procs: %s has %u total CPUs at %s", 
	     cluster_name, procs, tmp);
#endif
	/* FIXME: WRITE TO DATABASE HERE */
}

/* Note that the cluster is up and ready for work.
 * Generates a record of the cluster's processor count.
 * This should be executed whenever the cluser's processor count changes. */
extern void node_acct_ready(void)
{
	uint32_t procs = 0;
	struct node_record *node_ptr;
	int i, j;
	char *cluster_name = NULL;

	node_ptr = node_record_table_ptr;
	for (i = 0; i < node_record_count; i++, node_ptr++) {
		if (node_ptr->name == '\0')
			continue;
		if (cluster_name == NULL) {
			cluster_name = xstrdup(node_ptr->name);
			for (j = 0; cluster_name[j]; j++) {
				if (isdigit(cluster_name[j])) {
					cluster_name[j] = '\0';
					break;
				}
			}
		}
		if (slurmctld_conf.fast_schedule)
			procs += node_ptr->config_ptr->cpus;
		else
			procs += node_ptr->cpus;
	}

	node_acct_procs(cluster_name, procs);
	xfree(cluster_name);
}
