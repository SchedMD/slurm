/*****************************************************************************\
 **  pmix_db.h - PMIx KVS database
 *****************************************************************************
 *  Copyright (C) 2014-2015 Artem Polyakov. All rights reserved.
 *  Copyright (C) 2015-2017 Mellanox Technologies. All rights reserved.
 *  Written by Artem Polyakov <artpol84@gmail.com, artemp@mellanox.com>.
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

#ifndef PMIXP_NSPACES_H
#define PMIXP_NSPACES_H

#include "pmixp_common.h"
#include "pmixp_info.h"
#include "pmixp_debug.h"
#include "pmixp_state.h"

typedef struct {
	void *blob;
	int blob_sz;
} pmixp_blob_t;

typedef struct {
#ifndef NDEBUG
#define PMIXP_NSPACE_MAGIC 0xCAFED00D
	int magic;
#endif
	char name[PMIXP_MAX_NSLEN];
	uint32_t nnodes; /* number of nodes in this namespace */
	int node_id; /* relative position of this node in this step */
	uint32_t ntasks; /* total number of tasks in this namespace */
	uint32_t *task_cnts; /* Number of tasks on each node of namespace */
	char *task_map_packed; /* Packed task mapping information */
	uint32_t *task_map; /* i'th task is located on task_map[i] node */
	hostlist_t hl;
} pmixp_namespace_t;

typedef struct {
#ifndef NDEBUG
#define PMIXP_NSPACE_DB_MAGIC 0xCAFEBABE
	int magic;
#endif
	List nspaces;
	pmixp_namespace_t *local;
} pmixp_db_t;

int pmixp_nspaces_init(void);
int pmixp_nspaces_finalize(void);
pmixp_namespace_t *pmixp_nspaces_find(const char *name);
pmixp_namespace_t *pmixp_nspaces_local(void);
int pmixp_nspaces_add(char *name, uint32_t nnodes, int node_id,
		      uint32_t ntasks, uint32_t *task_cnts,
		      char *task_map_packed, hostlist_t hl);

/* operations on the specific namespace */
static inline hostlist_t pmixp_nspace_hostlist(pmixp_namespace_t *nsptr)
{
	hostlist_t hl = hostlist_copy(nsptr->hl);
	return hl;
}

hostlist_t pmixp_nspace_rankhosts(pmixp_namespace_t *nsptr,
				  const uint32_t *ranks, size_t nranks);
int pmixp_nspace_resolve(const char *name, int rank);

size_t pmixp_nspace_mdx_lsize(List l);
int pmixp_nspaces_push(Buf buf, int cnt);

#endif /* PMIXP_NSPACES_H */
