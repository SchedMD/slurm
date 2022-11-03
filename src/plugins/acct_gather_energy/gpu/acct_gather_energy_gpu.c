/*****************************************************************************\
 *  acct_gather_energy_gpu.c - slurm energy accounting plugin for GPUs.
 *****************************************************************************
 *  Copyright (C) 2019-2021 SchedMD LLC
 *  Copyright (c) 2019, Advanced Micro Devices, Inc. All rights reserved.
 *  Written by Advanced Micro Devices,
 *  who borrowed from the ipmi plugin of the same type
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

#include <dlfcn.h>

#include "src/common/slurm_xlator.h"
#include "src/interfaces/cgroup.h"
#include "src/interfaces/acct_gather_energy.h"
#include "src/interfaces/acct_gather_profile.h"
#include "src/interfaces/gpu.h"
#include "src/interfaces/gres.h"

#define DEFAULT_GPU_TIMEOUT 10
#define DEFAULT_GPU_FREQ 30

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
const char plugin_name[] = "AcctGatherEnergy gpu plugin";
const char plugin_type[] = "acct_gather_energy/gpu";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

/*
 * internal variables
 */
static int context_id = -1;
// copy of usable gpus and is only used by stepd for a job
static bitstr_t	*saved_usable_gpus = NULL;

static gpu_status_t *gpus = NULL;
static uint16_t gpus_len = 0;
static uint64_t *start_current_energies = NULL;

static int dataset_id = -1; // id of the dataset for profile data

static bool flag_energy_accounting_shutdown = false;
static bool flag_thread_started = false;
static pthread_mutex_t gpu_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gpu_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t launch_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t launch_cond = PTHREAD_COND_INITIALIZER;

static stepd_step_rec_t *step = NULL;

pthread_t thread_gpu_id_launcher = 0;
pthread_t thread_gpu_id_run = 0;

/*
 * Check running profile
 */
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

/*
 * Send profile
 */
static int _send_profile(void)
{
	uint16_t i;
	uint64_t data[gpus_len];
	time_t last_time = gpus[gpus_len - 1].last_update_time;

	if (!_running_profile())
		return SLURM_SUCCESS;

	if (dataset_id < 0) {
		acct_gather_profile_dataset_t dataset[gpus_len + 1];

		for (i = 0; i < gpus_len; i++) {
			dataset[i].name = xstrdup_printf("GPU%dPower", i);
			dataset[i].type = PROFILE_FIELD_UINT64;
		}
		dataset[i].name = NULL;
		dataset[i].type = PROFILE_FIELD_NOT_SET;
		dataset_id = acct_gather_profile_g_create_dataset(
			"Energy", NO_PARENT, dataset);
		for (i = 0; i < gpus_len; i++)
			xfree(dataset[i].name);
		log_flag(ENERGY, "Energy: dataset created (id = %d)",
			 dataset_id);
		if (dataset_id == SLURM_ERROR) {
			error("Energy: Failed to create the dataset");
			return SLURM_ERROR;
		}
	}

	/* pack an array of uint64_t with current power of gpus */
	memset(data, 0, sizeof(data));
	for (i = 0; i < gpus_len; i++) {
		data[i] = gpus[i].energy.current_watts;
		last_time = gpus[i].energy.poll_time;
	}

	if (slurm_conf.debug_flags & DEBUG_FLAG_PROFILE) {
		for (i = 0; i < gpus_len; i++) {
			info("PROFILE-Energy: GPU%dPower=%"PRIu64"",
			     i, data[i]);
		}
	}
	return acct_gather_profile_g_add_sample_data(dataset_id, (void *)data,
						     last_time);
}

/*
 * _get_additional_consumption computes consumption between 2 times
 * time0	(IN) Previous time
 * time1	(IN) Current time
 * watt0	(IN) Previous watts
 * watt1	(IN) Current watts
 */
static uint64_t _get_additional_consumption(time_t time0, time_t time1,
					    uint32_t watt0, uint32_t watt1)
{
	return (uint64_t) ((time1 - time0)*(watt1 + watt0)/2);
}

/* updates the given energy according to the last watts reading of the gpu
 * gpu		(IN/OUT) A pointer to gpu_status_t structure
 * readings	(IN) readings to calculate average watts
 */
static void _update_energy(gpu_status_t *gpu, uint32_t readings)
{
	uint32_t prev_watts;
	acct_gather_energy_t *e = &gpu->energy;

	if (e->current_watts && (e->current_watts != NO_VAL)) {
		prev_watts = e->current_watts;
		e->ave_watts = ((e->ave_watts * readings) +
				e->current_watts) / (readings + 1);
		e->current_watts = gpu->last_update_watt;
		if (gpu->previous_update_time == 0)
			e->base_consumed_energy = 0;
		else
			e->base_consumed_energy =
				_get_additional_consumption(
					gpu->previous_update_time,
					gpu->last_update_time,
					prev_watts,
					e->current_watts);
		e->previous_consumed_energy = e->consumed_energy;
		e->consumed_energy += e->base_consumed_energy;
	} else {
		e->consumed_energy = 0;
		e->ave_watts = 0;
		e->current_watts = gpu->last_update_watt;
	}
	e->poll_time = time(NULL);
}

/*
 * _thread_update_node_energy calls _read_gpu_values and updates all values
 * for node consumption
 */
static int _thread_update_node_energy(void)
{
	int rc = SLURM_SUCCESS;
	uint16_t i;
	static uint32_t readings = 0;

	for (i = 0; i < gpus_len; i++) {
		rc = gpu_g_energy_read(i, &gpus[i]);
		if (rc == SLURM_SUCCESS) {
			_update_energy(&gpus[i], readings);
		}
	}
	readings++;

	if (slurm_conf.debug_flags & DEBUG_FLAG_ENERGY) {
		for (i = 0; i < gpus_len; i++)
			info("gpu-thread: gpu %u current_watts: %u, consumed %"PRIu64" Joules %"PRIu64" new, ave watts %u",
			     i,
			     gpus[i].energy.current_watts,
			     gpus[i].energy.consumed_energy,
			     gpus[i].energy.base_consumed_energy,
			     gpus[i].energy.ave_watts);
	}

	return rc;
}

/*
 * _thread_init initializes values and conf for the gpu thread
 */
static int _thread_init(void)
{

	if (gpus_len && gpus) {
		log_flag(ENERGY, "%s thread init", plugin_name);
		return SLURM_SUCCESS;
	} else {
		error("%s thread init failed, no GPU available", plugin_name);
		return SLURM_ERROR;
	}
}

/*
 * _thread_gpu_run is the thread calling gpu periodically
 * and read the energy values from the AMD GPUs
 */
static void *_thread_gpu_run(void *no_data)
{
	struct timeval tvnow;
	struct timespec abs;

	flag_energy_accounting_shutdown = false;
	log_flag(ENERGY, "gpu-thread: launched");

	(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	slurm_mutex_lock(&gpu_mutex);
	if (_thread_init() != SLURM_SUCCESS) {
		log_flag(ENERGY, "gpu-thread: aborted");
		slurm_mutex_unlock(&gpu_mutex);

		slurm_mutex_lock(&launch_mutex);
		slurm_cond_signal(&launch_cond);
		slurm_mutex_unlock(&launch_mutex);

		return NULL;
	}

	(void) pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

	slurm_mutex_unlock(&gpu_mutex);
	flag_thread_started = true;

	slurm_mutex_lock(&launch_mutex);
	slurm_cond_signal(&launch_cond);
	slurm_mutex_unlock(&launch_mutex);

	/* setup timer */
	gettimeofday(&tvnow, NULL);
	abs.tv_sec = tvnow.tv_sec;
	abs.tv_nsec = tvnow.tv_usec * 1000;

	//loop until slurm stop
	while (!flag_energy_accounting_shutdown) {
		slurm_mutex_lock(&gpu_mutex);

		_thread_update_node_energy();

		/* Sleep until the next time. */
		abs.tv_sec += DEFAULT_GPU_FREQ;
		slurm_cond_timedwait(&gpu_cond, &gpu_mutex, &abs);

		slurm_mutex_unlock(&gpu_mutex);
	}

	log_flag(ENERGY, "gpu-thread: ended");

	return NULL;
}

/*
 * _thread_launcher is the thread that launches gpu thread
 */
static void *_thread_launcher(void *no_data)
{
	struct timeval tvnow;
	struct timespec abs;

	slurm_thread_create(&thread_gpu_id_run, _thread_gpu_run, NULL);

	/* setup timer */
	gettimeofday(&tvnow, NULL);
	abs.tv_sec = tvnow.tv_sec + DEFAULT_GPU_TIMEOUT;
	abs.tv_nsec = tvnow.tv_usec * 1000;

	slurm_mutex_lock(&launch_mutex);
	slurm_cond_timedwait(&launch_cond, &launch_mutex, &abs);
	slurm_mutex_unlock(&launch_mutex);

	if (!flag_thread_started) {
		error("%s threads failed to start in a timely manner",
		      plugin_name);

		flag_energy_accounting_shutdown = true;

		/*
		 * It is a known thing we can hang up on GPU calls cancel if
		 * we must.
		 */
		pthread_cancel(thread_gpu_id_run);

		/*
		 * Unlock just to make sure since we could have canceled the
		 * thread while in the lock.
		 */
		slurm_mutex_unlock(&gpu_mutex);
	}

	return NULL;
}

static void _add_energy(acct_gather_energy_t *energy_tot,
			acct_gather_energy_t *energy_new,
			int gpu_num)
{
	if (energy_new->current_watts == NO_VAL)
		return;

	energy_tot->base_consumed_energy += energy_new->base_consumed_energy;
	energy_tot->ave_watts += energy_new->ave_watts;
	energy_tot->consumed_energy += energy_new->consumed_energy;
	energy_tot->current_watts += energy_new->current_watts;
	energy_tot->previous_consumed_energy +=
		energy_new->previous_consumed_energy;
	/*
	 * node poll_time is computed as the oldest poll_time of
	 * the gpus
	 */
	if (!energy_tot->poll_time ||
	    (energy_tot->poll_time > energy_new->poll_time))
		energy_tot->poll_time = energy_new->poll_time;
	log_flag(ENERGY, "%s: gpu: %d, current_watts: %u, consumed %"PRIu64" Joules %"PRIu64" new, ave watts %u",
		 __func__, gpu_num, energy_new->current_watts,
		 energy_new->consumed_energy, energy_new->base_consumed_energy,
		 energy_new->ave_watts);
}

/* Get the energy for a job
 * energy	(IN) a pointer to a acct_gather_energy_t structure
 */
static void _get_node_energy_up(acct_gather_energy_t *energy)
{
	bool task_cgroup = false;
	bool constrained_devices = false;
	bool cgroups_active = false;

	uint16_t i;

	/*
	 * If saved_usable_gpus doesn't exist it means we don't have any gpus to
	 * track, just return.
	 */
	if (!saved_usable_gpus)
		return;

	// Check if GPUs are constrained by cgroups
	cgroup_conf_init();
	constrained_devices = slurm_cgroup_conf.constrain_devices;

	// Check if task/cgroup plugin is loaded
	if (xstrstr(slurm_conf.task_plugin, "cgroup"))
		task_cgroup = true;

	// If both of these are true, then GPUs will be constrained
	if (constrained_devices && task_cgroup) {
		cgroups_active = true;
		log_flag(ENERGY, "%s: cgroups are configured.", __func__);
	} else {
		log_flag(ENERGY, "%s: cgroups are NOT configured.", __func__);
	}

	// sum the energy of all gpus for this job
	memset(energy, 0, sizeof(acct_gather_energy_t));
	for (i = 0; i < gpus_len; i++) {
		// Skip if not using cgroups, or bit is not set
		if (cgroups_active && !bit_test(saved_usable_gpus, i)) {
			log_flag(ENERGY, "Passing over gpu %u", i);
			continue;
		}
		_add_energy(energy, &gpus[i].energy, i);
	}
	log_flag(ENERGY, "%s: current_watts: %u, consumed %"PRIu64" Joules %"PRIu64" new, ave watts %u",
		 __func__, energy->current_watts, energy->consumed_energy,
		 energy->base_consumed_energy, energy->ave_watts);
}

/* Get the energy for a node
 * energy	(IN) a pointer to a acct_gather_energy_t structure
 */
static void _get_node_energy(acct_gather_energy_t *energy)
{
	uint16_t i;

	// sum the energy of all gpus for this node
	memset(energy, 0, sizeof(acct_gather_energy_t));
	for (i = 0; i < gpus_len; i++)
		_add_energy(energy, &gpus[i].energy, i);
	log_flag(ENERGY, "%s: current_watts: %u, consumed %"PRIu64" Joules %"PRIu64" new, ave watts %u",
		 __func__, energy->current_watts, energy->consumed_energy,
		 energy->base_consumed_energy, energy->ave_watts);
}

/* Get the energy in joules for a job
 * delta	(IN) Use cache if data is newer than this in seconds
 */
static int _get_joules_task(uint16_t delta)
{
	time_t now = time(NULL);
	static bool stepd_first = true;
	uint64_t adjustment = 0;
	uint16_t i;
	acct_gather_energy_t *new, *old;

	/* gpus list */
	acct_gather_energy_t *energies = NULL;
	uint16_t gpu_cnt = 0;

	xassert(context_id != -1);

	if (slurm_get_node_energy(conf->node_name, context_id, delta, &gpu_cnt,
				  &energies)) {
		error("%s: can't get info from slurmd", __func__);
		return SLURM_ERROR;
	}
	if (stepd_first) {
		gpus_len = gpu_cnt;
		gpus = xcalloc(sizeof(gpu_status_t), gpus_len);
		start_current_energies = xcalloc(sizeof(uint64_t), gpus_len);
	}

	if (gpu_cnt != gpus_len) {
		error("%s: received %u sensors, %u expected",
		      __func__, gpu_cnt, gpus_len);
		acct_gather_energy_destroy(energies);
		return SLURM_ERROR;
	}

	for (i = 0; i < gpu_cnt; i++) {
		new = &energies[i];
		old = &gpus[i].energy;
		new->previous_consumed_energy = old->consumed_energy;

		adjustment = _get_additional_consumption(
			new->poll_time, now,
			new->current_watts,
			new->current_watts);

		if (!stepd_first) {
			/* if slurmd is reloaded while the step is alive */
			if (old->consumed_energy > new->consumed_energy)
				new->base_consumed_energy =
					new->consumed_energy + adjustment;
			else {
				new->consumed_energy -=
					start_current_energies[i];
				new->base_consumed_energy =
					adjustment +
					(new->consumed_energy -
					 old->consumed_energy);
			}
		} else {
			/*
			 * This is just for the step, so take all the pervious
			 * consumption out of the mix.
			 */
			start_current_energies[i] =
				new->consumed_energy + adjustment;
			new->base_consumed_energy = 0;
		}

		new->consumed_energy = new->previous_consumed_energy
			+ new->base_consumed_energy;
		memcpy(old, new, sizeof(acct_gather_energy_t));

		log_flag(ENERGY, "%s: consumed %"PRIu64" Joules (received %"PRIu64"(%u watts) from slurmd)",
			 __func__, new->consumed_energy,
			 new->base_consumed_energy, new->current_watts);
	}

	acct_gather_energy_destroy(energies);

	stepd_first = false;

	return SLURM_SUCCESS;
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

/*
 * fini() is called when the plugin exits.
 */
extern int fini(void)
{
	if (!running_in_slurmd_stepd())
		return SLURM_SUCCESS;

	flag_energy_accounting_shutdown = true;

	slurm_mutex_lock(&launch_mutex);
	/* clean up the launch thread */
	slurm_cond_signal(&launch_cond);
	slurm_mutex_unlock(&launch_mutex);

	if (thread_gpu_id_launcher)
		pthread_join(thread_gpu_id_launcher, NULL);

	slurm_mutex_lock(&gpu_mutex);
	/* clean up the run thread */
	slurm_cond_signal(&gpu_cond);
	slurm_mutex_unlock(&gpu_mutex);

	if (thread_gpu_id_run)
		pthread_join(thread_gpu_id_run, NULL);

	/*
	 * We don't really want to destroy the the state, so those values
	 * persist a reconfig. And if the process dies, this will be lost
	 * anyway. So not freeing these variables is not really a leak.
	 *
	 * xfree(gpus);
	 * xfree(start_current_energies);
	 * saved_usable_gpus = NULL;
	 */

	return SLURM_SUCCESS;
}

extern int acct_gather_energy_p_update_node_energy(void)
{
	int rc = SLURM_SUCCESS;
	xassert(running_in_slurmd_stepd());

	return rc;
}

extern int acct_gather_energy_p_get_data(enum acct_energy_type data_type,
					 void *data)
{
	uint16_t i;
	int rc = SLURM_SUCCESS;
	acct_gather_energy_t *energy = (acct_gather_energy_t *)data;
	time_t *last_poll = (time_t *)data;
	uint16_t *gpu_cnt = (uint16_t *)data;

	xassert(running_in_slurmd_stepd());
	switch (data_type) {
	case ENERGY_DATA_NODE_ENERGY_UP:
		slurm_mutex_lock(&gpu_mutex);
		if (running_in_slurmd()) {
			if (_thread_init() == SLURM_SUCCESS) {
				_thread_update_node_energy();
				_get_node_energy(energy);
			}
		} else {
			_get_joules_task(10);
			_get_node_energy_up(energy);
		}
		slurm_mutex_unlock(&gpu_mutex);
		break;
	case ENERGY_DATA_NODE_ENERGY:
		slurm_mutex_lock(&gpu_mutex);
		_get_node_energy(energy);
		slurm_mutex_unlock(&gpu_mutex);
		break;
	case ENERGY_DATA_LAST_POLL:
		slurm_mutex_lock(&gpu_mutex);
		if (gpus)
			*last_poll = gpus[gpus_len-1].last_update_time;
		else
			*last_poll = 0;
		slurm_mutex_unlock(&gpu_mutex);
		break;
	case ENERGY_DATA_SENSOR_CNT:
		slurm_mutex_lock(&gpu_mutex);
		*gpu_cnt = gpus_len;
		slurm_mutex_unlock(&gpu_mutex);
		break;
	case ENERGY_DATA_STRUCT:
		slurm_mutex_lock(&gpu_mutex);
		for (i = 0; i < gpus_len; i++)
			memcpy(&energy[i], &gpus[i].energy,
			       sizeof(acct_gather_energy_t));
		slurm_mutex_unlock(&gpu_mutex);
		break;
	case ENERGY_DATA_JOULES_TASK:
		slurm_mutex_lock(&gpu_mutex);
		if (running_in_slurmd()) {
			if (_thread_init() == SLURM_SUCCESS)
				_thread_update_node_energy();
		} else {
			_get_joules_task(10);
		}
		for (i = 0; i < gpus_len; ++i)
			memcpy(&energy[i], &gpus[i].energy,
			       sizeof(acct_gather_energy_t));
		slurm_mutex_unlock(&gpu_mutex);
		break;
	default:
		error("%s: unknown enum %d",
		      __func__, data_type);
		rc = SLURM_ERROR;
		break;
	}
	return rc;
}

extern int acct_gather_energy_p_set_data(enum acct_energy_type data_type,
					 void *data)
{
	int rc = SLURM_SUCCESS;
	int *delta = (int *)data;

	xassert(running_in_slurmd_stepd());

	switch (data_type) {
	case ENERGY_DATA_RECONFIG:
		break;
	case ENERGY_DATA_PROFILE:
		slurm_mutex_lock(&gpu_mutex);
		_get_joules_task(*delta);
		_send_profile();
		slurm_mutex_unlock(&gpu_mutex);
		break;
	case ENERGY_DATA_STEP_PTR:
	{
		/* set global job if needed later */
		step = (stepd_step_rec_t *)data;

		/*
		 * Get the GPUs used in the step so we only poll those when
		 * looking at them
		 */
		rc = gres_get_step_info(step->step_gres_list, "gpu", 0,
					GRES_STEP_DATA_BITMAP,
					&saved_usable_gpus);
		/*
		 * If a step isn't using gpus it will return ESLURM_INVALID_GRES
		 * not a real error, so we only print out debug2.
		 */
		if (rc == SLURM_SUCCESS)
			log_flag(ENERGY, "usable_gpus = %d of %"PRId64,
				 bit_set_count(saved_usable_gpus),
				 bit_size(saved_usable_gpus));
		else if (rc == ESLURM_INVALID_GRES)
			debug2("Step most likely doesn't have any gpus, no power gathering");
		else
			error("gres_get_step_info returned: %s",
			      slurm_strerror(rc));
		break;
	}
	default:
		error("%s: unknown enum %d",
		      __func__, data_type);
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
	static bool flag_init = false;

	context_id = context_id_in;

	if (!running_in_slurmd_stepd())
		return;

	if (!flag_init) {
		flag_init = true;
		if (running_in_slurmd()) {
			gpu_g_get_device_count((unsigned int *)&gpus_len);
			if (gpus_len) {
				gpus = xcalloc(sizeof(gpu_status_t), gpus_len);
				slurm_thread_create(&thread_gpu_id_launcher,
						    _thread_launcher, NULL);
			}
			log_flag(ENERGY, "%s thread launched", plugin_name);
		} else
			_get_joules_task(0);

	}

	debug("%s loaded", plugin_name);

	return;
}

extern void acct_gather_energy_p_conf_values(List *data)
{
	return;
}
