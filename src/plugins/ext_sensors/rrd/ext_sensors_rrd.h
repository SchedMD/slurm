/*****************************************************************************\
 *  ext_sensors_rrd.h - slurm external sensors plugin for rrd.
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

#include "src/slurmctld/slurmctld.h"

#ifndef _EXT_SENSORS_RRD_H_
#define _EXT_SENSORS_RRD_H_

/* ext_sensors data collection option flags */
#define EXT_SENSORS_OPT_JOB_ENERGY	0x00000001
#define EXT_SENSORS_OPT_NODE_ENERGY	0x00000002
#define EXT_SENSORS_OPT_NODE_TEMP	0x00000004
#define EXT_SENSORS_OPT_SWITCH_ENERGY	0x00000008
#define EXT_SENSORS_OPT_SWITCH_TEMP	0x00000010
#define EXT_SENSORS_OPT_COLDDOOR_TEMP	0x00000020

/* ext_sensors plugins configuration parameters */
typedef struct ext_sensors_config {
	uint64_t dataopts;
	uint32_t min_watt;
	uint32_t max_watt;
	uint32_t min_temp;
	uint32_t max_temp;
	char    *energy_rra_name;
	char    *temp_rra_name;
	char    *energy_rrd_file;
	char    *temp_rrd_file;
} ext_sensors_conf_t;

/* read external sensors configuration file */
extern int ext_sensors_read_conf(void);

/* clear and free external sensors configuration structures */
extern void ext_sensors_free_conf(void);

/* update external sensors data for hardware components */
extern int ext_sensors_p_update_component_data(void);

/* get external sensors data at start of jobstep */
extern int ext_sensors_p_get_stepstartdata(struct step_record *step_rec);

/* get external sensors data at end of jobstep */
extern int ext_sensors_p_get_stependdata(struct step_record *step_rec);

/* get external sensor config file */
extern List ext_sensors_p_get_config(void);

/* consolidate RRD data */
extern uint64_t RRD_consolidate(time_t step_starttime, time_t step_endtime,
				bitstr_t* bitmap_of_nodes);

extern int init(void);
extern int fini(void);

#endif
