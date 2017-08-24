/*****************************************************************************\
 **  pmix_state.c - PMIx agent state related code
 *****************************************************************************
 *  Copyright (C) 2014-2015 Artem Polyakov. All rights reserved.
 *  Copyright (C) 2015      Mellanox Technologies. All rights reserved.
 *  Written by Artem Polyakov <artpol84@gmail.com, artemp@mellanox.com>.
 *
 *  This file is part of SLURM, a resource management program.
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

#include "pmixp_common.h"
#include "pmixp_debug.h"
#include "pmixp_info.h"
#include "pmixp_state.h"
#include "pmixp_nspaces.h"
#include "pmixp_coll.h"

pmixp_state_t _pmixp_state;

void _xfree_coll(void *x)
{
	pmixp_coll_t *coll = (pmixp_coll_t *)x;
	pmixp_coll_free(coll);
	xfree(coll);
}

int pmixp_state_init(void)
{
#ifndef NDEBUG
	_pmixp_state.magic = PMIX_STATE_MAGIC;
#endif
	_pmixp_state.coll = list_create(_xfree_coll);

	slurm_mutex_init(&_pmixp_state.lock);
	return SLURM_SUCCESS;
}

void pmixp_state_finalize(void)
{
#ifndef NDEBUG
	_pmixp_state.magic = 0;
#endif
	list_destroy(_pmixp_state.coll);
}

static bool _compare_ranges(const pmix_proc_t *r1, const pmix_proc_t *r2,
			    size_t nprocs)
{
	int i;
	for (i = 0; i < nprocs; i++) {
		if (0 != xstrcmp(r1[i].nspace, r2[i].nspace)) {
			return false;
		}
		if (r1[i].rank != r2[i].rank) {
			return false;
		}
	}
	return true;
}

static pmixp_coll_t *_find_collective(pmixp_coll_type_t type,
				      const pmix_proc_t *procs,
				      size_t nprocs)
{
	pmixp_coll_t *coll = NULL, *ret = NULL;
	ListIterator it;

	/* Walk through the list looking for the collective descriptor */
	it = list_iterator_create(_pmixp_state.coll);
	while (NULL != (coll = list_next(it))) {
		if (coll->nprocs != nprocs) {
			continue;
		}
		if (coll->type != type) {
			continue;
		}
		if (0 == coll->nprocs) {
			ret = coll;
			goto exit;
		}
		if (_compare_ranges(coll->procs, procs, nprocs)) {
			ret = coll;
			goto exit;
		}
	}
exit:
	list_iterator_destroy(it);
	return ret;
}

pmixp_coll_t *pmixp_state_coll_get(pmixp_coll_type_t type,
				   const pmix_proc_t *procs,
				   size_t nprocs)
{
	pmixp_coll_t *ret = NULL;

	/* Collectives are created once for each type and process set
	 * and resides till the end of jobstep lifetime.
	 * So in most cases we will find that collective is already
	 * exists.
	 * First we try to find collective in the list without locking. */

	if (NULL != (ret = _find_collective(type, procs, nprocs))) {
		return ret;
	}

	/* if we failed to find the collective we most probably need
	 * to create a new structure. To do so we need to lo lock the
	 * whole state and try to search again to exclude situation where
	 * concurent thread already created it while we were doing the
	 * first search */

	if (0 != pmixp_coll_belong_chk(type, procs, nprocs)) {
		return NULL;
	}

	slurm_mutex_lock(&_pmixp_state.lock);

	if (NULL == (ret = _find_collective(type, procs, nprocs))) {
		/* 1. Create and insert unitialized but locked coll
		 * structure into the list. We can release the state
		 * structure right after that */
		ret = xmalloc(sizeof(*ret));
		/* initialize with unlocked list but locked element */
		if (PMIX_SUCCESS != pmixp_coll_init(ret, procs, nprocs, type)) {
			if (NULL != ret->procs) {
				xfree(ret->procs);
			}
			xfree(ret);
			ret = NULL;
		} else {
			list_append(_pmixp_state.coll, ret);
		}
	}

	slurm_mutex_unlock(&_pmixp_state.lock);
	return ret;
}

void pmixp_state_coll_cleanup(void)
{
	pmixp_coll_t *coll = NULL;
	ListIterator it;
	time_t ts = time(NULL);

	/* Walk through the list looking for the collective descriptor */
	it = list_iterator_create(_pmixp_state.coll);
	while (NULL != (coll = list_next(it))) {
		pmixp_coll_reset_if_to(coll, ts);
	}
	list_iterator_destroy(it);
}
