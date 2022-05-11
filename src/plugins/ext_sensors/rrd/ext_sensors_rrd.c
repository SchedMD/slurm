/*****************************************************************************\
 *  ext_sensors_rrd.c - slurm external sensors plugin for rrd.
 *****************************************************************************
 *  Copyright (C) 2013
 *  Written by Bull- Thomas Cadeau/Martin Perry/Yiannis Georgiou
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

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>

/* slurm_xlator.h must be first */
#include "src/common/slurm_xlator.h"
#include "ext_sensors_rrd.h"
#include "src/common/fd.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_ext_sensors.h"
#include "src/common/xstring.h"
#include "src/slurmd/common/proctrack.h"

#include <rrd.h>

enum ext_sensors_value_type {
	EXT_SENSORS_VALUE_ENERGY,
	EXT_SENSORS_VALUE_TEMPERATURE,
};

#define _WATT_MIN 10
#define _WATT_MAX 500
#define _TEMP_MIN 1
#define _TEMP_MAX 300
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
const char plugin_name[] = "ExtSensors rrd plugin";
const char plugin_type[] = "ext_sensors/rrd";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

static ext_sensors_conf_t ext_sensors_conf;
static ext_sensors_conf_t *ext_sensors_cnf = &ext_sensors_conf;
static time_t last_valid_time;
static rrd_value_t last_valid_watt;

/* Local plugin functions */
static int _update_node_data(void);
static int _update_switch_data(void);
static int _update_door_data(void);
extern int _ext_sensors_read_conf(void);
static void _ext_sensors_clear_free_conf(void);

/* Local RRD functions */
static rrd_value_t _get_additional_consumption(time_t time0, time_t time1,
					       rrd_value_t watt0,
					       rrd_value_t watt1);
static rrd_value_t _validate_watt(rrd_value_t *v);
static char* _get_node_rrd_path(char* component_name,
				enum ext_sensors_value_type sensor_type);
static uint32_t _rrd_get_last_one(char* filename, char* rra_name);
static uint64_t _rrd_consolidate_one(time_t t0, time_t t1,
				     char* filename, char* rra_name,
				     bool flag_approximate);


static rrd_value_t _get_additional_consumption(time_t time0, time_t time1,
					       rrd_value_t watt0,
					       rrd_value_t watt1)
{
	rrd_value_t consumption = (time1 - time0)*(watt1 + watt0)/2;
	return consumption;
}

static rrd_value_t _validate_watt(rrd_value_t *v)
{
	rrd_value_t r = (rrd_value_t)NO_VAL;
	if (v != NULL &&
	    *v > ext_sensors_cnf->min_watt &&
	    *v < ext_sensors_cnf->max_watt)
		r = *v;
	return r;
}

static char* _get_node_rrd_path(char* component_name,
				enum ext_sensors_value_type sensor_type)
{
	char *p;
	char *rrd_file;
	struct stat buf;

	switch (sensor_type) {
	case EXT_SENSORS_VALUE_ENERGY:
		rrd_file = ext_sensors_cnf->energy_rrd_file;
		break;
	case EXT_SENSORS_VALUE_TEMPERATURE:
		rrd_file = ext_sensors_cnf->temp_rrd_file;
		break;
	default:
		error("ext_sensors: _get_node_rrd_path: unknown enum %d",
		      sensor_type);
		return NULL;
	}

	if (!component_name || !strlen(component_name) || !rrd_file)
		return NULL;

	p = slurm_conf_expand_slurmd_path(rrd_file, component_name, NULL);

	if (!xstrcmp(p, rrd_file)) {
		xfree(p);
		return NULL;
	}

	if (stat(p, &buf) == -1) {
		xfree(p);
		return NULL;
	}

	return p;
}

static uint32_t _rrd_get_last_one(char* filename, char* rra_name)
{
	/* RRD library usage notes:
	 * do not use the following lines for compatibility:
	 * 1.3.8-6 : (argv={lastupdate, filename}
	 * status = rrd_lastupdate(argc, argv, &time, &ds_count,
	 *                         &ds_names, &last_ds);
	 * 1.4.7 :
	 * status = rrd_lastupdate_r(filename, &time, &ds_count,
	 *                           &ds_names, &last_ds);
	 */
	rrd_info_t *data, *data_p;
	char line[] = "ds[%s].last_ds", *p, *rra = NULL;
	char *argv[] = {"info", filename, NULL};
	uint32_t temperature = NO_VAL;

	p = xstrdup(line);

	data = rrd_info(2, argv);
	data_p = data;

	if (rra_name == NULL) {
		while (data_p) {
			if (!xstrncmp(line, data_p->key, 3)) {
				rra = xstrdup(data_p->key + 3);
				xstrsubstitute(rra, strchr(rra, ']'), "");
				break;
			}
			data_p = data_p->next;
		}
	} else
		rra = rra_name;

	if (rra != NULL) {
		xstrsubstitute(p, "%s", rra_name);
		if (rra_name == NULL)
			xfree(rra);
		if (xstrcmp(p,line) == 0) {
			xfree(p);
			rrd_info_free(data);
			return temperature;
		}
	} else {
		xfree(p);
		rrd_info_free(data);
		return temperature;
	}

	while (data_p) {
		if (!xstrcmp(p, data_p->key)) {
			if (!sscanf(data_p->value.u_str, "%d", &temperature))
				temperature = 1;
			break;
		}
		data_p = data_p->next;
	}

	xfree(p);
	rrd_info_free(data);

	return temperature;
}

static uint64_t _rrd_consolidate_one(time_t t0, time_t t1,
				     char* filename, char* rra_name,
				     bool flag_approximate)
{
	int status, rra_nb = -1;
	unsigned long step = 1, ds_count, ii;
	char cf[] = "AVERAGE";
	char **ds_names;
	time_t ti, start = t0-1, end = t1+1;
	uint32_t nb_miss = 0, nb_values = 0;
	rrd_value_t *rrd_data, *rrd_data_p;
	rrd_value_t current_watt = (rrd_value_t)NO_VAL;
	rrd_value_t temp_energy = 0, consumed_energy = 0;

	last_valid_time = 0;
	last_valid_watt = (rrd_value_t)NO_VAL;

	status = rrd_fetch_r(filename, cf,
			     &start, &end, &step,
			     &ds_count, &ds_names,
			     &rrd_data);

	if (status != 0){
		log_flag(EXT_SENSORS, "ext_sensors: error rrd_fetch %s",
			 filename);
		return NO_VAL64;
	}

	rrd_data_p = rrd_data;

	do {
		if (start == end) {
			consumed_energy = (rrd_value_t)NO_VAL64;
			break;
		}
		if (ds_count == 0) {
			log_flag(EXT_SENSORS, "ext_sensors: error ds_count==0 in RRD %s",
				 filename);
			consumed_energy = (rrd_value_t)NO_VAL64;
			break;
		} else if (ds_count == 1 || rra_name == NULL)
			rra_nb = 0;
		else {
			for (ii = 0; ii < ds_count; ii++){
				if (!xstrcmp(ds_names[ii],rra_name)) {
					rra_nb = ii;
					break;
				}
			}
			if (rra_nb == -1) {
				log_flag(EXT_SENSORS, "ext_sensors: error RRA %s not found in RRD %s",
					 rra_name, filename);
				consumed_energy = (rrd_value_t)NO_VAL64;
				break;
			}
		}
		ti = start;
		do {
			for (ii = 0; ii < rra_nb; ii++)
				rrd_data_p++;
			last_valid_watt = _validate_watt(rrd_data_p);
			if (last_valid_watt != (rrd_value_t)NO_VAL)
				last_valid_time = ti;
			for (ii = rra_nb; ii < ds_count; ii++)
				rrd_data_p++;
			ti += step;
		} while (ti < t0 && ti < end);

		if (ti != t0 && ti < end) {
			for (ii = 0; ii < rra_nb; ii++)
				rrd_data_p++;
			current_watt = _validate_watt(rrd_data_p);

			if (current_watt != (rrd_value_t)NO_VAL) {
				temp_energy = _get_additional_consumption(
					t0, ti < t1 ? ti : t1,
					current_watt, current_watt);
				last_valid_watt = current_watt;
				last_valid_time = ti;
				consumed_energy += temp_energy;
				nb_values += 1;
			} else {
				nb_miss += 10001;
			}

			for (ii = rra_nb; ii < ds_count; ii++)
				rrd_data_p++;
		} else if ((ti == t0) && (ti < end)) {
			for (ii = 0; ii < rra_nb; ii++)
				rrd_data_p++;
			current_watt = _validate_watt(rrd_data_p);
			if (current_watt != (rrd_value_t)NO_VAL) {
				last_valid_watt = current_watt;
				last_valid_time = ti;
			}
			for (ii = rra_nb; ii < ds_count; ii++)
				rrd_data_p++;
			ti += step;
		}
		while (((ti += step) <= t1) && (ti < end)) {
			for (ii = 0; ii < rra_nb; ii++)
				rrd_data_p++;
			current_watt = _validate_watt(rrd_data_p);
			if (current_watt != (rrd_value_t)NO_VAL &&
			    last_valid_watt != (rrd_value_t)NO_VAL) {
				temp_energy = _get_additional_consumption(
					ti-step, ti,
					last_valid_watt, current_watt);
				last_valid_watt = current_watt;
				last_valid_time = ti;
				consumed_energy += temp_energy;
				nb_values += 1;
			} else {
				nb_miss += 1;
			}
			for (ii = rra_nb; ii < ds_count; ii++)
				rrd_data_p++;
		}
		if ((ti > t1) && (t1 > (t0 + step)) && (ti-step < t1)) {
			if (current_watt != (rrd_value_t)NO_VAL) {
				temp_energy = _get_additional_consumption(
					ti-step, t1,
					current_watt, current_watt);
				consumed_energy += temp_energy;
				nb_values += 1;
			} else {
				nb_miss += 1;
			}
		}
	} while(0);

	if (nb_miss >= 10000) {
		log_flag(EXT_SENSORS, "ext_sensors: RRD: no first value");
		nb_miss -= 10000;
	}
	log_flag(EXT_SENSORS, "ext_sensors: RRD: have %d values and miss %d values",
		 nb_values, nb_miss);

	if (flag_approximate &&
	    current_watt == (rrd_value_t)NO_VAL &&
	    last_valid_watt != (rrd_value_t)NO_VAL) {
		temp_energy = _get_additional_consumption(
			last_valid_time, t1,
			last_valid_watt, last_valid_watt);
		consumed_energy += temp_energy;
	}

	for (ii = 0; ii < ds_count; ii++)
		free(ds_names[ii]);

	free(ds_names);
	free(rrd_data);

	return (uint64_t)consumed_energy;
}

extern uint64_t RRD_consolidate(time_t step_starttime, time_t step_endtime,
				bitstr_t* bitmap_of_nodes)
{
	uint64_t consumed_energy = 0;
	uint64_t tmp;
	char *node_name = NULL;
	hostlist_t hl;
	char* path;

	node_name = bitmap2node_name(bitmap_of_nodes);
	hl = hostlist_create(node_name);
	xfree(node_name);
	while ((node_name = hostlist_shift(hl))) {
		if (!(path = _get_node_rrd_path(node_name,
						EXT_SENSORS_VALUE_ENERGY)))
			consumed_energy = NO_VAL64;
		free(node_name);
		if ((tmp = _rrd_consolidate_one(
			     step_starttime, step_endtime, path,
			     ext_sensors_cnf->energy_rra_name, true))
		    == NO_VAL64)
			consumed_energy = NO_VAL64;
		xfree(path);
		if (consumed_energy == NO_VAL64)
			break;
		consumed_energy += tmp;
	}
	hostlist_destroy(hl);

	return consumed_energy;
}

static int _update_node_data(void)
{
	int i;
	char* path;
	uint32_t tmp32;
	uint64_t tmp;
	ext_sensors_data_t *ext_sensors;
	time_t now = time(NULL);
	node_record_t *node_ptr;

	if (ext_sensors_cnf->dataopts & EXT_SENSORS_OPT_NODE_ENERGY) {
		for (i = 0; (node_ptr = next_node(&i)); i++) {
			ext_sensors = node_ptr->ext_sensors;
			if (ext_sensors->energy_update_time == 0) {
				ext_sensors->energy_update_time = now;
				ext_sensors->consumed_energy = 0;
				ext_sensors->current_watts = 0;
				continue;
			}
			if (!(path = _get_node_rrd_path(
				      node_ptr->name,
				      EXT_SENSORS_VALUE_ENERGY))) {
				ext_sensors->consumed_energy = NO_VAL64;
				ext_sensors->current_watts = NO_VAL;
				continue;
			}
			tmp = _rrd_consolidate_one(
				ext_sensors->energy_update_time,
				now, path,
				ext_sensors_cnf->energy_rra_name,
				false);
			xfree(path);
			if ((tmp != (uint64_t)NO_VAL) && (tmp != 0) &&
			    (last_valid_time != 0) &&
			    (last_valid_watt != (rrd_value_t)NO_VAL)) {
				if ((ext_sensors->consumed_energy <= 0) ||
				    (ext_sensors->consumed_energy ==
				     NO_VAL64)) {
					ext_sensors->consumed_energy = tmp;
				} else {
					ext_sensors->consumed_energy += tmp;
				}
				ext_sensors->energy_update_time =
					last_valid_time;
				ext_sensors->current_watts =
					(uint32_t)last_valid_watt;
			}
		}
	}

	if (ext_sensors_cnf->dataopts & EXT_SENSORS_OPT_NODE_TEMP) {
		for (i = 0; (node_ptr = next_node(&i)); i++) {
			ext_sensors = node_ptr->ext_sensors;
			if (!(path = _get_node_rrd_path(
				      node_ptr->name,
				      EXT_SENSORS_VALUE_TEMPERATURE))) {
				ext_sensors->temperature = NO_VAL;
				continue;
			}
			tmp32 = _rrd_get_last_one(
				path, ext_sensors_cnf->temp_rra_name);
			xfree(path);
			if (tmp32 != NO_VAL &&
			    tmp32 > ext_sensors_cnf->min_temp &&
			    tmp32 < ext_sensors_cnf->max_temp) {
				ext_sensors->temperature = tmp32;
			} else {
				ext_sensors->temperature = NO_VAL;
			}
		}
	}
	return SLURM_SUCCESS;
}

static int _update_switch_data(void)
{
	/* TODO: insert code here to do the following:
	 * If SwitchData is configured in ext_sensors_cnf->dataopts:
	 * for each switch, update data in switch_record from RRD database */
	return SLURM_SUCCESS;
}

static int _update_door_data(void)
{
	/* TODO: insert code here to do the following:
	 * If ColdDoorData is configured in ext_sensors_cnf->dataopts:
	 * for each door, update data in door_record from RRD database */
	return SLURM_SUCCESS;
}

extern int _ext_sensors_read_conf(void)
{
	s_p_options_t options[] = {
		{"JobData", S_P_STRING},
		{"NodeData", S_P_STRING},
		{"SwitchData", S_P_STRING},
		{"ColdDoorData", S_P_STRING},
		{"MinWatt", S_P_UINT32},
		{"MaxWatt", S_P_UINT32},
		{"MinTemp", S_P_UINT32},
		{"MaxTemp", S_P_UINT32},
		{"EnergyRRA", S_P_STRING},
		{"TempRRA", S_P_STRING},
		{"EnergyPathRRD", S_P_STRING},
		{"TempPathRRD", S_P_STRING},
		{NULL} };
	s_p_hashtbl_t *tbl = NULL;
	char *conf_path = NULL;
	struct stat buf;
	char *temp_str = NULL;

	/* Set initial values */
	if (ext_sensors_cnf == NULL) {
		return SLURM_ERROR;
	}
	_ext_sensors_clear_free_conf();
	/* Get the ext_sensors.conf path and validate the file */
	conf_path = get_extra_conf_path("ext_sensors.conf");
	if ((conf_path == NULL) || (stat(conf_path, &buf) == -1)) {
		fatal("ext_sensors: No ext_sensors file (%s)", conf_path);
	} else {
		debug2("ext_sensors: Reading ext_sensors file %s", conf_path);
		tbl = s_p_hashtbl_create(options);
		if (s_p_parse_file(tbl, NULL, conf_path, false, NULL) ==
		    SLURM_ERROR) {
			fatal("ext_sensors: Could not open/read/parse "
			      "ext_sensors file %s", conf_path);
		}
		/* ext_sensors initialization parameters */
		if (s_p_get_string(&temp_str, "JobData", tbl)) {
			if (strstr(temp_str, "energy"))
				ext_sensors_cnf->dataopts
					|= EXT_SENSORS_OPT_JOB_ENERGY;
		}
		xfree(temp_str);
		if (s_p_get_string(&temp_str, "NodeData", tbl)) {
			if (strstr(temp_str, "energy"))
				ext_sensors_cnf->dataopts
					|= EXT_SENSORS_OPT_NODE_ENERGY;
			if (strstr(temp_str, "temp"))
				ext_sensors_cnf->dataopts
					|= EXT_SENSORS_OPT_NODE_TEMP;
		}
		xfree(temp_str);
		if (s_p_get_string(&temp_str, "SwitchData", tbl)) {
			if (strstr(temp_str, "energy"))
				ext_sensors_cnf->dataopts
					|= EXT_SENSORS_OPT_SWITCH_ENERGY;
			if (strstr(temp_str, "temp"))
				ext_sensors_cnf->dataopts
					|= EXT_SENSORS_OPT_SWITCH_TEMP;
		}
		xfree(temp_str);
		if (s_p_get_string(&temp_str, "ColdDoorData", tbl)) {
			if (strstr(temp_str, "temp"))
				ext_sensors_cnf->dataopts
					|= EXT_SENSORS_OPT_COLDDOOR_TEMP;
		}
		xfree(temp_str);


		s_p_get_uint32(&ext_sensors_cnf->min_watt,"MinWatt", tbl);
		s_p_get_uint32(&ext_sensors_cnf->max_watt,"MaxWatt", tbl);
		s_p_get_uint32(&ext_sensors_cnf->min_temp,"MinTemp", tbl);
		s_p_get_uint32(&ext_sensors_cnf->max_temp,"MaxTemp", tbl);
		if (!s_p_get_string(&ext_sensors_cnf->energy_rra_name,
				    "EnergyRRA", tbl)) {
			if (ext_sensors_cnf->dataopts
			    & EXT_SENSORS_OPT_JOB_ENERGY)
				fatal("ext_sensors/rrd: EnergyRRA "
				      "must be set to gather JobData=energy.  "
				      "Please set this value in your "
				      "ext_sensors.conf file.");
		}

		if (!s_p_get_string(&ext_sensors_cnf->temp_rra_name,
				    "TempRRA", tbl)) {
			if (ext_sensors_cnf->dataopts
			    & EXT_SENSORS_OPT_NODE_TEMP)
				fatal("ext_sensors/rrd: TempRRA "
				      "must be set to gather NodeData=temp.  "
				      "Please set this value in your "
				      "ext_sensors.conf file.");
		}
		s_p_get_string(&ext_sensors_cnf->energy_rrd_file,
			       "EnergyPathRRD", tbl);
		s_p_get_string(&ext_sensors_cnf->temp_rrd_file,
			       "TempPathRRD", tbl);

		s_p_hashtbl_destroy(tbl);
	}
	xfree(conf_path);
	return SLURM_SUCCESS;
}

static void _ext_sensors_clear_free_conf(void)
{
	ext_sensors_cnf->dataopts = 0;
	ext_sensors_cnf->min_watt = _WATT_MIN;
	ext_sensors_cnf->max_watt = _WATT_MAX;
	ext_sensors_cnf->min_temp = _TEMP_MIN;
	ext_sensors_cnf->max_temp = _TEMP_MAX;
	xfree(ext_sensors_cnf->energy_rra_name);
	xfree(ext_sensors_cnf->temp_rra_name);
	xfree(ext_sensors_cnf->energy_rrd_file);
	xfree(ext_sensors_cnf->temp_rrd_file);
}

extern int ext_sensors_p_update_component_data(void)
{
	int rc_node, rc_switch, rc_door;

	rc_node = _update_node_data();
	rc_switch = _update_switch_data();
	rc_door = _update_door_data();
	if ((rc_node == SLURM_SUCCESS) &&
	    (rc_switch == SLURM_SUCCESS) &&
	    (rc_door == SLURM_SUCCESS))
		return SLURM_SUCCESS;
	return SLURM_ERROR;
}

extern int ext_sensors_p_get_stepstartdata(step_record_t *step_rec)
{
	/* Nothing to do here for ext_sensors/rrd plugin */
	int rc = SLURM_SUCCESS;
	return rc;
}

extern int ext_sensors_p_get_stependdata(step_record_t *step_rec)
{
	time_t step_endtime = time(NULL);
	int rc = SLURM_SUCCESS;

	if (ext_sensors_cnf->dataopts & EXT_SENSORS_OPT_JOB_ENERGY) {
		step_rec->ext_sensors->consumed_energy =
			RRD_consolidate(step_rec->start_time, step_endtime,
					step_rec->step_node_bitmap);
		if (step_rec->jobacct &&
		    (!step_rec->jobacct->energy.consumed_energy
		     || (step_rec->jobacct->energy.consumed_energy ==
			 NO_VAL64))) {
			step_rec->jobacct->energy.consumed_energy =
				step_rec->ext_sensors->consumed_energy;
		}
	}

	return rc;
}

extern List ext_sensors_p_get_config(void)
{
	config_key_pair_t *key_pair;
	List ext_list = list_create(destroy_config_key_pair);

	char *sep = ", ";
	char *tmp_val = NULL;

	if (ext_sensors_cnf->dataopts & EXT_SENSORS_OPT_JOB_ENERGY) {
		key_pair = xmalloc(sizeof(config_key_pair_t));
		key_pair->name = xstrdup("JobData");
		key_pair->value = xstrdup("energy");
		list_append(ext_list, key_pair);
	}

	if (ext_sensors_cnf->dataopts & EXT_SENSORS_OPT_NODE_ENERGY)
		tmp_val = xstrdup("energy");

	if (ext_sensors_cnf->dataopts & EXT_SENSORS_OPT_NODE_TEMP) {
		if (tmp_val)
			xstrcat(tmp_val, sep);
		xstrcat(tmp_val, "temp");
	}
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("NodeData");
	key_pair->value = tmp_val;
	list_append(ext_list, key_pair);
	tmp_val = NULL;

	if (ext_sensors_cnf->dataopts & EXT_SENSORS_OPT_SWITCH_ENERGY)
		tmp_val = xstrdup("energy");

	if (ext_sensors_cnf->dataopts & EXT_SENSORS_OPT_SWITCH_TEMP) {
		if (tmp_val)
			xstrcat(tmp_val, sep);

		xstrcat(tmp_val, "temp");
	}
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SwitchData");
	key_pair->value = tmp_val;
	list_append(ext_list, key_pair);
	tmp_val = NULL;

	if (ext_sensors_cnf->dataopts & EXT_SENSORS_OPT_COLDDOOR_TEMP) {
		key_pair = xmalloc(sizeof(config_key_pair_t));
		key_pair->name = xstrdup("ColdDoorData");
		key_pair->value = xstrdup("temp");
		list_append(ext_list, key_pair);
	}

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MinWatt");
	key_pair->value = xstrdup_printf("%u", ext_sensors_cnf->min_watt);
	list_append(ext_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MaxWatt");
	key_pair->value = xstrdup_printf("%u", ext_sensors_cnf->max_watt);
	list_append(ext_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MinTemp");
	key_pair->value = xstrdup_printf("%u", ext_sensors_cnf->min_temp);
	list_append(ext_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MaxTemp");
	key_pair->value = xstrdup_printf("%u", ext_sensors_cnf->max_temp);
	list_append(ext_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EnergyRRA");
	key_pair->value = xstrdup(ext_sensors_cnf->energy_rra_name);
	list_append(ext_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("TempRRA");
	key_pair->value = xstrdup(ext_sensors_cnf->temp_rra_name);
	list_append(ext_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EnergyPathRRD");
	key_pair->value = xstrdup(ext_sensors_cnf->energy_rrd_file);
	list_append(ext_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("TempPathRRD");
	key_pair->value = xstrdup(ext_sensors_cnf->temp_rrd_file);
	list_append(ext_list, key_pair);

	list_sort(ext_list, (ListCmpF) sort_key_pairs);

	return ext_list;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	/* read ext_sensors configuration */
	if (_ext_sensors_read_conf())
		return SLURM_ERROR;

	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

extern int fini(void)
{
	_ext_sensors_clear_free_conf();
	return SLURM_SUCCESS;
}
