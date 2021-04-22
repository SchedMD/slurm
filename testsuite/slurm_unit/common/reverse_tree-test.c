/*****************************************************************************\
 *  Copyright (C) 2020 SchedMD LLC.
 *  Written by Nathan Rini <nate@schedmd.com>
 *
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher J. Morrone <morrone2@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *  Portions copyright (C) 2014 Institute of Semiconductor Physics
 *                     Siberian Branch of Russian Academy of Science
 *  Written by Artem Polyakov <artpol84@gmail.com>.
 *  All rights reserved.
 *  Portions copyright (C) 2017 Mellanox Technologies.
 *  Written by Artem Polyakov <artpol84@gmail.com>.
 *  All rights reserved.
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <check.h>

#include "slurm/slurm_errno.h"
#include "src/common/log.h"
#include "src/common/reverse_tree.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/* define the external variable */
void *conf;

/*
 * Dumb brute force function
 */
static int _dumb_direct_children(int *children, int width, int id,
				 int max_node_id)
{
	int child;
	int count = 0;

	for (child = id + 1; child < max_node_id; child++) {
		int parent_id, child_num, depth, max_depth;

		reverse_tree_info(child, max_node_id, width, &parent_id,
				  &child_num, &depth, &max_depth);
		if (parent_id == id)
			children[count++] = child;
	}

	return count;
}

/* Bug 8196 makes the second case to fail on 20.02 */
static const int nodes_loop[] = {8192/*,  8192*/};
static const int width_loop[] = {   5/*, 65533*/};
START_TEST(verify_children)
{
	int nodes = nodes_loop[_i];
	int width = width_loop[_i];
	int parent, children, depth, maxdepth;
	int *children1 = xcalloc(sizeof(int), width);
	int *children2 = xcalloc(sizeof(int), width);

	for (int i = 0; i < nodes; i++) {
		int cnt1, cnt2;

		reverse_tree_info(i, nodes, width, &parent, &children, &depth,
				  &maxdepth);

		ck_assert_msg(children >= 0, "nchild: %d", children);

/* 		debug("%s: %d : parent: %d nchild: %d depth: %d, maxdepth: %d", */
/* 		      __func__, i, parent, children, depth, maxdepth); */
		cnt1 = _dumb_direct_children(children1, width, i, nodes);
		cnt2 = reverse_tree_direct_children(i, nodes, width, depth, children2);

		ck_assert_msg(cnt1 == cnt2,
			      "%s: Direct children sanity check error: cnt1 = %d, cnt2 = %d",
			      __func__, cnt1, cnt2);

		for (int j = 0; j < cnt1; j++) {
			ck_assert_msg(children1[j] == children2[j],
				      "Direct children: cnt1 = %d, cnt2 = %d: %d'th element: children1[%d] = %d, children2[%d] = %d",
				      cnt1, cnt2, j, j, children1[j], j,
				      children2[j]);
		}
	}

	xfree(children1);
	xfree(children2);
}
END_TEST

Suite *suite_reverse_tree(void)
{
	Suite *s = suite_create("reverse_tree");
	TCase *tc_core = tcase_create("reverse_tree");
	tcase_set_timeout(tc_core, 60); /* Avoid timeouts with --coverage */
	tcase_add_loop_test(tc_core, verify_children, 0, sizeof(nodes_loop) /
			    sizeof(int));
	suite_add_tcase(s, tc_core);
	return s;
}

int main(void)
{
	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	log_opts.stderr_level = LOG_LEVEL_DEBUG5;
	log_init("reverse_tree-test", log_opts, 0, NULL);

	int number_failed;
	SRunner *sr = srunner_create(suite_reverse_tree());
	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
