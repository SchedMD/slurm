/*****************************************************************************\
 *  info.c - get nodes information in slurm
 *****************************************************************************
 *  Copyright (C) 2012-2013 Los Alamos National Security, LLC.
 *  Written by Jimmy Cao <Jimmy.Cao@emc.com>, Ralph Castain <rhc@open-mpi.org>
 *  All rights reserved.
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

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/log.h"
#include "src/common/node_conf.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"

#include "src/slurmctld/locks.h"

#include "info.h"

static uint16_t fast_schedule = (uint16_t) NO_VAL;

/**
 *	get total number of nodes and slots in slurm.
 *
 *	IN:
 *	OUT Parameter:
 *		nodes: number of nodes in slurm
 *		slots: number of slots in slurm
 */
void get_total_nodes_slots (uint16_t *nodes, uint16_t *slots)
{
	int i;
	struct node_record *node_ptr;
	/* Locks: Read node */
	slurmctld_lock_t node_read_lock = {
					NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK };

	if (fast_schedule == (uint16_t) NO_VAL)
		fast_schedule = slurm_get_fast_schedule();

	*slots = 0;
	lock_slurmctld(node_read_lock);
	*nodes = node_record_count;
	for (i = 0, node_ptr = node_record_table_ptr; i < node_record_count;
	     i++, node_ptr++) {
		if (fast_schedule == 2)
			(*slots) += node_ptr->config_ptr->cpus;
		else
			(*slots) += node_ptr->cpus;
	}
	unlock_slurmctld(node_read_lock);
}

/**
 *	get number of available nodes and slots in slurm.
 *
 *	IN:
 *	OUT Parameter:
 *		nodes: number of available nodes in slurm
 *		slots: number of available slots in slurm
 */
void get_free_nodes_slots (uint16_t *nodes, uint16_t *slots)
{
	int i;
	struct node_record *node_ptr;
	/* Locks: Read node */
	slurmctld_lock_t node_read_lock = {
					NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK };

	if (fast_schedule == (uint16_t) NO_VAL)
		fast_schedule = slurm_get_fast_schedule();

	*nodes = 0;
	*slots = 0;
	lock_slurmctld(node_read_lock);
	for (i = 0, node_ptr = node_record_table_ptr; i < node_record_count;
	     i++, node_ptr++) {
		if (IS_NODE_IDLE(node_ptr)) {
			(*nodes) ++;
			if (fast_schedule == 2)
				(*slots) += node_ptr->config_ptr->cpus;
			else
				(*slots) += node_ptr->cpus;
		}
	}
	unlock_slurmctld(node_read_lock);
}

/**
 *	get available node list in slurm.
 *
 *	IN:
 *	OUT Parameter:
 *	RET OUT:
 *		hostlist_t: available node list in slurm
 *
 *	Note: the return result should be slurm_hostlist_destroy(hostlist)
 */
hostlist_t get_available_host_list_system_m(void)
{
	int i;
	struct node_record *node_ptr;
	hostlist_t hostlist = NULL;

	/* Locks: Read node */
	slurmctld_lock_t node_read_lock = {
					NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK };

	hostlist = slurm_hostlist_create(NULL);
	lock_slurmctld(node_read_lock);
	for (i = 0, node_ptr = node_record_table_ptr; i < node_record_count;
	     i++, node_ptr++) {
		if (IS_NODE_IDLE(node_ptr)) {
			 slurm_hostlist_push_host(hostlist, node_ptr->name);
		}
	}
	unlock_slurmctld(node_read_lock);

	return hostlist;
}

/**
 *	get the range of available node list in slurm.
 *
 *	IN:
 *	OUT Parameter:
 *	RET OUT:
 *		a string indicating the range of available node list in slurm
 *
 *	Note: the return result should be free(str)
 */
char* get_available_host_list_range_sytem_m(void)
{
	hostlist_t hostlist = NULL;
	char *range = NULL;

	hostlist = get_available_host_list_system_m();
	range = slurm_hostlist_ranged_string_malloc (hostlist);
	slurm_hostlist_destroy(hostlist);
	return range;
}

/**
 *	get available node list within a given node list range
 *
 *	IN:
 *		node_list: the given node list range
 *	OUT Parameter:
 *	RET OUT
 *		available node list
 *
 * Note: the return result should be slurm_hostlist_destroy(hostlist)
 */
hostlist_t choose_available_from_node_list_m(const char *node_list)
{
	char *hostname = NULL;
	hostlist_t given_hl = NULL;
	hostlist_t avail_hl_system = NULL;
	hostlist_t result_hl = NULL;

	given_hl = slurm_hostlist_create (node_list);
	avail_hl_system  = get_available_host_list_system_m();
	result_hl = slurm_hostlist_create(NULL);

	while ((hostname = slurm_hostlist_shift(given_hl))) {
		if (-1 != slurm_hostlist_find (avail_hl_system, hostname)) {
			slurm_hostlist_push_host(result_hl, hostname);
		}
		/* Note: to free memory after slurm_hostlist_shift(),
		 * 	remember to use free(str), not xfree(str)
		 */
		free(hostname);
	}

	slurm_hostlist_destroy(given_hl);
	slurm_hostlist_destroy(avail_hl_system);
	return result_hl;
}

/**
 *	get a subset node range with node_num nodes from a host_name_list
 *
 *	IN:
 *		host_name_list: the given host_name_list
 *		node_num: the number of host to choose
 *	OUT Parameter:
 *	RET OUT
 *		the subset node range, NULL if the node number of subset is
 *		larger than the node number in host_name_list
 *
 *	Note: the return should be free(str)
 */
char* get_hostlist_subset_m(const char *host_name_list, uint16_t node_num)
{
	hostlist_t hostlist = NULL;
	hostlist_t temp_hl = NULL;
	int sum;
	char *hostname = NULL;
	char *range = NULL;
	int i;

	if(NULL == host_name_list)
		return NULL;

	hostlist = slurm_hostlist_create(host_name_list);
	sum = slurm_hostlist_count(hostlist);

	if (sum < node_num) {
		error ("node_num > sum of host in hostlist");
		slurm_hostlist_destroy(hostlist);
		return NULL;
	}

	temp_hl = slurm_hostlist_create(NULL);

	for (i = 0; i < node_num; i++) {
		hostname = slurm_hostlist_shift(hostlist);
		if (NULL != hostname) {
			slurm_hostlist_push_host(temp_hl, hostname);
			free(hostname);
		}
	}

	range = slurm_hostlist_ranged_string_malloc(temp_hl);

	slurm_hostlist_destroy(temp_hl);
	slurm_hostlist_destroy(hostlist);
	return range;
}
