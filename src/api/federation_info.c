/*****************************************************************************\
 *  federation_info.c - functions dealing with Federations in the controller.
 *****************************************************************************
 *  Copyright (C) 2016 SchedMD LLC.
 *  Written by Brian Christiansen <brian@schedmd.com>
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

#include "slurm/slurm.h"
#include "slurm/slurmdb.h"
#include "slurm/slurm_errno.h"

#include "src/common/slurm_protocol_api.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"

/*
 * slurm_load_federation - issue RPC to get federation status from controller
 * IN/OUT fed_pptr - place to store returned federation information.
 * 		     slurmdb_federation_rec_t treated as a void pointer to since
 * 		     slurm.h doesn't have ties to slurmdb.h.
 * RET 0 or -1 on error
 */
extern int slurm_load_federation(void **fed_pptr)
{
	int rc;
	slurm_msg_t resp_msg;
	slurm_msg_t req_msg;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	req_msg.msg_type = REQUEST_FED_INFO;
	req_msg.data     = NULL;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_FED_INFO:
		*fed_pptr = resp_msg.data;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		if (rc)
			slurm_seterrno_ret(rc);
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_PROTOCOL_SUCCESS;
}

int _sort_clusters_by_name(void *x, void *y)
{
	slurmdb_cluster_rec_t *c1 = *(slurmdb_cluster_rec_t **)x;
	slurmdb_cluster_rec_t *c2 = *(slurmdb_cluster_rec_t **)y;

	return (xstrcmp(c1->name, c2->name));
}

/*
 * slurm_print_federation - prints slurmdb_federation_rec_t (passed as void*
 * 			    since slurm.h doesn't know about slurmdb.h).
 */
extern void slurm_print_federation(void *ptr)
{
	ListIterator itr;
	slurmdb_cluster_rec_t *cluster;
	int left_col_size;
	char *cluster_name = NULL;
	char *fed_flag_str = NULL;

	slurmdb_federation_rec_t *fed = (slurmdb_federation_rec_t *)ptr;

	xassert(fed);

	if (!fed->name)
		return;

	fed_flag_str = slurmdb_federation_flags_str(fed->flags);
	if (working_cluster_rec)
		cluster_name = xstrdup(working_cluster_rec->name);
	else
		cluster_name = slurm_get_cluster_name();

	left_col_size = strlen("federation:");
	printf("%-*s %s Flags:%s\n", left_col_size, "Federation:",
	       fed->name, (fed_flag_str) ? fed_flag_str : "None");
	list_sort(fed->cluster_list, (ListCmpF)_sort_clusters_by_name);
	itr = list_iterator_create(fed->cluster_list);

	/* Display local Cluster*/
	while ((cluster = list_next(itr))) {
		if (!xstrcmp(cluster->name, cluster_name)) {
			char *tmp_str =
				slurmdb_cluster_fed_states_str(
						cluster->fed.state);
			printf("%-*s %s:%s:%d ID:%d FedState:%s Weight:%d\n",
			       left_col_size, "Self:", cluster->name,
			       cluster->control_host, cluster->control_port,
			       cluster->fed.id, (tmp_str ? tmp_str : ""),
			       cluster->fed.weight);
		}
	}

	/* Display siblings */
	list_iterator_reset(itr);
	while ((cluster = list_next(itr))) {
		char *tmp_str = NULL;

		if (!xstrcmp(cluster->name, cluster_name))
			continue;

		tmp_str = slurmdb_cluster_fed_states_str(cluster->fed.state);
		printf("%-*s %s:%s:%d ID:%d FedState:%s Weight:%d "
		       "PersistConnSend/Recv:%s/%s\n",
		       left_col_size, "Sibling:", cluster->name,
		       cluster->control_host, cluster->control_port,
		       cluster->fed.id, (tmp_str ? tmp_str : ""),
		       cluster->fed.weight,
		       cluster->fed.send ? "Yes" : "No",
		       cluster->fed.recv ? "Yes" : "No");
	}

	list_iterator_destroy(itr);
	xfree(cluster_name);
	xfree(fed_flag_str);
}
