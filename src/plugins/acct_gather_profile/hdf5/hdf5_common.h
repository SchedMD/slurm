/****************************************************************************\
 *  hdf5_common.h
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC.
 *  Written by Danny Auble <da@schedmd.com>
 *
 *  Provide support for acct_gather_profile plugins based on HDF5 files.
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
\****************************************************************************/
#ifndef __ACCT_GATHER_HDF5_COMMON_H__
#define __ACCT_GATHER_HDF5_COMMON_H__

#if HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif			/* HAVE_INTTYPES_H */
#else				/* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif				/*  HAVE_CONFIG_H */

#include <hdf5.h>
#include "src/common/slurm_acct_gather_profile.h"

#define MAX_PROFILE_PATH 1024
#define MAX_ATTR_NAME 64
#define MAX_GROUP_NAME 64
#define MAX_DATASET_NAME 64

#define ATTR_NODENAME "Node Name"
#define ATTR_STARTTIME "Start Time"
#define ATTR_NSTEPS "Number of Steps"
#define ATTR_NNODES "Number of Nodes"
#define ATTR_NTASKS "Number of Tasks"
#define ATTR_TASKID "Task Id"
#define ATTR_CPUPERTASK "CPUs per Task"
#define ATTR_DATATYPE "Data Type"
#define ATTR_SUBDATATYPE "Subdata Type"
#define ATTR_STARTTIME "Start Time"
#define ATTR_STARTSEC "Start Second"
#define SUBDATA_DATA "Data"
#define SUBDATA_NODE "Node"
#define SUBDATA_SAMPLE "Sample"
#define SUBDATA_SERIES "Series"
#define SUBDATA_TOTAL "Total"
#define SUBDATA_SUMMARY "Summary"

#define GRP_STEP "Step"
#define GRP_NODES "Nodes"
#define GRP_NODE "Node"
#define GRP_SAMPLES "Time Series"
#define GRP_SAMPLE "Sample"
#define GRP_TASKS "Tasks"
#define GRP_TOTALS "Totals"

#define GRP_TASK "Task"

// Data types supported by all HDF5 plugins of this type

#define TOD_LEN 24
#define TOD_FMT "%F %T"

extern hid_t typTOD;

extern void profile_init();
extern void profile_fini();

/*
 * Put string attribute
 *
 * Parameters
 *	parent	- handle to parent group.
 *	name	- name of the attribute
 *	value	- value of the attribute
 */
void put_string_attribute(hid_t parent, char* name, char* value);

/*
 * Put integer attribute
 *
 * Parameters
 *	parent	- handle to parent group.
 *	name	- name of the attribute
 *	value	- value of the attribute
 */
void put_int_attribute(hid_t parent, char* name, int value);

/*
 * Put one data sample into a new group in an HDF5 file
 *
 * Parameters
 *	parent  - handle to parent group.
 *	type    - type of data (PROFILE_*_DATA from slurm_acct_gather_profile.h)
 *	subtype - generally source (node, series, ...) or summary
 *	group   - name of new group
 *	data    - data for the sample
 *      nItems  - number of items of type in the data
 */
void put_hdf5_data(hid_t parent, char* type, char* subtype, char* group,
		void* data, int nItems);

/*
 * make group by name.
 *
 * Parameters
 *	parent	- handle to parent group.
 *	name	- name of the group
 *
 * Returns - handle for group (or -1 on error), caller must close
 */
hid_t make_group(hid_t parent, char* name);

/*
 * get group by name.
 *
 * Parameters
 *	parent	- handle to parent group.
 *	name	- name of the group
 *
 * Returns - handle for group (or -1 when not found), caller must close
 */
hid_t get_group(hid_t parent, char* name);

#endif
