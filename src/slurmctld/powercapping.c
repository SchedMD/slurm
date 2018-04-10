/*****************************************************************************\
 *  powercapping.c - Definitions for power capping logic in the controller
 *****************************************************************************
 *  Copyright (C) 2013 CEA/DAM/DIF
 *  Written by Matthieu Hautreux <matthieu.hautreux@cea.fr>
 *
 *  Copyright (C) 2014 Bull S.A.S.
 *  Written by Yiannis Georgiou <yiannis.georgiou@bull.net>
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
#include <stdlib.h>
#include <string.h>

#include "src/common/bitstring.h"
#include "src/common/layouts_mgr.h"
#include "src/common/macros.h"
#include "src/common/node_conf.h"
#include "src/common/power.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xstring.h"
#include "src/slurmctld/powercapping.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"


#define L_NAME		"power"
#define L_CLUSTER	"Cluster"
#define L_SUM_MAX	"MaxSumWatts"
#define L_SUM_IDLE	"IdleSumWatts"
#define L_SUM_CUR	"CurrentSumPower"
#define L_NODE_MAX	"MaxWatts"
#define L_NODE_IDLE	"IdleWatts"
#define L_NODE_DOWN	"DownWatts"
#define L_NODE_SAVE	"PowerSaveWatts"
#define L_NODE_CUR	"CurrentPower"
#define L_NUM_FREQ	"NumFreqChoices"
#define L_CUR_POWER	"CurrentCorePower"

static bool _powercap_enabled(void)
{
	if (powercap_get_cluster_current_cap() == 0)
		return false;
	return true;
}

int _which_power_layout(char *layout)
{
	uint32_t max_watts;

	return layouts_entity_get_kv(layout, L_CLUSTER, L_SUM_MAX,
					 &max_watts, L_T_UINT32);

}

int which_power_layout(void)
{
	layout_t* layout;
	
	if (!_powercap_enabled())
		return 0;

	layout = layouts_get_layout("power");

	if (layout == NULL)
		return 0;
	else if (xstrcmp(layout->name,"default") == 0)
		return 1;
	else if (xstrcmp(layout->name,"cpufreq") == 0)
		return 2;
	
	return 0;
}

bool power_layout_ready(void)
{
	static time_t last_error_time = (time_t) 0;
	time_t now = time(NULL);
	struct node_record *node_ptr;
	uint32_t data[2];
	int i;

	for (i = 0, node_ptr = node_record_table_ptr; i < node_record_count;
	     i++, node_ptr++) {
		if (layouts_entity_get_mkv(L_NAME, node_ptr->name,
		    "MaxWatts,IdleWatts", data, (sizeof(uint32_t) * 2),
		    L_T_UINT32)) {
			/* Limit error message frequency, once per minute */
			if (difftime(now, last_error_time) < 60)
				return false;
			last_error_time = now;
			error("%s: node %s is not in the layouts.d/power.conf file",
			     __func__, node_ptr->name);
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

	layouts_entity_get_kv(L_NAME, L_CLUSTER, L_SUM_MAX, &max_watts,
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
	     i++, node_ptr++) {
		layouts_entity_get_kv(L_NAME, node_ptr->name, L_NODE_IDLE,
					  &tmp_watts, L_T_UINT32);
		layouts_entity_get_kv(L_NAME, node_ptr->name, L_NODE_DOWN,
					  &down_watts, L_T_UINT32);
		tmp_watts = MIN(tmp_watts, down_watts);
		layouts_entity_get_kv(L_NAME, node_ptr->name, L_NODE_SAVE,
					  &save_watts, L_T_UINT32);
		tmp_watts = MIN(tmp_watts, save_watts);
		min_watts += tmp_watts;
	}

	return min_watts;
}

uint32_t powercap_get_cluster_current_cap(void)
{
	char *end_ptr = NULL, *power_params, *tmp_ptr;
	uint32_t cap_watts = 0;

	power_params = slurm_get_power_parameters();
	if (!power_params)
		return cap_watts;

	if ((tmp_ptr = strstr(power_params, "cap_watts=INFINITE"))) {
		cap_watts = INFINITE;
	} else if ((tmp_ptr = strstr(power_params, "cap_watts=UNLIMITED"))) {
		cap_watts = INFINITE;
	} else if ((tmp_ptr = strstr(power_params, "cap_watts="))) {
		cap_watts = strtol(tmp_ptr + 10, &end_ptr, 10);
		if ((end_ptr[0] == 'k') || (end_ptr[0] == 'K')) {
			cap_watts *= 1000;
		} else if ((end_ptr[0] == 'm') || (end_ptr[0] == 'M')) {
			cap_watts *= 1000000;
		}
	}
	xfree(power_params);

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
	char *power_params, *sep, *tmp_ptr;

	power_params = slurm_get_power_parameters();
	if (power_params) {
		while ((tmp_ptr = strstr(power_params, "cap_watts="))) {
			_strip_cap_watts(tmp_ptr);
		}
	}
	if (power_params && power_params[0])
		sep = ",";
	else
		sep = "";
	if (new_cap == INFINITE)
		xstrfmtcat(power_params, "%scap_watts=INFINITE", sep);
	else
		xstrfmtcat(power_params, "%scap_watts=%u", sep, new_cap);
	slurm_set_power_parameters(power_params);
	power_g_reconfig();
	xfree(power_params);

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
	     i++, node_ptr++) {
		if (bit_test(power_node_bitmap, i)) {
			layouts_entity_get_kv(L_NAME, node_ptr->name,
					L_NODE_SAVE, &val, L_T_UINT32);
		} else if (!bit_test(up_node_bitmap, i)) {
			layouts_entity_get_kv(L_NAME, node_ptr->name,
					L_NODE_DOWN, &val, L_T_UINT32);
		} else {
			layouts_entity_get_kv(L_NAME, node_ptr->name,
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
	
	if (which_power_layout() == 1) {
		cur_max_watts = powercap_get_node_bitmap_maxwatts(NULL);
	} else {
		cur_max_watts = powercap_get_node_bitmap_maxwatts_dvfs(
					NULL, NULL, NULL, 0, 0);
	}

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
	 * bitmap as the input bitmap tagging nodes to consider 
	 * as idle while computing the max watts of the cluster */
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
				layouts_entity_get_kv(L_NAME,
						node_ptr->name, L_NODE_SAVE,
						&val, L_T_UINT32);
			} else {
				layouts_entity_get_kv(L_NAME,
						node_ptr->name, L_NODE_IDLE,
						&val, L_T_UINT32);
			}
		} else {
			/* non idle nodes, 2 cases : down or not */
			if (!bit_test(up_node_bitmap, i)) {
				layouts_entity_get_kv(L_NAME, 
						node_ptr->name, L_NODE_DOWN,
						&val, L_T_UINT32);
			} else {
				layouts_entity_get_kv(L_NAME,
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

uint32_t powercap_get_job_cap(struct job_record *job_ptr, time_t when,
			      bool reboot)
{
	uint32_t powercap = 0, resv_watts;

	powercap = powercap_get_cluster_current_cap();
	if (powercap == INFINITE)
		powercap = powercap_get_cluster_max_watts();
	if (powercap == 0)
		return 0; /* should not happened */

	/* get the amount of watts reserved for the job */
	resv_watts = job_test_watts_resv(job_ptr, when, reboot);

	/* avoid underflow of the cap value, return at least 0 */
	if (resv_watts > powercap)
		resv_watts = powercap;

	return (powercap - resv_watts);
}

uint32_t powercap_get_cpufreq(bitstr_t *select_bitmap, int k)
{
	int i;
	struct node_record *node_ptr;
	char ename[128];
	uint32_t cpufreq = 0;

	if (!_powercap_enabled())
		return cpufreq;

	for (i = 0, node_ptr = node_record_table_ptr; i < node_record_count;
	     i++, node_ptr++) {
		if (bit_test(select_bitmap, i)) {
			sprintf(ename, "Cpufreq%d", k);
			layouts_entity_get_kv(L_NAME, node_ptr->name,
						  ename, &cpufreq, L_T_UINT32);
		}
		break;
	}

	return cpufreq;
}

int powercap_get_job_optimal_cpufreq(uint32_t powercap, int *allowed_freqs)
{
	uint32_t cur_max_watts = 0, *tmp_max_watts_dvfs = NULL;
	int k = 1;
	bitstr_t *tmp_bitmap = NULL;

	if (!_powercap_enabled())
		return 0;

	tmp_max_watts_dvfs = xmalloc(sizeof(uint32_t) * (allowed_freqs[0]+1));
	tmp_bitmap = bit_copy(idle_node_bitmap);
	bit_not(tmp_bitmap);

	cur_max_watts = powercap_get_node_bitmap_maxwatts_dvfs(tmp_bitmap,
				idle_node_bitmap, tmp_max_watts_dvfs,
				allowed_freqs, 0);
	FREE_NULL_BITMAP(tmp_bitmap);

	if (cur_max_watts > powercap) {
		while (tmp_max_watts_dvfs[k] > powercap &&
		      k < allowed_freqs[0] + 1) {
			k++;
		}
		if (k == allowed_freqs[0] + 1)
			k--;
	} else {
		k = 1;
	}
	xfree(tmp_max_watts_dvfs);

	return k;
}

int *powercap_get_job_nodes_numfreq(bitstr_t *select_bitmap,
				    uint32_t cpu_freq_min,
				    uint32_t cpu_freq_max)
{
	uint16_t num_freq = 0;
	int i, p, *allowed_freqs = NULL, new_num_freq = 0;
	struct node_record *node_ptr;
	char ename[128];
	uint32_t cpufreq;

	if (!_powercap_enabled())
		return NULL;
	if ((cpu_freq_min == NO_VAL) && (cpu_freq_max == NO_VAL)) {
		allowed_freqs = xmalloc(sizeof(int) * 2);
		/* allowed_freqs[0] = 0; Default value */
		return allowed_freqs;
	}

	for (i = 0, node_ptr = node_record_table_ptr; i < node_record_count;
	     i++, node_ptr++) {
		if (bit_test(select_bitmap, i)) {
			layouts_entity_get_kv(L_NAME, node_ptr->name,
					L_NUM_FREQ, &num_freq, L_T_UINT16);
			allowed_freqs = xmalloc(sizeof(int)*((int)num_freq+2));
			allowed_freqs[0] = (int) num_freq;
			for (p = num_freq; p > 0; p--) {
				sprintf(ename, "Cpufreq%d", p);
				layouts_entity_get_kv(L_NAME,
					  	  node_ptr->name, ename,
						  &cpufreq, L_T_UINT32);

		/* In case a job is submitted with flags Low,High, etc on
		 * --cpu-freq parameter then we consider the whole range
		 * of available frequencies on nodes */
				if (((cpu_freq_min <= cpufreq) &&
				    (cpufreq <= cpu_freq_max)) ||
				    ((cpu_freq_min & CPU_FREQ_RANGE_FLAG) ||
				    (cpu_freq_max & CPU_FREQ_RANGE_FLAG))) {
					new_num_freq++;
					allowed_freqs[new_num_freq] = p;
				}
			}
			break;
		}
	}

	if (allowed_freqs) {
		allowed_freqs[0] = new_num_freq;
	} else {
		allowed_freqs = xmalloc(sizeof(int) * 2);
		/* allowed_freqs[0] = 0; Default value */
	}
	return allowed_freqs;
}

uint32_t powercap_get_node_bitmap_maxwatts_dvfs(bitstr_t *idle_bitmap,
			  bitstr_t *select_bitmap, uint32_t *max_watts_dvfs,
			  int* allowed_freqs, uint32_t num_cpus)
{
	uint32_t max_watts = 0, tmp_max_watts = 0, val = 0;
	uint32_t *tmp_max_watts_dvfs = NULL;
	struct node_record *node_ptr;
	int i, p;
	char ename[128], keyname[128];
	bitstr_t *tmp_bitmap = NULL;
	uint32_t data[5], core_data[4];

	if (!_powercap_enabled())
		return 0;

	if (max_watts_dvfs != NULL) {
		tmp_max_watts_dvfs = 
			  xmalloc(sizeof(uint32_t)*(allowed_freqs[0]+1));
	}

	/* if no input bitmap, consider the current idle nodes 
	 * bitmap as the input bitmap tagging nodes to consider 
	 * as idle while computing the max watts of the cluster */
	if (idle_bitmap == NULL && select_bitmap == NULL) {
		tmp_bitmap = bit_copy(idle_node_bitmap);
		idle_bitmap = tmp_bitmap;
		select_bitmap = tmp_bitmap;
	}
	
	for (i = 0, node_ptr = node_record_table_ptr; i < node_record_count;
	     i++, node_ptr++) {
		if (bit_test(idle_bitmap, i)) {
			/* idle nodes, 2 cases : power save or not */
			if (bit_test(power_node_bitmap, i)) {
				layouts_entity_get_kv(L_NAME,
						  node_ptr->name, L_NODE_SAVE,
						  &val, L_T_UINT32);
			} else {
				layouts_entity_get_kv(L_NAME,
						  node_ptr->name, L_NODE_IDLE,
						  &val, L_T_UINT32);
			}
		
		} else if (bit_test(select_bitmap, i)) {
			layouts_entity_get_mkv(L_NAME, node_ptr->name,
				"IdleWatts,MaxWatts,CoresCount,LastCore,CurrentPower",
				data, (sizeof(uint32_t) * 5), L_T_UINT32);

			/* tmp_max_watts = IdleWatts - cpus*IdleCoreWatts
			 * + cpus*MaxCoreWatts */
			sprintf(ename, "virtualcore%u", data[3]);
			if (num_cpus == 0 || num_cpus > data[2])
				num_cpus = data[2];
			layouts_entity_get_mkv(L_NAME, ename,
					       "IdleCoreWatts,MaxCoreWatts",
					       core_data,
					       (sizeof(uint32_t) * 2),
					       L_T_UINT32);
			if (data[4] == 0) {
				tmp_max_watts += data[0] -
					  num_cpus*core_data[0] +
					  num_cpus*core_data[1];
			} else if (data[4] > 0) {
				tmp_max_watts += data[4] -
					  num_cpus*core_data[0] +
					  num_cpus*core_data[1];
			} else if (num_cpus == data[2])
				tmp_max_watts += data[1];

			if (!tmp_max_watts_dvfs)
				goto skip_dvfs;
			for (p = 1; p < (allowed_freqs[0] + 1); p++) {
				sprintf(keyname, 
					"IdleCoreWatts,MaxCoreWatts,"
					"Cpufreq%dWatts,CurrentCorePower",
					allowed_freqs[p]);
				layouts_entity_get_mkv(L_NAME, ename, keyname,
					  core_data, (sizeof(uint32_t) * 4),
					  L_T_UINT32);
				if (num_cpus == data[2]) {
					tmp_max_watts_dvfs[p] +=
						  num_cpus*core_data[2];
				} else {
					if (data[4] == 0) {
						tmp_max_watts_dvfs[p] +=
						 	data[0] -
							num_cpus*core_data[0] +
							num_cpus*core_data[2];
					} else {
						tmp_max_watts_dvfs[p] +=
							data[4] -
							num_cpus*core_data[0] +
							num_cpus*core_data[2];
					}
				}
			}
  skip_dvfs:		;
		} else {
			/* non-idle nodes, 2 cases : down or not */
			if (!bit_test(up_node_bitmap, i)) {
				layouts_entity_get_kv(L_NAME,
						  node_ptr->name, L_NODE_DOWN,
						  &val, L_T_UINT32);
			} else {
				layouts_entity_get_kv(L_NAME,
						  node_ptr->name, L_NODE_CUR,
						  &val, L_T_UINT32);
			}
		}
		max_watts += val;
		val = 0;
	}
	if (max_watts_dvfs) {	
		for (p = 1; p < allowed_freqs[0] + 1; p++) {
			max_watts_dvfs[p] = max_watts + tmp_max_watts_dvfs[p];
		}
		xfree(tmp_max_watts_dvfs);
	}
	max_watts += tmp_max_watts;

	if (tmp_bitmap)
		bit_free(tmp_bitmap);

	return max_watts;
}
