/*****************************************************************************\
  *  node_features.h - common node features handling
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

#include "src/common/bitstring.h"
#include "src/common/job_record.h"
#include "src/common/list.h"

extern list_t *active_feature_list; /* currently active node_feature_t's */
extern list_t *avail_feature_list; /* available node_feature_t's */
extern bool node_features_updated;

typedef struct node_features {
	uint32_t magic; /* magic cookie to test data integrity */
	char *name; /* name of a feature */
	bitstr_t *node_bitmap; /* bitmap of nodes with this feature */
} node_feature_t;

/*
 * For a configuration where available_features == active_features,
 * build new active and available feature lists
 */
extern void node_features_build_list_eq(void);

/*
 * For a configuration where available_features != active_features,
 * build new active and available feature lists
 */
extern void node_features_build_list_ne(void);

/*
 * Build a list of active features available in the job nodes
 */
extern void node_features_build_active_list(job_record_t *job_ptr);

/*
 * Update active_feature_list or avail_feature_list
 * feature_list IN - list to update: active_feature_list or avail_feature_list
 * new_features IN - New active_features
 * node_bitmap IN - Nodes with the new active_features value
 */
extern void node_features_update_list(list_t *feature_list, char *new_features,
				      bitstr_t *node_bitmap);

/*
 * Free global active_feature_list and avail_feature_lists.
 */
extern void node_features_free_lists(void);
