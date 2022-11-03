/*****************************************************************************\
 *  acct_gather_energy_pm_counters.c -
 *  slurm energy accounting plugin for HPE Cray pm_counters interface
 *****************************************************************************
 *  Copyright (C) 2015 SchedMD LLC
 *  Written by Danny Auble <da@schedmd.com> who borrowed from the rapl
 *  plugin of the same type
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
 *
\*****************************************************************************/

/*   acct_gather_energy_pm_counters_
 * This plugin does not initiate a node-level thread.
 * It will be used to get energy values from the cray bmc when available
 */


#include "src/common/slurm_xlator.h"
#include "src/interfaces/acct_gather_energy.h"


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
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "jobacct" for Slurm job completion logging) and <method>
 * is a description of how this plugin satisfies that application.  Slurm will
 * only load job completion logging plugins if the plugin_type string has a
 * prefix of "jobacct/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[] = "AcctGatherEnergy pm_counters plugin";
const char plugin_type[] = "acct_gather_energy/pm_counters";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

static acct_gather_energy_t *local_energy = NULL;
static stepd_step_rec_t *step = NULL;

enum {
	GET_ENERGY,
	GET_POWER
};

extern void acct_gather_energy_p_conf_set(int context_id_in,
					  s_p_hashtbl_t *tbl);

static uint64_t _get_latest_stats(int type)
{
	uint64_t data = 0;
	int fd;
	FILE *fp = NULL;
	char *file_name;
	char sbuf[72];
	int num_read;

	switch (type) {
	case GET_ENERGY:
		file_name = "/sys/cray/pm_counters/energy";
		break;
	case GET_POWER:
		file_name = "/sys/cray/pm_counters/power";
		break;
	default:
		error("unknown type %d", type);
		return 0;
		break;
	}

	if (!(fp = fopen(file_name, "r"))) {
		error("%s: unable to open %s", __func__, file_name);
		return data;
	}

	fd = fileno(fp);
	if (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1)
		error("%s: fcntl(%s): %m", __func__, file_name);
	num_read = read(fd, sbuf, (sizeof(sbuf) - 1));
	if (num_read > 0) {
		sbuf[num_read] = '\0';
		sscanf(sbuf, "%"PRIu64, &data);
	}
	fclose(fp);

	return data;
}

static void _get_joules_task(acct_gather_energy_t *energy)
{
	uint64_t curr_energy, diff_energy = 0;
	uint32_t curr_power;
	time_t now;
	static uint32_t readings = 0;

	if (energy->current_watts == NO_VAL)
		return;

	now = time(NULL);
	curr_energy = _get_latest_stats(GET_ENERGY);
	curr_power = (uint32_t) _get_latest_stats(GET_POWER);

	if (energy->previous_consumed_energy) {
		diff_energy = curr_energy - energy->previous_consumed_energy;

		energy->consumed_energy += diff_energy;
		energy->ave_watts =  ((energy->ave_watts * readings) +
				       energy->current_watts) / (readings + 1);
	} else {
		energy->base_consumed_energy = curr_energy;
		energy->ave_watts = 0;
	}
	readings++;
	energy->current_watts = curr_power;

	log_flag(ENERGY, "%s: %"PRIu64" Joules consumed over last %ld secs. Currently at %u watts, ave watts %u",
		 __func__, diff_energy,
		 (energy->poll_time ? now - energy->poll_time : 0),
		 curr_power, energy->ave_watts);

	energy->previous_consumed_energy = curr_energy;
	energy->poll_time = now;
}

static int _running_profile(void)
{
	static bool run = false;
	static uint32_t profile_opt = ACCT_GATHER_PROFILE_NOT_SET;

	if (profile_opt == ACCT_GATHER_PROFILE_NOT_SET) {
		acct_gather_profile_g_get(ACCT_GATHER_PROFILE_RUNNING,
					  &profile_opt);
		if (profile_opt & ACCT_GATHER_PROFILE_ENERGY)
			run = true;
	}

	return run;
}

static int _send_profile(void)
{
	uint64_t curr_watts;
	acct_gather_profile_dataset_t dataset[] = {
		{ "Power", PROFILE_FIELD_UINT64 },
		{ NULL, PROFILE_FIELD_NOT_SET }
	};

	static int dataset_id = -1; /* id of the dataset for profile data */

	if (!_running_profile())
		return SLURM_SUCCESS;

	log_flag(ENERGY, "%s: consumed %d watts",
		 __func__, local_energy->current_watts);

	if (dataset_id < 0) {
		dataset_id = acct_gather_profile_g_create_dataset(
			"Energy", NO_PARENT, dataset);
		log_flag(ENERGY, "Energy: dataset created (id = %d)",
			 dataset_id);
		if (dataset_id == SLURM_ERROR) {
			error("Energy: Failed to create the dataset for RAPL");
			return SLURM_ERROR;
		}
	}

	curr_watts = (uint64_t)local_energy->current_watts;

	log_flag(PROFILE, "PROFILE-Energy: power=%u",
		 local_energy->current_watts);

	return acct_gather_profile_g_add_sample_data(dataset_id,
	                                             (void *)&curr_watts,
						     local_energy->poll_time);
}

extern int acct_gather_energy_p_update_node_energy(void)
{
	int rc = SLURM_SUCCESS;

	xassert(running_in_slurmd_stepd());

	if (!local_energy || local_energy->current_watts == NO_VAL)
		return rc;

	_get_joules_task(local_energy);

	return rc;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	/* put anything that requires the .conf being read in
	   acct_gather_energy_p_conf_parse
	*/

	return SLURM_SUCCESS;
}

extern int fini(void)
{
	/*
	 * We don't really want to destroy the the state, so those values
	 * persist a reconfig. And if the process dies, this will be lost
	 * anyway. So not freeing this variable is not really a leak.
	 *
	 * if (!running_in_slurmd_stepd())
	 * 	return SLURM_SUCCESS;
	 *
	 * acct_gather_energy_destroy(local_energy);
	 * local_energy = NULL;
	 */

	return SLURM_SUCCESS;
}

extern int acct_gather_energy_p_get_data(enum acct_energy_type data_type,
					 void *data)
{
	int rc = SLURM_SUCCESS;
	acct_gather_energy_t *energy = (acct_gather_energy_t *)data;
	time_t *last_poll = (time_t *)data;
	uint16_t *sensor_cnt = (uint16_t *)data;

	xassert(running_in_slurmd_stepd());

	if (!local_energy) {
		debug("%s: trying to get data %d, but no local_energy yet.",
		      __func__, data_type);
		acct_gather_energy_p_conf_set(0, NULL);
	}

	switch (data_type) {
	case ENERGY_DATA_JOULES_TASK:
	case ENERGY_DATA_NODE_ENERGY_UP:
		if (local_energy->current_watts == NO_VAL)
			energy->consumed_energy = NO_VAL64;
		else
			_get_joules_task(energy);
		break;
	case ENERGY_DATA_STRUCT:
	case ENERGY_DATA_NODE_ENERGY:
		memcpy(energy, local_energy, sizeof(acct_gather_energy_t));
		break;
	case ENERGY_DATA_LAST_POLL:
		*last_poll = local_energy->poll_time;
		break;
	case ENERGY_DATA_SENSOR_CNT:
		*sensor_cnt = 1;
		break;
	default:
		error("acct_gather_energy_p_get_data: unknown enum %d",
		      data_type);
		rc = SLURM_ERROR;
		break;
	}
	return rc;
}

extern int acct_gather_energy_p_set_data(enum acct_energy_type data_type,
					 void *data)
{
	int rc = SLURM_SUCCESS;

	xassert(running_in_slurmd_stepd());

	switch (data_type) {
	case ENERGY_DATA_RECONFIG:
		break;
	case ENERGY_DATA_PROFILE:
		_get_joules_task(local_energy);
		_send_profile();
		break;
	case ENERGY_DATA_STEP_PTR:
		/* set global job if needed later */
		step = (stepd_step_rec_t *)data;
		break;
	default:
		error("acct_gather_energy_p_set_data: unknown enum %d",
		      data_type);
		rc = SLURM_ERROR;
		break;
	}
	return rc;
}

extern void acct_gather_energy_p_conf_options(s_p_options_t **full_options,
					      int *full_options_cnt)
{
	return;
}

extern void acct_gather_energy_p_conf_set(int context_id_in,
					  s_p_hashtbl_t *tbl)
{
	static bool flag_init = 0;

	if (!running_in_slurmd_stepd())
		return;

	/* Already been here, we shouldn't need to visit again */
	if (local_energy)
		return;

	if (!flag_init) {
		flag_init = 1;
		local_energy = acct_gather_energy_alloc(1);
		if (!_get_latest_stats(GET_ENERGY))
			local_energy->current_watts = NO_VAL;
		else
			_get_joules_task(local_energy);
	}

	debug("%s loaded", plugin_name);

	return;
}

extern void acct_gather_energy_p_conf_values(List *data)
{
	return;
}
