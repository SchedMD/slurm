/*****************************************************************************\
 *  node_features.h - Infrastructure for changing a node's features on user
 *	demand
 *****************************************************************************
 *  Copyright (C) 2016 SchedMD LLC.
 *  Written by Morris Jette <jette@schedmd.com>
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
\*****************************************************************************/

#ifndef _NODE_FEATURES_H
#define _NODE_FEATURES_H

#include "slurm/slurm.h"

/* Perform plugin initialization: read configuration files, etc. */
extern int node_features_g_init(void);

/* Perform plugin termination: save state, free memory, etc. */
extern int node_features_g_fini(void);

/* Return count of node_feature plugins configured */
extern int node_features_g_count(void);

/* Reset plugin configuration information */
extern int node_features_g_reconfig(void);

/* Update active and available features on specified nodes, sets features on
 * all nodes is node_list is NULL */
extern int node_features_g_get_node(char *node_list);

/* Test if a job's feature specification is valid */
extern int node_features_g_job_valid(char *job_features);

/* Translate a job's feature specification to node boot options
 * RET node boot options, must be xfreed */
extern char *node_features_g_job_xlate(char *job_features);

#endif /* !_NODE_FEATURES_H */
