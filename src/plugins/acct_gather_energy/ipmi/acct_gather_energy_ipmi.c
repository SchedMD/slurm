/*****************************************************************************\
 *  acct_gather_energy_ipmi.c - slurm energy accounting plugin for ipmi.
 *****************************************************************************
 *  Copyright (C) 2012
 *  Initially written by Thomas Cadeau @ Bull. Adapted by Yoann Blein @ Bull.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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
#define IPMI_VERSION 2		/* Data structure version number */
#define MAX_LOG_ERRORS 5	/* Max sensor reading errors log messages */

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
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */

const char plugin_name[] = "AcctGatherEnergy IPMI plugin";
const char plugin_type[] = "acct_gather_energy/ipmi";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

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
static time_t last_update_time = 0;
static time_t previous_update_time = 0;

/* array of struct to track the status of multiple sensors */
typedef struct sensor_status {
	uint32_t id;
	uint32_t last_update_watt;
	acct_gather_energy_t energy;
} sensor_status_t;
static sensor_status_t *sensors = NULL;
static uint16_t sensors_len = 0;
static uint64_t *start_current_energies = NULL;

/* array of struct describing the configuration of the sensors */
typedef struct description {
	const char* label;
	uint16_t sensor_cnt;
	uint16_t *sensor_idxs;
} description_t;
static description_t *descriptions;
static uint16_t       descriptions_len;
static const char *NODE_DESC = "Node";

static int dataset_id = -1; /* id of the dataset for profile data */

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
static uint64_t _get_additional_consumption(time_t time0, time_t time1,
					    uint32_t watt0, uint32_t watt1)
{
	return (uint64_t) ((time1 - time0)*(watt1 + watt0)/2);
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
	/* check the sensors list */
	void *sensor_reading;
	int rc;
	int sensor_units;
	uint16_t i;
	unsigned int ids[sensors_len];
	static uint8_t check_err_cnt = 0;

	for (i = 0; i < sensors_len; ++i)
		ids[i] = sensors[i].id;
	rc = ipmi_monitoring_sensor_readings_by_record_id(ipmi_ctx,
							  hostname,
							  &ipmi_config,
							  sensor_reading_flags,
							  ids,
							  sensors_len,
							  NULL,
							  NULL);
	if (rc != sensors_len) {
		if (check_err_cnt < MAX_LOG_ERRORS) {
			error("ipmi_monitoring_sensor_readings_by_record_id: "
			      "%s", ipmi_monitoring_ctx_errormsg(ipmi_ctx));
			check_err_cnt++;
		} else if (check_err_cnt == MAX_LOG_ERRORS) {
			error("ipmi_monitoring_sensor_readings_by_record_id: "
			      "%s. Stop logging these errors after %d attempts",
			      ipmi_monitoring_ctx_errormsg(ipmi_ctx),
			      MAX_LOG_ERRORS);
			check_err_cnt++;
		}
		return SLURM_FAILURE;
	}

	check_err_cnt = 0;

	i = 0;
	do {
		/* check if the sensor unit is watts */
		sensor_units =
		    ipmi_monitoring_sensor_read_sensor_units(ipmi_ctx);
		if (sensor_units < 0) {
			error("ipmi_monitoring_sensor_read_sensor_units: %s",
			      ipmi_monitoring_ctx_errormsg(ipmi_ctx));
			return SLURM_FAILURE;
		}
		if (sensor_units != slurm_ipmi_conf.variable) {
			error("Configured sensor is not in Watt, "
			      "please check ipmi.conf");
			return SLURM_FAILURE;
		}

		/* update current value of the sensor */
		sensor_reading =
		    ipmi_monitoring_sensor_read_sensor_reading(ipmi_ctx);
		if (sensor_reading) {
			sensors[i].last_update_watt =
			    (uint32_t) (*((double *)sensor_reading));
		} else {
			error("ipmi read an empty value for power consumption");
			return SLURM_FAILURE;
		}
		++i;
	} while (ipmi_monitoring_sensor_iterator_next(ipmi_ctx));

	previous_update_time = last_update_time;
	last_update_time = time(NULL);

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
	static uint8_t find_err_cnt = 0;

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
		if (find_err_cnt < MAX_LOG_ERRORS) {
			error("ipmi_monitoring_sensor_readings_by_record_id: "
			      "%s", ipmi_monitoring_ctx_errormsg(ipmi_ctx));
			find_err_cnt++;
		} else if (find_err_cnt == MAX_LOG_ERRORS) {
			error("ipmi_monitoring_sensor_readings_by_record_id: "
			      "%s. Stop logging these errors after %d attempts",
			      ipmi_monitoring_ctx_errormsg(ipmi_ctx),
			      MAX_LOG_ERRORS);
			find_err_cnt++;
		}
		return SLURM_FAILURE;
	}

	find_err_cnt = 0;

	for (i = 0; i < sensor_count; i++,
		     ipmi_monitoring_sensor_iterator_next(ipmi_ctx)) {
		sensor_units =
			ipmi_monitoring_sensor_read_sensor_units(ipmi_ctx);
		if (sensor_units < 0) {
			error("ipmi_monitoring_sensor_read_sensor_units: %s",
			      ipmi_monitoring_ctx_errormsg(ipmi_ctx));
			return SLURM_FAILURE;
		}

		if (sensor_units != slurm_ipmi_conf.variable)
			continue;

		record_id = ipmi_monitoring_sensor_read_record_id(ipmi_ctx);
		if (record_id < 0) {
			error("ipmi_monitoring_sensor_read_record_id: %s",
			      ipmi_monitoring_ctx_errormsg(ipmi_ctx));
			return SLURM_FAILURE;
		}

		sensor_reading =
			ipmi_monitoring_sensor_read_sensor_reading(ipmi_ctx);
		if (sensor_reading) {
			/* we found a valid sensor, allocate room for its
			 * status and its description as the main sensor */
			sensors_len = 1;
			sensors = xmalloc(sizeof(sensor_status_t));
			sensors[0].id = (uint32_t)record_id;
			sensors[0].last_update_watt =
			    (uint32_t) (*((double *)sensor_reading));

			descriptions_len = 1;
			descriptions = xmalloc(sizeof(description_t));
			descriptions[0].label = xstrdup(NODE_DESC);
			descriptions[0].sensor_cnt = 1;
			descriptions[0].sensor_idxs = xmalloc(sizeof(uint16_t));
			descriptions[0].sensor_idxs[0] = 0;

			previous_update_time = last_update_time;
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
		info("Power sensor found: %d", sensors_len);

	return rc;
}

/*
 * _read_ipmi_values read the Power sensor and update last_update_watt and times
 */
static int _read_ipmi_values(void)
{
	/* read sensors list */
	void *sensor_reading;
	int rc;
	uint16_t i;
	unsigned int ids[sensors_len];
	static uint8_t read_err_cnt = 0;

	for (i = 0; i < sensors_len; ++i)
		ids[i] = sensors[i].id;
	rc = ipmi_monitoring_sensor_readings_by_record_id(ipmi_ctx,
							  hostname,
							  &ipmi_config,
							  sensor_reading_flags,
							  ids,
							  sensors_len,
							  NULL,
							  NULL);

	if (rc != sensors_len) {
		if (read_err_cnt < MAX_LOG_ERRORS) {
			error("ipmi_monitoring_sensor_readings_by_record_id: "
			      "%s", ipmi_monitoring_ctx_errormsg(ipmi_ctx));
			read_err_cnt++;
		} else if (read_err_cnt == MAX_LOG_ERRORS) {
			error("ipmi_monitoring_sensor_readings_by_record_id: "
			      "%s. Stop logging these errors after %d attempts",
			      ipmi_monitoring_ctx_errormsg(ipmi_ctx),
			      MAX_LOG_ERRORS);
			read_err_cnt++;
		}
		return SLURM_FAILURE;
	}

	read_err_cnt = 0;

	i = 0;
	do {
		sensor_reading =
		    ipmi_monitoring_sensor_read_sensor_reading(ipmi_ctx);
		if (sensor_reading) {
			sensors[i].last_update_watt =
			    (uint32_t) (*((double *)sensor_reading));
		} else {
			error("ipmi read an empty value for power consumption");
			return SLURM_FAILURE;
		}
		++i;
	} while (ipmi_monitoring_sensor_iterator_next(ipmi_ctx));

	previous_update_time = last_update_time;
	last_update_time = time(NULL);

	return SLURM_SUCCESS;
}

/* updates the given energy according to the last watt reading of the sensor */
static void _update_energy(acct_gather_energy_t *e, uint32_t last_update_watt)
{
	if (e->current_watts) {
		e->base_watts = e->current_watts;
		e->current_watts = last_update_watt;
		if (previous_update_time == 0)
			e->base_consumed_energy = 0;
		else
			e->base_consumed_energy =
				_get_additional_consumption(
					previous_update_time,
					last_update_time,
					e->base_watts,
					e->current_watts);
		e->previous_consumed_energy = e->consumed_energy;
		e->consumed_energy += e->base_consumed_energy;
	} else {
		e->consumed_energy = 0;
		e->base_watts = 0;
		e->current_watts = last_update_watt;
	}
	e->poll_time = time(NULL);
}

/*
 * _thread_update_node_energy calls _read_ipmi_values and updates all values
 * for node consumption
 */
static int _thread_update_node_energy(void)
{
	int rc = SLURM_SUCCESS;
	uint16_t i;

	rc = _read_ipmi_values();

	if (rc == SLURM_SUCCESS) {
		/* sensors list */
		for (i = 0; i < sensors_len; ++i) {
			if (sensors[i].energy.current_watts == NO_VAL)
				return rc;
			_update_energy(&sensors[i].energy,
				       sensors[i].last_update_watt);
		}

		if (previous_update_time == 0)
			previous_update_time = last_update_time;
	}

	if (debug_flags & DEBUG_FLAG_ENERGY) {
		for (i = 0; i < sensors_len; ++i)
			info("ipmi-thread: sensor %u current_watts: %u, "
			     "consumed %"PRIu64" Joules %"PRIu64" new",
			     sensors[i].id,
			     sensors[i].energy.current_watts,
			     sensors[i].energy.consumed_energy,
			     sensors[i].energy.base_consumed_energy);
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
	uint16_t i;

	if (!first)
		return first_init;
	first = false;

	if (_init_ipmi_config() != SLURM_SUCCESS) {
		//TODO verbose error?
		rc = SLURM_FAILURE;
	} else {
		if ((sensors_len == 0 && _find_power_sensor() != SLURM_SUCCESS)
		    || _check_power_sensor() != SLURM_SUCCESS) {
			/* no valid sensors found */
			for (i = 0; i < sensors_len; ++i) {
				sensors[i].energy.current_watts = NO_VAL;
			}
		} else {
			for (i = 0; i < sensors_len; ++i) {
				sensors[i].energy.current_watts =
					sensors[i].last_update_watt;
			}
		}
		if (slurm_ipmi_conf.reread_sdr_cache)
			//IPMI cache is reread only on initialisation
			//This option need a big EnergyIPMITimeout
			sensor_reading_flags ^=
				IPMI_MONITORING_SENSOR_READING_FLAGS_REREAD_SDR_CACHE;
	}
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
	uint16_t i, j;
	uint64_t data[descriptions_len];
	uint32_t id;
	time_t last_time = last_update_time;

	if (!_running_profile())
		return SLURM_SUCCESS;

	if (dataset_id < 0) {
		acct_gather_profile_dataset_t dataset[descriptions_len+1];
		for (i = 0; i < descriptions_len; i++) {
			dataset[i].name = xstrdup_printf(
				"%sPower", descriptions[i].label);
			dataset[i].type = PROFILE_FIELD_UINT64;
		}
		dataset[i].name = NULL;
		dataset[i].type = PROFILE_FIELD_NOT_SET;
		dataset_id = acct_gather_profile_g_create_dataset(
			"Energy", NO_PARENT, dataset);
		for (i = 0; i < descriptions_len; ++i)
			xfree(dataset[i].name);
		if (debug_flags & DEBUG_FLAG_ENERGY)
			debug("Energy: dataset created (id = %d)", dataset_id);
		if (dataset_id == SLURM_ERROR) {
			error("Energy: Failed to create the dataset for IPMI");
			return SLURM_ERROR;
		}
	}

	/* pack an array of uint64_t with current power of sensors */
	memset(data, 0, sizeof(data));
	for (i = 0; i < descriptions_len; ++i) {
		for (j = 0; j < descriptions[i].sensor_cnt; ++j) {
			id = descriptions[i].sensor_idxs[j];
			data[i] += sensors[id].energy.current_watts;
		}
		if (descriptions[i].sensor_cnt)
			last_time = sensors[id].energy.poll_time;
	}

	if (debug_flags & DEBUG_FLAG_PROFILE) {
		for (i = 0; i < descriptions_len; i++) {
			id = descriptions[i].sensor_idxs[j];
			info("PROFILE-Energy: %sPower=%d",
			     descriptions[i].label,
			     sensors[id].energy.current_watts);
		}
	}
	return acct_gather_profile_g_add_sample_data(dataset_id, (void *)data,
						     last_time);
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
	time_t now = time(NULL);
	static bool first = true;
	uint64_t adjustment = 0;
	uint16_t i;
	acct_gather_energy_t *new, *old;

	/* sensors list */
	acct_gather_energy_t *energies = NULL;
	uint16_t sensor_cnt = 0;

	if (slurm_get_node_energy(NULL, delta, &sensor_cnt, &energies)) {
		error("_get_joules_task: can't get info from slurmd");
		return SLURM_ERROR;
	}
	if (first) {
		sensors_len = sensor_cnt;
		sensors = xmalloc(sizeof(sensor_status_t) * sensors_len);
		start_current_energies =
			xmalloc(sizeof(uint64_t) * sensors_len);
	}

	if (sensor_cnt != sensors_len) {
		error("_get_joules_task: received %u sensors, %u expected",
		      sensor_cnt, sensors_len);
		acct_gather_energy_destroy(energies);
		return SLURM_ERROR;
	}


	for (i = 0; i < sensor_cnt; ++i) {
		new = &energies[i];
		old = &sensors[i].energy;
		new->previous_consumed_energy = old->consumed_energy;

		if (slurm_ipmi_conf.adjustment)
			adjustment = _get_additional_consumption(
				new->poll_time, now,
				new->current_watts,
				new->current_watts);

		if (!first) {
			new->consumed_energy -= start_current_energies[i];
			new->base_consumed_energy = adjustment +
				(new->consumed_energy - old->consumed_energy);
		} else {
			/* This is just for the step, so take all the pervious
			   consumption out of the mix.
			   */
			start_current_energies[i] =
				new->consumed_energy + adjustment;
			new->base_consumed_energy = 0;
		}

		new->consumed_energy = new->previous_consumed_energy
			+ new->base_consumed_energy;
		memcpy(old, new, sizeof(acct_gather_energy_t));

		if (debug_flags & DEBUG_FLAG_ENERGY)
			info("_get_joules_task: consumed %"PRIu64" Joules "
			     "(received %"PRIu64"(%u watts) from slurmd)",
			     new->consumed_energy,
			     new->base_consumed_energy,
			     new->current_watts);
	}

	acct_gather_energy_destroy(energies);

	first = false;

	return SLURM_SUCCESS;
}

static void _get_node_energy(acct_gather_energy_t *energy)
{
	uint16_t i, j, id;
	acct_gather_energy_t *e;

	/* find the "Node" description */
	for (i = 0; i < descriptions_len; ++i)
		if (xstrcmp(descriptions[i].label, NODE_DESC) == 0)
			break;
	/* not found, init is not finished or there is no watt sensors */
	if (i >= descriptions_len)
		return;

	/* sum the energy of all sensors described for "Node" */
	memset(energy, 0, sizeof(acct_gather_energy_t));
	for (j = 0; j < descriptions[i].sensor_cnt; ++j) {
		id = descriptions[i].sensor_idxs[j];
		e = &sensors[id].energy;
		energy->base_consumed_energy += e->base_consumed_energy;
		energy->base_watts += e->base_watts;
		energy->consumed_energy += e->consumed_energy;
		energy->current_watts += e->current_watts;
		energy->previous_consumed_energy += e->previous_consumed_energy;
		/* node poll_time is computed as the oldest poll_time of
		   the sensors */
		if (energy->poll_time == 0 || energy->poll_time > e->poll_time)
			energy->poll_time = e->poll_time;
	}
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
	uint16_t i;

	if (!_run_in_daemon())
		return SLURM_SUCCESS;

	flag_energy_accounting_shutdown = true;

	slurm_mutex_lock(&ipmi_mutex);
	if (thread_ipmi_id_run)
		pthread_cancel(thread_ipmi_id_run);
	if (cleanup_handler_thread)
		pthread_join(cleanup_handler_thread, NULL);
	slurm_mutex_unlock(&ipmi_mutex);

	xfree(sensors);
	xfree(start_current_energies);

	for (i = 0; i < descriptions_len; ++i) {
		xfree(descriptions[i].label);
		xfree(descriptions[i].sensor_idxs);
	}
	xfree(descriptions);

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
	uint16_t i;
	int rc = SLURM_SUCCESS;
	acct_gather_energy_t *energy = (acct_gather_energy_t *)data;
	time_t *last_poll = (time_t *)data;
	uint16_t *sensor_cnt = (uint16_t *)data;

	xassert(_run_in_daemon());

	switch (data_type) {
	case ENERGY_DATA_NODE_ENERGY_UP:
		slurm_mutex_lock(&ipmi_mutex);
		if (_is_thread_launcher()) {
			if (_thread_init() == SLURM_SUCCESS)
				_thread_update_node_energy();
		} else {
			_get_joules_task(10);
		}
		_get_node_energy(energy);
		slurm_mutex_unlock(&ipmi_mutex);
		break;
	case ENERGY_DATA_NODE_ENERGY:
		slurm_mutex_lock(&ipmi_mutex);
		_get_node_energy(energy);
		slurm_mutex_unlock(&ipmi_mutex);
		break;
	case ENERGY_DATA_LAST_POLL:
		slurm_mutex_lock(&ipmi_mutex);
		*last_poll = last_update_time;
		slurm_mutex_unlock(&ipmi_mutex);
		break;
	case ENERGY_DATA_SENSOR_CNT:
		*sensor_cnt = sensors_len;
		break;
	case ENERGY_DATA_STRUCT:
		slurm_mutex_lock(&ipmi_mutex);
		for (i = 0; i < sensors_len; ++i)
			memcpy(&energy[i], &sensors[i].energy,
				sizeof(acct_gather_energy_t));
		slurm_mutex_unlock(&ipmi_mutex);
		break;
	case ENERGY_DATA_JOULES_TASK:
		slurm_mutex_lock(&ipmi_mutex);
		if (_is_thread_launcher()) {
			if (_thread_init() == SLURM_SUCCESS)
				_thread_update_node_energy();
		} else {
			_get_joules_task(10);
		}
		for (i = 0; i < sensors_len; ++i)
			memcpy(&energy[i], &sensors[i].energy,
				sizeof(acct_gather_energy_t));
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

/* Parse the sensor descriptions stored into slurm_ipmi_conf.power_sensors.
 * Expected format: comma-separated sensors ids and semi-colon-separated
 * sensors descriptions. Also expects a mandatory description with label
 * "Node". */
static int _parse_sensor_descriptions(void)
{
	/* TODO: error propagation */

	const char *sep1 = ";";
	const char *sep2 = ",";
	char *str_desc_list, *str_desc, *str_id, *mid, *endptr;
	char *saveptr1, *saveptr2; // pointers for strtok_r storage
	uint16_t i, j, k;
	uint16_t id;
	uint16_t *idx;
	description_t *d;
	bool found;

	if (!slurm_ipmi_conf.power_sensors || !slurm_ipmi_conf.power_sensors[0])
		return SLURM_SUCCESS;

	/* count the number of descriptions */
	str_desc_list = xstrdup(slurm_ipmi_conf.power_sensors);
	descriptions_len = 0;
	str_desc = strtok_r(str_desc_list, sep1, &saveptr1);
	while (str_desc) {
		++descriptions_len;
		str_desc = strtok_r(NULL, sep1, &saveptr1);
	}

	descriptions = xmalloc(sizeof(description_t) * descriptions_len);

	/* parse descriptions */
	strcpy(str_desc_list, slurm_ipmi_conf.power_sensors);
	i = 0;
	str_desc = strtok_r(str_desc_list, sep1, &saveptr1);
	while (str_desc) {
		mid = xstrchr(str_desc, '=');
		if (!mid || mid == str_desc) {
			goto error;
		}
		/* label */
		*mid = '\0';
		d = &descriptions[i];
		d->label = xstrdup(str_desc);
		/* associated sensors */
		++mid;
		str_id = strtok_r(mid, sep2, &saveptr2);
		/* parse sensor ids of the current description */
		while (str_id) {
			id = strtol(str_id, &endptr, 10);
			if (*endptr != '\0')
				goto error;
			d->sensor_cnt++;
			xrealloc(d->sensor_idxs,
				 sizeof(uint16_t) * d->sensor_cnt);
			d->sensor_idxs[d->sensor_cnt - 1] = id;
			str_id = strtok_r(NULL, sep2, &saveptr2);
		}
		++i;
		str_desc = strtok_r(NULL, sep1, &saveptr1);
	}
	xfree(str_desc_list);

	/* Ensure that the "Node" description is provided */
	found = false;
	for (i = 0; i < descriptions_len && !found; ++i)
		found = (xstrcasecmp(descriptions[i].label, NODE_DESC) == 0);
	if (!found)
		goto error;

	/* Here we have the list of descriptions with sensors ids in the
	 * sensors_idxs field instead of their indexes. We still have to
	 * gather the unique sensors ids and replace sensors_idxs by their
	 * indexes in the sensors array */
	for (i = 0; i < descriptions_len; ++i) {
		for (j = 0; j < descriptions[i].sensor_cnt; ++j) {
			idx = &descriptions[i].sensor_idxs[j];
			found = false;
			for (k = 0; k < sensors_len && !found; ++k)
				found = (*idx == sensors[k].id);
			if (found) {
				*idx = k - 1;
			} else {
				++sensors_len;
				xrealloc(sensors, sensors_len
					 * sizeof(sensor_status_t));
				sensors[sensors_len - 1].id = *idx;
				*idx = sensors_len - 1;;
			}
		}
	}

	return SLURM_SUCCESS;

error:
	error("Configuration of EnergyIPMIPowerSensors is malformed. "
	      "Make sure that the expected format is respected and that "
	      "the \"Node\" label is provided.");
	for (i = 0; i < descriptions_len; ++i) {
		xfree(descriptions[i].label);
		xfree(descriptions[i].sensor_idxs);
	}
	xfree(descriptions); descriptions = NULL;
	return SLURM_ERROR;
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
		{"EnergyIPMIPowerSensors", S_P_STRING},
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

		s_p_get_string(&slurm_ipmi_conf.power_sensors,
			       "EnergyIPMIPowerSensors", tbl);

		s_p_get_uint32(&slurm_ipmi_conf.timeout,
			       "EnergyIPMITimeout", tbl);

		if (s_p_get_string(&tmp_char, "EnergyIPMIVariable", tbl)) {
			if (!xstrcmp(tmp_char, "Temp"))
				slurm_ipmi_conf.variable =
					IPMI_MONITORING_SENSOR_UNITS_CELSIUS;
			else if (!xstrcmp(tmp_char, "Voltage"))
				slurm_ipmi_conf.variable =
					IPMI_MONITORING_SENSOR_UNITS_VOLTS;
			else if (!xstrcmp(tmp_char, "Fan"))
				slurm_ipmi_conf.variable =
					IPMI_MONITORING_SENSOR_UNITS_RPM;
			xfree(tmp_char);
		}
	}

	if (!_run_in_daemon())
		return;

	if (!flag_init) {
		/* try to parse the PowerSensors settings */
		_parse_sensor_descriptions();

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
	key_pair->name = xstrdup("EnergyIPMIPowerSensors");
	key_pair->value =
	    xstrdup_printf("%s", slurm_ipmi_conf.power_sensors);
	list_append(*data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EnergyIPMITimeout");
	key_pair->value = xstrdup_printf("%u", slurm_ipmi_conf.timeout);
	list_append(*data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EnergyIPMIVariable");
	switch (slurm_ipmi_conf.variable) {
	case IPMI_MONITORING_SENSOR_UNITS_CELSIUS:
		key_pair->value = xstrdup("Temp");
		break;
	case IPMI_MONITORING_SENSOR_UNITS_RPM:
		key_pair->value = xstrdup("Fan");
		break;
	case IPMI_MONITORING_SENSOR_UNITS_VOLTS:
		key_pair->value = xstrdup("Voltage");
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
