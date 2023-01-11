/*****************************************************************************\
 *  acct_gather_energy_ipmi.c - slurm energy accounting plugin for ipmi.
 *****************************************************************************
 *  Copyright (C) 2012
 *  Initially written by Thomas Cadeau @ Bull. Adapted by Yoann Blein @ Bull.
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
#include "src/interfaces/acct_gather_energy.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/fd.h"
#include "src/common/xstring.h"
#include "src/interfaces/proctrack.h"

#include "src/slurmd/slurmd/slurmd.h"
#include "acct_gather_energy_ipmi_config.h"

/*
 * freeipmi includes for the lib
 */
#include <freeipmi/freeipmi.h>
#include <ipmi_monitoring.h>
#include <ipmi_monitoring_bitmasks.h>

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
extern slurmd_conf_t *conf __attribute__((weak_import));
#else
slurmd_conf_t *conf = NULL;
#endif

#define _DEBUG 1
#define _DEBUG_ENERGY 1
#define IPMI_VERSION 2		/* Data structure version number */
#define MAX_LOG_ERRORS 5	/* Max sensor reading errors log messages */

/* IPMI extended DCMI power modes will be identified by these invented ids. */
#define DCMI_MODE 0xBEEF
#define DCMI_ENH_MODE 0xBEAF

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
static stepd_step_rec_t *step = NULL;
static int context_id = -1;

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
static bool flag_energy_accounting_shutdown = false;
static bool flag_thread_started = false;
static bool flag_init = false;
static pthread_mutex_t ipmi_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ipmi_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t launch_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t launch_cond = PTHREAD_COND_INITIALIZER;
pthread_t thread_ipmi_id_launcher = 0;
pthread_t thread_ipmi_id_run = 0;

/*
 * DCMI context cannot be reused between threads and this plugin can be called
 * from different slurmd threads, so we need the __thread specifier.
 */
__thread ipmi_ctx_t ipmi_dcmi_ctx = NULL;
static int dcmi_cnt = 0;

static int _read_ipmi_dcmi_values(void);
static int _read_ipmi_non_dcmi_values(bool check_sensor_units_watts);

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
 * _open_dcmi_context opens the inband ipmi device for DCMI power reading
 */
static int _open_dcmi_context(void)
{
	int ret;

	if (!dcmi_cnt)
		return SLURM_SUCCESS;

	ipmi_dcmi_ctx = ipmi_ctx_create();
	if (!ipmi_dcmi_ctx) {
		error("Failed creating dcmi ipmi context");
		return SLURM_ERROR;
	}

	ret = ipmi_ctx_find_inband(ipmi_dcmi_ctx,
	                           NULL,
	                           ipmi_config.disable_auto_probe,
	                           ipmi_config.driver_address,
	                           ipmi_config.register_spacing,
	                           ipmi_config.driver_device,
	                           ipmi_config.workaround_flags,
	                           IPMI_FLAGS_DEFAULT);
	if (ret < 0) {
		error("Error finding inband dcmi ipmi device: %s",
		      ipmi_ctx_errormsg(ipmi_dcmi_ctx));
		ipmi_ctx_destroy(ipmi_dcmi_ctx);
		ipmi_dcmi_ctx = NULL;
		return SLURM_ERROR;
	} else if (!ret) {
		error("No inband dcmi ipmi device found");
		ipmi_ctx_destroy(ipmi_dcmi_ctx);
		ipmi_dcmi_ctx = NULL;
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
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
		return SLURM_ERROR;
	}
	if (!(ipmi_ctx = ipmi_monitoring_ctx_create())) {
		error("ipmi_monitoring_ctx_create");
		return SLURM_ERROR;
	}
	if (sdr_cache_directory) {
		if (ipmi_monitoring_ctx_sdr_cache_directory(
			    ipmi_ctx, sdr_cache_directory) < 0) {
			error("ipmi_monitoring_ctx_sdr_cache_directory: %s",
			      ipmi_monitoring_ctx_errormsg(ipmi_ctx));
			return SLURM_ERROR;
		}
	}
	/* Must call otherwise only default interpretations ever used */
	if (ipmi_monitoring_ctx_sensor_config_file(
		    ipmi_ctx, sensor_config_file) < 0) {
		error("ipmi_monitoring_ctx_sensor_config_file: %s",
		      ipmi_monitoring_ctx_errormsg(ipmi_ctx));
		return SLURM_ERROR;
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

	if (_open_dcmi_context() != SLURM_SUCCESS)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

/*
 * _check_power_sensor check if the sensor is in Watt
 */
static int _check_power_sensor(void)
{
	uint32_t non_dcmi_cnt = sensors_len - dcmi_cnt;

	/*
	 * Only check for non-DCMI sensors since DCMI ones are always in watts
	 * in this plugin. Note that we do a sensor reading too so we update the
	 * last_update_time.
	 */
	if (non_dcmi_cnt) {
		if (_read_ipmi_non_dcmi_values(true) != SLURM_SUCCESS)
			return SLURM_ERROR;
		previous_update_time = last_update_time;
		last_update_time = time(NULL);
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
	int rc = SLURM_ERROR;
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
		return SLURM_ERROR;
	}

	find_err_cnt = 0;

	for (i = 0; i < sensor_count; i++,
		     ipmi_monitoring_sensor_iterator_next(ipmi_ctx)) {
		sensor_units =
			ipmi_monitoring_sensor_read_sensor_units(ipmi_ctx);
		if (sensor_units < 0) {
			error("ipmi_monitoring_sensor_read_sensor_units: %s",
			      ipmi_monitoring_ctx_errormsg(ipmi_ctx));
			return SLURM_ERROR;
		}

		if (sensor_units != slurm_ipmi_conf.variable)
			continue;

		record_id = ipmi_monitoring_sensor_read_record_id(ipmi_ctx);
		if (record_id < 0) {
			error("ipmi_monitoring_sensor_read_record_id: %s",
			      ipmi_monitoring_ctx_errormsg(ipmi_ctx));
			return SLURM_ERROR;
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
			rc = SLURM_ERROR;
			continue;
		}
		rc = SLURM_SUCCESS;
		break;
	}

	if (rc != SLURM_SUCCESS)
		info("Power sensor not found.");
	else
		log_flag(ENERGY, "Power sensor found: %d", sensors_len);

	return rc;
}

/*
 * _get_dcmi_power_reading reads current power in Watt from the ipmi context
 * returns power in watts on success, a negative value on failure
 */
static int _get_dcmi_power_reading(uint16_t dcmi_mode)
{
	uint8_t mode;
	uint8_t mode_attributes = 0;

	uint64_t current_power;
	fiid_obj_t dcmi_rs;
	int ret;

	if (!ipmi_dcmi_ctx) {
		error("%s: IPMI DCMI context not initialized", __func__);
		return SLURM_ERROR;
	}

	dcmi_rs = fiid_obj_create(tmpl_cmd_dcmi_get_power_reading_rs);
	if (!dcmi_rs) {
		error("%s: Failed creating DCMI fiid obj", __func__);
		return SLURM_ERROR;
	}

	if (dcmi_mode == DCMI_MODE)
		mode = IPMI_DCMI_POWER_READING_MODE_SYSTEM_POWER_STATISTICS;
	else if (dcmi_mode == DCMI_ENH_MODE)
		mode = IPMI_DCMI_POWER_READING_MODE_ENHANCED_SYSTEM_POWER_STATISTICS;
	else {
		error("%s: DCMI mode %d not supported: ", __func__, dcmi_mode);
		return SLURM_ERROR;
	}
	ret = ipmi_cmd_dcmi_get_power_reading(ipmi_dcmi_ctx, mode,
	                                      mode_attributes, dcmi_rs);
	if (ret < 0) {
		error("%s: get DCMI power reading failed", __func__);
		fiid_obj_destroy(dcmi_rs);
		return SLURM_ERROR;
	}

	ret = FIID_OBJ_GET(dcmi_rs, "current_power", &current_power);
	fiid_obj_destroy(dcmi_rs);
	if (ret < 0) {
		error("%s: DCMI FIID_OBJ_GET failed", __func__);
		return SLURM_ERROR;
	}

	return current_power;
}

static int _read_ipmi_dcmi_values(void)
{
	int i, dcmi_res;

	for (i = 0; i < sensors_len; i++) {
		if ((sensors[i].id != DCMI_MODE) &&
		    (sensors[i].id != DCMI_ENH_MODE))
			continue;
		dcmi_res = _get_dcmi_power_reading(sensors[i].id);
		if (dcmi_res < 0)
			return SLURM_ERROR;
		sensors[i].last_update_watt = dcmi_res;
	}

	return SLURM_SUCCESS;
}

static int _ipmi_check_unit_watts()
{
	int sensor_units = ipmi_monitoring_sensor_read_sensor_units(ipmi_ctx);

	if (sensor_units < 0) {
		error("ipmi_monitoring_sensor_read_sensor_units: %s",
		      ipmi_monitoring_ctx_errormsg(ipmi_ctx));
		return SLURM_ERROR;
	}

	if (sensor_units != slurm_ipmi_conf.variable) {
		error("Configured sensor is not in Watt, please check ipmi.conf");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static int _ipmi_read_sensor_readings(int id)
{
	void *sensor_reading;

	sensor_reading = ipmi_monitoring_sensor_read_sensor_reading(ipmi_ctx);

	if (sensor_reading) {
		sensors[id].last_update_watt = (uint32_t) (*((double *)
							    sensor_reading));
	} else {
		error("%s: ipmi read an empty value for power consumption",
		      __func__);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static int _read_ipmi_non_dcmi_values(bool check_sensor_units_watts)
{
	int i, j, rc;
	uint32_t non_dcmi_cnt = sensors_len - dcmi_cnt;
	unsigned int ids[non_dcmi_cnt];
	static uint8_t read_err_cnt = 0;

	/* Next code is only for non-DCMI sensors. */
	for (i = 0, j = 0; i < sensors_len; i++) {
		if ((sensors[i].id != DCMI_MODE) &&
		    (sensors[i].id != DCMI_ENH_MODE)) {
			ids[j] = sensors[i].id;
			j++;
		}
	}

	rc = ipmi_monitoring_sensor_readings_by_record_id(ipmi_ctx,
							  hostname,
							  &ipmi_config,
							  sensor_reading_flags,
							  ids,
							  non_dcmi_cnt,
							  NULL,
							  NULL);
	if (rc != non_dcmi_cnt) {
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
		return SLURM_ERROR;
	}

	for (i = 0; i < sensors_len; i++) {
		if ((sensors[i].id != DCMI_MODE) &&
		    (sensors[i].id != DCMI_ENH_MODE)) {
			/* Check sensor units are in watts if required. */
			if (check_sensor_units_watts &&
			    (_ipmi_check_unit_watts() != SLURM_SUCCESS))
				return SLURM_ERROR;
		}

		/* Read sensor readings. */
		if (_ipmi_read_sensor_readings(i) != SLURM_SUCCESS)
			return SLURM_ERROR;

		if (ipmi_monitoring_sensor_iterator_next(ipmi_ctx) < 0) {
			error("Cannot parse next sensor in ipmi ctx");
		} else if (!ipmi_monitoring_sensor_iterator_next(ipmi_ctx))
			break;
	}

	return SLURM_SUCCESS;
}

/*
 * _read_ipmi_values read the Power sensor and update last_update_watt and times
 */
static int _read_ipmi_values(void)
{
	uint32_t non_dcmi_cnt = sensors_len - dcmi_cnt;
	int rc1 = 0, rc2 = 0;

	/* Start by reading DCMI sensors */
	if (dcmi_cnt)
		rc1 = _read_ipmi_dcmi_values();

	if (non_dcmi_cnt)
		rc2 = _read_ipmi_non_dcmi_values(false);

	if ((rc1 == SLURM_ERROR) && (rc2 == SLURM_ERROR))
		return SLURM_ERROR;

	previous_update_time = last_update_time;
	last_update_time = time(NULL);

	return SLURM_SUCCESS;
}

/* updates the given energy according to the last watt reading of the sensor */
static void _update_energy(acct_gather_energy_t *e, uint32_t last_update_watt,
			   uint32_t readings)
{
	uint32_t prev_watts;

	if (e->current_watts) {
		prev_watts = e->current_watts;
		e->ave_watts = ((e->ave_watts * readings) +
				 e->current_watts) / (readings + 1);
		e->current_watts = last_update_watt;
		if (previous_update_time == 0)
			e->base_consumed_energy = 0;
		else
			e->base_consumed_energy =
				_get_additional_consumption(
					previous_update_time,
					last_update_time,
					prev_watts,
					e->current_watts);
		e->previous_consumed_energy = e->consumed_energy;
		e->consumed_energy += e->base_consumed_energy;
	} else {
		e->consumed_energy = 0;
		e->ave_watts = 0;
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
	static uint32_t readings = 0;

	rc = _read_ipmi_values();

	if (rc == SLURM_SUCCESS) {
		/* sensors list */
		for (i = 0; i < sensors_len; ++i) {
			if (sensors[i].energy.current_watts == NO_VAL)
				return rc;
			_update_energy(&sensors[i].energy,
				       sensors[i].last_update_watt,
				       readings);
		}

		if (previous_update_time == 0)
			previous_update_time = last_update_time;
	}

	readings++;

	if (slurm_conf.debug_flags & DEBUG_FLAG_ENERGY) {
		for (i = 0; i < sensors_len; ++i) {
			char *log_str = NULL;

			if (sensors[i].id == DCMI_MODE)
				xstrcat(log_str, "DCMI");
			else if (sensors[i].id == DCMI_ENH_MODE)
				xstrcat(log_str, "DCMI Enhanced");
			else
				xstrfmtcat(log_str, "%u", sensors[i].id);

			info("ipmi-thread: sensor %s current_watts: %u, consumed %"PRIu64" Joules %"PRIu64" new, ave watts %u",
			     log_str,
			     sensors[i].energy.current_watts,
			     sensors[i].energy.consumed_energy,
			     sensors[i].energy.base_consumed_energy,
			     sensors[i].energy.ave_watts);

			xfree(log_str);
		}
	}

	return rc;
}

/*
 * _thread_init initializes values and conf for the ipmi thread
 */
static int _thread_init(void)
{
	static bool first = true;
	static bool first_init = SLURM_ERROR;
	int rc = SLURM_SUCCESS;
	uint16_t i;

	if (!first && ipmi_ctx)
		return first_init;
	first = false;

	if (_init_ipmi_config() != SLURM_SUCCESS) {
		//TODO verbose error?
		rc = SLURM_ERROR;
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
			//IPMI cache is reread only on initialization
			//This option need a big EnergyIPMITimeout
			sensor_reading_flags ^=
				IPMI_MONITORING_SENSOR_READING_FLAGS_REREAD_SDR_CACHE;
	}

	if (rc != SLURM_SUCCESS)
		if (ipmi_ctx)
			ipmi_monitoring_ctx_destroy(ipmi_ctx);

	log_flag(ENERGY, "%s thread init", plugin_name);

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
		log_flag(ENERGY, "Energy: dataset created (id = %d)", dataset_id);
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

	if (slurm_conf.debug_flags & DEBUG_FLAG_PROFILE) {
		for (i = 0; i < descriptions_len; i++) {
			info("PROFILE-Energy: %sPower=%"PRIu64"",
			     descriptions[i].label, data[i]);
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
	struct timeval tvnow;
	struct timespec abs;

	flag_energy_accounting_shutdown = false;
	log_flag(ENERGY, "ipmi-thread: launched");

	(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	slurm_mutex_lock(&ipmi_mutex);
	if (_thread_init() != SLURM_SUCCESS) {
		log_flag(ENERGY, "ipmi-thread: aborted");
		slurm_mutex_unlock(&ipmi_mutex);

		slurm_mutex_lock(&launch_mutex);
		slurm_cond_signal(&launch_cond);
		slurm_mutex_unlock(&launch_mutex);

		return NULL;
	}

	(void) pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

	slurm_mutex_unlock(&ipmi_mutex);
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
		slurm_mutex_lock(&ipmi_mutex);

		_thread_update_node_energy();

		/* Sleep until the next time. */
		abs.tv_sec += slurm_ipmi_conf.freq;
		slurm_cond_timedwait(&ipmi_cond, &ipmi_mutex, &abs);

		slurm_mutex_unlock(&ipmi_mutex);
	}

	log_flag(ENERGY, "ipmi-thread: ended");

	return NULL;
}

static void *_thread_launcher(void *no_data)
{
	//what arg would countain? frequency, socket?
	struct timeval tvnow;
	struct timespec abs;

	slurm_thread_create(&thread_ipmi_id_run, _thread_ipmi_run, NULL);

	/* setup timer */
	gettimeofday(&tvnow, NULL);
	abs.tv_sec = tvnow.tv_sec + slurm_ipmi_conf.timeout;
	abs.tv_nsec = tvnow.tv_usec * 1000;

	slurm_mutex_lock(&launch_mutex);
	slurm_cond_timedwait(&launch_cond, &launch_mutex, &abs);
	slurm_mutex_unlock(&launch_mutex);

	if (!flag_thread_started) {
		error("%s threads failed to start in a timely manner",
		     plugin_name);

		flag_energy_accounting_shutdown = true;

		/*
		 * It is a known thing we can hang up on IPMI calls cancel if
		 * we must.
		 */
		pthread_cancel(thread_ipmi_id_run);

		/*
		 * Unlock just to make sure since we could have canceled the
		 * thread while in the lock.
		 */
		slurm_mutex_unlock(&ipmi_mutex);
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

	xassert(context_id != -1);

	if (slurm_get_node_energy(conf->node_name, context_id, delta,
				  &sensor_cnt, &energies)) {
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

		log_flag(ENERGY, "%s: consumed %"PRIu64" Joules (received %"PRIu64"(%u watts) from slurmd)",
			 __func__, new->consumed_energy,
			 new->base_consumed_energy, new->current_watts);
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
		energy->ave_watts += e->ave_watts;
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
	/* put anything that requires the .conf being read in
	   acct_gather_energy_p_conf_parse
	*/

	return SLURM_SUCCESS;
}

extern int fini(void)
{
	uint16_t i;

	if (!running_in_slurmd_stepd())
		return SLURM_SUCCESS;

	flag_energy_accounting_shutdown = true;

	slurm_mutex_lock(&launch_mutex);
	/* clean up the launch thread */
	slurm_cond_signal(&launch_cond);
	slurm_mutex_unlock(&launch_mutex);

	if (thread_ipmi_id_launcher)
		pthread_join(thread_ipmi_id_launcher, NULL);

	slurm_mutex_lock(&ipmi_mutex);
	/* clean up the run thread */
	slurm_cond_signal(&ipmi_cond);

	if (ipmi_ctx) {
		ipmi_monitoring_ctx_destroy(ipmi_ctx);
		ipmi_ctx = NULL;
	}

	if (ipmi_dcmi_ctx) {
		ipmi_ctx_close(ipmi_dcmi_ctx);
		ipmi_ctx_destroy(ipmi_dcmi_ctx);
		ipmi_dcmi_ctx = NULL;
	}

	reset_slurm_ipmi_conf(&slurm_ipmi_conf);

	slurm_mutex_unlock(&ipmi_mutex);

	if (thread_ipmi_id_run)
		pthread_join(thread_ipmi_id_run, NULL);

	/*
	 * We don't really want to destroy the sensors nor the initial state,
	 * so those values persist a reconfig. And if the process dies, this
	 * will be lost anyway. So not freeing these variables is not really a
	 * leak.
	 *
	 * xfree(sensors);
	 * xfree(start_current_energies);
	 */

	for (i = 0; i < descriptions_len; ++i) {
		xfree(descriptions[i].label);
		xfree(descriptions[i].sensor_idxs);
	}
	xfree(descriptions);
	descriptions = NULL;
	descriptions_len = 0;

	flag_init = false;
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
	uint16_t *sensor_cnt = (uint16_t *)data;

	xassert(data);
	xassert(running_in_slurmd_stepd());

	switch (data_type) {
	case ENERGY_DATA_NODE_ENERGY_UP:
		slurm_mutex_lock(&ipmi_mutex);
		if (running_in_slurmd()) {
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
		slurm_mutex_lock(&ipmi_mutex);
		*sensor_cnt = sensors_len;
		slurm_mutex_unlock(&ipmi_mutex);
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
		if (running_in_slurmd()) {
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

	xassert(running_in_slurmd_stepd());

	switch (data_type) {
	case ENERGY_DATA_RECONFIG:
		break;
	case ENERGY_DATA_PROFILE:
		slurm_mutex_lock(&ipmi_mutex);
		_get_joules_task(*delta);
		_ipmi_send_profile();
		slurm_mutex_unlock(&ipmi_mutex);
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
			/*
			 * DCMI and DCMI_ENHANCED are special cases for
			 * the IPMI extension commands. Actually we support
			 * these ones and we convert them to numerical ids.
			 */
			if (!xstrcmp(str_id, "DCMI")) {
				dcmi_cnt++;
				id = DCMI_MODE;
			} else if (!xstrcmp(str_id, "DCMI_ENHANCED")) {
				dcmi_cnt++;
				id = DCMI_ENH_MODE;
			} else {
				id = strtol(str_id, &endptr, 10);
				if (*endptr != '\0')
					goto error;
			}
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
	fatal("Configuration of EnergyIPMIPowerSensors is malformed. "
	      "Make sure that the expected format is respected and that "
	      "the \"Node\" label is provided.");

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

extern void acct_gather_energy_p_conf_set(int context_id_in,
					  s_p_hashtbl_t *tbl)
{
	char *tmp_char;

	/* Set initial values */
	reset_slurm_ipmi_conf(&slurm_ipmi_conf);

	if (tbl) {
		/* ipmi initialization parameters */
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

	context_id = context_id_in;
	if (!running_in_slurmd_stepd())
		return;

	if (!flag_init) {
		/* try to parse the PowerSensors settings */
		_parse_sensor_descriptions();

		flag_init = true;
		if (running_in_slurmd()) {
			slurm_thread_create(&thread_ipmi_id_launcher,
					    _thread_launcher, NULL);
			log_flag(ENERGY, "%s thread launched", plugin_name);
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
