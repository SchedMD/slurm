/*****************************************************************************\
 *  info_assoc_mgr.c - Association Manager information from the
 *                     slurmctld functions for scontrol.
 *****************************************************************************
 *  Copyright (C) 2004 CSCS
 *  Copyright (C) 2015 SchedMD LLC
 *  Written by Stephen Trofinoff and Danny Auble
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

#include "scontrol.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"

static uint32_t tres_cnt = 0;
static char **tres_names = NULL;
static uint32_t req_flags = 0;

static void _print_tres_line(const char *name, uint64_t *limits, uint64_t *used,
			     uint64_t divider)
{
	int i;
	bool comma = 0;

	xassert(tres_cnt);
	xassert(tres_names);

	printf("%s=", name);
	if (!limits)
		return;

	for (i=0; i<tres_cnt; i++) {
		/* only print things that have limits or usage */
		if (!used && (limits[i] == INFINITE64))
			continue;

		printf("%s%s=", comma ? "," : "", tres_names[i]);
		if (limits[i] == INFINITE64)
			printf("N");
		else
			printf("%"PRIu64, limits[i]);

		if (used) {
			uint64_t total_used = used[i];

			if (divider)
				total_used /= divider;

			printf("(%"PRIu64")", total_used);
		}

		comma = 1;
	}
}

static int _print_used_acct_limit(slurmdb_used_limits_t *used_limit,
				  slurmdb_qos_rec_t *qos_rec)
{
	char *new_line_char = one_liner ? " " : "\n        ";

	printf("%s%s%s",
	       one_liner ? " " : "\n      ",
	       used_limit->acct,
	       one_liner ? "={" : new_line_char);

	printf("MaxJobsPA=");
	if (qos_rec->max_jobs_pa != INFINITE)
		printf("%u", qos_rec->max_jobs_pa);
	else
		printf("N");
	printf("(%u) ", used_limit->jobs);

	printf("MaxJobsAccruePA=");
	if (qos_rec->max_jobs_accrue_pa != INFINITE)
		printf("%u", qos_rec->max_jobs_accrue_pa);
	else
		printf("N");
	printf("(%u) ", used_limit->accrue_cnt);

	printf("MaxSubmitJobsPA=");
	if (qos_rec->max_submit_jobs_pa != INFINITE)
		printf("%u", qos_rec->max_submit_jobs_pa);
	else
		printf("N");
	printf("(%u)%s", used_limit->submit_jobs, new_line_char);

	_print_tres_line("MaxTRESPA",
			 qos_rec->max_tres_pa_ctld,
			 used_limit->tres, 0);

	if (one_liner)
		printf("}");

	/* MaxTRESRunMinsPA doesn't do anything yet, if/when it does
	 * change the last param in the print_tres_line to 0. */

	/* printf("%s", one_liner ? "" : "    "); */
	/* _print_tres_line("MaxTRESRunMinsPA", */
	/* 		 qos_rec->max_tres_run_mins_pa_ctld, */
	/* 		 used_limit->tres_run_mins, 60, 1); */


	return SLURM_SUCCESS;
}

static int _print_used_user_limit(slurmdb_used_limits_t *used_limit,
				  slurmdb_qos_rec_t *qos_rec)
{
	char *new_line_char = one_liner ? " " : "\n        ";
	char *user_name = uid_to_string(used_limit->uid);

	printf("%s%s(%d)%s",
	       one_liner ? " " : "\n      ",
	       user_name,
	       used_limit->uid,
	       one_liner ? "={" : new_line_char);
	xfree(user_name);

	printf("MaxJobsPU=");
	if (qos_rec->max_jobs_pu != INFINITE)
		printf("%u", qos_rec->max_jobs_pu);
	else
		printf("N");
	printf("(%u) ", used_limit->jobs);

	printf("MaxJobsAccruePU=");
	if (qos_rec->max_jobs_accrue_pu != INFINITE)
		printf("%u", qos_rec->max_jobs_accrue_pu);
	else
		printf("N");
	printf("(%u) ", used_limit->accrue_cnt);

	printf("MaxSubmitJobsPU=");
	if (qos_rec->max_submit_jobs_pu != INFINITE)
		printf("%u", qos_rec->max_submit_jobs_pu);
	else
		printf("N");
	printf("(%u)%s", used_limit->submit_jobs, new_line_char);

	_print_tres_line("MaxTRESPU",
			 qos_rec->max_tres_pu_ctld,
			 used_limit->tres, 0);

	if (one_liner)
		printf("}");

	/* MaxTRESRunMinsPU doesn't do anything yet, if/when it does
	 * change the last param in the print_tres_line to 0. */

	/* printf("%s", one_liner ? "" : "    "); */
	/* _print_tres_line("MaxTRESRunMinsPU", */
	/* 		 qos_rec->max_tres_run_mins_pu_ctld, */
	/* 		 used_limit->tres_run_mins, 60, 1); */

	return SLURM_SUCCESS;
}

static void _print_assoc_mgr_info(assoc_mgr_info_msg_t *msg)
{
	ListIterator itr;
	slurmdb_user_rec_t *user_rec;
	slurmdb_assoc_rec_t *assoc_rec;
	slurmdb_qos_rec_t *qos_rec;
	uint64_t tmp64_array[msg->tres_cnt];
	char *new_line_char = one_liner ? " " : "\n    ";
	int i;

	printf("Current Association Manager state\n");

	tres_cnt = msg->tres_cnt;
	tres_names = msg->tres_names;

	if (!msg->user_list || !list_count(msg->user_list)) {
		if (req_flags & ASSOC_MGR_INFO_FLAG_USERS)
			printf("%sNo users currently cached in Slurm.%s\n",
			       one_liner ? "" : "\n", one_liner ? "" : "\n");
	} else {
		printf("%sUser Records%s\n",
		       one_liner ? "" : "\n", one_liner ? "" : "\n");

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
		if (req_flags & ASSOC_MGR_INFO_FLAG_ASSOC)
			printf("%sNo associations currently "
			       "cached in Slurm.%s\n",
			       one_liner ? "" : "\n", one_liner ? "" : "\n");
	} else {
		printf("%sAssociation Records%s\n",
		       one_liner ? "" : "\n", one_liner ? "" : "\n");

		itr = list_iterator_create(msg->assoc_list);
		while ((assoc_rec = list_next(itr))) {
			if (!assoc_rec->usage)
				continue;

			printf("ClusterName=%s Account=%s ",
			       assoc_rec->cluster,
			       assoc_rec->acct);

			if (assoc_rec->user)
				printf("UserName=%s(%u) ",
				       assoc_rec->user,
				       assoc_rec->uid);
			else
				printf("UserName= ");

			printf("Partition=%s Priority=%u ID=%u%s",
			       assoc_rec->partition ? assoc_rec->partition : "",
			       assoc_rec->priority, assoc_rec->id,
			       new_line_char);

			printf("SharesRaw/Norm/Level/Factor="
			       "%u/%.2f/%u/%.2f%s",
			       assoc_rec->shares_raw,
			       assoc_rec->usage->shares_norm,
			       (assoc_rec->usage->level_shares == NO_VAL) ?
			       1 : assoc_rec->usage->level_shares,
			       assoc_rec->usage->fs_factor,
			       new_line_char);

			printf("UsageRaw/Norm/Efctv=%.2Lf/%.2Lf/%.2Lf%s",
			       assoc_rec->usage->usage_raw,
			       (assoc_rec->usage->usage_norm ==
				(long double)NO_VAL) ?
			       1 : assoc_rec->usage->usage_norm,
			       (assoc_rec->usage->usage_efctv ==
				(long double)NO_VAL) ?
			       1 : assoc_rec->usage->usage_efctv,
			       new_line_char);

			if (assoc_rec->parent_acct)
				printf("ParentAccount=%s(%u) ",
				       assoc_rec->parent_acct,
				       assoc_rec->parent_id);
			else
				printf("ParentAccount= ");

			/* rgt isn't always valid coming from the
			 * association manager (so don't print it).
			 */
			printf("Lft=%u DefAssoc=%s%s",
			       assoc_rec->lft,
			       assoc_rec->is_def ? "Yes" : "No",
			       new_line_char);


			if (assoc_rec->grp_jobs != INFINITE)
				printf("GrpJobs=%u(%u) ",
				       assoc_rec->grp_jobs,
				       assoc_rec->usage->used_jobs);
			else
				printf("GrpJobs=N(%u) ",
				       assoc_rec->usage->used_jobs);
			if (assoc_rec->grp_jobs_accrue != INFINITE)
				printf("GrpJobsAccrue=%u(%u)",
				       assoc_rec->grp_jobs_accrue,
				       assoc_rec->usage->accrue_cnt);
			else
				printf("GrpJobsAccrue=N(%u)",
				       assoc_rec->usage->accrue_cnt);
			/* NEW LINE */
			printf("%s", new_line_char);

			if (assoc_rec->grp_submit_jobs != INFINITE)
				printf("GrpSubmitJobs=%u(%u) ",
				       assoc_rec->grp_submit_jobs,
				       assoc_rec->usage->used_submit_jobs);
			else
				printf("GrpSubmitJobs=N(%u) ",
				       assoc_rec->usage->used_submit_jobs);
			if (assoc_rec->grp_wall != INFINITE)
				printf("GrpWall=%u(%.2f)",
				       assoc_rec->grp_wall,
				       assoc_rec->usage->grp_used_wall/60);
			else
				printf("GrpWall=N(%.2f)",
				       assoc_rec->usage->grp_used_wall/60);
			/* NEW LINE */
			printf("%s", new_line_char);

			_print_tres_line("GrpTRES",
					 assoc_rec->grp_tres_ctld,
					 assoc_rec->usage->grp_used_tres, 0);

			/* NEW LINE */
			printf("%s", new_line_char);

			memset(tmp64_array, 0, sizeof(tmp64_array));
			if (assoc_rec->usage->usage_tres_raw)
				for (i=0; i<tres_cnt; i++)
					tmp64_array[i] = (uint64_t)
						assoc_rec->usage->
						usage_tres_raw[i];
			_print_tres_line("GrpTRESMins",
					 assoc_rec->grp_tres_mins_ctld,
					 tmp64_array, 60);

			/* NEW LINE */
			printf("%s", new_line_char);

			_print_tres_line("GrpTRESRunMins",
					 assoc_rec->grp_tres_run_mins_ctld,
					 assoc_rec->usage->
					 grp_used_tres_run_secs, 60);

			/* NEW LINE */
			printf("%s", new_line_char);

			if (assoc_rec->max_jobs != INFINITE)
				printf("MaxJobs=%u(%u) ",
				       assoc_rec->max_jobs,
				       assoc_rec->usage->used_jobs);
			else
				printf("MaxJobs= ");

			if (assoc_rec->max_jobs_accrue != INFINITE)
				printf("MaxJobsAccrue=%u(%u) ",
				       assoc_rec->max_jobs_accrue,
				       assoc_rec->usage->accrue_cnt);
			else
				printf("MaxJobsAccrue= ");

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

			_print_tres_line("MaxTRESPJ",
					 assoc_rec->max_tres_ctld,
					 NULL, 0);

			/* NEW LINE */
			printf("%s", new_line_char);

			_print_tres_line("MaxTRESPN",
					 assoc_rec->max_tres_pn_ctld,
					 NULL, 0);

			/* NEW LINE */
			printf("%s", new_line_char);

			_print_tres_line("MaxTRESMinsPJ",
					 assoc_rec->max_tres_mins_ctld,
					 NULL, 0);

			/* NEW LINE */
			printf("%s", new_line_char);

			if (assoc_rec->min_prio_thresh != INFINITE)
				printf("MinPrioThresh=%u",
				       assoc_rec->min_prio_thresh);
			else
				printf("MinPrioThresh=");

			/* NEW LINE */
			printf("%s", new_line_char);

			printf("Comment=%s", assoc_rec->comment);

			/* NEW LINE */
			printf("\n");

			/* Doesn't do anything yet */
			/* _print_tres_line("MaxTRESRunMins", */
			/* 		 assoc_rec->max_tres_mins_ctld, */
			/* 		 NULL, 0); */

			/* NEW LINE */
			/* printf("%s", new_line_char); */

		}
	}

	if (!msg->qos_list || !list_count(msg->qos_list)) {
		if (req_flags & ASSOC_MGR_INFO_FLAG_QOS)
			printf("%sNo QOS currently cached in Slurm.%s\n",
			       one_liner ? "" : "\n", one_liner ? "" : "\n");
	} else {

		printf("%sQOS Records%s\n",
		       one_liner ? "" : "\n", one_liner ? "" : "\n");


		itr = list_iterator_create(msg->qos_list);
		while ((qos_rec = list_next(itr))) {
			if (!qos_rec->usage)
				continue;

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
				printf("GrpJobs=N(%u) ",
				       qos_rec->usage->grp_used_jobs);
			if (qos_rec->grp_jobs_accrue != INFINITE)
				printf("GrpJobsAccrue=%u(%u) ",
				       qos_rec->grp_jobs_accrue,
				       qos_rec->usage->accrue_cnt);
			else
				printf("GrpJobsAccrue=N(%u) ",
				       qos_rec->usage->accrue_cnt);
			if (qos_rec->grp_submit_jobs != INFINITE)
				printf("GrpSubmitJobs=%u(%u) ",
				       qos_rec->grp_submit_jobs,
				       qos_rec->usage->grp_used_submit_jobs);
			else
				printf("GrpSubmitJobs=N(%u) ",
				       qos_rec->usage->grp_used_submit_jobs);
			if (qos_rec->grp_wall != INFINITE)
				printf("GrpWall=%u(%.2f)",
				       qos_rec->grp_wall,
				       qos_rec->usage->grp_used_wall/60);
			else
				printf("GrpWall=N(%.2f)",
				       qos_rec->usage->grp_used_wall/60);
			/* NEW LINE */
			printf("%s", new_line_char);

			_print_tres_line("GrpTRES",
					 qos_rec->grp_tres_ctld,
					 qos_rec->usage->grp_used_tres, 0);

			/* NEW LINE */
			printf("%s", new_line_char);

			memset(tmp64_array, 0, sizeof(tmp64_array));
			if (qos_rec->usage->usage_tres_raw)
				for (i=0; i<tres_cnt; i++)
					tmp64_array[i] = (uint64_t)
						qos_rec->usage->
						usage_tres_raw[i];
			_print_tres_line("GrpTRESMins",
					 qos_rec->grp_tres_mins_ctld,
					 tmp64_array, 60);

			/* NEW LINE */
			printf("%s", new_line_char);

			_print_tres_line("GrpTRESRunMins",
					 qos_rec->grp_tres_run_mins_ctld,
					 qos_rec->usage->
					 grp_used_tres_run_secs, 60);

			/* NEW LINE */
			printf("%s", new_line_char);

			if (qos_rec->max_wall_pj != INFINITE)
				printf("MaxWallPJ=%u",
				       qos_rec->max_wall_pj);
			else
				printf("MaxWallPJ=");

			/* NEW LINE */
			printf("%s", new_line_char);

			_print_tres_line("MaxTRESPJ",
					 qos_rec->max_tres_pj_ctld,
					 NULL, 0);

			/* NEW LINE */
			printf("%s", new_line_char);

			_print_tres_line("MaxTRESPN",
					 qos_rec->max_tres_pn_ctld,
					 NULL, 0);

			/* NEW LINE */
			printf("%s", new_line_char);

			_print_tres_line("MaxTRESMinsPJ",
					 qos_rec->max_tres_mins_pj_ctld,
					 NULL, 0);

			/* NEW LINE */
			printf("%s", new_line_char);

			/* Doesn't do anything yet */
			/* _print_tres_line("MaxTRESRunMinsPA", */
			/* 		 qos_rec->max_tres_mins_pa_ctld, */
			/* 		 NULL, 0); */

			/* NEW LINE */
			/* printf("%s", new_line_char); */

			/* _print_tres_line("MaxTRESRunMinsPU", */
			/* 		 qos_rec->max_tres_mins_pu_ctld, */
			/* 		 NULL, 0); */

			/* NEW LINE */
			/* printf("%s", new_line_char); */

			if (qos_rec->min_prio_thresh != INFINITE)
				printf("MinPrioThresh=%u ",
				       qos_rec->min_prio_thresh);
			else
				printf("MinPrioThresh= ");

			/* NEW LINE */
			printf("%s", new_line_char);

			_print_tres_line("MinTRESPJ",
					 qos_rec->min_tres_pj_ctld,
					 NULL, 0);

			/* NEW LINE */
			printf("%s", new_line_char);

			printf("PreemptMode=%s%s",
			       preempt_mode_string(qos_rec->preempt_mode),
			       one_liner ? " " : "\n    ");

			if (qos_rec->priority == INFINITE ||
			    qos_rec->priority == NO_VAL)
				printf("Priority=NONE");
			else
				printf("Priority=%u",
				       qos_rec->priority);

			/* NEW LINE */
			printf("%s", new_line_char);

			printf("Account Limits%s",
			       one_liner ? "=" : "");
			if (qos_rec->usage->acct_limit_list) {
				list_for_each(qos_rec->usage->acct_limit_list,
					      (ListForF)_print_used_acct_limit,
					      qos_rec);
			} else
				printf("%sNo Accounts",
				       one_liner ? "" : "\n        ");

			/* NEW LINE */
			printf("%s", new_line_char);

			printf("User Limits%s",
			       one_liner ? "=" : "");
			if (qos_rec->usage->user_limit_list) {
				list_for_each(qos_rec->usage->user_limit_list,
					      (ListForF)_print_used_user_limit,
					      qos_rec);
			} else
				printf("%sNo Users",
				       one_liner ? "" : "\n        ");

			/* NEW LINE */
			printf("\n");
		}
	}
}

/* scontrol_print_assoc_mgr_info()
 *
 * Retrieve and display the association manager information
 * from the controller
 *
 */

extern void scontrol_print_assoc_mgr_info(int argc, char **argv)
{
	char *tag = NULL, *val = NULL;
	int cc, tag_len, i;
	assoc_mgr_info_request_msg_t req;
	assoc_mgr_info_msg_t *msg = NULL;

	memset(&req, 0, sizeof(assoc_mgr_info_request_msg_t));

	for (i = 0; i < argc; ++i) {
		tag = argv[i];
		tag_len = strlen(tag);
		val = strchr(argv[i], '=');
		if (val) {
			tag_len = val - argv[i];
			val++;
		}

		/* We free every list before creating it. This way we ensure
		 * we are just appending the last value if user repeats entity.
		 */
		if (!val || !val[0]) {
			fprintf(stderr, "No value given for option %s\n", tag);
			goto endit;
		} else if (!xstrncasecmp(tag, "accounts", MAX(tag_len, 1))) {
			if (!req.acct_list)
				req.acct_list = list_create(xfree_ptr);
			slurm_addto_char_list(req.acct_list, val);
		} else if (!xstrncasecmp(tag, "flags", MAX(tag_len, 1))) {
			if (xstrcasestr(val, "users"))
				req.flags |= ASSOC_MGR_INFO_FLAG_USERS;
			if (xstrcasestr(val, "assoc"))
				req.flags |= ASSOC_MGR_INFO_FLAG_ASSOC;
			if (xstrcasestr(val, "qos"))
				req.flags |= ASSOC_MGR_INFO_FLAG_QOS;

			if (!req.flags) {
				fprintf(stderr, "invalid flag '%s', "
					"valid options are "
					"'Assoc, QOS, and/or Users'\n",
					val);
				goto endit;
			}
		} else if (!xstrncasecmp(tag, "qos", MAX(tag_len, 1))) {
			if (!req.qos_list)
				req.qos_list = list_create(xfree_ptr);
			slurm_addto_char_list(req.qos_list, val);
		} else if (!xstrncasecmp(tag, "users", MAX(tag_len, 1))) {
			if (!req.user_list)
				req.user_list = list_create(xfree_ptr);
			/*
			 * Since we don't have a real connection to the dbd to
			 * know if case is enforced or not so we will assume
			 * it is.
			 */
			slurm_addto_char_list_with_case(req.user_list, val, 0);
		} else {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr, "invalid entity: %s for keyword"
					":show assoc_mgr\n", tag);
			goto endit;
		}
	}

	if (!req.flags)
		req.flags = ASSOC_MGR_INFO_FLAG_ASSOC |
			ASSOC_MGR_INFO_FLAG_USERS |
			ASSOC_MGR_INFO_FLAG_QOS;

	req_flags = req.flags;

	/* call the controller to get the meat */
	cc = slurm_load_assoc_mgr_info(&req, &msg);

	if (cc == SLURM_SUCCESS) {
		/* print the info
		 */
		_print_assoc_mgr_info(msg);
	} else {
		/* Hosed, crap out. */
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror("slurm_load_assoc_mgr_info error");
	}

	slurm_free_assoc_mgr_info_msg(msg);
endit:
	slurm_free_assoc_mgr_info_request_members(&req);

	return;
}
