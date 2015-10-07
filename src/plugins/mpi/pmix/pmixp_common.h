/*****************************************************************************\
 **  pmix_common.h - PMIx common declarations and includes
 *****************************************************************************
 *  Copyright (C) 2014-2015 Artem Polyakov. All rights reserved.
 *  Copyright (C) 2015      Mellanox Technologies. All rights reserved.
 *  Written by Artem Polyakov <artpol84@gmail.com, artemp@mellanox.com>.
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

#ifndef PMIXP_COMMON_H
#define PMIXP_COMMON_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <unistd.h>

/* Common includes for all source files
 * Define SLURM translator header first to override
 * all translated functions
 */
#include "src/common/slurm_xlator.h"

/* Other useful includes */
#include "slurm/slurm_errno.h"
#include "src/common/slurm_mpi.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"
#include "src/common/xmalloc.h"
#include "src/common/xassert.h"
#include "src/common/eio.h"
#include "src/common/mapping.h"
#include "src/common/fd.h"
#include "src/common/net.h"

/* PMIx library header */
#include <pmix_server.h>

/* TODO: use /tmp directory instead */
#define MAX_USOCK_PATH							\
	((size_t) &(((struct sockaddr_un *)0 + 1)->sun_family) -	\
	(size_t)&(((struct sockaddr_un *)0)->sun_path))

/* Job/step resource description */
#define PMIXP_STEP_NODES_ENV "SLURM_STEP_NODELIST"
#define PMIXP_JOB_NODES_ENV "SLURM_JOB_NODELIST"
#define PMIXP_CPUS_PER_NODE_ENV "SLURM_JOB_CPUS_PER_NODE"
#define PMIXP_CPUS_PER_TASK "SLURM_CPUS_PER_TASK"
#define PMIXP_SLURM_MAPPING_ENV "SLURM_PMIX_MAPPING_SERV"
/* Mode */
#define PMIXP_TIMEOUT "SLURM_PMIX_TIMEOUT"
#define PMIXP_TIMEOUT_DEFAULT 10
#define PMIXP_TMPDIR_SRV "SLURM_PMIX_SRV_TMPDIR"
#define PMIXP_TMPDIR_CLI "SLURM_PMIX_TMPDIR"
#define PMIXP_TMPDIR_DEFAULT "/tmp/"
#define PMIXP_OS_TMPDIR_ENV "TMPDIR"
#define PMIXP_PMIXLIB_TMPDIR "PMIX_SERVER_TMPDIR"

#endif /* PMIXP_COMMON_H */
