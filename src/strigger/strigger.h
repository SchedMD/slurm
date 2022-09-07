/****************************************************************************\
 *  strigger.h - definitions used for strigger functions
 *****************************************************************************
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) 2010-2016 SchedMD LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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
\****************************************************************************/

#ifndef _STRIGGER_H
#define _STRIGGER_H

#include "slurm/slurm.h"
#include "src/common/macros.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurmdb_defs.h"

struct strigger_parameters {
	bool     burst_buffer;
	List     clusters;
	uint16_t flags;
	bool     front_end;
	bool     job_fini;
	uint32_t job_id;
	bool     mode_set;
	bool     mode_get;
	bool     mode_clear;
	bool	 pri_ctld_fail;
	bool	 pri_ctld_res_op;
	bool	 pri_ctld_res_ctrl;
	bool	 pri_ctld_acct_buffer_full;
	bool	 bu_ctld_fail;
	bool	 bu_ctld_res_op;
	bool	 bu_ctld_as_ctrl;
	bool	 pri_dbd_fail;
	bool	 pri_dbd_res_op;
	bool	 pri_db_fail;
	bool	 pri_db_res_op;
	bool     no_header;
	bool     node_down;
	bool     node_drained;
	bool node_draining;
	char *   node_id;
	bool     node_idle;
	bool node_fail;
	bool node_resume;
	bool     node_up;
	int      offset;
	char *   program;
	bool     quiet;
	bool     reconfig;
	bool     time_limit;
	uint32_t trigger_id;
	uint32_t user_id;
	int      verbose;
};

extern struct strigger_parameters params;

extern void parse_command_line(int argc, char **argv);

#endif
