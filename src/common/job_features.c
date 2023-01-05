/*****************************************************************************\
 *  job_features.c
 *****************************************************************************
 *  Copyright (C) 2023 SchedMD LLC.
 *  Written by Marshall Garey <marshall@schedmd.com>
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

#include "src/common/job_features.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

typedef struct {
	bool debug_flag;
	list_t *distribute_lists;
	list_t *feature_set;
	list_t *new_feature_sets;
} distribute_arg_t;

typedef struct {
	bool debug_flag;
	int last_paren_cnt;
	int last_op;
	int last_paren_op;
	list_t *paren_lists;
	list_t *feature_sets;
	list_t *tmp_feature_list;
	list_t *working_list;
} evalute_feature_arg_t;

typedef struct {
	bool first;
	char *pos;
	char *str;
} job_feature2str_arg_t;

static int _cmp_job_feature(void *x, void *key)
{
	job_feature_t *f1 = x;
	job_feature_t *f2 = key;
	return !xstrcmp(f1->name, f2->name);
}

static int _foreach_job_feature2str(void *x, void *arg)
{
	job_feature_t *job_feat_ptr = x;
	job_feature2str_arg_t *args = arg;

	if (args->first) {
		xstrfmtcatat(args->str, &args->pos, "%s", job_feat_ptr->name);
		args->first = false;
	} else {
		xstrfmtcatat(args->str, &args->pos, ",%s", job_feat_ptr->name);
	}

	return 0;
}

static int _copy_job_feature_ptr_unique(void *x, void *arg)
{
	job_feature_t *job_feat_ptr = x;
	list_t *feature_set = arg;

	if (!list_find_first_ro(feature_set, _cmp_job_feature, job_feat_ptr))
		list_append(feature_set, job_feat_ptr);

	return 0;
}

/*
 * Merge unique items in feature_set and distribute_set into a new list.
 * Append the new list to new_feature_sets.
 */
static int _distribute_one_list(void *x, void *arg)
{
	list_t *distribute_set = x;
	distribute_arg_t *dist_args = arg;
	list_t *new_feature_set;

	/*
	 * NOTE: list_shallow_copy just copies pointers and does not copy the
	 * destructor function. This is okay because the destructor function is
	 * already NULL - these are just pointers to job_feature_t, not full
	 * copies.
	 */
	new_feature_set = list_shallow_copy(dist_args->feature_set);
	list_for_each(distribute_set, _copy_job_feature_ptr_unique,
		      new_feature_set);

	list_append(dist_args->new_feature_sets, new_feature_set);

	if (dist_args->debug_flag) {
		char *dist_str = NULL;
		char *old_str = NULL;
		char *new_str = NULL;

		job_features_set2str(dist_args->feature_set, &old_str);
		job_features_set2str(distribute_set, &dist_str);
		job_features_set2str(new_feature_set, &new_str);

		log_flag(NODE_FEATURES,
			 "%s: Copy %s to %s: result list=%s",
			 __func__, dist_str, old_str, new_str);
		xfree(dist_str);
		xfree(old_str);
		xfree(new_str);
	}

	return 0;
}

static int _foreach_distribute_lists(void *x, void *arg)
{
	list_t *possible_list = x;
	distribute_arg_t *distribute_args = arg;
	distribute_arg_t distribute_args2 = {
		.debug_flag = distribute_args->debug_flag,
		.feature_set = possible_list,
		.new_feature_sets = distribute_args->new_feature_sets,
	};

	list_for_each(distribute_args->distribute_lists,
		      _distribute_one_list,
		      &distribute_args2);

	return 0;
}

/*
 *
 * Distribute each list in distribute_lists to each list in feature_sets (like a
 * multiply). If feature_sets is empty, just transfer distribute_lists into
 * feature_sets.
 *
 * Example:
 *
 * Job feature string: "a&(b|c)"
 *
 * When we get to the closing paren:
 *
 * feature_sets = {[a]}
 * distribute_lists = {[b],[c]}
 *
 * After _distribute_lists, we want feature_sets to be:
 *
 * {[a,b],[a,c]}
 *
 *
 * Another example:
 *
 * job feature string: "(a|b)&(c|d)
 *
 * When we get to the first closing paren "b)":
 *
 * feature_sets = {}
 * distribute_lists = {[a],[b]}
 *
 * Just transfer distribute_lists into feature_sets:
 *
 * feature_sets = {[a],[b]}
 * distribute_lists = {}
 *
 * When we get to the second closing paren "d)":
 *
 * feature_sets = {[a],[b]}
 * distribute_lists = {[c],[d]}
 *
 * Copy [c] and [d] to each of [a] and [b]:
 *
 * feature_sets = {[a,c],[a,d],[b,c],[b,d]}
 */
static void _distribute_lists(list_t **feature_sets,
			      list_t *distribute_lists,
			      bool debug_flag)
{
	list_t *new_feature_sets;

	xassert(feature_sets);
	xassert(*feature_sets);

	/*
	 * Build a new list which will have the distributed features.
	 * Iterate over the original list.
	 */
	new_feature_sets = list_create((ListDelF) list_destroy);
	if (list_is_empty(*feature_sets)) {
		list_transfer(new_feature_sets, distribute_lists);
	} else {
		distribute_arg_t distribute_args = {
			.debug_flag = debug_flag,
			.distribute_lists = distribute_lists,
			.new_feature_sets = new_feature_sets,
		};

		if (debug_flag) {
			char *feature_sets_str = NULL;
			char *paren_lists_str = NULL;

			list_for_each(*feature_sets, job_features_set2str,
				      &feature_sets_str);
			list_for_each(distribute_lists, job_features_set2str,
				      &paren_lists_str);
			log_flag(NODE_FEATURES, "%s: Distribute %s to %s",
				 __func__, feature_sets_str, paren_lists_str);

			xfree(feature_sets_str);
			xfree(paren_lists_str);
		}

		list_for_each(*feature_sets, _foreach_distribute_lists,
			      &distribute_args);
	}

	FREE_NULL_LIST(*feature_sets);
	*feature_sets = new_feature_sets;
}

static int _evaluate_job_feature(void *x, void *arg)
{
	job_feature_t *job_feat_ptr = x;
	evalute_feature_arg_t *args = arg;

	if (args->last_paren_cnt < job_feat_ptr->paren) {
		/*
		 * Start of expression in parentheses.
		 * Create a list of possible features for the expression
		 * in the parentheses.
		 */
		args->last_paren_op = args->last_op;
		args->last_op = FEATURE_OP_AND;
		args->paren_lists = list_create((ListDelF) list_destroy);
		args->working_list = args->paren_lists;
		args->tmp_feature_list = NULL;
	}

	/* Always do matching OR logic */
	if ((args->last_op == FEATURE_OP_OR) ||
	    (args->last_op == FEATURE_OP_MOR)) {
		/* New list */
		/* Each list-node is a pointer - nothing to free */
		args->tmp_feature_list = list_create(NULL);
		list_append(args->tmp_feature_list, job_feat_ptr);
		list_append(args->working_list, args->tmp_feature_list);
	} else { /* FEATURE_OP_AND, other ops not supported */
		/*
		 * - If we're in a paren, append to current feature list.
		 * - Otherwise, distribute to all possible lists.
		 */
		if (args->paren_lists) {
			if (!args->tmp_feature_list) {
				args->tmp_feature_list = list_create(NULL);
				list_append(args->paren_lists,
					    args->tmp_feature_list);
			}
			if (!list_find_first_ro(args->tmp_feature_list,
						_cmp_job_feature,
						job_feat_ptr)) {
				list_append(args->tmp_feature_list,
					    job_feat_ptr);
			}
		} else {
			list_t *tmp = list_create((ListDelF) list_destroy);
			list_t *features = list_create(NULL);

			list_append(features, job_feat_ptr);
			list_append(tmp, features);
			_distribute_lists(&args->feature_sets, tmp,
					  args->debug_flag);
			/* Update working_list to the new list */
			args->working_list = args->feature_sets;

			FREE_NULL_LIST(tmp);
		}
	}

	if (args->last_paren_cnt > job_feat_ptr->paren) {
		/*
		 * End of expression in parenthesis
		 * OR: Transfer paren_lists to feature_sets.
		 */
		if ((args->last_paren_op == FEATURE_OP_OR) ||
		    (args->last_paren_op == FEATURE_OP_MOR)) {
			list_transfer(args->feature_sets,
				      args->paren_lists);
		} else { /* FEATURE_OP_AND, other ops not supported */
			_distribute_lists(&args->feature_sets,
					  args->paren_lists,
					  args->debug_flag);
		}
		FREE_NULL_LIST(args->paren_lists);
		args->tmp_feature_list = NULL;
		/* Update working_list to the new list */
		args->working_list = args->feature_sets;
	}

	if (args->debug_flag) {
		char *feature_sets_str = NULL, *paren_lists_str = NULL;

		if (args->feature_sets)
			list_for_each(args->feature_sets,
				      job_features_set2str,
				      &feature_sets_str);
		if (args->paren_lists)
			list_for_each(args->paren_lists,
				      job_features_set2str,
				      &paren_lists_str);
		log_flag(NODE_FEATURES, "%s: After evaluating feature %s: feature sets: %s; paren lists: %s",
			 __func__, job_feat_ptr->name, feature_sets_str,
			 paren_lists_str);

		xfree(feature_sets_str);
		xfree(paren_lists_str);
	}

	args->last_op = job_feat_ptr->op_code;
	args->last_paren_cnt = job_feat_ptr->paren;

	return 0;
}

extern list_t *job_features_list2feature_sets(char *job_features,
					      list_t *job_feature_list,
					      bool suppress_log_flag)
{
	evalute_feature_arg_t feature_sets_arg = {
		.last_paren_cnt = 0,
		.last_op = FEATURE_OP_AND,
		.last_paren_op = FEATURE_OP_AND,
		.paren_lists = NULL,
		.feature_sets = NULL,
		.tmp_feature_list = NULL,
		.working_list = NULL,
	};

	feature_sets_arg.debug_flag =
		suppress_log_flag ? false :
		(slurm_conf.debug_flags & DEBUG_FLAG_NODE_FEATURES);

	feature_sets_arg.feature_sets = list_create((ListDelF) list_destroy);
	feature_sets_arg.working_list = feature_sets_arg.feature_sets;

	if (feature_sets_arg.debug_flag)
		log_flag(NODE_FEATURES, "%s: Convert %s to a matching OR expression",
			 __func__, job_features);
	list_for_each(job_feature_list, _evaluate_job_feature,
		      &feature_sets_arg);

	FREE_NULL_LIST(feature_sets_arg.paren_lists);

	return feature_sets_arg.feature_sets;
}

extern int job_features_set2str(void *x, void *arg)
{
	list_t *feature_list = x;
	char **str_ptr = arg;
	job_feature2str_arg_t feature2str_args = {
		.first = true,
		.pos = NULL,
		.str = *str_ptr,
	};

	/*
	 * If a list is already in *str_ptr, then separate this new list
	 * with a bar |.
	 */
	if (xstrchr(feature2str_args.str, ')'))
		xstrfmtcatat(feature2str_args.str, &feature2str_args.pos, "|(");
	else
		xstrfmtcatat(feature2str_args.str, &feature2str_args.pos, "(");
	list_for_each(feature_list, _foreach_job_feature2str,
		      &feature2str_args);
	xstrfmtcatat(feature2str_args.str, &feature2str_args.pos, ")");

	*str_ptr = feature2str_args.str;

	return 0;
}
