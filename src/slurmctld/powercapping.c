/*****************************************************************************\
 *  powercapping.c - Definitions for power capping logic in the controller
 *****************************************************************************
 *  Copyright (C) 2013 CEA/DAM/DIF
 *  Written by Matthieu Hautreux <matthieu.hautreux@cea.fr>
 *
 *  Copyright (C) 2014 Bull S.A.S.
 *  Written by Yiannis Georgiou <yiannis.georgiou@bull.net>
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
#include "src/common/bitstring.h"
#include "src/common/node_conf.h"
#include "src/common/slurm_protocol_api.h"
#include "src/slurmctld/powercapping.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/common/layouts_mgr.h"

#define L_POWER     "power"
#define L_CLUSTER   "Cluster"
#define L_MAX   "MaxSumWatts"
#define L_MIN   "IdleSumWatts"
#define L_CUR   "CurrentSumPower"
#define L_NODE_CUR   "CurrentPower"
#define L_NODE_MAX   "MaxWatts"
#define L_NODE_MIN   "IdleWatts"

bool _powercap_enabled(void)
{
	if (slurm_get_powercap() == 0)
		return false;
	else
		return true;
}

uint32_t powercap_get_cluster_max_watts(void)
{
	uint32_t max_watts;

	if (!_powercap_enabled())
		return 0;

	if (layouts_entity_pullget_kv(L_POWER, L_CLUSTER, L_MAX, &max_watts,
				      L_T_UINT32))
		return 0;

	return max_watts;
}

/* 
 * Nodes configured with powercap_priority=0 are always 
 * considered as being allowed to consume their max power
 */
uint32_t powercap_get_cluster_min_watts(void)
{
	uint32_t min_watts;

	if (!_powercap_enabled())
		return 0;

	/*TODO: make changes for powercap_priority*/
	if (layouts_entity_pullget_kv(L_POWER, L_CLUSTER, L_MIN, &min_watts,
				      L_T_UINT32))
		return 0;

	return min_watts;
}

uint32_t powercap_get_cluster_current_cap(void)
{
	uint32_t powercap = 0;

	powercap = slurm_get_powercap();
	if (powercap == NO_VAL)
		return 0;

	return powercap;
}

uint32_t powercap_get_cluster_adjusted_max_watts(void)
{
	uint32_t adj_max_watts,val;
	struct node_record *node_ptr;
	int i;

	if (!_powercap_enabled())
		return 0;

	/* TODO: we should have an additional parameters to decide if
	 * we need to use min_watts or 0 for nodes powered down by SLURM
	 */
	/*TODO: make changes for powercap_priority*/
	for (i=0, node_ptr=node_record_table_ptr; i<node_record_count;
	     i++, node_ptr++){
		if (bit_test(power_node_bitmap, i))
			if (layouts_entity_pullget_kv(L_POWER, node_ptr->name,
			    L_MIN, &val, L_T_UINT32))
                		return 0;
		else
			if (layouts_entity_pullget_kv(L_POWER, node_ptr->name,
			    L_MIN, &val, L_T_UINT32))
				return 0;
		adj_max_watts += val;
	}

	return adj_max_watts;
}

uint32_t powercap_get_cluster_current_max_watts(void)
{
	uint32_t cur_max_watts=0;

	if (!_powercap_enabled())
		return 0;

	cur_max_watts = powercap_get_node_bitmap_maxwatts(NULL);
	return cur_max_watts;
}

uint32_t powercap_get_node_bitmap_maxwatts(bitstr_t *idle_bitmap)
{
	uint32_t max_watts = 0;
	struct node_record *node_ptr;
	int i;
	bitstr_t *tmp_bitmap = NULL;

	if (!_powercap_enabled())
		return 0;

	/* if no input bitmap, consider the current idle nodes 
	   bitmap as the input bitmap tagging nodes to consider 
	   as idle while computing the max watts of the cluster */
	if (idle_bitmap == NULL) {
		tmp_bitmap = bit_copy(idle_node_bitmap);
		idle_bitmap = tmp_bitmap;
	}

	/* TODO: optimize the iteration over the nodes using consecutive 
	   set bits when possible */
	for(i=0, node_ptr=node_record_table_ptr; i<node_record_count;
	    i++, node_ptr++){
		/* TODO: we should have an additional parameters to decide if
		 * we need to use min_watts or 0 for nodes powered down by SLURM
		 */
		if (!bit_test(idle_bitmap, i)) {
			if (layouts_entity_pullget_kv(L_POWER, node_ptr->name,
			    L_NODE_MIN, &val, L_T_UINT32))
				return 0;
                 /* TODO: same logic to apply here too */
		} else {
			if (layouts_entity_pullget_kv(L_POWER, node_ptr->name,
			    L_NODE_MAX, &val, L_T_UINT32))
				return 0;
                }
		max_watts += val;
	}

	if (tmp_bitmap)
		bit_free(tmp_bitmap);

	return max_watts;
}

uint32_t powercap_get_job_cap(struct job_record *job_ptr,
			      time_t when)
{
	uint32_t powercap = 0, resv_watts;

	powercap = slurm_get_powercap();
	if (powercap == NO_VAL)
		return 0;
	else if (powercap == (uint32_t) INFINITE)
		powercap = powercap_get_cluster_max_watts();
	if (powercap == 0)
		return 0; /* should not happened */

	/* get the amount of watts reserved for the job */
	resv_watts = job_test_watts_resv(job_ptr, when);

	return (powercap - resv_watts);
}
