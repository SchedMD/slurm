/*****************************************************************************\
 *  info_assoc_mgr.c - Association Manager information from the
 *                     slurmctld functions for scontrol.
 *****************************************************************************
 *  Copyright (C) 2004 CSCS
 *  Copyright (C) 2015 SchedMD LLC
 *  Written by Stephen Trofinoff and Danny Auble
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

#include "scontrol.h"

static void _print_assoc_mgr_info(const char *name, assoc_mgr_info_msg_t *msg)
{
	ListIterator itr;
	slurmdb_user_rec_t *user_rec;
	slurmdb_assoc_rec_t *assoc_rec;
	slurmdb_qos_rec_t *qos_rec;
	uint64_t usage_mins;
	char *new_line_char = one_liner ? " " : "\n    ";
	int i;

	printf("Current Association Manager state\n");

	if (!msg->user_list || !list_count(msg->user_list)) {
		printf("\nNo users currently cached in Slurm.\n\n");
	} else {
		printf("\nUser Records\n\n");

		itr = list_iterator_create(msg->user_list);
		while ((user_rec = list_next(itr))) {
			printf("UserName=%s(%u) DefAccount=%s "
			       "DefWckey=%s AdminLevel=%s\n",
			       user_rec->name,
			       user_rec->uid,
			       user_rec->default_acct,
			       user_rec->default_wckey,
			       slurmdb_admin_level_str(user_rec->admin_level));
		}
		list_iterator_destroy(itr);
	}

	if (!msg->assoc_list || !list_count(msg->assoc_list)) {
		printf("\nNo associations currently cached in Slurm.\n\n");
	} else {
		printf("\nAssociation Records\n\n");

		itr = list_iterator_create(msg->assoc_list);
		while ((assoc_rec = list_next(itr))) {
			if (!assoc_rec->usage)
				continue;

			usage_mins =
				(uint64_t)(assoc_rec->usage->usage_raw / 60.0);

			printf("ClusterName=%s Account=%s ",
			       assoc_rec->cluster,
			       assoc_rec->acct);

			if (assoc_rec->user)
				printf("UserName=%s(%u) ",
				       assoc_rec->user,
				       assoc_rec->uid);
			else
				printf("UserName= ");

			printf("Partition=%s ID=%u%s",
			       assoc_rec->partition ? assoc_rec->partition : "",
			       assoc_rec->id,
			       new_line_char);

			printf("SharesRaw/Norm/Level/Factor="
			       "%u/%.2f/%u/%.2f%s",
			       assoc_rec->shares_raw,
			       assoc_rec->usage->shares_norm,
			       assoc_rec->usage->level_shares,
			       assoc_rec->usage->fs_factor,
			       new_line_char);

			printf("UsageRaw/Norm/Efctv=%.2Lf/%.2Lf/%.2Lf%s",
			       assoc_rec->usage->usage_raw,
			       assoc_rec->usage->usage_norm,
			       assoc_rec->usage->usage_efctv,
			       new_line_char);

			if (assoc_rec->parent_acct)
				printf("ParentAccount=%s(%u) ",
				       assoc_rec->parent_acct,
				       assoc_rec->parent_id);
			else
				printf("ParentAccount= ");

			printf("Lft-Rgt=%u-%u DefAssoc=%s%s",
			       assoc_rec->lft,
			       assoc_rec->rgt,
			       assoc_rec->is_def ? "Yes" : "No",
			       new_line_char);


			if (assoc_rec->grp_jobs != INFINITE)
				printf("GrpJobs=%u(%u) ",
				       assoc_rec->grp_jobs,
				       assoc_rec->usage->used_jobs);
			else
				printf("GrpJobs= ");
			/* NEW LINE */
			printf("%s", new_line_char);

			if (assoc_rec->grp_submit_jobs != INFINITE)
				printf("GrpSubmitJobs=%u(%u) ",
				       assoc_rec->grp_submit_jobs,
				       assoc_rec->usage->used_submit_jobs);
			else
				printf("GrpSubmitJobs= ");
			if (assoc_rec->grp_wall != INFINITE)
				printf("GrpWall=%u(%.2f)",
				       assoc_rec->grp_wall,
				       assoc_rec->usage->grp_used_wall);
			else
				printf("GrpWall=");
			/* NEW LINE */
			printf("%s", new_line_char);

			if (assoc_rec->grp_tres) {
				printf("GrpTRES=%s", assoc_rec->grp_tres);
				if (assoc_rec->usage->tres_cnt &&
				    assoc_rec->usage->grp_used_tres) {
					printf("(");
					for (i=0; i<assoc_rec->usage->tres_cnt;
					     i++)
						printf("%s%"PRIu64,
						       i ? "," : "",
						       assoc_rec->usage->
						       grp_used_tres[i]);
					printf(")");
				}
				printf("%s", new_line_char);
			} else
				printf("GrpTRES=%s", new_line_char);

			if (assoc_rec->grp_tres_mins)
				printf("GrpTRESMins=%s(%"PRIu64")%s",
				       assoc_rec->grp_tres_mins,
				       usage_mins,
				       new_line_char);
			else
				printf("GrpTRESMins=%s", new_line_char);

			if (assoc_rec->grp_tres_mins) {
				printf("GrpTRESRunMins=%s",
				       assoc_rec->grp_tres_run_mins);
				if (assoc_rec->usage->tres_cnt &&
				    assoc_rec->usage->grp_used_tres_run_secs) {
					printf("(");
					for (i=0; i<assoc_rec->usage->tres_cnt;
					     i++)
						printf("%s%"PRIu64,
						       i ? "," : "",
						       assoc_rec->usage->
						       grp_used_tres_run_secs[i]
						       / 60);
					printf(")");
				}
				printf("%s", new_line_char);
			} else
				printf("GrpTRESRunMins=%s", new_line_char);

			if (assoc_rec->max_jobs != INFINITE)
				printf("MaxJobs=%u(%u) ",
				       assoc_rec->max_jobs,
				       assoc_rec->usage->used_jobs);
			else
				printf("MaxJobs= ");

			if (assoc_rec->max_submit_jobs != INFINITE)
				printf("MaxSubmitJobs=%u(%u) ",
				       assoc_rec->max_submit_jobs,
				       assoc_rec->usage->used_submit_jobs);
			else
				printf("MaxSubmitJobs= ");

			if (assoc_rec->max_wall_pj != INFINITE)
				printf("MaxWallPJ=%u",
				       assoc_rec->max_wall_pj);
			else
				printf("MaxWallPJ=");

			/* NEW LINE */
			printf("%s", new_line_char);

			if (assoc_rec->max_tres_pj)
				printf("MaxTRESPJ=%s%s",
				       assoc_rec->max_tres_pj,
				       new_line_char);
			else
				printf("MaxTRESPJ=%s", new_line_char);

			if (assoc_rec->max_tres_mins_pj)
				printf("MaxTRESMinsPJ=%s\n",
				       assoc_rec->max_tres_mins_pj);
			else
				printf("MaxTRESMinsPJ=\n");
		}
	}

	if (!msg->qos_list || !list_count(msg->qos_list)) {
		printf("\nNo QOS currently cached in Slurm.\n\n");
	} else {

		printf("\nQOS Records\n\n");

		itr = list_iterator_create(msg->qos_list);
		while ((qos_rec = list_next(itr))) {
			if (!qos_rec->usage)
				continue;

			usage_mins =
				(uint64_t)(qos_rec->usage->usage_raw / 60.0);

			printf("QOS=%s(%u)%s", qos_rec->name, qos_rec->id,
				new_line_char);

			printf("UsageRaw=%Lf%s",
			       qos_rec->usage->usage_raw,
			       new_line_char);

			if (qos_rec->grp_jobs != INFINITE)
				printf("GrpJobs=%u(%u) ",
				       qos_rec->grp_jobs,
				       qos_rec->usage->grp_used_jobs);
			else
				printf("GrpJobs= ");
			if (qos_rec->grp_submit_jobs != INFINITE)
				printf("GrpSubmitJobs=%u(%u) ",
				       qos_rec->grp_submit_jobs,
				       qos_rec->usage->grp_used_submit_jobs);
			else
				printf("GrpSubmitJobs= ");
			if (qos_rec->grp_wall != INFINITE)
				printf("GrpWall=%u(%.2f)",
				       qos_rec->grp_wall,
				       qos_rec->usage->grp_used_wall);
			else
				printf("GrpWall=");
			/* NEW LINE */
			printf("%s", new_line_char);

			if (qos_rec->grp_tres) {
				printf("GrpTRES=%s", qos_rec->grp_tres);
				if (qos_rec->usage->tres_cnt &&
				    qos_rec->usage->grp_used_tres) {
					printf("(");
					for (i=0; i<qos_rec->usage->tres_cnt;
					     i++)
						printf("%s%"PRIu64,
						       i ? "," : "",
						       qos_rec->usage->
						       grp_used_tres[i]);
					printf(")");
				}
				printf("%s", new_line_char);
			} else
				printf("GrpTRES=%s", new_line_char);

			if (qos_rec->grp_tres_mins)
				printf("GrpTRESMins=%s(%"PRIu64")%s",
				       qos_rec->grp_tres_mins,
				       usage_mins,
				       new_line_char);
			else
				printf("GrpTRESMins=%s", new_line_char);

			if (qos_rec->grp_tres_mins) {
				printf("GrpTRESRunMins=%s",
				       qos_rec->grp_tres_run_mins);
				if (qos_rec->usage->tres_cnt &&
				    qos_rec->usage->grp_used_tres_run_secs) {
					printf("(");
					for (i=0; i<qos_rec->usage->tres_cnt;
					     i++)
						printf("%s%"PRIu64,
						       i ? "," : "",
						       qos_rec->usage->
						       grp_used_tres_run_secs[i]
						       / 60);
					printf(")");
				}
				printf("%s", new_line_char);
			} else
				printf("GrpTRESRunMins=%s", new_line_char);

			if (qos_rec->max_jobs_pu != INFINITE)
				printf("MaxJobsPU=%u(%u) ",
				       qos_rec->max_jobs_pu,
				       qos_rec->usage->grp_used_jobs);
			else
				printf("MaxJobs= ");

			if (qos_rec->max_submit_jobs_pu != INFINITE)
				printf("MaxSubmitJobs=%u(%u) ",
				       qos_rec->max_submit_jobs_pu,
				       qos_rec->usage->grp_used_submit_jobs);
			else
				printf("MaxSubmitJobs= ");

			if (qos_rec->max_wall_pj != INFINITE)
				printf("MaxWallPJ=%u",
				       qos_rec->max_wall_pj);
			else
				printf("MaxWallPJ=");

			/* NEW LINE */
			printf("%s", new_line_char);

			if (qos_rec->max_tres_pj)
				printf("MaxTRESPJ=%s%s",
				       qos_rec->max_tres_pj,
				       new_line_char);
			else
				printf("MaxTRESPJ=%s", new_line_char);

			if (qos_rec->max_tres_pu)
				printf("MaxTRESPU=%s%s",
				       qos_rec->max_tres_pu,
				       new_line_char);
			else
				printf("MaxTRESPU=%s", new_line_char);

			if (qos_rec->max_tres_mins_pj)
				printf("MaxTRESMinsPJ=%s%s",
				       qos_rec->max_tres_mins_pj,
					new_line_char);
			else
				printf("MaxTRESMinsPJ=%s", new_line_char);

			if (qos_rec->min_tres_pj)
				printf("MinTRESPJ=%s\n", qos_rec->min_tres_pj);
			else
				printf("MinTRESPJ=\n");
		}
	}
}

/* scontrol_print_assoc_mgr_info()
 *
 * Retrieve and display the association manager information
 * from the controller
 *
 */

extern void scontrol_print_assoc_mgr_info(const char *name)
{
	int cc;
	assoc_mgr_info_request_msg_t req;
	assoc_mgr_info_msg_t *msg = NULL;

	/* FIXME: add more filtering in the future */
	memset(&req, 0, sizeof(assoc_mgr_info_request_msg_t));
	req.flags = ASSOC_MGR_INFO_FLAG_ASSOC | ASSOC_MGR_INFO_FLAG_USERS |
		ASSOC_MGR_INFO_FLAG_QOS;
	if (name) {
		req.user_list = list_create(NULL);
		list_append(req.user_list, (char *)name);
	}
	/* call the controller to get the meat */
	cc = slurm_load_assoc_mgr_info(&req, &msg);

	FREE_NULL_LIST(req.user_list);

	if (cc != SLURM_PROTOCOL_SUCCESS) {
		/* Hosed, crap out. */
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror("slurm_load_assoc_mgr_info error");
		return;
	}

	/* print the info
	 */
	_print_assoc_mgr_info(name, msg);

	/* free at last
	 */
	slurm_free_assoc_mgr_info_msg(msg);

	return;
}

