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
#include "src/slurmd/common/proctrack.h"

#include "src/slurmd/slurmd/slurmd.h"
#include "acct_gather_energy_ipmi_config.h"
#include "acct_gather_energy_ipmi.h"

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
static uint32_t debug_flags = 0;
static bool flag_energy_accounting_shutdown = false;
static bool flag_thread_run_running = false;
static bool flag_thread_write_running = false;
static bool flag_thread_started = false;
static bool flag_init = false;
static bool flag_use_profile = false;
static pthread_mutex_t ipmi_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t cleanup_handler_thread = 0;
pthread_t thread_ipmi_id_launcher = 0;
pthread_t thread_ipmi_id_run = 0;
pthread_t thread_ipmi_id_write = 0;

typedef struct ipmi_message {
	uint32_t energy;
	uint32_t watts;
	time_t time;
	int nb_values;
} ipmi_message_t;

static int profile_message_size;
static int profile_message_memory;
typedef struct ipmi_message_profile {
	uint32_t watts;
	time_t time;
} ipmi_message_profile_t;
static ipmi_message_profile_t *profile_message;

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

static void _clean_profile_message(ipmi_message_profile_t * m)
{
	profile_message_size = 0;
	profile_message_memory = 0;
	if (m != NULL) {
		xfree(m);
	}
	m = NULL;
}

static void _task_sleep(int rem)
{
	while (rem)
		rem = sleep(rem);	// subject to interupt
}

static int _use_profile(void)
{
	uint32_t profile_opt;
	acct_gather_profile_g_get(ACCT_GATHER_PROFILE_RUNNING, &profile_opt);
	return (profile_opt & ACCT_GATHER_PROFILE_ENERGY);
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
 * _read_last_consumed_energy reads the pipe and remove it
 * update consumed_energy for tasks
 */
static int _read_last_consumed_energy(ipmi_message_t* message)
{
	int rc = SLURM_SUCCESS;
	int pipe;
	struct timeval t0,t1;
	int time_left, timeout_pipe = 200;
	char *name = NULL, *mutex_name = NULL;
	uint16_t format_version = 0;

	xstrfmtcat(name, "%s/%s_ipmi_pipe", conf->spooldir, conf->node_name);
	xstrfmtcat(mutex_name, "%s/%s_ipmi_pipe_mutex",
		   conf->spooldir, conf->node_name);
	gettimeofday(&t0, NULL);
	while (access(mutex_name, F_OK) != -1) {
		gettimeofday(&t1, NULL);
		time_left =   (t1.tv_sec  - t0.tv_sec ) * 1000;
		time_left += ((t1.tv_usec - t0.tv_usec + 500) / 1000);
		if (time_left > timeout_pipe) {
			info("error: ipmi_read: timeout on mutex"
			     "(wait more than %d millisec",timeout_pipe);
			xfree(name);
			xfree(mutex_name);
			return SLURM_ERROR;
		}
	}
	mkfifo(mutex_name, 0777);
	pipe = open(name, O_RDONLY);
	if (pipe < 0) {
		info("error: ipmi_read: failed to open ipmi pipe: %m");
		remove(mutex_name);
		xfree(name);
		xfree(mutex_name);
		return SLURM_ERROR;
	}
	safe_read(pipe, &format_version, sizeof(uint16_t));
	if (format_version != IPMI_VERSION) {
		error("error: ipmi_read: unsupported version number: %u"
		      format_version);
		goto rwfail;
	}
	safe_read(pipe, message, sizeof(ipmi_message_t));
	close(pipe);
	remove(name);
	xfree(name);
	xfree(mutex_name);
	return rc;
rwfail:
	info("error: ipmi_read: Unable to read on ipmi pipe.");
	close(pipe);
	remove(mutex_name);
	xfree(name);
	xfree(mutex_name);
	return SLURM_ERROR;
}

static int _update_profile_message(void)
{
	ipmi_message_profile_t *tmp;
	int new_size;
	static int job_acct_gather_freq = -1;

	if (profile_message_memory==0) {
		/* FIXME: This math looks wrong.
		*/
		if (job_acct_gather_freq == -1)
			job_acct_gather_freq = atoi(conf->job_acct_gather_freq);
		new_size = 4 * (2+ job_acct_gather_freq/slurm_ipmi_conf.freq);

		tmp = (ipmi_message_profile_t *)
			xmalloc(sizeof(ipmi_message_profile_t)* new_size);
		if (tmp == NULL)
			return SLURM_FAILURE;
		profile_message_memory = new_size;
		profile_message = tmp;
	}

	if (profile_message_size >= profile_message_memory) {
		//lost values because no job is running
		profile_message_size = 0;
	}

	profile_message[profile_message_size].watts =
		local_energy->current_watts;
	profile_message[profile_message_size].time = last_update_time;
	profile_message_size++;
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

	slurm_mutex_lock(&ipmi_mutex);
	if (rc == SLURM_SUCCESS) {
		uint32_t additional_consumption;
		if (previous_update_time == 0) {
			additional_consumption = 0;
			previous_update_time = last_update_time;
		} else {
			additional_consumption =
				_get_additional_consumption(
					previous_update_time,last_update_time,
					local_energy->base_watts,
					local_energy->current_watts);
		}
		if (local_energy->current_watts != 0) {
			local_energy->base_watts = local_energy->current_watts;
			local_energy->current_watts = last_update_watt;
			local_energy->consumed_energy += additional_consumption;
		}
		if (local_energy->current_watts == 0) {
			local_energy->consumed_energy = 0;
			local_energy->base_watts = 0;
			local_energy->current_watts = last_update_watt;
		}
		_update_profile_message();
	}
	if (debug_flags & DEBUG_FLAG_ENERGY) {
		info("ipmi-thread = %d sec, current %d Watts, "
		     "consumed %d Joules",
		     (int) (last_update_time - previous_update_time),
		     local_energy->current_watts,
		     local_energy->consumed_energy);
	}
	slurm_mutex_unlock(&ipmi_mutex);

	return rc;
}

/*
 * _thread_init initializes values and conf for the ipmi thread
 */
static int _thread_init(void)
{
	int rc = SLURM_SUCCESS;

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
	slurm_mutex_lock(&ipmi_mutex);
	local_energy->consumed_energy = 0;
	local_energy->base_watts = 0;
	slurm_mutex_unlock(&ipmi_mutex);

	_clean_profile_message(profile_message);

	if (rc != SLURM_SUCCESS)
		if (ipmi_ctx)
			ipmi_monitoring_ctx_destroy(ipmi_ctx);

	if (debug_flags & DEBUG_FLAG_ENERGY)
		info("%s thread init", plugin_name);
	return rc;
}

/*
 * _thread_fini finalizes values for the ipmi thread
 */
static int _thread_fini(void)
{
	//acct_gather_energy_destroy(local_energy);
	//local_energy = NULL;
	if (ipmi_ctx)
		ipmi_monitoring_ctx_destroy(ipmi_ctx);
	reset_slurm_ipmi_conf(&slurm_ipmi_conf);
	return SLURM_SUCCESS;
}

static int _ipmi_write_profile(void)
{
	int pipe;
	char *name = NULL;
	uint16_t format_version = IPMI_VERSION;

	if (profile_message_size == 0)
		return SLURM_SUCCESS;

	xstrfmtcat(name, "%s/%s_ipmi_pipe_profile",
		   conf->spooldir, conf->node_name);
	pipe = open(name, O_WRONLY);
	xfree(name);

	if (pipe < 0) {
		if (debug_flags & DEBUG_FLAG_ENERGY)
			info("ipmi-thread-write: no profile pipe");
		return SLURM_FAILURE;
	}

	if (debug_flags & DEBUG_FLAG_ENERGY)
		info("ipmi-thread-write: write profile message on pipe");
	safe_write(pipe, &format_version, sizeof(format_version));
	safe_write(pipe, profile_message,
		(int)(profile_message_size*sizeof(ipmi_message_profile_t)));
	close(pipe);

	_clean_profile_message(profile_message);

	return SLURM_SUCCESS;
rwfail:
	info("error: ipmi-thread-write: Unable to write on ipmi profile pipe.");
	close(pipe);
	_clean_profile_message(profile_message);
	return SLURM_FAILURE;
}

static int _ipmi_read_profile(bool all_value)
{
	int pipe, i;
	char *name = NULL;
	ipmi_message_profile_t *recv_energy;
	uint16_t format_version = 0;

	xstrfmtcat(name, "%s/%s_ipmi_pipe_profile",
		   conf->spooldir, conf->node_name);

	if (profile_message_size == 0) {
		remove(name);
		return SLURM_SUCCESS;
	}

	recv_energy = (ipmi_message_profile_t *)
				xmalloc(sizeof(ipmi_message_profile_t)
				* profile_message_size);

	pipe = open(name, O_RDONLY);
	if (pipe < 0) {
		info("error: ipmi_read: failed to open profile pipe: %m");
		xfree(recv_energy);
		return SLURM_ERROR;
	}
	safe_read(pipe, &format_version, sizeof(format_version));
	if (format_version != IPMI_VERSION) {
		error("error: ipmi_read: unsupported version number: %u"
		      format_version);
		goto rwfail;
	}
	safe_read(pipe, recv_energy,
		(int)(profile_message_size*sizeof(ipmi_message_profile_t)));
	close(pipe);
	remove(name);
	xfree(name);

	if (debug_flags & DEBUG_FLAG_ENERGY)
		info("ipmi: read profile pipe, %d values",
		     profile_message_size);

	if (all_value) {
		acct_energy_data_t ener;
		memset(&ener, 0, sizeof(acct_energy_data_t));
		ener.cpu_freq = 1;
		for (i = 0; i < profile_message_size; i++) {
			/*TODO function to calculate Average CPUs Frequency*/
			/*ener->cpu_freq = // read /proc/...*/
			ener.time = recv_energy[i].time;
			ener.power = recv_energy[i].watts;
			acct_gather_profile_g_add_sample_data(
				ACCT_GATHER_PROFILE_ENERGY, &ener);
		}
		if (debug_flags & DEBUG_FLAG_ENERGY)
			info("ipmi: save profile data, %d values",
			     profile_message_size);
	}
	_clean_profile_message(recv_energy);

	return SLURM_SUCCESS;
rwfail:
	info("error: ipmi-read: Unable to read on ipmi profile pipe.");
	xfree(name);
	close(pipe);
	//remove(name);
	_clean_profile_message(recv_energy);
	return SLURM_ERROR;
}

/*
 * _thread_ipmi_write is an independant thread writing in pipe
 */
static void *_thread_ipmi_write(void *no_data)
{
	int pipe;
	char *name = NULL, *mutex_name = NULL;
	ipmi_message_t message;
	uint16_t format_version = IPMI_VERSION;

	(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	if (debug_flags & DEBUG_FLAG_ENERGY)
		info("ipmi-thread-write: launched");

	flag_thread_write_running = true;
	flag_energy_accounting_shutdown = false;

	xstrfmtcat(name, "%s/%s_ipmi_pipe", conf->spooldir, conf->node_name);
	remove(name);

	xstrfmtcat(mutex_name, "%s/%s_ipmi_pipe_mutex",
		   conf->spooldir, conf->node_name);
	remove(mutex_name);
	mkfifo(name, 0777);

	while (!flag_energy_accounting_shutdown && flag_thread_run_running) {
		//wait until pipe is read
		pipe = open(name, O_WRONLY);
		if (pipe < 0) {
			info("error: ipmi-thread-write:"
			     "Unable to open on ipmi pipe: %m");
			continue;
		}
		slurm_mutex_lock(&ipmi_mutex);
		message.energy = local_energy->consumed_energy;
		message.watts = local_energy->current_watts;
		message.time = last_update_time;
		message.nb_values = profile_message_size;
		if (debug_flags & DEBUG_FLAG_ENERGY)
			info("ipmi-thread-write: write message on pipe");

		safe_write(pipe, &format_version, sizeof(format_version));
		safe_write(pipe, &(message), sizeof(ipmi_message_t));
		close(pipe);
		_ipmi_write_profile();
		slurm_mutex_unlock(&ipmi_mutex);
		//wait for free pipe
		while (access(name, F_OK) != -1) {//do nothing
		}
		mkfifo(name, 0777);
		remove(mutex_name);
	}
	remove(name);
	xfree(name);
	xfree(mutex_name);

	if (debug_flags & DEBUG_FLAG_ENERGY)
		info("ipmi-thread-write: ended");

	flag_thread_write_running = false;
	return NULL;
rwfail:
	info("error: ipmi-thread-write: Unable to write on ipmi pipe.");
	remove(name);
	remove(mutex_name);
	xfree(name);
	xfree(mutex_name);
	return NULL;
}

/*
 * _thread_ipmi_run is the thread calling ipmi and launching _thread_ipmi_write
 */
static void *_thread_ipmi_run(void *no_data)
{
	ipmi_message_t message_trash;
// need input (attr)
	int time_lost;
	pthread_attr_t attr_write;

	(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	flag_thread_run_running = true;
	flag_energy_accounting_shutdown = false;
	if (debug_flags & DEBUG_FLAG_ENERGY)
		info("ipmi-thread: launched");

	if (_thread_init() != SLURM_SUCCESS) {
		if (debug_flags & DEBUG_FLAG_ENERGY)
			info("ipmi-thread: aborted");
		flag_thread_run_running = false;
		return NULL;
	}

	flag_thread_started = true;

	//launch _thread_ipmi_write
	slurm_attr_init(&attr_write);
	if (pthread_create(&thread_ipmi_id_write, &attr_write,
			   &_thread_ipmi_write, NULL)) {
		//if (pthread_create(... (void *)arg)) {
		debug("energy accounting failed to create _thread_ipmi_write "
		      "thread: %m");
	}
	slurm_attr_destroy(&attr_write);

	//loop until slurm stop
	while (!flag_energy_accounting_shutdown) {
		time_lost = (int)(time(NULL) - last_update_time);
		_task_sleep(slurm_ipmi_conf.freq - time_lost);
		_thread_update_node_energy();
	}

	flag_thread_run_running = false;
	_clean_profile_message(profile_message);
	_read_last_consumed_energy(&message_trash);
	_thread_fini();
	if (debug_flags & DEBUG_FLAG_ENERGY)
		info("ipmi-thread: ended");

	return NULL;
}

static void *_cleanup_thread(void *no_data)
{
	if (thread_ipmi_id_write)
		pthread_join(thread_ipmi_id_write, NULL);
	if (thread_ipmi_id_run)
		pthread_join(thread_ipmi_id_run, NULL);

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
			error("ipmi thread launch timeout");
			rc = SLURM_ERROR;
			break;
		}
		if (flag_thread_run_running)
			break;
		_task_sleep(1);
	}

	begin_time = time(NULL);
	while (rc == SLURM_SUCCESS) {
		if (time(NULL) - begin_time > slurm_ipmi_conf.timeout) {
			error("ipmi thread init timeout");
			rc = SLURM_ERROR;
			break;
		}
		if (!flag_thread_run_running) {
			error("ipmi thread lost");
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
		if (thread_ipmi_id_write) {
			pthread_cancel(thread_ipmi_id_write);
			pthread_join(thread_ipmi_id_write, NULL);
		}
		flag_thread_write_running = false;

		if (thread_ipmi_id_run) {
			pthread_cancel(thread_ipmi_id_run);
			pthread_join(thread_ipmi_id_run, NULL);
		}
		flag_thread_run_running = false;

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

static int _get_joules_task(void)
{
	ipmi_message_t message;
	char *name_profile = NULL;
	time_t time_call = time(NULL);
	bool flag_use_profile_old = flag_use_profile;

	if (local_energy->consumed_energy == NO_VAL ||
		local_energy->base_consumed_energy == 0) {
		local_energy->consumed_energy = NO_VAL;
		return SLURM_ERROR;
	}

	if(!(flag_use_profile) && _use_profile())
		flag_use_profile = true;

	xstrfmtcat(name_profile, "%s/%s_ipmi_pipe_profile",
		   conf->spooldir, conf->node_name);
	remove(name_profile);
	if (flag_use_profile)
		mkfifo(name_profile, 0777);
	xfree(name_profile);

	if (_read_last_consumed_energy(&message) != SLURM_SUCCESS) {
		/* Don't set consumed_energy = NO_VAL here.  If we
		   fail here we still have a coherent value.  If it
		   appears during a sstat call, it's not a big deal,
		   the value will be updated later with another sstat
		   or at the end of the step.  But if it was at the end
		   of the step then the value on the accounting is
		   not right.
		*/
		//local_energy->consumed_energy = NO_VAL;
		return SLURM_ERROR;
	}

	if (slurm_ipmi_conf.adjustment) {
		local_energy->consumed_energy =
			message.energy -
			local_energy->base_consumed_energy +
			_get_additional_consumption(
				message.time,time_call,
				message.watts,message.watts);
	} else {
		local_energy->consumed_energy =
			message.energy -
			local_energy->base_consumed_energy;
	}

	if (flag_use_profile) {
		profile_message_size = message.nb_values;
		if (flag_use_profile_old)
			_ipmi_read_profile(true);
		else
			_ipmi_read_profile(false);
	}

	if (debug_flags & DEBUG_FLAG_ENERGY) {
		info("_get_joules_task_ipmi = consumed %d Joules"
		     "(received %d from ipmi thread)",
		     local_energy->consumed_energy,message.energy);
	}
	return SLURM_SUCCESS;
}

static int _first_update_task_energy(void)
{
	ipmi_message_t message;
	char *name_profile=NULL;
	time_t time_call = time(NULL);
	int nb_try=0, max_try=NBFIRSTREAD;

	flag_use_profile = _use_profile();

	xstrfmtcat(name_profile, "%s/%s_ipmi_pipe_profile",
		   conf->spooldir, conf->node_name);
	remove(name_profile);

	if (flag_use_profile)
		mkfifo(name_profile, 0777);
	xfree(name_profile);
	while (_read_last_consumed_energy(&message) != SLURM_SUCCESS) {
		if (nb_try > max_try) {
			local_energy->consumed_energy = NO_VAL;
			return SLURM_ERROR;
		}
		nb_try++;
		_task_sleep(1);
	}

	if (flag_use_profile) {
		profile_message_size = message.nb_values;
		_ipmi_read_profile(false);
	}

	if (slurm_ipmi_conf.adjustment) {
		local_energy->base_consumed_energy =
			message.energy +
			_get_additional_consumption(
				message.time,time_call,
				message.watts,message.watts);
		local_energy->consumed_energy = 0;
	} else {
		local_energy->base_consumed_energy =
			message.energy;
		local_energy->consumed_energy = 0;
	}
	if (debug_flags & DEBUG_FLAG_ENERGY) {
		info("_get_joules_task_ipmi = first %d Joules",
		     local_energy->base_consumed_energy);
	}
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
	if (thread_ipmi_id_write)
		pthread_cancel(thread_ipmi_id_write);
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
					 acct_gather_energy_t *energy)
{
	int rc = SLURM_SUCCESS;

	xassert(_run_in_daemon());

	switch (data_type) {
	case ENERGY_DATA_JOULES_TASK:
		slurm_mutex_lock(&ipmi_mutex);
		_get_joules_task();
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
	default:
		error("acct_gather_energy_p_get_data: unknown enum %d",
		      data_type);
		rc = SLURM_ERROR;
		break;
	}
	return rc;
}

extern int acct_gather_energy_p_set_data(enum acct_energy_type data_type,
					 acct_gather_energy_t *energy)
{
	int rc = SLURM_SUCCESS;

	xassert(_run_in_daemon());

	switch (data_type) {
	case ENERGY_DATA_RECONFIG:
		debug_flags = slurm_get_debug_flags();
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
		s_p_get_uint32(&slurm_ipmi_conf. workaround_flags,
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
			_first_update_task_energy();
	}

	verbose("%s loaded", plugin_name);
}
