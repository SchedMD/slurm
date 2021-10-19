/*****************************************************************************\
 *  read_oci_conf.h - parse oci.conf configuration file.
 *****************************************************************************
 *  Copyright (C) 2021 SchedMD LLC.
 *  Written by Nathan Rini <nate@schedmd.com>
 *  All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
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
\*****************************************************************************/

#ifndef _READ_OCI_CONF_H
#define _READ_OCI_CONF_H

typedef struct {
	char *container_path; /* path pattern to use for holding OCI config */
	bool create_env_file; /* create file with environment */
	char *runtime_create; /* OCI runtime pattern to execute create */
	char *runtime_delete; /* OCI runtime pattern to execute delete */
	char *runtime_kill; /* OCI runtime pattern to execute kill */
	char *runtime_query; /* OCI runtime pattern to execute query */
	char *runtime_run; /* OCI runtime pattern to execute run */
	char *runtime_start; /* OCI runtime pattern to execute start */
} oci_conf_t;

extern int get_oci_conf(oci_conf_t **oci);

extern void free_oci_conf(oci_conf_t *oci);

#define FREE_NULL_OCI_CONF(_X)             \
	do {                               \
		if (_X)                    \
			free_oci_conf(_X); \
		_X = NULL;                 \
	} while (0)

#endif /*_READ_OCI_CONF_H */
