/*****************************************************************************\
 *  capmc_resume.c - Power up identified nodes with (optional) features.
 *  Once complete, modify the node's active features as needed.
 *
 *  Usage: "capmc_resume <hostlist> [features]"
 *****************************************************************************
 *  Copyright (C) 2016 SchedMD LLC.
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

int main(int argc, char *argv[])
{
	char *features, *save_ptr = NULL, *tok;
	update_node_msg_t node_msg;
	int rc =  SLURM_SUCCESS;

	if (argc == 3) {
		features = xstrdup(argv[2]);
		tok = strtok_r(features, ",", &save_ptr);
		while (tok) {
			printf("%s\n", tok);
			tok = strtok_r(NULL, ",", &save_ptr);
		}
		xfree(features);
	}

	if (argc == 3) {
		slurm_init_update_node_msg(&node_msg);
		node_msg.node_names = argv[1];
		node_msg.features_act = argv[2];
		rc = slurm_update_node(&node_msg);
	}

	if (rc == SLURM_SUCCESS) {
		exit(0);
	} else {
		fprintf(stderr, "slurm_update_node(\'%s\', \'%s\'): %s\n",
			argv[1], argv[2], slurm_strerror(slurm_get_errno()));
		exit(1);
	}
}
