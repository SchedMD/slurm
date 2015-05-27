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
#include <stdlib.h>
#include <string.h>

#include "src/common/bitstring.h"
#include "src/common/layouts_mgr.h"
#include "src/common/node_conf.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xstring.h"
#include "src/slurmctld/powercapping.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"

#define L_POWER     	"power"
#define L_CLUSTER   	"Cluster"
#define L_SUM_MAX   	"MaxSumWatts"
#define L_SUM_IDLE  	"IdleSumWatts"
#define L_SUM_CUR   	"CurrentSumPower"
#define L_NODE_MAX   	"MaxWatts"
#define L_NODE_IDLE   	"IdleWatts"
#define L_NODE_DOWN   	"DownWatts"
#define L_NODE_SAVE   	"PowerSaveWatts"
#define L_NODE_CUR   	"CurrentPower"

static bool _powercap_enabled(void)
{
	if (powercap_get_cluster_current_cap() == 0)
		return false;
	return true;
}

bool power_layout_ready(void)
{
	struct node_record *node_ptr;
	uint32_t *data = xmalloc(sizeof(uint32_t) * 2);
	int i;

	for (i = 0, node_ptr = node_record_table_ptr; i < node_record_count;
	     i++, node_ptr++){
		if (layouts_entity_get_mkv(L_POWER, node_ptr->name, 
		    "MaxWatts,IdleWatts", data, (sizeof(uint32_t) * 2), 
		    L_T_UINT32)) {
			error("powercapping: node %s is not in the"
			     "layouts.d/power.conf file", node_ptr->name);
			return false;
		}
	}
	return true;
}


uint32_t powercap_get_cluster_max_watts(void)
{
	uint32_t max_watts;

	if (!_powercap_enabled())
		return 0;

	if (!power_layout_ready())
		return 0;

	layouts_entity_pullget_kv(L_POWER, L_CLUSTER, L_SUM_MAX, &max_watts,
				  L_T_UINT32);

	return max_watts;
}

uint32_t powercap_get_cluster_min_watts(void)
{
	uint32_t min_watts = 0, tmp_watts, save_watts, down_watts;
	struct node_record *node_ptr;
	int i;

	if (!_powercap_enabled())
		return 0;
	
	if (!power_layout_ready())
		return 0;

	for (i = 0, node_ptr = node_record_table_ptr; i < node_record_count;
	     i++, node_ptr++){

		layouts_entity_pullget_kv(L_POWER, node_ptr->name, L_NODE_IDLE,
					  &tmp_watts, L_T_UINT32);
		layouts_entity_pullget_kv(L_POWER, node_ptr->name, L_NODE_DOWN, 
					  &down_watts, L_T_UINT32);
		tmp_watts = MIN(tmp_watts, down_watts);
		layouts_entity_pullget_kv(L_POWER, node_ptr->name, L_NODE_SAVE, 
					  &save_watts, L_T_UINT32);
		tmp_watts = MIN(tmp_watts, save_watts);
		min_watts += tmp_watts;
	}

	return min_watts;
}

uint32_t powercap_get_cluster_current_cap(void)
{
	char *end_ptr = NULL, *sched_params, *tmp_ptr;
	uint32_t cap_watts = 0;

	sched_params = slurm_get_power_parameters();
	if (!sched_params)
		return cap_watts;

	if ((tmp_ptr = strstr(sched_params, "cap_watts=INFINITE"))) {
		cap_watts = INFINITE;
	} else if ((tmp_ptr = strstr(sched_params, "cap_watts=UNLIMITED"))) {
		cap_watts = INFINITE;
	} else if ((tmp_ptr = strstr(sched_params, "cap_watts="))) {
		cap_watts = strtol(tmp_ptr + 10, &end_ptr, 10);
		if ((end_ptr[0] == 'k') || (end_ptr[0] == 'K')) {
			cap_watts *= 1000;
		} else if ((end_ptr[0] == 'm') || (end_ptr[0] == 'M')) {
			cap_watts *= 1000000;
		}
	}
	xfree(sched_params);

	return cap_watts;
}

/* Strip "cap_watts=..." pointed to by tmp_ptr out of the string by shifting
 * other string contents down over it. */
static void _strip_cap_watts(char *tmp_ptr)
{
	char *end_ptr;
	int i;

	end_ptr = strchr(tmp_ptr, ',');
	if (!end_ptr) {
		tmp_ptr[0] = '\0';
		return;
	}
	end_ptr++;
	for (i = 0; ; i++) {
		tmp_ptr[i] = end_ptr[i];
		if (tmp_ptr[i] == '\0')
			break;
	}

}

int powercap_set_cluster_cap(uint32_t new_cap)
{
	char *sched_params, *sep, *tmp_ptr;

	sched_params = slurm_get_power_parameters();
	if (sched_params) {
		while ((tmp_ptr = strstr(sched_params, "cap_watts="))) {
			_strip_cap_watts(tmp_ptr);
		}
	}
	if (sched_params && sched_params[0])
		sep = ",";
	else
		sep = "";
	if (new_cap == INFINITE)
		xstrfmtcat(sched_params, "%scap_watts=INFINITE", sep);
	else
		xstrfmtcat(sched_params, "%scap_watts=%u", sep, new_cap);
	slurm_set_power_parameters(sched_params);
	xfree(sched_params);

	return 0;
}

uint32_t powercap_get_cluster_adjusted_max_watts(void)
{
	uint32_t adj_max_watts = 0,val;
	struct node_record *node_ptr;
	int i;

	if (!_powercap_enabled())
		return 0;
	if (!power_layout_ready())
		return 0;
	for (i = 0, node_ptr = node_record_table_ptr; i < node_record_count;
	     i++, node_ptr++){
		if (bit_test(power_node_bitmap, i)) {
			layouts_entity_pullget_kv(L_POWER, node_ptr->name,
					L_NODE_SAVE, &val, L_T_UINT32);
		} else if (!bit_test(up_node_bitmap, i)) {
			layouts_entity_pullget_kv(L_POWER, node_ptr->name,
					L_NODE_DOWN, &val, L_T_UINT32);
		} else {
			layouts_entity_pullget_kv(L_POWER, node_ptr->name,
					L_NODE_MAX, &val, L_T_UINT32);
		}
		adj_max_watts += val;
	}

	return adj_max_watts;
}

uint32_t powercap_get_cluster_current_max_watts(void)
{
	uint32_t cur_max_watts = 0;

	if (!_powercap_enabled())
		return 0;
	if (!power_layout_ready())
		return 0;

	cur_max_watts = powercap_get_node_bitmap_maxwatts(NULL);
	return cur_max_watts;
}

uint32_t powercap_get_node_bitmap_maxwatts(bitstr_t *idle_bitmap)
{
	uint32_t max_watts = 0, val;
	struct node_record *node_ptr;
	int i;
	bitstr_t *tmp_bitmap = NULL;

	if (!_powercap_enabled())
		return 0;
	if (!power_layout_ready())
		return 0;

	/* if no input bitmap, consider the current idle nodes 
	   bitmap as the input bitmap tagging nodes to consider 
	   as idle while computing the max watts of the cluster */
	if (idle_bitmap == NULL) {
		tmp_bitmap = bit_copy(idle_node_bitmap);
		idle_bitmap = tmp_bitmap;
	}

	for (i = 0, node_ptr = node_record_table_ptr; i < node_record_count;
	     i++, node_ptr++) {
		/* non reserved node, evaluate the different cases */
		if (bit_test(idle_bitmap, i)) {
			 /* idle nodes, 2 cases : power save or not */
			if (bit_test(power_node_bitmap, i)) {
				layouts_entity_pullget_kv(L_POWER, 
						node_ptr->name, L_NODE_SAVE,
						&val, L_T_UINT32);
			} else {
				layouts_entity_pullget_kv(L_POWER, 
						node_ptr->name, L_NODE_IDLE,
						&val, L_T_UINT32);
			}
		} else {
			/* non idle nodes, 2 cases : down or not */
			if (!bit_test(up_node_bitmap, i)) {
				layouts_entity_pullget_kv(L_POWER, 
						node_ptr->name, L_NODE_DOWN,
						&val, L_T_UINT32);
			} else {
				layouts_entity_pullget_kv(L_POWER,
						node_ptr->name, L_NODE_MAX,
						&val, L_T_UINT32);
			}
		}
		max_watts += val;	
	}

	if (tmp_bitmap)
		bit_free(tmp_bitmap);

	return max_watts;
}

uint32_t powercap_get_job_cap(struct job_record *job_ptr, time_t when)
{
	uint32_t powercap = 0, resv_watts;

	powercap = powercap_get_cluster_current_cap();
	if (powercap == INFINITE)
		powercap = powercap_get_cluster_max_watts();
	if (powercap == 0)
		return 0; /* should not happened */

	/* get the amount of watts reserved for the job */
	resv_watts = job_test_watts_resv(job_ptr, when);

	/* avoid underflow of the cap value, return at least 0 */
	if (resv_watts > powercap)
		resv_watts = powercap;

	return (powercap - resv_watts);
}
