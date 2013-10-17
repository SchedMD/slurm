/*****************************************************************************\
 *  slurmd_plugstack.h - driver for slurmctld plugstack plugin
 *****************************************************************************
 *  Copyright (C) 2013 Intel Inc
 *  Written by Ralph H Castain <ralph.h.castain@intel.com>
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

#ifndef _SLURMD_PLUGSTACK_H
#define _SLURMD_PLUGSTACK_H

#include "slurm/slurm.h"
#include "src/slurmctld/slurmctld.h"

/*****************************************************************************\
 *  Plugin slurmctld/nonstop callback functions
\*****************************************************************************/
typedef struct slurm_nonstop_ops {
	void		(*job_begin)	( struct job_record *job_ptr );
	void		(*job_fini)	( struct job_record *job_ptr );
	void		(*node_fail)	( struct job_record *job_ptr,
					  struct node_record *node_ptr);
} slurm_nonstop_ops_t;
extern slurm_nonstop_ops_t nonstop_ops;
/*
 * Initialize the slurmd plugstack plugin.
 *
 * Returns a SLURM errno.
 */
extern int slurmd_plugstack_init(void);

/*
 * Terminate the slurmd plugstack plugin. Free memory.
 *
 * Returns a SLURM errno.
 */
extern int slurmd_plugstack_fini(void);

#endif /* !_SLURMD_PLUGSTACK_H */
