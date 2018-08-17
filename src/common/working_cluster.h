/*****************************************************************************\
 *  workingcluster.h - definitions dealing with the working cluster
 ******************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble da@llnl.gov, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
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
\*****************************************************************************/
#ifndef _WORKINGCLUSTER_H
#define _WORKINGCLUSTER_H

/* Return the number of dimensions in the current working cluster */
extern uint16_t slurmdb_setup_cluster_dims(void);

/* Return the size of each dimensions in the current working cluster.
 * Returns NULL if information not available or not applicable. */
extern int * slurmdb_setup_cluster_dim_size(void);

/* Return the number of digits required in the numeric suffix of hostnames
 * in the current working cluster */
extern uint16_t slurmdb_setup_cluster_name_dims(void);

/* Return true if the working cluster is a Cray system */
extern bool is_cray_system(void);

/* Return the architecture flags in the current working cluster */
extern uint32_t slurmdb_setup_cluster_flags(void);

/* Translate architecture flag strings to their equivalent bitmaps */
extern uint32_t slurmdb_str_2_cluster_flags(char *flags_in);

/*
 * Translate architecture flag bitmaps to their equivalent comma-delimited
 * string
 *
 * NOTE: Call xfree() to release memory allocated to the return value
 */
extern char *slurmdb_cluster_flags_2_str(uint32_t flags_in);

/*
 * Return the plugin select id of the cluster working or current
 */
extern uint32_t slurmdb_setup_plugin_id_select(void);

/*
 * Setup the working_cluster_rec with the working_cluster_rec and node_addrs
 * returned in an allocation response msg.
 */
extern void
slurm_setup_remote_working_cluster(resource_allocation_response_msg_t *msg);

#endif
