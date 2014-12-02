/*****************************************************************************\
 *  caches.c - Functions for obtaining slurmctld cache information
 *****************************************************************************
 *  Copyright (C) 2008-2011 Lawrence Livermore National Security.
 *  Produced at CSCS
 *  Written by Stephen Trofinoff
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

#include <stdlib.h>
#include <string.h>

#include "slurm/slurm_errno.h"

#include "src/common/assoc_mgr.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"

static pthread_mutex_t cache_mutex = PTHREAD_MUTEX_INITIALIZER;
static void _pack_cache(slurmdb_user_rec_t *cache, Buf buffer,
			uint16_t protocol_version);
static void _pack_assoc(slurmdb_assoc_rec_t *assoc, Buf buffer,
			uint16_t protocol_version);

/*
 *
 * Return controller cache information to the library.
 */
extern void
get_all_cache_info(char **buffer_ptr, int *buffer_size,
                     uid_t uid, uint16_t protocol_version)
{
	ListIterator         iter;
	slurmdb_user_rec_t  *cache_entry;
	uint32_t             caches_packed;
	int                  tmp_offset;
	Buf                  buffer;
	time_t               now = time(NULL);
	slurmdb_assoc_rec_t *assoc_entry;
	uint32_t             assocs_packed;

	debug2("%s: calling for all cache user records", __func__);

	buffer_ptr[0] = NULL;
	*buffer_size  = 0;

	buffer = init_buf(BUF_SIZE);

	/* write header: version and time
	 */
	caches_packed = 0;
	assocs_packed = 0;
	pack32(caches_packed, buffer);
	pack32(assocs_packed, buffer);
	pack_time(now, buffer);

	slurm_mutex_lock(&cache_mutex);
	if (assoc_mgr_user_list) {
		iter = list_iterator_create(assoc_mgr_user_list);
		while ((cache_entry = list_next(iter))) {
			/* Now encode the cache data structure.
			 */
			_pack_cache(cache_entry, buffer, protocol_version);
			++caches_packed;
		}
		list_iterator_destroy(iter);
	}

	if (assoc_mgr_assoc_list) {
		iter = list_iterator_create(assoc_mgr_assoc_list);
		while ((assoc_entry = list_next(iter))) {
			_pack_assoc(assoc_entry, buffer, protocol_version);
			++assocs_packed;
		}
		list_iterator_destroy(iter);
	}

	slurm_mutex_unlock(&cache_mutex);
	debug2("%s: processed %d cache user records", __func__, caches_packed);

	/* put the real record count in the message body header
	 */
	tmp_offset = get_buf_offset(buffer);
	set_buf_offset(buffer, 0);
	pack32(caches_packed, buffer);
	pack32(assocs_packed, buffer);
	set_buf_offset(buffer, tmp_offset);

	*buffer_size  = get_buf_offset(buffer);
	buffer_ptr[0] = xfer_buf_data(buffer);
}

/* pack_cache()
 *
 * Encode the caches data structure.
 *
 *	uint16_t	admin_level;
 *	char*		default_acct;
 *	char*		default_wckey;
 *	char*		name;
 *	char*		old_name;
 *	uint32_t	uid;
 *
 * Omitting, for now, the assoc_list, coord_accts and wckey_list fields.
 * If in the future they should be needed they can always be added.
 *
 */
static void
_pack_cache(slurmdb_user_rec_t *cache, Buf buffer, uint16_t protocol_version)
{
	if (protocol_version >= SLURM_15_08_PROTOCOL_VERSION) {
		pack16 (cache->admin_level, buffer);
		packstr(cache->default_acct, buffer);
		packstr(cache->default_wckey, buffer);
		packstr(cache->name, buffer);
		packstr(cache->old_name, buffer);
		pack32 (cache->uid, buffer);
	} else {
		error("\
%s: protocol_version %hu not supported", __func__, protocol_version);
	}
}

static void
_pack_assoc(slurmdb_assoc_rec_t *assoc, Buf buffer,
	    uint16_t protocol_version)
{
	if (protocol_version >= SLURM_15_08_PROTOCOL_VERSION) {
		packstr(assoc->acct, buffer);
		packstr(assoc->cluster, buffer);
		pack32 (assoc->def_qos_id, buffer);
		pack64 (assoc->grp_cpu_mins, buffer);
		pack64 (assoc->grp_cpu_run_mins, buffer);
		pack32 (assoc->grp_cpus, buffer);
		pack32 (assoc->grp_jobs, buffer);
		pack32 (assoc->grp_mem, buffer);
		pack32 (assoc->grp_nodes, buffer);
		pack32 (assoc->grp_submit_jobs, buffer);
		pack32 (assoc->grp_wall, buffer);
		pack32 (assoc->id, buffer);
		pack16 (assoc->is_def, buffer);
		pack32 (assoc->lft, buffer);
		pack64 (assoc->max_cpu_mins_pj, buffer);
		pack64 (assoc->max_cpu_run_mins, buffer);
		pack32 (assoc->max_cpus_pj, buffer);
		pack32 (assoc->max_jobs, buffer);
		pack32 (assoc->max_nodes_pj, buffer);
		pack32 (assoc->max_submit_jobs, buffer);
		pack32 (assoc->max_wall_pj, buffer);
		packstr(assoc->parent_acct, buffer);
		pack32 (assoc->parent_id, buffer);
		packstr(assoc->partition, buffer);
		pack32 (assoc->rgt, buffer);
		pack32 (assoc->shares_raw, buffer);
		pack32 (assoc->uid, buffer);
		packstr(assoc->user, buffer);
	} else {
		error("\
%s: protocol_version %hu not supported", __func__, protocol_version);
	}
}
