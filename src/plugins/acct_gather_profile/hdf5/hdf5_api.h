/****************************************************************************\
 *  hdf5_api.h
 *****************************************************************************
 *  Copyright (C) 2013 Bull S. A. S.
 *		Bull, Rue Jean Jaures, B.P.68, 78340, Les Clayes-sous-Bois.
 *
 *  Written by Rod Schultz <rod.schultz@bull.com>
 *
 *  Portions Copyright (C) 2013 SchedMD LLC.
 *  Written by Danny Auble <da@schedmd.com>
 *
 *  Provide support for acct_gather_profile plugins based on HDF5 files.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com>.
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
\****************************************************************************/
#ifndef __ACCT_GATHER_HDF5_API_H__
#define __ACCT_GATHER_HDF5_API_H__

#include <inttypes.h>
#include <stdlib.h>

#include <hdf5.h>
#include <hdf5_hl.h>

#define MAX_PROFILE_PATH 1024
#define MAX_ATTR_NAME 64
#define MAX_GROUP_NAME 64

#define ATTR_NODEINX "Node Index"
#define ATTR_NODENAME "Node Name"
#define ATTR_NSTEPS "Number of Steps"
#define ATTR_NNODES "Number of Nodes"
#define ATTR_NTASKS "Number of Tasks"
#define ATTR_CPUPERTASK "CPUs per Task"
#define ATTR_STARTTIME "Start Time"

#define GRP_ENERGY "Energy"
#define GRP_FILESYSTEM "Filesystem"
#define GRP_STEPS "Steps"
#define GRP_NODES "Nodes"
#define GRP_NETWORK "Network"
#define GRP_TASK "Task"

/*
 * Finalize profile (initialize static memory)
 */
void profile_fini(void);

/*
 * get attribute handle by name.
 *
 * Parameters
 *	parent	- handle to parent group.
 *	name	- name of the attribute
 *
 * Returns - handle for attribute (or -1 when not found), caller must close
 */
hid_t get_attribute_handle(hid_t parent, char* name);

/*
 * get group by name.
 *
 * Parameters
 *	parent	- handle to parent group.
 *	name	- name of the group
 *
 * Returns - handle for group (or -1 when not found), caller must close
 */
hid_t get_group(hid_t parent, const char* name);

/*
 * make group by name.
 *
 * Parameters
 *	parent	- handle to parent group.
 *	name	- name of the group
 *
 * Returns - handle for group (or -1 on error), caller must close
 */
hid_t make_group(hid_t parent, const char* name);

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

#endif /*__ACCT_GATHER_HDF5_API_H__*/
