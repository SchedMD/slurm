/*****************************************************************************\
 *  cluster_info.c - get/print the cluster state information of slurm
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#include "slurm/slurm.h"
#include "slurm/slurmdb.h"

#include "src/common/list.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

static int _match_and_setup_cluster_rec(void *x, void *key)
{
	list_t *cluster_name_list = key;
	slurmdb_cluster_rec_t *cluster_rec = x;

	if (slurmdb_setup_cluster_rec(cluster_rec))
		/* setup failed */
		return 0;

	if (!cluster_name_list)
		/* match all clusters */
		return 1;

	if (list_find_first(cluster_name_list, slurm_find_char_in_list,
			    cluster_rec->name))
		return 1;

	return 0;
}

static int _get_clusters_from_fed(list_t **cluster_records, char *cluster_names)
{
	int transfer_count = 0;
	list_t *cluster_list = list_create(slurmdb_destroy_cluster_rec);
	list_t *cluster_name_list = NULL;
	slurmdb_federation_rec_t *fed = NULL;

	if (slurm_load_federation((void *)&fed) || !fed) {
		error("--federation set or \"fed_display\" configured, but could not load federation information: %m");
		FREE_NULL_LIST(cluster_list);
		return SLURM_ERROR;
	}

	cluster_name_list = list_create(xfree_ptr);
	slurm_addto_char_list(cluster_name_list, cluster_names);

	transfer_count = list_transfer_match(fed->cluster_list, cluster_list,
					     _match_and_setup_cluster_rec,
					     cluster_name_list);
	if (transfer_count != list_count(cluster_name_list)) {
		/*
		 * One of the requested clusters isn't part of the federation.
		 * Go ask the dbd about it.
		 */
		FREE_NULL_LIST(cluster_list);
		FREE_NULL_LIST(cluster_name_list);
		return SLURM_ERROR;
	}

	*cluster_records = cluster_list;

	FREE_NULL_LIST(cluster_name_list);

	return SLURM_SUCCESS;
}

extern int slurm_get_cluster_info(list_t **cluster_records, char *cluster_names,
				  uint16_t show_flags)
{
	xassert(cluster_records);

	if (!cluster_records)
		return SLURM_ERROR;

	/* get cluster records from slurmctld federation record */
	if (xstrcasecmp(cluster_names, "all") &&
	    ((show_flags & SHOW_FEDERATION) ||
	     (xstrstr(slurm_conf.fed_params, "fed_display")))) {
		if (!_get_clusters_from_fed(cluster_records, cluster_names))
			return SLURM_SUCCESS;
	}

	/* get cluster records from slurmdbd */
	if (!(*cluster_records = slurmdb_get_info_cluster(cluster_names))) {
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}
