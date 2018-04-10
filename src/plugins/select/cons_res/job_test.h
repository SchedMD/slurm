/*****************************************************************************\
 *  select_cons_res.h
 *****************************************************************************
 *  Copyright (C) 2006 Hewlett-Packard Development Company, L.P.
 *  Written by Susanne M. Balle, <susanne.balle@hp.com>
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

#ifndef _CR_JOB_TEST_H
#define _CR_JOB_TEST_H

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/node_select.h"
#include "src/common/pack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurm_resource_info.h"
#include "src/slurmctld/slurmctld.h"


/* _job_test - does most of the real work for select_p_job_test(), which
 *	pretty much just handles load-leveling and max_share logic */
int cr_job_test(struct job_record *job_ptr, bitstr_t *node_bitmap,
		uint32_t min_nodes, uint32_t max_nodes, uint32_t req_nodes,
		int mode, uint16_t cr_type,
		enum node_cr_state job_node_req, uint32_t cr_node_cnt,
		struct part_res_record *cr_part_ptr,
		struct node_use_record *node_usage, bitstr_t *exc_core_bitmap,
		bool prefer_alloc_nodes, bool qos_preemptor, bool preempt_mode);

/*
 * Given an available node_bitmap, return a corresponding available core_bitmap,
 *	excluding all specialized cores.
 *
 * node_map IN - Bitmap of available nodes
 * core_spec IN - Count of specialized cores requested by the job or NO_VAL
 * RET bitmap of cores available for use by this job or reservation
 * NOTE: Call bit_free() on return value to avoid memory leak.
 */
extern bitstr_t *make_core_bitmap(bitstr_t *node_map, uint16_t core_spec);

#endif /* !_CR_JOB_TEST_H */
