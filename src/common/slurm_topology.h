/*****************************************************************************\
 *  slurm_topology.h - Define topology plugin functions.
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#ifndef __SLURM_CONTROLLER_TOPO_PLUGIN_API_H__
#define __SLURM_CONTROLLER_TOPO_PLUGIN_API_H__

#include "slurm/slurm.h"
#include "src/slurmctld/slurmctld.h"

/*****************************************************************************\
 *  SWITCH topology data structures
 *  defined here but is really tree plugin related
\*****************************************************************************/
struct switch_record {
	uint32_t consumed_energy;	/* consumed energy, in joules */
	int level;			/* level in hierarchy, leaf=0 */
	uint32_t link_speed;		/* link speed, arbitrary units */
	char *name;			/* switch name */
	bitstr_t *node_bitmap;		/* bitmap of all nodes descended from
					 * this switch */
	char *nodes;			/* name if direct descendant nodes */
	uint16_t  num_switches;         /* number of descendant switches */
	uint16_t  parent;		/* index of parent switch */
	char *switches;			/* name of direct descendant switches */
	uint16_t *switch_index;		/* indexes of child switches */
	uint32_t temp;			/* temperature, in celsius */
};

extern struct switch_record *switch_record_table;  /* ptr to switch records */
extern int switch_record_cnt;		/* size of switch_record_table */
extern int switch_levels;               /* number of switch levels     */

/*****************************************************************************\
 *  Slurm topology functions
\*****************************************************************************/

/*
 * Initialize the topology plugin.
 *
 * Returns a SLURM errno.
 */
int slurm_topo_init( void );

/*
 * Terminate the topology plugin.
 *
 * Returns a SLURM errno.
 */
extern int slurm_topo_fini(void);

/*
 **************************************************************************
 *                          P L U G I N   C A L L S                       *
 **************************************************************************
 */

/*
 * slurm_topo_build_config - build or rebuild system topology information
 *	after a system startup or reconfiguration.
 */
extern int slurm_topo_build_config( void );

/*
 * slurm_topo_generate_node_ranking  -  populate node_rank fields
 * NOTE: This operation is only supported by those topology plugins for
 *       which the node ordering between slurmd and slurmctld is invariant.
 */
extern bool slurm_topo_generate_node_ranking( void );

/*
 * slurm_topo_get_node_addr - build node address and the associated pattern
 *      based on the topology information
 */
extern int slurm_topo_get_node_addr( char* node_name, char** addr,
				     char** pattern );

#endif /*__SLURM_CONTROLLER_TOPO_PLUGIN_API_H__*/
