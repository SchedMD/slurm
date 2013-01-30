/*****************************************************************************\
 *  acct_gather_energy_ipmi.c - slurm energy accounting plugin for ipmi.
 *****************************************************************************
 *  Copyright (C) 2012
 *  Written by Bull- Thomas Cadeau
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
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

//#include "src/plugins/acct_gather_energy/ipmi/ipmi_inttypes.h"
//#include "src/plugins/acct_gather_energy/ipmi/ipmi.h"
//#include "src/plugins/acct_gather_energy/ipmi/ipmi_intf.h"
//#include "src/plugins/acct_gather_energy/ipmi/ipmi_sdr.h"
//#include "src/plugins/acct_gather_energy/ipmi/ipmi_sel.h"
//#include "src/plugins/acct_gather_energy/ipmi/ipmi_mc.h"
//#include "src/plugins/acct_gather_energy/ipmi/ipmi_sensor.h"
//#include "src/plugins/acct_gather_energy/ipmi/ipmi_sol.h"


#define _DEBUG 1
#define _DEBUG_ENERGY 1

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
 * The static functions are organized as follows
 * general functions:
 *     _task_sleep
 *     _get_additional_consumption
 * freeipmi functions are called in:
 *     _init_ipmi_config
 *     _check_power_sensor
 *     _find_power_sensor
 *     _read_ipmi_values
 * the thread calling freeipmi function is:
 *     _thread_ipmi_run
 * it calls functions:
 *     _thread_update_node_energy
 *     _thread_init
 *     _thread_fini
 * and create the thread writing on the pipe:
 *     _thread_ipmi_write
 * the pipe is read in function:
 *     _read_last_consumed_energy
 */

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
static uint32_t flag_energy_accounting_shutdown = 0;
static uint32_t flag_thread_run_running = false;
static uint32_t flag_thread_write_running = false;
static pthread_mutex_t ipmi_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_t thread_ipmi_id_run = (pthread_t) -1;
pthread_t thread_ipmi_id_write = (pthread_t) -1;

typedef struct ipmi_message {
	uint32_t energy;
	uint32_t watts;
	time_t time;
} ipmi_message_t;

static void _task_sleep(int rem)
{
	while (rem)
		rem = sleep(rem);	// subject to interupt
}

/*
 * _get_additional_consumption computes consumption between 2 times
 * method is set to third method strongly
 */
static uint32_t _get_additional_consumption(time_t time0, time_t time1,
					    uint32_t watt0, uint32_t watt1)
{
	int method=3;
	uint32_t consumption=0;

	switch (method) {
	case 1:
		//watt0 is used on all time window
		consumption = (uint32_t) ((time1 - time0)*watt0);
		break;
	case 2:
		//watt1 is used on all time window
		consumption = (uint32_t) ((time1 - time0)*watt1);
		break;
	case 3:
		//mean of watt (affine function) is used on each time window
		consumption = (uint32_t) ((time1 - time0)*(watt1 + watt0)/2.);
		break;
	}

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
	if (sensor_config_file) {
		if (ipmi_monitoring_ctx_sensor_config_file (ipmi_ctx,
						sensor_config_file) < 0) {
			error("ipmi_monitoring_ctx_sensor_config_file: %s",
				ipmi_monitoring_ctx_errormsg (ipmi_ctx));
			return SLURM_FAILURE;
		}
	} else {
		if (ipmi_monitoring_ctx_sensor_config_file (ipmi_ctx, NULL)
									< 0) {
			error("ipmi_monitoring_ctx_sensor_config_file: %s",
				ipmi_monitoring_ctx_errormsg (ipmi_ctx));
			return SLURM_FAILURE;
		}
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
	if (slurm_ipmi_conf.entity_sensor_names)
		sensor_reading_flags |=
			IPMI_MONITORING_SENSOR_READING_FLAGS_ENTITY_SENSOR_NAMES;

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
		     NULL,NULL)) != record_ids_length) {
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

	if (sensor_units != IPMI_MONITORING_SENSOR_UNITS_WATTS) {
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

		if (sensor_units != IPMI_MONITORING_SENSOR_UNITS_WATTS)
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
		}
		rc = SLURM_SUCCESS;
		break;
	}
	if (debug_flags & DEBUG_FLAG_ENERGY)
		info("Power sensor found: %d",slurm_ipmi_conf.power_sensor_num);
	if (rc != SLURM_SUCCESS)
		info("Power sensor not found.");
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
	char *name = NULL;

	xstrfmtcat(name, "%s/%s_ipmi_pipe", conf->spooldir, conf->node_name);
	pipe = open(name, O_RDONLY);
	if (pipe < 0) {
		error("ipmi: failed to read ipmi pipe: %m");
		return SLURM_ERROR;
	}
	safe_read(pipe, message, sizeof(ipmi_message_t));
	close(pipe);
	remove(name);
	return rc;
rwfail:
	error("Unable to recv consumption information.");
	return SLURM_ERROR;
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
		slurm_mutex_lock(&ipmi_mutex);
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
		slurm_mutex_unlock(&ipmi_mutex);
	}
	if (debug_flags & DEBUG_FLAG_ENERGY) {
		slurm_mutex_lock(&ipmi_mutex);
		info("ipmi-thread = %d sec, current %d Watts, "
		     "consumed %d Joules",
		     (int) (last_update_time - previous_update_time),
		     local_energy->current_watts,
		     local_energy->consumed_energy);
		slurm_mutex_unlock(&ipmi_mutex);
	}
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
		if (ipmi_ctx)
			ipmi_monitoring_ctx_destroy(ipmi_ctx);
		rc = SLURM_FAILURE;
	} else {
		if ((slurm_ipmi_conf.power_sensor_num == -1
		     && _find_power_sensor() != SLURM_SUCCESS)
		    || _check_power_sensor() != SLURM_SUCCESS) {
			local_energy->consumed_energy=0;
			local_energy->base_watts=0;
			local_energy->current_watts = NO_VAL;
		} else {
			local_energy->consumed_energy=0;
			local_energy->base_watts=0;
			local_energy->current_watts = 0;
		}
	}
	slurm_mutex_lock(&ipmi_mutex);
	local_energy->consumed_energy = 0;
	local_energy->base_watts = 0;
	local_energy->current_watts = last_update_watt;
	slurm_mutex_unlock(&ipmi_mutex);

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

/*
 * _thread_ipmi_write is an independant thread writing in pipe
 */
static void *_thread_ipmi_write(void *no_data)
{
	int pipe;
	char *name = NULL;
	ipmi_message_t message;

	if (debug_flags & DEBUG_FLAG_ENERGY)
		info("ipmi-thread-write: launched");

	flag_thread_run_running = true;
	flag_energy_accounting_shutdown = false;

	xstrfmtcat(name, "%s/%s_ipmi_pipe", conf->spooldir, conf->node_name);
	remove(name);

	while (!flag_energy_accounting_shutdown && flag_thread_run_running) {
		mkfifo(name, 0777);
		//wait until pipe is read
		pipe = open(name, O_WRONLY);
		slurm_mutex_lock(&ipmi_mutex);
		message.energy = local_energy->consumed_energy;
		message.watts = local_energy->current_watts;
		message.time = last_update_time;
		slurm_mutex_unlock(&ipmi_mutex);
		if (debug_flags & DEBUG_FLAG_ENERGY)
			info("ipmi-thread-write: write message on pipe");

		safe_write(pipe, &(message), sizeof(ipmi_message_t));
		close(pipe);
		//wait for free pipe
		while (access(name, F_OK) != -1) {//do nothing
		}
	}
	remove(name);

	if (debug_flags & DEBUG_FLAG_ENERGY)
		info("ipmi-thread-write: ended");

	flag_thread_run_running = false;
	return NULL;
rwfail:
	error("Unable to send consumption information.");
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
	int rc = SLURM_SUCCESS;

	if (debug_flags & DEBUG_FLAG_ENERGY)
		info("ipmi-thread: launched");

	flag_thread_write_running = true;
	flag_energy_accounting_shutdown = false;

	if (_thread_init() != SLURM_SUCCESS) {
		if (debug_flags & DEBUG_FLAG_ENERGY)
			info("ipmi-thread: aborted");
		return NULL;
	}

	//launch _thread_ipmi_write
	slurm_attr_init(&attr_write);
	rc = pthread_attr_setdetachstate(&attr_write, PTHREAD_CREATE_DETACHED);
	if (rc != 0) {
		//xfree(arg);
		error("Unable to set detachstate on attr: %m");
		slurm_attr_destroy(&attr_write);
		return NULL;
	}
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

	flag_thread_write_running = false;
	_read_last_consumed_energy(&message_trash);
	_thread_fini();
	if (debug_flags & DEBUG_FLAG_ENERGY)
		info("ipmi-thread: ended");

	return NULL;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	int rc = SLURM_SUCCESS;
	local_energy = acct_gather_energy_alloc();
	local_energy->consumed_energy=0;
	local_energy->base_consumed_energy=0;
	if (read_slurm_ipmi_conf(&slurm_ipmi_conf)!=SLURM_SUCCESS) {
		return SLURM_ERROR;
	}
	verbose("%s loaded", plugin_name);
	debug_flags = slurm_get_debug_flags();
	return rc;
}

extern int fini(void)
{
	time_t begin_fini = time(NULL);
	while (flag_thread_run_running || flag_thread_write_running) {
		if ((time(NULL) - begin_fini) > (slurm_ipmi_conf.freq + 1)) {
			error("Ipmi threads not finilized in appropriate time. "
			      "Exit plugin without finalising threads.");
			break;
		}
		_task_sleep(1);
		//wait for thread stop
	}
	acct_gather_energy_destroy(local_energy);
	local_energy = NULL;
	return SLURM_SUCCESS;
}


extern int acct_gather_energy_p_update_node_energy(void)
{
	return SLURM_SUCCESS;
}

extern int acct_gather_energy_p_get_data(enum acct_energy_type data_type,
					 acct_gather_energy_t *energy)
{
	int rc = SLURM_SUCCESS;
	ipmi_message_t message;
	time_t time_call;

	switch (data_type) {
	case ENERGY_DATA_JOULES_TASK:
		// TODO if fail -> NC
		time_call = time(NULL);
		if (local_energy->consumed_energy == NO_VAL ||
		    _read_last_consumed_energy(&message) !=
		    SLURM_SUCCESS) {
			rc = SLURM_ERROR;
			local_energy->consumed_energy = NO_VAL;
			break;
		}
		local_energy->consumed_energy = message.energy;
		local_energy->current_watts = message.watts;
		last_update_time = message.time;
		if (energy->base_consumed_energy != 0) {
			if (slurm_ipmi_conf.adjustment) {
				energy->consumed_energy =
					local_energy->consumed_energy -
					energy->base_consumed_energy +
					_get_additional_consumption(
						last_update_time,time_call,
						local_energy->current_watts,
						local_energy->current_watts);
			} else {
				energy->consumed_energy =
					local_energy->consumed_energy -
					energy->base_consumed_energy;
			}
		} else {
			if (slurm_ipmi_conf.adjustment) {
				energy->base_consumed_energy =
					local_energy->consumed_energy +
					_get_additional_consumption(
						last_update_time,time_call,
						local_energy->current_watts,
						local_energy->current_watts);
				energy->consumed_energy = 0;
			} else {
				energy->base_consumed_energy =
					local_energy->consumed_energy;
				energy->consumed_energy = 0;
			}
		}
		if (debug_flags & DEBUG_FLAG_ENERGY) {
			info("_get_joules_task_ipmi = consumed %d Joules"
			     "(received %d from ipmi thread)",
			     energy->consumed_energy,local_energy->
			     consumed_energy);
		}
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


extern int acct_gather_energy_p_start_thread(void)
{
//what arg would countain? frequency, socket?
	int rc = SLURM_SUCCESS;
	pthread_attr_t attr_run;
	//my_struct *arg = xmalloc(sizeof(my_struct));
	//arg->bla = bla;

	slurm_attr_init(&attr_run);
	rc = pthread_attr_setdetachstate(&attr_run, PTHREAD_CREATE_DETACHED);
	if (rc != 0) {
		//xfree(arg);
		error("Unable to set detachstate on attr: %m");
		slurm_attr_destroy(&attr_run);
		return rc;
	}
	if (pthread_create(&thread_ipmi_id_run, &attr_run,
			   &_thread_ipmi_run, NULL)) {
		//if (pthread_create(... (void *)arg)) {
		debug("energy accounting failed to create _thread_ipmi_run "
		      "thread: %m");
	}
	slurm_attr_destroy(&attr_run);

	if (debug_flags & DEBUG_FLAG_ENERGY)
		info("%s thread launched", plugin_name);

	return rc;
}

extern int acct_gather_energy_p_end_thread(void)
{
	int rc = SLURM_SUCCESS;

	flag_energy_accounting_shutdown = true;

	return rc;
}
