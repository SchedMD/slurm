/*****************************************************************************\
 **  pmix_state.h - PMIx agent state related code
 *****************************************************************************
 *  Copyright (C) 2014-2015 Artem Polyakov. All rights reserved.
 *  Copyright (C) 2015-2018 Mellanox Technologies. All rights reserved.
 *  Written by Artem Polyakov <artpol84@gmail.com, artemp@mellanox.com>,
 *             Boris Karasev <karasev.b@gmail.com, boriska@mellanox.com>.
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

#ifndef PMIXP_STATE_H
#define PMIXP_STATE_H

#include "pmixp_common.h"
#include "pmixp_debug.h"
#include "pmixp_io.h"
#include "pmixp_coll.h"
#include "pmixp_dmdx.h"

/*
 * PMIx plugin state structure
 */

typedef struct {
#ifndef NDEBUG
#define PMIXP_STATE_MAGIC 0xFEEDCAFE
	int magic;
#endif
	list_t *coll;
	eio_handle_t *srv_handle;
	pthread_mutex_t lock;
} pmixp_state_t;

extern pmixp_state_t _pmixp_state;

/*
 * General PMIx plugin state manipulation functions
 */

int pmixp_state_init(void);
void pmixp_state_finalize(void);

static inline void pmixp_state_sanity_check(void)
{
	xassert(_pmixp_state.magic == PMIXP_STATE_MAGIC);
}

/*
 * Collective state
 */

pmixp_coll_t *pmixp_state_coll_get(pmixp_coll_type_t type,
				   const pmix_proc_t *ranges,
				   size_t nranges);
pmixp_coll_t *pmixp_state_coll_new(pmixp_coll_type_t type,
				   const pmix_proc_t *ranges,
				   size_t nranges);

void pmixp_state_coll_cleanup(void);

#endif /* PMIXP_STATE_H */
