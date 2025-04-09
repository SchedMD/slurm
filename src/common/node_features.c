/*****************************************************************************\
  *  node_features.c - common node features handling
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

#include "src/common/node_features.h"
#include "src/common/bitstring.h"
#include "src/common/job_record.h"
#include "src/common/node_conf.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmctld/read_config.h"
#include "src/slurmctld/slurmctld.h"

#define FEATURE_MAGIC 0x34dfd8b5

/* Global variables */
list_t *active_feature_list = NULL;
list_t *avail_feature_list = NULL;
bool node_features_updated = true;

/*
 * Add feature to list
 * feature_list IN - destination list, either active_feature_list or
 *	avail_feature_list
 * feature IN - name of the feature to add
 * node_bitmap IN - bitmap of nodes with named feature
 */
static void _add_config_feature(list_t *feature_list, char *feature,
				bitstr_t *node_bitmap)
{
	node_feature_t *feature_ptr;
	list_itr_t *feature_iter;
	bool match = false;

	/* If feature already in avail_feature_list, just update the bitmap */
	feature_iter = list_iterator_create(feature_list);
	while ((feature_ptr = list_next(feature_iter))) {
		if (xstrcmp(feature, feature_ptr->name))
			continue;
		bit_or(feature_ptr->node_bitmap, node_bitmap);
		match = true;
		break;
	}
	list_iterator_destroy(feature_iter);

	if (!match) { /* Need to create new avail_feature_list record */
		feature_ptr = xmalloc(sizeof(node_feature_t));
		feature_ptr->magic = FEATURE_MAGIC;
		feature_ptr->name = xstrdup(feature);
		feature_ptr->node_bitmap = bit_copy(node_bitmap);
		list_append(feature_list, feature_ptr);
	}
}

/*
 * Add feature to list
 * feature_list IN - destination list, either active_feature_list or
 *     avail_feature_list
 * feature IN - name of the feature to add
 * node_inx IN - index of the node with named feature
 */
static void _add_config_feature_inx(list_t *feature_list, char *feature,
				    int node_inx)
{
	node_feature_t *feature_ptr;
	list_itr_t *feature_iter;
	bool match = false;

	/* If feature already in avail_feature_list, just update the bitmap */
	feature_iter = list_iterator_create(feature_list);
	while ((feature_ptr = list_next(feature_iter))) {
		if (xstrcmp(feature, feature_ptr->name))
			continue;
		bit_set(feature_ptr->node_bitmap, node_inx);
		match = true;
		break;
	}
	list_iterator_destroy(feature_iter);

	if (!match) { /* Need to create new avail_feature_list record */
		feature_ptr = xmalloc(sizeof(node_feature_t));
		feature_ptr->magic = FEATURE_MAGIC;
		feature_ptr->name = xstrdup(feature);
		feature_ptr->node_bitmap = bit_alloc(node_record_count);
		bit_set(feature_ptr->node_bitmap, node_inx);
		list_append(feature_list, feature_ptr);
	}
}

/*
 * _list_delete_feature - delete an entry from the feature list,
 *	see list.h for documentation
 */
static void _list_delete_feature(void *feature_entry)
{
	node_feature_t *feature_ptr = feature_entry;

	xassert(feature_ptr);
	xassert(feature_ptr->magic == FEATURE_MAGIC);
	xfree(feature_ptr->name);
	FREE_NULL_BITMAP(feature_ptr->node_bitmap);
	xfree(feature_ptr);
}

/*
 * For a configuration where available_features == active_features,
 * build new active and available feature lists
 */
extern void node_features_build_list_eq(void)
{
	list_itr_t *config_iterator;
	config_record_t *config_ptr;
	node_feature_t *active_feature_ptr, *avail_feature_ptr;
	list_itr_t *feature_iter;
	char *tmp_str, *token, *last = NULL;

	node_features_free_lists();
	active_feature_list = list_create(_list_delete_feature);
	avail_feature_list = list_create(_list_delete_feature);

	config_iterator = list_iterator_create(config_list);
	while ((config_ptr = list_next(config_iterator))) {
		if (config_ptr->feature) {
			tmp_str = xstrdup(config_ptr->feature);
			token = strtok_r(tmp_str, ",", &last);
			while (token) {
				_add_config_feature(avail_feature_list, token,
						    config_ptr->node_bitmap);
				token = strtok_r(NULL, ",", &last);
			}
			xfree(tmp_str);
		}
	}
	list_iterator_destroy(config_iterator);

	/* Copy avail_feature_list to active_feature_list */
	feature_iter = list_iterator_create(avail_feature_list);
	while ((avail_feature_ptr = list_next(feature_iter))) {
		active_feature_ptr = xmalloc(sizeof(node_feature_t));
		active_feature_ptr->magic = FEATURE_MAGIC;
		active_feature_ptr->name = xstrdup(avail_feature_ptr->name);
		active_feature_ptr->node_bitmap =
			bit_copy(avail_feature_ptr->node_bitmap);
		list_append(active_feature_list, active_feature_ptr);
	}
	list_iterator_destroy(feature_iter);
}

/*
 * For a configuration where available_features != active_features,
 * build new active and available feature lists
 */
extern void node_features_build_list_ne(void)
{
	node_record_t *node_ptr;
	char *tmp_str, *token, *last = NULL;
	int i;

	node_features_free_lists();
	active_feature_list = list_create(_list_delete_feature);
	avail_feature_list = list_create(_list_delete_feature);

	for (i = 0; (node_ptr = next_node(&i)); i++) {
		if (node_ptr->features_act) {
			tmp_str = xstrdup(node_ptr->features_act);
			token = strtok_r(tmp_str, ",", &last);
			while (token) {
				_add_config_feature_inx(active_feature_list,
							token, node_ptr->index);
				token = strtok_r(NULL, ",", &last);
			}
			xfree(tmp_str);
		}
		if (node_ptr->features) {
			tmp_str = xstrdup(node_ptr->features);
			token = strtok_r(tmp_str, ",", &last);
			while (token) {
				_add_config_feature_inx(avail_feature_list,
							token, node_ptr->index);
				token = strtok_r(NULL, ",", &last);
			}
			xfree(tmp_str);
		}
	}
}

/*
 * Update active_feature_list or avail_feature_list
 * feature_list IN - list to update: active_feature_list or avail_feature_list
 * new_features IN - New active_features
 * node_bitmap IN - Nodes with the new active_features value
 */
extern void node_features_update_list(list_t *feature_list, char *new_features,
				      bitstr_t *node_bitmap)
{
	node_feature_t *feature_ptr;
	list_itr_t *feature_iter;
	char *tmp_str, *token, *last = NULL;

	/*
	 * Clear these nodes from the feature_list record,
	 * then restore as needed
	 */
	feature_iter = list_iterator_create(feature_list);
	while ((feature_ptr = list_next(feature_iter))) {
		bit_and_not(feature_ptr->node_bitmap, node_bitmap);
	}
	list_iterator_destroy(feature_iter);

	if (new_features) {
		tmp_str = xstrdup(new_features);
		token = strtok_r(tmp_str, ",", &last);
		while (token) {
			_add_config_feature(feature_list, token, node_bitmap);
			token = strtok_r(NULL, ",", &last);
		}
		xfree(tmp_str);
	}
	node_features_updated = true;
}

extern void node_features_build_active_list(job_record_t *job_ptr)
{
	node_record_t *node_ptr;
	char *tmp_str, *token, *saveptr = NULL;

	active_feature_list = list_create(_list_delete_feature);

	for (int i = 0; (node_ptr = next_node_bitmap(job_ptr->node_bitmap, &i));
	     i++) {
		if (node_ptr->features_act) {
			tmp_str = xstrdup(node_ptr->features_act);
			for (token = strtok_r(tmp_str, ",", &saveptr); token;
			     token = strtok_r(NULL, ",", &saveptr)) {
				_add_config_feature_inx(active_feature_list,
							token, node_ptr->index);
			}
			xfree(tmp_str);
		}
	}
}

extern void node_features_free_lists(void)
{
	FREE_NULL_LIST(active_feature_list);
	FREE_NULL_LIST(avail_feature_list);
}
