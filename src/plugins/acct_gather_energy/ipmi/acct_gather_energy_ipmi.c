/*****************************************************************************\
 *  acct_gather_energy_ipmi.c - slurm energy accounting plugin for ipmi.
 *****************************************************************************
 *  Copyright (C) 2012
 *  Written by Bull- Thomas Cadeau
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

/*   acct_gather_energy_ipmi
 * This plugin initiates a node-level thread to periodically
 * issue reads to a BMC over an ipmi interface driver 'kipmi0'
 *
 * TODO .. the library is under ../plugins/ipmi/lib and its
 * headers under ../plugins/ipmi/include
 * Currently the code is still using the 'getwatts' app.
 * This must be changed to utilize this library directly ..
 * including adjusting the makefiles for SLUrM building .
 */

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>

#include "src/common/slurm_xlator.h"
#include "src/common/slurm_acct_gather_energy.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/fd.h"
#include "src/slurmd/common/proctrack.h"

#include "src/slurmd/slurmd/slurmd.h"
#include "acct_gather_energy_ipmi_config.h"

/*
 * freeipmi includes for the lib
 */
#include <ipmi_monitoring.h>
#include <ipmi_monitoring_bitmasks.h>

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
slurmd_conf_t *conf __attribute__((weak_import)) = NULL;
#else
slurmd_conf_t *conf = NULL;
#endif

#define _DEBUG 1
#define _DEBUG_ENERGY 1
#define IPMI_VERSION 1		/* Data structure version number */
#define NBFIRSTREAD 3

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

const char plugin_name[] = "AcctGatherEnergy IPMI plugin";
const char plugin_type[] = "acct_gather_energy/ipmi";
const uint32_t plugin_version = 100;

/*
 * freeipmi variable declaration
 */
/* Global structure */
struct ipmi_monitoring_ipmi_config ipmi_config;
ipmi_monitoring_ctx_t ipmi_ctx = NULL;
unsigned int sensor_reading_flags = 0;
/* Hostname, NULL for In-band communication, non-null for a hostname */
char *hostname = NULL;
/* Set to an appropriate alternate if desired */
char *sdr_cache_directory = "/tmp";
char *sensor_config_file = NULL;
/*
 * internal variables
 */
static uint32_t last_update_watt = 0;
static time_t last_update_time = 0;
static time_t previous_update_time = 0;
static acct_gather_energy_t *local_energy = NULL;
static slurm_ipmi_conf_t slurm_ipmi_conf;
static uint64_t debug_flags = 0;
static bool flag_energy_accounting_shutdown = false;
static bool flag_thread_started = false;
static bool flag_init = false;
static pthread_mutex_t ipmi_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t cleanup_handler_thread = 0;
pthread_t thread_ipmi_id_launcher = 0;
pthread_t thread_ipmi_id_run = 0;

static bool _is_thread_launcher(void)
{
	static bool set = false;
	static bool run = false;

	if (!set) {
		set = 1;
		run = run_in_daemon("slurmd");
	}

	return run;
}

static bool _run_in_daemon(void)
{
	static bool set = false;
	static bool run = false;

	if (!set) {
		set = 1;
		run = run_in_daemon("slurmd,slurmstepd");
	}

	return run;
}

static void _task_sleep(int rem)
{
	while (rem)
		rem = sleep(rem);	// subject to interupt
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

/*
 * _get_additional_consumption computes consumption between 2 times
 * method is set to third method strongly
 */
static uint32_t _get_additional_consumption(time_t time0, time_t time1,
					    uint32_t watt0, uint32_t watt1)
{
	uint32_t consumption;
	consumption = (uint32_t) ((time1 - time0)*(watt1 + watt0)/2);

	return consumption;
}

/*
 * _init_ipmi_config initializes parameters for freeipmi library
 */
static int _init_ipmi_config (void)
{
	int errnum;
	/* Initialization flags
	 * Most commonly bitwise OR IPMI_MONITORING_FLAGS_DEBUG and/or
	 * IPMI_MONITORING_FLAGS_DEBUG_IPMI_PACKETS for extra debugging
	 * information.
	 */
	unsigned int ipmimonitoring_init_flags = 0;
	memset(&ipmi_config, 0, sizeof(struct ipmi_monitoring_ipmi_config));
	ipmi_config.driver_type = (int) slurm_ipmi_conf.driver_type;
	ipmi_config.disable_auto_probe =
		(int) slurm_ipmi_conf.disable_auto_probe;
	ipmi_config.driver_address =
		(unsigned int) slurm_ipmi_conf.driver_address;
	ipmi_config.register_spacing =
		(unsigned int) slurm_ipmi_conf.register_spacing;
	ipmi_config.driver_device = slurm_ipmi_conf.driver_device;
	ipmi_config.protocol_version = (int) slurm_ipmi_conf.protocol_version;
	ipmi_config.username = slurm_ipmi_conf.username;
	ipmi_config.password = slurm_ipmi_conf.password;
	ipmi_config.k_g = slurm_ipmi_conf.k_g;
	ipmi_config.k_g_len = (unsigned int) slurm_ipmi_conf.k_g_len;
	ipmi_config.privilege_level = (int) slurm_ipmi_conf.privilege_level;
	ipmi_config.authentication_type =
		(int) slurm_ipmi_conf.authentication_type;
	ipmi_config.cipher_suite_id = (int) slurm_ipmi_conf.cipher_suite_id;
	ipmi_config.session_timeout_len = (int) slurm_ipmi_conf.session_timeout;
	ipmi_config.retransmission_timeout_len =
		(int) slurm_ipmi_conf.retransmission_timeout;
	ipmi_config.workaround_flags =
		(unsigned int) slurm_ipmi_conf.workaround_flags;

	if (ipmi_monitoring_init(ipmimonitoring_init_flags, &errnum) < 0) {
		error("ipmi_monitoring_init: %s",
		      ipmi_monitoring_ctx_strerror(errnum));
		return SLURM_FAILURE;
	}
	if (!(ipmi_ctx = ipmi_monitoring_ctx_create())) {
		error("ipmi_monitoring_ctx_create");
		return SLURM_FAILURE;
	}
	if (sdr_cache_directory) {
		if (ipmi_monitoring_ctx_sdr_cache_directory(
			    ipmi_ctx, sdr_cache_directory) < 0) {
			error("ipmi_monitoring_ctx_sdr_cache_directory: %s",
			      ipmi_monitoring_ctx_errormsg(ipmi_ctx));
			return SLURM_FAILURE;
		}
	}
	/* Must call otherwise only default interpretations ever used */
	if (ipmi_monitoring_ctx_sensor_config_file(
		    ipmi_ctx, sensor_config_file) < 0) {
		error("ipmi_monitoring_ctx_sensor_config_file: %s",
		      ipmi_monitoring_ctx_errormsg(ipmi_ctx));
		return SLURM_FAILURE;
	}

	if (slurm_ipmi_conf.reread_sdr_cache)
		sensor_reading_flags |=
			IPMI_MONITORING_SENSOR_READING_FLAGS_REREAD_SDR_CACHE;
	if (slurm_ipmi_conf.ignore_non_interpretable_sensors)
		sensor_reading_flags |=
			IPMI_MONITORING_SENSOR_READING_FLAGS_IGNORE_NON_INTERPRETABLE_SENSORS;
	if (slurm_ipmi_conf.bridge_sensors)
		sensor_reading_flags |=
			IPMI_MONITORING_SENSOR_READING_FLAGS_BRIDGE_SENSORS;
	if (slurm_ipmi_conf.interpret_oem_data)
		sensor_reading_flags |=
			IPMI_MONITORING_SENSOR_READING_FLAGS_INTERPRET_OEM_DATA;
	if (slurm_ipmi_conf.shared_sensors)
		sensor_reading_flags |=
			IPMI_MONITORING_SENSOR_READING_FLAGS_SHARED_SENSORS;
	if (slurm_ipmi_conf.discrete_reading)
		sensor_reading_flags |=
			IPMI_MONITORING_SENSOR_READING_FLAGS_DISCRETE_READING;
	if (slurm_ipmi_conf.ignore_scanning_disabled)
		sensor_reading_flags |=
			IPMI_MONITORING_SENSOR_READING_FLAGS_IGNORE_SCANNING_DISABLED;
	if (slurm_ipmi_conf.assume_bmc_owner)
		sensor_reading_flags |=
			IPMI_MONITORING_SENSOR_READING_FLAGS_ASSUME_BMC_OWNER;
	/* FIXME: This is not included until later versions of IPMI, so don't
	   always have it.
	*/
	/* if (slurm_ipmi_conf.entity_sensor_names) */
	/* 	sensor_reading_flags |= */
	/* 		IPMI_MONITORING_SENSOR_READING_FLAGS_ENTITY_SENSOR_NAMES; */

	return SLURM_SUCCESS;
}

/*
 * _check_power_sensor check if the sensor is in Watt
 */
static int _check_power_sensor(void)
{
	unsigned int record_ids[] = {(int) slurm_ipmi_conf.power_sensor_num};
	unsigned int record_ids_length = 1;
	int sensor_units;
	void* sensor_reading;

	if ((ipmi_monitoring_sensor_readings_by_record_id(
		     ipmi_ctx,
		     hostname,
		     &ipmi_config,
		     sensor_reading_flags,
		     record_ids,
		     record_ids_length,
		     NULL, NULL)) != record_ids_length) {
		error("ipmi_monitoring_sensor_readings_by_record_id: %s",
		      ipmi_monitoring_ctx_errormsg(ipmi_ctx));
		return SLURM_FAILURE;
	}

	if ((sensor_units = ipmi_monitoring_sensor_read_sensor_units(ipmi_ctx))
	    < 0) {
		error("ipmi_monitoring_sensor_read_sensor_units: %s",
		      ipmi_monitoring_ctx_errormsg(ipmi_ctx));
		return SLURM_FAILURE;
	}

	if (sensor_units != slurm_ipmi_conf.variable) {
		error("Configured sensor is not in Watt, "
		      "please check ipmi.conf");
		return SLURM_FAILURE;
	}

	ipmi_monitoring_sensor_iterator_first(ipmi_ctx);
	if (ipmi_monitoring_sensor_read_record_id(ipmi_ctx) < 0) {
		error("ipmi_monitoring_sensor_read_record_id: %s",
		      ipmi_monitoring_ctx_errormsg(ipmi_ctx));
		return SLURM_FAILURE;
	}

	sensor_reading = ipmi_monitoring_sensor_read_sensor_reading(ipmi_ctx);
	if (sensor_reading) {
		last_update_watt = (uint32_t)(*((double *)sensor_reading));
		previous_update_time = last_update_time;
		last_update_time = time(NULL);
	} else {
		error("ipmi read an empty value for power consumption");
		return SLURM_FAILURE;
	}
	return SLURM_SUCCESS;
}

/*
 * _find_power_sensor reads all sensors and find sensor in Watt
 */
static int _find_power_sensor(void)
{
	int sensor_count;
	int i;
	int rc = SLURM_FAILURE;
	void* sensor_reading;
	int sensor_units, record_id;

	sensor_count = ipmi_monitoring_sensor_readings_by_record_id(
		ipmi_ctx,
		hostname,
		&ipmi_config,
		sensor_reading_flags,
		NULL,
		0,
		NULL,
		NULL);

	if (sensor_count < 0) {
		error("ipmi_monitoring_sensor_readings_by_record_id: %s",
		      ipmi_monitoring_ctx_errormsg(ipmi_ctx));
		return SLURM_FAILURE;
	}

	for (i = 0; i < sensor_count; i++,
		     ipmi_monitoring_sensor_iterator_next(ipmi_ctx)) {
		if ((sensor_units =
		     ipmi_monitoring_sensor_read_sensor_units(ipmi_ctx))
		    < 0) {
			error("ipmi_monitoring_sensor_read_sensor_units: %s",
			      ipmi_monitoring_ctx_errormsg(ipmi_ctx));
			return SLURM_FAILURE;
		}

		if (sensor_units != slurm_ipmi_conf.variable)
			continue;

		if ((record_id =
		     ipmi_monitoring_sensor_read_record_id(ipmi_ctx))
		    < 0) {
			error("ipmi_monitoring_sensor_read_record_id: %s",
			      ipmi_monitoring_ctx_errormsg(ipmi_ctx));
			return SLURM_FAILURE;
		}
		slurm_ipmi_conf.power_sensor_num = (uint32_t) record_id;
		sensor_reading = ipmi_monitoring_sensor_read_sensor_reading(
			ipmi_ctx);
		if (sensor_reading) {
			last_update_watt =
				(uint32_t)(*((double *)sensor_reading));
			last_update_time = time(NULL);
		} else {
			error("ipmi read an empty value for power consumption");
			rc = SLURM_FAILURE;
			continue;
		}
		rc = SLURM_SUCCESS;
		break;
	}

	if (rc != SLURM_SUCCESS)
		info("Power sensor not found.");
	else if (debug_flags & DEBUG_FLAG_ENERGY)
		info("Power sensor found: %d",
		     slurm_ipmi_conf.power_sensor_num);

	return rc;
}

/*
 * _read_ipmi_values read the Power sensor and update last_update_watt and times
 */
static int _read_ipmi_values(void)
{
	unsigned int record_ids[] = {(int) slurm_ipmi_conf.power_sensor_num};
	unsigned int record_ids_length = 1;
	void* sensor_reading;

	if ((ipmi_monitoring_sensor_readings_by_record_id(
		     ipmi_ctx,
		     hostname,
		     &ipmi_config,
		     sensor_reading_flags,
		     record_ids,
		     record_ids_length,
		     NULL,NULL)) != record_ids_length) {
		error("ipmi_monitoring_sensor_readings_by_record_id: %s",
		      ipmi_monitoring_ctx_errormsg(ipmi_ctx));
		return SLURM_FAILURE;
	}
	ipmi_monitoring_sensor_iterator_first(ipmi_ctx);
	if (ipmi_monitoring_sensor_read_record_id(ipmi_ctx) < 0) {
		error("ipmi_monitoring_sensor_read_record_id: %s",
		      ipmi_monitoring_ctx_errormsg(ipmi_ctx));
		return SLURM_FAILURE;
	}
	sensor_reading = ipmi_monitoring_sensor_read_sensor_reading(ipmi_ctx);
	if (sensor_reading) {
		last_update_watt = (uint32_t)(*((double *)sensor_reading));
		previous_update_time = last_update_time;
		last_update_time = time(NULL);
	} else {
		error("ipmi read an empty value for power consumption");
		return SLURM_FAILURE;
	}

	return SLURM_SUCCESS;
}

/*
 * _thread_update_node_energy calls _read_ipmi_values and updates all values
 * for node consumption
 */
static int _thread_update_node_energy(void)
{
	int rc = SLURM_SUCCESS;

	if (local_energy->current_watts == NO_VAL)
		return rc;

	rc = _read_ipmi_values();

	if (rc == SLURM_SUCCESS) {
		if (local_energy->current_watts != 0) {
			local_energy->base_watts = local_energy->current_watts;
			local_energy->current_watts = last_update_watt;
			if (previous_update_time == 0)
				local_energy->base_consumed_energy = 0;
			else
				local_energy->base_consumed_energy =
					_get_additional_consumption(
						previous_update_time,
						last_update_time,
						local_energy->base_watts,
						local_energy->current_watts);
			local_energy->previous_consumed_energy =
				local_energy->consumed_energy;
			local_energy->consumed_energy +=
				local_energy->base_consumed_energy;
		}
		if (previous_update_time == 0)
			previous_update_time = last_update_time;
		if (local_energy->current_watts == 0) {
			local_energy->consumed_energy = 0;
			local_energy->base_watts = 0;
			local_energy->current_watts = last_update_watt;
		}
		local_energy->poll_time = time(NULL);
	}
	if (debug_flags & DEBUG_FLAG_ENERGY) {
		info("ipmi-thread = %d sec, current %d Watts, "
		     "consumed %d Joules %d new",
		     (int) (last_update_time - previous_update_time),
		     local_energy->current_watts,
		     local_energy->consumed_energy,
		     local_energy->base_consumed_energy);
	}

	return rc;
}

/*
 * _thread_init initializes values and conf for the ipmi thread
 */
static int _thread_init(void)
{
	static bool first = true;
	static bool first_init = SLURM_FAILURE;
	int rc = SLURM_SUCCESS;

	if (!first)
		return first_init;
	first = false;

	if (_init_ipmi_config() != SLURM_SUCCESS) {
		//TODO verbose error?
		rc = SLURM_FAILURE;
	} else {
		if ((slurm_ipmi_conf.power_sensor_num == -1
		     && _find_power_sensor() != SLURM_SUCCESS)
		    || _check_power_sensor() != SLURM_SUCCESS) {
			local_energy->current_watts = NO_VAL;
		} else {
			local_energy->current_watts = last_update_watt;
		}
		if (slurm_ipmi_conf.reread_sdr_cache)
			//IPMI cache is reread only on initialisation
			//This option need a big EnergyIPMITimeout
			sensor_reading_flags ^=
				IPMI_MONITORING_SENSOR_READING_FLAGS_REREAD_SDR_CACHE;
	}
	local_energy->consumed_energy = 0;
	local_energy->base_watts = 0;
	slurm_mutex_unlock(&ipmi_mutex);

	if (rc != SLURM_SUCCESS)
		if (ipmi_ctx)
			ipmi_monitoring_ctx_destroy(ipmi_ctx);

	if (debug_flags & DEBUG_FLAG_ENERGY)
		info("%s thread init", plugin_name);

	first_init = SLURM_SUCCESS;

	return rc;
}

static int _ipmi_send_profile(void)
{
	acct_energy_data_t ener;

	if (!_running_profile())
		return SLURM_SUCCESS;

	if (debug_flags & DEBUG_FLAG_ENERGY)
		info("_ipmi_send_profile: consumed %d watts",
		     local_energy->current_watts);

	memset(&ener, 0, sizeof(acct_energy_data_t));
	/*TODO function to calculate Average CPUs Frequency*/
	/*ener->cpu_freq = // read /proc/...*/
	ener.cpu_freq = 1;
	ener.time = time(NULL);
	ener.power = local_energy->current_watts;
	acct_gather_profile_g_add_sample_data(
		ACCT_GATHER_PROFILE_ENERGY, &ener);

	return SLURM_ERROR;
}


/*
 * _thread_ipmi_run is the thread calling ipmi and launching _thread_ipmi_write
 */
static void *_thread_ipmi_run(void *no_data)
{
// need input (attr)
	int time_lost;

	(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	flag_energy_accounting_shutdown = false;
	if (debug_flags & DEBUG_FLAG_ENERGY)
		info("ipmi-thread: launched");

	slurm_mutex_lock(&ipmi_mutex);
	if (_thread_init() != SLURM_SUCCESS) {
		if (debug_flags & DEBUG_FLAG_ENERGY)
			info("ipmi-thread: aborted");
		slurm_mutex_unlock(&ipmi_mutex);
		return NULL;
	}
	slurm_mutex_unlock(&ipmi_mutex);

	flag_thread_started = true;

	//loop until slurm stop
	while (!flag_energy_accounting_shutdown) {
		time_lost = (int)(time(NULL) - last_update_time);
		if (time_lost <= slurm_ipmi_conf.freq)
			_task_sleep(slurm_ipmi_conf.freq - time_lost);
		else
			_task_sleep(1);
		slurm_mutex_lock(&ipmi_mutex);
		_thread_update_node_energy();
		slurm_mutex_unlock(&ipmi_mutex);
	}

	if (debug_flags & DEBUG_FLAG_ENERGY)
		info("ipmi-thread: ended");

	return NULL;
}

static void *_cleanup_thread(void *no_data)
{
	if (thread_ipmi_id_run)
		pthread_join(thread_ipmi_id_run, NULL);

	if (ipmi_ctx)
		ipmi_monitoring_ctx_destroy(ipmi_ctx);
	reset_slurm_ipmi_conf(&slurm_ipmi_conf);

	return NULL;
}

static void *_thread_launcher(void *no_data)
{
	//what arg would countain? frequency, socket?
	pthread_attr_t attr_run;
	time_t begin_time;
	int rc = SLURM_SUCCESS;

	slurm_attr_init(&attr_run);
	if (pthread_create(&thread_ipmi_id_run, &attr_run,
			   &_thread_ipmi_run, NULL)) {
		//if (pthread_create(... (void *)arg)) {
		debug("energy accounting failed to create _thread_ipmi_run "
		      "thread: %m");
	}
	slurm_attr_destroy(&attr_run);

	begin_time = time(NULL);
	while (rc == SLURM_SUCCESS) {
		if (time(NULL) - begin_time > slurm_ipmi_conf.timeout) {
			error("ipmi thread init timeout");
			rc = SLURM_ERROR;
			break;
		}
		if (flag_thread_started)
			break;
		_task_sleep(1);
	}

	if (rc != SLURM_SUCCESS) {
		error("%s threads failed to start in a timely manner",
		     plugin_name);

		if (thread_ipmi_id_run) {
			pthread_cancel(thread_ipmi_id_run);
			pthread_join(thread_ipmi_id_run, NULL);
		}

		flag_energy_accounting_shutdown = true;
	} else {
		/* This is here to join the decay thread so we don't core
		 * dump if in the sleep, since there is no other place to join
		 * we have to create another thread to do it. */
		slurm_attr_init(&attr_run);
		if (pthread_create(&cleanup_handler_thread, &attr_run,
				   _cleanup_thread, NULL))
			fatal("pthread_create error %m");

		slurm_attr_destroy(&attr_run);
	}

	return NULL;
}

static int _get_joules_task(uint16_t delta)
{
	acct_gather_energy_t *last_energy = NULL;
	time_t now;
	static bool first = true;
	static uint32_t start_current_energy = 0;
	uint32_t adjustment = 0;

	last_energy = local_energy;
	local_energy = NULL;

	if (slurm_get_node_energy(NULL, delta, &local_energy)) {
		error("_get_joules_task: can't get info from slurmd");
		local_energy = last_energy;
		return SLURM_ERROR;
	}
	now = time(NULL);

	local_energy->previous_consumed_energy = last_energy->consumed_energy;

	if (slurm_ipmi_conf.adjustment)
		adjustment = _get_additional_consumption(
			local_energy->poll_time, now,
			local_energy->current_watts,
			local_energy->current_watts);

	if (!first) {
		local_energy->consumed_energy -= start_current_energy;

		local_energy->base_consumed_energy =
			(local_energy->consumed_energy
			 - last_energy->consumed_energy)
			+ adjustment;
	} else {
		/* This is just for the step, so take all the pervious
		   consumption out of the mix.
		*/
		start_current_energy =
			local_energy->consumed_energy + adjustment;
		local_energy->base_consumed_energy = 0;
		first = false;
	}

	local_energy->consumed_energy = local_energy->previous_consumed_energy
		+ local_energy->base_consumed_energy;

	acct_gather_energy_destroy(last_energy);

	if (debug_flags & DEBUG_FLAG_ENERGY)
		info("_get_joules_task: consumed %u Joules "
		     "(received %u(%u watts) from slurmd)",
		     local_energy->consumed_energy,
		     local_energy->base_consumed_energy,
		     local_energy->current_watts);

	return SLURM_SUCCESS;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	debug_flags = slurm_get_debug_flags();
	/* put anything that requires the .conf being read in
	   acct_gather_energy_p_conf_parse
	*/

	return SLURM_SUCCESS;
}

extern int fini(void)
{
	if (!_run_in_daemon())
		return SLURM_SUCCESS;

	flag_energy_accounting_shutdown = true;

	slurm_mutex_lock(&ipmi_mutex);
	if (thread_ipmi_id_run)
		pthread_cancel(thread_ipmi_id_run);
	if (cleanup_handler_thread)
		pthread_join(cleanup_handler_thread, NULL);
	slurm_mutex_unlock(&ipmi_mutex);

	acct_gather_energy_destroy(local_energy);
	local_energy = NULL;
	return SLURM_SUCCESS;
}

extern int acct_gather_energy_p_update_node_energy(void)
{
	int rc = SLURM_SUCCESS;
	xassert(_run_in_daemon());

	return rc;
}

extern int acct_gather_energy_p_get_data(enum acct_energy_type data_type,
					 void *data)
{
	int rc = SLURM_SUCCESS;
	acct_gather_energy_t *energy = (acct_gather_energy_t *)data;
	time_t *last_poll = (time_t *)data;

	xassert(_run_in_daemon());

	switch (data_type) {
	case ENERGY_DATA_JOULES_TASK:
		slurm_mutex_lock(&ipmi_mutex);
		if (_is_thread_launcher()) {
			if (_thread_init() == SLURM_SUCCESS)
				_thread_update_node_energy();
		} else
			_get_joules_task(10); /* Since we don't have
						 access to the
						 frequency here just
						 send in something.
					      */
		memcpy(energy, local_energy, sizeof(acct_gather_energy_t));
		slurm_mutex_unlock(&ipmi_mutex);
		break;
	case ENERGY_DATA_STRUCT:
		slurm_mutex_lock(&ipmi_mutex);
		memcpy(energy, local_energy, sizeof(acct_gather_energy_t));
		slurm_mutex_unlock(&ipmi_mutex);
		if (debug_flags & DEBUG_FLAG_ENERGY) {
			info("_get_joules_node_ipmi = consumed %d Joules",
			     energy->consumed_energy);
		}
		break;
	case ENERGY_DATA_LAST_POLL:
		slurm_mutex_lock(&ipmi_mutex);
		*last_poll = local_energy->poll_time;
		slurm_mutex_unlock(&ipmi_mutex);
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
	int *delta = (int *)data;

	xassert(_run_in_daemon());

	switch (data_type) {
	case ENERGY_DATA_RECONFIG:
		debug_flags = slurm_get_debug_flags();
		break;
	case ENERGY_DATA_PROFILE:
		slurm_mutex_lock(&ipmi_mutex);
		_get_joules_task(*delta);
		_ipmi_send_profile();
		slurm_mutex_unlock(&ipmi_mutex);
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
//	s_p_options_t *full_options_ptr;
	s_p_options_t options[] = {
		{"EnergyIPMIDriverType", S_P_UINT32},
		{"EnergyIPMIDisableAutoProbe", S_P_UINT32},
		{"EnergyIPMIDriverAddress", S_P_UINT32},
		{"EnergyIPMIRegisterSpacing", S_P_UINT32},
		{"EnergyIPMIDriverDevice", S_P_STRING},
		{"EnergyIPMIProtocolVersion", S_P_UINT32},
		{"EnergyIPMIUsername", S_P_STRING},
		{"EnergyIPMIPassword", S_P_STRING},
/* FIXME: remove these from the structure? */
//		{"EnergyIPMIk_g", S_P_STRING},
//		{"EnergyIPMIk_g_len", S_P_UINT32},
		{"EnergyIPMIPrivilegeLevel", S_P_UINT32},
		{"EnergyIPMIAuthenticationType", S_P_UINT32},
		{"EnergyIPMICipherSuiteId", S_P_UINT32},
		{"EnergyIPMISessionTimeout", S_P_UINT32},
		{"EnergyIPMIRetransmissionTimeout", S_P_UINT32},
		{"EnergyIPMIWorkaroundFlags", S_P_UINT32},
		{"EnergyIPMIRereadSdrCache", S_P_BOOLEAN},
		{"EnergyIPMIIgnoreNonInterpretableSensors", S_P_BOOLEAN},
		{"EnergyIPMIBridgeSensors", S_P_BOOLEAN},
		{"EnergyIPMIInterpretOemData", S_P_BOOLEAN},
		{"EnergyIPMISharedSensors", S_P_BOOLEAN},
		{"EnergyIPMIDiscreteReading", S_P_BOOLEAN},
		{"EnergyIPMIIgnoreScanningDisabled", S_P_BOOLEAN},
		{"EnergyIPMIAssumeBmcOwner", S_P_BOOLEAN},
		{"EnergyIPMIEntitySensorNames", S_P_BOOLEAN},
		{"EnergyIPMIFrequency", S_P_UINT32},
		{"EnergyIPMICalcAdjustment", S_P_BOOLEAN},
		{"EnergyIPMIPowerSensor", S_P_UINT32},
		{"EnergyIPMITimeout", S_P_UINT32},
		{"EnergyIPMIVariable", S_P_STRING},
		{NULL} };

	transfer_s_p_options(full_options, options, full_options_cnt);
}

extern void acct_gather_energy_p_conf_set(s_p_hashtbl_t *tbl)
{
	char *tmp_char;

	/* Set initial values */
	reset_slurm_ipmi_conf(&slurm_ipmi_conf);

	if (tbl) {
		/* ipmi initialisation parameters */
		s_p_get_uint32(&slurm_ipmi_conf.driver_type,
			       "EnergyIPMIDriverType", tbl);
		s_p_get_uint32(&slurm_ipmi_conf.disable_auto_probe,
			       "EnergyIPMIDisableAutoProbe", tbl);
		s_p_get_uint32(&slurm_ipmi_conf.driver_address,
			       "EnergyIPMIDriverAddress", tbl);
		s_p_get_uint32(&slurm_ipmi_conf.register_spacing,
			       "EnergyIPMIRegisterSpacing", tbl);

		s_p_get_string(&slurm_ipmi_conf.driver_device,
			       "EnergyIPMIDriverDevice", tbl);

		s_p_get_uint32(&slurm_ipmi_conf.protocol_version,
			       "EnergyIPMIProtocolVersion", tbl);

		if (!s_p_get_string(&slurm_ipmi_conf.username,
				    "EnergyIPMIUsername", tbl))
			slurm_ipmi_conf.username = xstrdup(DEFAULT_IPMI_USER);

		s_p_get_string(&slurm_ipmi_conf.password,
			       "EnergyIPMIPassword", tbl);
		if (!slurm_ipmi_conf.password)
			slurm_ipmi_conf.password = xstrdup("foopassword");

		s_p_get_uint32(&slurm_ipmi_conf.privilege_level,
			       "EnergyIPMIPrivilegeLevel", tbl);
		s_p_get_uint32(&slurm_ipmi_conf.authentication_type,
			       "EnergyIPMIAuthenticationType", tbl);
		s_p_get_uint32(&slurm_ipmi_conf.cipher_suite_id,
			       "EnergyIPMICipherSuiteId", tbl);
		s_p_get_uint32(&slurm_ipmi_conf.session_timeout,
			       "EnergyIPMISessionTimeout", tbl);
		s_p_get_uint32(&slurm_ipmi_conf.retransmission_timeout,
			       "EnergyIPMIRetransmissionTimeout", tbl);
		s_p_get_uint32(&slurm_ipmi_conf.workaround_flags,
			       "EnergyIPMIWorkaroundFlags", tbl);

		if (!s_p_get_boolean(&slurm_ipmi_conf.reread_sdr_cache,
				     "EnergyIPMIRereadSdrCache", tbl))
			slurm_ipmi_conf.reread_sdr_cache = false;
		if (!s_p_get_boolean(&slurm_ipmi_conf.
				     ignore_non_interpretable_sensors,
				     "EnergyIPMIIgnoreNonInterpretableSensors",
				     tbl))
			slurm_ipmi_conf.ignore_non_interpretable_sensors =
				false;
		if (!s_p_get_boolean(&slurm_ipmi_conf.bridge_sensors,
				     "EnergyIPMIBridgeSensors", tbl))
			slurm_ipmi_conf.bridge_sensors = false;
		if (!s_p_get_boolean(&slurm_ipmi_conf.interpret_oem_data,
				     "EnergyIPMIInterpretOemData", tbl))
			slurm_ipmi_conf.interpret_oem_data = false;
		if (!s_p_get_boolean(&slurm_ipmi_conf.shared_sensors,
				     "EnergyIPMISharedSensors", tbl))
			slurm_ipmi_conf.shared_sensors = false;
		if (!s_p_get_boolean(&slurm_ipmi_conf.discrete_reading,
				     "EnergyIPMIDiscreteReading", tbl))
			slurm_ipmi_conf.discrete_reading = false;
		if (!s_p_get_boolean(&slurm_ipmi_conf.ignore_scanning_disabled,
				     "EnergyIPMIIgnoreScanningDisabled", tbl))
			slurm_ipmi_conf.ignore_scanning_disabled = false;
		if (!s_p_get_boolean(&slurm_ipmi_conf.assume_bmc_owner,
				     "EnergyIPMIAssumeBmcOwner", tbl))
			slurm_ipmi_conf.assume_bmc_owner = false;
		if (!s_p_get_boolean(&slurm_ipmi_conf.entity_sensor_names,
				     "EnergyIPMIEntitySensorNames", tbl))
			slurm_ipmi_conf.entity_sensor_names = false;

		s_p_get_uint32(&slurm_ipmi_conf.freq,
			       "EnergyIPMIFrequency", tbl);

		if ((int)slurm_ipmi_conf.freq <= 0)
			fatal("EnergyIPMIFrequency must be a positive integer "
			      "in acct_gather.conf.");

		if (!s_p_get_boolean(&(slurm_ipmi_conf.adjustment),
				     "EnergyIPMICalcAdjustment", tbl))
			slurm_ipmi_conf.adjustment = false;

		s_p_get_uint32(&slurm_ipmi_conf.power_sensor_num,
			       "EnergyIPMIPowerSensor", tbl);

		s_p_get_uint32(&slurm_ipmi_conf.timeout,
			       "EnergyIPMITimeout", tbl);

		if (s_p_get_string(&tmp_char, "EnergyIPMIVariable", tbl)) {
			if (!strcmp(tmp_char, "Temp"))
				slurm_ipmi_conf.variable =
					IPMI_MONITORING_SENSOR_TYPE_TEMPERATURE;
			xfree(tmp_char);
		}
	}

	if (!_run_in_daemon())
		return;

	if (!flag_init) {
		local_energy = acct_gather_energy_alloc();
		local_energy->consumed_energy=0;
		local_energy->base_consumed_energy=0;
		local_energy->base_watts=0;
		flag_init = true;
		if (_is_thread_launcher()) {
			pthread_attr_t attr;
			slurm_attr_init(&attr);
			if (pthread_create(&thread_ipmi_id_launcher, &attr,
					   &_thread_launcher, NULL)) {
				//if (pthread_create(... (void *)arg)) {
				debug("energy accounting failed to create "
				      "_thread_launcher thread: %m");
			}
			slurm_attr_destroy(&attr);
			if (debug_flags & DEBUG_FLAG_ENERGY)
				info("%s thread launched", plugin_name);
		} else
			_get_joules_task(0);
	}

	verbose("%s loaded", plugin_name);
}

extern void acct_gather_energy_p_conf_values(List *data)
{
	config_key_pair_t *key_pair;

	xassert(*data);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EnergyIPMIDriverType");
	key_pair->value = xstrdup_printf("%u", slurm_ipmi_conf.driver_type);
	list_append(*data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EnergyIPMIDisableAutoProbe");
	key_pair->value = xstrdup_printf("%u",
					 slurm_ipmi_conf.disable_auto_probe);
	list_append(*data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EnergyIPMIDriverAddress");
	key_pair->value = xstrdup_printf("%u", slurm_ipmi_conf.driver_address);
	list_append(*data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EnergyIPMIRegisterSpacing");
	key_pair->value = xstrdup_printf("%u",
					 slurm_ipmi_conf.register_spacing);
	list_append(*data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EnergyIPMIDriverDevice");
	key_pair->value = xstrdup(slurm_ipmi_conf.driver_device);
	list_append(*data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EnergyIPMIProtocolVersion");
	key_pair->value = xstrdup_printf("%u",
					 slurm_ipmi_conf.protocol_version);
	list_append(*data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EnergyIPMIUsername");
	key_pair->value = xstrdup(slurm_ipmi_conf.username);
	list_append(*data, key_pair);

	/* Don't give out the password */
	/* key_pair = xmalloc(sizeof(config_key_pair_t)); */
	/* key_pair->name = xstrdup("EnergyIPMIPassword"); */
	/* key_pair->value = xstrdup(slurm_ipmi_conf.password); */
	/* list_append(*data, key_pair); */

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EnergyIPMIPrivilegeLevel");
	key_pair->value = xstrdup_printf("%u", slurm_ipmi_conf.privilege_level);
	list_append(*data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EnergyIPMIAuthenticationType");
	key_pair->value = xstrdup_printf("%u",
					 slurm_ipmi_conf.authentication_type);
	list_append(*data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EnergyIPMICipherSuiteId");
	key_pair->value = xstrdup_printf("%u", slurm_ipmi_conf.cipher_suite_id);
	list_append(*data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EnergyIPMISessionTimeout");
	key_pair->value = xstrdup_printf("%u", slurm_ipmi_conf.session_timeout);
	list_append(*data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EnergyIPMIRetransmissionTimeout");
	key_pair->value = xstrdup_printf(
		"%u", slurm_ipmi_conf.retransmission_timeout);
	list_append(*data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EnergyIPMIWorkaroundFlags");
	key_pair->value = xstrdup_printf(
		"%u", slurm_ipmi_conf.workaround_flags);
	list_append(*data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EnergyIPMIRereadSdrCache");
	key_pair->value = xstrdup(slurm_ipmi_conf.reread_sdr_cache
				  ? "Yes" : "No");
	list_append(*data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EnergyIPMIIgnoreNonInterpretableSensors");
	key_pair->value = xstrdup(
		slurm_ipmi_conf.ignore_non_interpretable_sensors
		? "Yes" : "No");
	list_append(*data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EnergyIPMIBridgeSensors");
	key_pair->value = xstrdup(slurm_ipmi_conf.bridge_sensors
				  ? "Yes" : "No");
	list_append(*data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EnergyIPMIInterpretOemData");
	key_pair->value = xstrdup(slurm_ipmi_conf.interpret_oem_data
				  ? "Yes" : "No");
	list_append(*data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EnergyIPMISharedSensors");
	key_pair->value = xstrdup(slurm_ipmi_conf.shared_sensors
				  ? "Yes" : "No");
	list_append(*data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EnergyIPMIDiscreteReading");
	key_pair->value = xstrdup(slurm_ipmi_conf.discrete_reading
				  ? "Yes" : "No");
	list_append(*data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EnergyIPMIIgnoreScanningDisabled");
	key_pair->value = xstrdup(slurm_ipmi_conf.ignore_scanning_disabled
				  ? "Yes" : "No");
	list_append(*data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EnergyIPMIAssumeBmcOwner");
	key_pair->value = xstrdup(slurm_ipmi_conf.assume_bmc_owner
				  ? "Yes" : "No");
	list_append(*data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EnergyIPMIEntitySensorNames");
	key_pair->value = xstrdup(slurm_ipmi_conf.entity_sensor_names
				  ? "Yes" : "No");
	list_append(*data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EnergyIPMIFrequency");
	key_pair->value = xstrdup_printf("%u", slurm_ipmi_conf.freq);
	list_append(*data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EnergyIPMICalcAdjustment");
	key_pair->value = xstrdup(slurm_ipmi_conf.adjustment
				  ? "Yes" : "No");
	list_append(*data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EnergyIPMIPowerSensor");
	key_pair->value = xstrdup_printf(
		"%u", slurm_ipmi_conf.power_sensor_num);
	list_append(*data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EnergyIPMITimeout");
	key_pair->value = xstrdup_printf("%u", slurm_ipmi_conf.timeout);
	list_append(*data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EnergyIPMIVariable");
	switch (slurm_ipmi_conf.variable) {
	case IPMI_MONITORING_SENSOR_TYPE_TEMPERATURE:
		key_pair->value = xstrdup("Temp");
		break;
	case IPMI_MONITORING_SENSOR_UNITS_WATTS:
		key_pair->value = xstrdup("Watts");
		break;
	default:
		key_pair->value = xstrdup("Unknown");
		break;
	}
	list_append(*data, key_pair);

	return;

}
