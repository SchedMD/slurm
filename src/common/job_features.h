/*****************************************************************************\
 *  job_features.h
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

#ifndef _JOB_FEATURES_H
#define _JOB_FEATURES_H

#include <inttypes.h>
#include <stdbool.h>

#include "src/common/bitstring.h"

#include "src/common/list.h"

#define FEATURE_OP_OR   0
#define FEATURE_OP_AND  1
#define FEATURE_OP_MOR  2		/* "matching" OR - all nodes in the job
					 * must have the same set of features */
#define FEATURE_OP_XAND 3		/* Used for counts of how many nodes
					 * in the allocation must have a
					 * particular set of features */
#define FEATURE_OP_END  4		/* last entry lacks separator */

typedef struct {
	char *name;			/* name of feature */
	uint16_t bracket;		/* count of enclosing brackets. */
	bool changeable;		/* return value of
					 * node_features_g_changeable_feature */
	uint16_t count;			/* count of nodes with this feature */
	uint8_t op_code;		/* separator, see FEATURE_OP_ above */
	bitstr_t *node_bitmap_active;	/* nodes with this feature active */
	bitstr_t *node_bitmap_avail;	/* nodes with this feature available */
	uint16_t paren;			/* count of enclosing parenthesis */
} job_feature_t;

/*
 * This function handles FEATURE_OP_MOR in job_feature_list.
 * This reads the job_feature_list (made from build_feature_list) and returns
 * a list of lists of job_feature_t. Each feature list is a set of features
 * that could be valid for the job. This is used for job feature expressions
 * that contain at least one changeable node feature where every bar ('|')
 * character is treated as FEATURE_OP_MOR (including '|' inside of
 * parentheses), not FEATURE_OP_OR. This is done because it does not make sense
 * to allow a mix of features in a job allocation. For example, if a job
 * requests:
 *
 * salloc -C 'a|b' -N2
 *
 * For static features, you could get one node with feature 'a' and one node
 * with feature 'b'. For changeable features, we want all nodes to have
 * feature 'a' or all nodes to have feature 'b' (some nodes could have both
 * features); we do not want a mix of feature sets the allocation. Given this
 * feature request, this function returns the following list of lists:
 *
 * {[a],[b]}
 *
 * Here is a more complicated example:
 *
 * job_features == "(a|b)&(c|d)"
 *
 * This function returns the following list of lists:
 *
 * {[a,c],[a,d],[b,c],[b,d]}
 *
 * Each feature (i.e. 'a', 'b', 'c', 'd') is of type job_feature_t.
 *
 * IN job_features - feature string requested by the user
 * IN job_feature_list - list created by build_feature_list
 * IN suppress_log_flag - if true, do not call log_flag
 * RETURN - A list of lists, with destructor function (ListDelF) list_destroy.
 *          Each inner list is a list of pointers to job_feature_t, not full
 *          copies, so this list is only valid as long as job_feature_list is
 *          unchanged. The caller must free the return value with
 *          FREE_NULL_LIST().
 */
extern list_t *job_features_list2feature_sets(char *job_features,
					      list_t *job_feature_list,
					      bool suppress_log_flag);

/*
 * IN x - list_t job_feature_list
 * OUT arg - char **str_ptr
 *
 * The string is the feature names separated by commas enclosed in parentheses.
 * If this is called successively with the same str_ptr, then the new string
 * will be appended to *str_ptr and separated by a '|' character.
 * Since this is just comma-separated feature names, this does not accurately
 * represent the feature request unless the feature list was made from
 * job_features_list2feature_sets().
 *
 * *str_ptr is set to the result. The result must be xfree'd.
 */
extern int job_features_set2str(void *x, void *arg);

#endif
