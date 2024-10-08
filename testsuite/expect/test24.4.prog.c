/*****************************************************************************\
 *  test24.4.prog.c - link and test algo of Fair Tree multifactor.
 *
 *  Usage: test24.4.prog
 *****************************************************************************
 *  Modified by Brigham Young University
 *      Ryan Cox <ryan_cox@byu.edu> and Levi Morrison <levi_morrison@byu.edu>
 *
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <inttypes.h>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "src/interfaces/priority.h"
#include "src/common/assoc_mgr.h"
#include "src/common/xstring.h"
#include "src/common/log.h"
#include "src/sshare/sshare.h"

/* set up some fake system */
void *acct_db_conn = NULL;
uint32_t cluster_cpus = 50;
int exit_code = 0;
sshare_time_format_t time_format = SSHARE_TIME_MINS;
char *time_format_string = "Minutes";
time_t last_job_update = (time_t) 0;
uint16_t running_cache = RUNNING_CACHE_STATE_NOTRUNNING;

list_t *job_list = NULL;		/* job_record list */
static pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;

/* this will leak memory, but we don't care really */
static void _list_delete_job(void *job_entry)
{
	job_record_t *job_ptr = (job_record_t *) job_entry;

	xfree(job_ptr);
}

int _setup_assoc_list(void)
{
	slurmdb_update_object_t update;
	slurmdb_assoc_rec_t *assoc = NULL;
	slurmdb_tres_rec_t *tres = NULL;
	assoc_init_args_t assoc_init_arg;

	/* make the main list */
	assoc_mgr_assoc_list =
		list_create(slurmdb_destroy_assoc_rec);
	assoc_mgr_user_list =
		list_create(slurmdb_destroy_user_rec);
	assoc_mgr_qos_list =
		list_create(slurmdb_destroy_qos_rec);

	/* we just want make it so we setup_children so just pretend
	 * we are running off cache */
	memset(&assoc_init_arg, 0, sizeof(assoc_init_args_t));
	assoc_init_arg.running_cache = &running_cache;
	running_cache = RUNNING_CACHE_STATE_RUNNING;
	assoc_mgr_init(NULL, &assoc_init_arg, SLURM_SUCCESS);

	/* Here we make the tres we want to add to the system.
	 * We do this as an update to avoid having to do setup. */
	memset(&update, 0, sizeof(slurmdb_update_object_t));
	update.type = SLURMDB_ADD_TRES;
	update.objects = list_create(slurmdb_destroy_tres_rec);

	tres = xmalloc(sizeof(slurmdb_tres_rec_t));
	tres->id = 1;
	tres->type = xstrdup("cpu");
	list_append(update.objects, tres);

	if (assoc_mgr_update_tres(&update, false))
		error("assoc_mgr_update_tres: %m");
	FREE_NULL_LIST(update.objects);

	/* Here we make the associations we want to add to the system.
	 * We do this as an update to avoid having to do setup. */
	memset(&update, 0, sizeof(slurmdb_update_object_t));
	update.type = SLURMDB_ADD_ASSOC;
	update.objects = list_create(slurmdb_destroy_assoc_rec);

	/* root assoc */
	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 1;
	assoc->acct = xstrdup("root");
	assoc->lineage = xstrdup("/");
	list_append(update.objects, assoc);

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 2;
	assoc->parent_id = 1;
	assoc->shares_raw = 40;
	assoc->acct = xstrdup("aA");
	assoc->lineage = xstrdup("/aA/");
	list_append(update.objects, assoc);

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 21;
	assoc->parent_id = 2;
	assoc->shares_raw = 30;
	assoc->acct = xstrdup("aAA");
	assoc->lineage = xstrdup("/aAA/");
	list_append(update.objects, assoc);

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 211;
	assoc->parent_id = 21;
	assoc->shares_raw = 1;
	assoc->usage->usage_raw = 20;
	assoc->acct = xstrdup("aAA");
	assoc->user = xstrdup("uAA1");
	assoc->lineage = xstrdup("/aAA/0-uAA1/");
	list_append(update.objects, assoc);

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 22;
	assoc->parent_id = 2;
	assoc->shares_raw = 10;
	assoc->acct = xstrdup("aAB");
	assoc->lineage = xstrdup("/aAB/");
	list_append(update.objects, assoc);

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 221;
	assoc->parent_id = 22;
	assoc->shares_raw = 1;
	assoc->usage->usage_raw = 25;
	assoc->acct = xstrdup("aAB");
	assoc->user = xstrdup("uAB1");
	assoc->lineage = xstrdup("/aAB/uAB1/");
	list_append(update.objects, assoc);

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 222;
	assoc->parent_id = 22;
	assoc->shares_raw = 1;
	assoc->usage->usage_raw = 0;
	assoc->acct = xstrdup("aAB");
	assoc->user = xstrdup("uAB2");
	assoc->lineage = xstrdup("/aAB/uAB2/");
	list_append(update.objects, assoc);

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 3;
	assoc->parent_id = 1;
	assoc->shares_raw = 60;
	assoc->acct = xstrdup("aB");
	assoc->lineage = xstrdup("/aB/");
	list_append(update.objects, assoc);

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 31;
	assoc->parent_id = 3;
	assoc->shares_raw = 25;
	assoc->acct = xstrdup("aBA");
	assoc->lineage = xstrdup("/aB/aBA/");
	list_append(update.objects, assoc);

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 311;
	assoc->parent_id = 31;
	assoc->shares_raw = 1;
	assoc->usage->usage_raw = 25;
	assoc->acct = xstrdup("aBA");
	assoc->user = xstrdup("uBA1");
	assoc->lineage = xstrdup("/aB/aBA/0-uBA1/");
	list_append(update.objects, assoc);

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 32;
	assoc->parent_id = 3;
	assoc->shares_raw = 35;
	assoc->acct = xstrdup("aBB");
	assoc->lineage = xstrdup("/aB/aBB/");
	list_append(update.objects, assoc);

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 321;
	assoc->parent_id = 32;
	assoc->shares_raw = 1;
	assoc->usage->usage_raw = 0;
	assoc->acct = xstrdup("aBB");
	assoc->user = xstrdup("uBB1");
	assoc->lineage = xstrdup("/aB/aBB/0-uBB1/");
	list_append(update.objects, assoc);

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 4;
	assoc->parent_id = 1;
	assoc->shares_raw = 0;
	assoc->usage->usage_raw = 30;
	assoc->acct = xstrdup("aC");
	assoc->lineage = xstrdup("/aC/");
	list_append(update.objects, assoc);

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 41;
	assoc->parent_id = 4;
	assoc->shares_raw = 0;
	assoc->usage->usage_raw = 30;
	assoc->acct = xstrdup("aC");
	assoc->user = xstrdup("uC1");
	assoc->lineage = xstrdup("/aC/0-uC1/");
	list_append(update.objects, assoc);

	/* Check for proper handling of Fairshare=parent */

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 5;
	assoc->parent_id = 1;
	assoc->shares_raw = 50;
	assoc->acct = xstrdup("aD");
	assoc->lineage = xstrdup("/aD/");
	list_append(update.objects, assoc);

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 51;
	assoc->parent_id = 5;
	assoc->shares_raw = SLURMDB_FS_USE_PARENT;
	assoc->usage->usage_raw = 35;
	assoc->acct = xstrdup("aDA");
	assoc->lineage = xstrdup("/aD/aDA/");
	list_append(update.objects, assoc);

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 511;
	assoc->parent_id = 51;
	assoc->shares_raw = SLURMDB_FS_USE_PARENT;
	assoc->usage->usage_raw = 10;
	assoc->acct = xstrdup("aDA");
	assoc->user = xstrdup("uDA1");
	assoc->lineage = xstrdup("/aD/aDA/uDA1/");
	list_append(update.objects, assoc);

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 512;
	assoc->parent_id = 51;
	assoc->shares_raw = 30;
	assoc->usage->usage_raw = 10;
	assoc->acct = xstrdup("aDA");
	assoc->user = xstrdup("uDA2");
	assoc->lineage = xstrdup("/aD/aDA/uDA1/0-uDA2/");
	list_append(update.objects, assoc);

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 513;
	assoc->parent_id = 51;
	assoc->shares_raw = 50;
	assoc->usage->usage_raw = 25;
	assoc->acct = xstrdup("aDA");
	assoc->user = xstrdup("uDA3");
	assoc->lineage = xstrdup("/aD/aDA/uDA1/0-uDA3/");
	list_append(update.objects, assoc);

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 52;
	assoc->parent_id = 5;
	assoc->shares_raw = SLURMDB_FS_USE_PARENT;
	assoc->usage->usage_raw = 20;
	assoc->acct = xstrdup("aD");
	assoc->user = xstrdup("uD1");
	assoc->lineage = xstrdup("/aD/0-uD1/");
	list_append(update.objects, assoc);

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 53;
	assoc->parent_id = 5;
	assoc->shares_raw = 40;
	assoc->usage->usage_raw = 20;
	assoc->acct = xstrdup("aD");
	assoc->user = xstrdup("uD2");
	assoc->lineage = xstrdup("/aD/0-uD2/");
	list_append(update.objects, assoc);

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 54;
	assoc->parent_id = 5;
	assoc->shares_raw = 50;
	assoc->usage->usage_raw = 25;
	assoc->acct = xstrdup("aD");
	assoc->user = xstrdup("uD3");
	assoc->lineage = xstrdup("/aD/0-uD3/");
	list_append(update.objects, assoc);

	/* Check for proper tie handling */

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 6;
	assoc->parent_id = 1;
	assoc->shares_raw = 10;
	assoc->usage->usage_raw = 0;
	assoc->acct = xstrdup("aE");
	assoc->lineage = xstrdup("/aE/");
	list_append(update.objects, assoc);

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 61;
	assoc->parent_id = 6;
	assoc->shares_raw = 10;
	assoc->usage->usage_raw = 0;
	assoc->acct = xstrdup("aE");
	assoc->user = xstrdup("aE1");
	assoc->lineage = xstrdup("/aE/0-aE1/");
	list_append(update.objects, assoc);

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 62;
	assoc->parent_id = 6;
	assoc->shares_raw = 10;
	assoc->usage->usage_raw = 0;
	assoc->acct = xstrdup("aE");
	assoc->user = xstrdup("aE2");
	assoc->lineage = xstrdup("/aE/0-aE2/");
	list_append(update.objects, assoc);

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 7;
	assoc->parent_id = 1;
	assoc->shares_raw = 10;
	assoc->usage->usage_raw = 0;
	assoc->acct = xstrdup("root");
	assoc->user = xstrdup("u1");
	assoc->lineage = xstrdup("/0-u1/");
	list_append(update.objects, assoc);

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 8;
	assoc->parent_id = 1;
	assoc->shares_raw = 20;
	assoc->usage->usage_raw = 0;
	assoc->acct = xstrdup("aF");
	assoc->lineage = xstrdup("/aF/");
	list_append(update.objects, assoc);

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 81;
	assoc->parent_id = 8;
	assoc->shares_raw = 10;
	assoc->usage->usage_raw = 0;
	assoc->acct = xstrdup("aF");
	assoc->user = xstrdup("uF1");
	assoc->lineage = xstrdup("/aF/0-uF1/");
	list_append(update.objects, assoc);

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 82;
	assoc->parent_id = 8;
	assoc->shares_raw = 20;
	assoc->usage->usage_raw = 0;
	assoc->acct = xstrdup("aF");
	assoc->user = xstrdup("uF2");
	assoc->lineage = xstrdup("/aF/0-uF2/");
	list_append(update.objects, assoc);

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 9;
	assoc->parent_id = 1;
	assoc->shares_raw = 8;
	assoc->usage->usage_raw = 20;
	assoc->acct = xstrdup("aG");
	assoc->lineage = xstrdup("/aG/");
	list_append(update.objects, assoc);

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 91;
	assoc->parent_id = 9;
	assoc->shares_raw = 10;
	assoc->usage->usage_raw = 10;
	assoc->acct = xstrdup("aG");
	assoc->user = xstrdup("uG1");
	assoc->lineage = xstrdup("/aG/0-uG1/");
	list_append(update.objects, assoc);

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 92;
	assoc->parent_id = 9;
	assoc->shares_raw = 10;
	assoc->usage->usage_raw = 10;
	assoc->acct = xstrdup("aGA");
	assoc->lineage = xstrdup("/aGA/");
	list_append(update.objects, assoc);

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 921;
	assoc->parent_id = 92;
	assoc->shares_raw = 20;
	assoc->usage->usage_raw = 4;
	assoc->acct = xstrdup("aGA");
	assoc->user = xstrdup("uGA1");
	assoc->lineage = xstrdup("/aGA/0-uGA1");
	list_append(update.objects, assoc);

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 921;
	assoc->parent_id = 92;
	assoc->shares_raw = 20;
	assoc->usage->usage_raw = 6;
	assoc->acct = xstrdup("aGA");
	assoc->user = xstrdup("uGA2");
	assoc->lineage = xstrdup("/aGA/0-uGA2");
	list_append(update.objects, assoc);

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc->cluster = xstrdup(slurm_conf.cluster_name);
	assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	assoc->id = 1001;
	assoc->parent_id = 1;
	assoc->shares_raw = 10;
	assoc->usage->usage_raw = 10;
	assoc->acct = xstrdup("root");
	assoc->user = xstrdup("u2");
	assoc->lineage = xstrdup("/0-u2");
	list_append(update.objects, assoc);

	if (assoc_mgr_update_assocs(&update, false))
		error("assoc_mgr_update_assocs: %m");
	list_destroy(update.objects);

	return SLURM_SUCCESS;
}

int main (int argc, char **argv)
{
	log_options_t logopt = LOG_OPTS_STDERR_ONLY;
	slurm_conf_t *conf = NULL;
	shares_response_msg_t resp;

	log_init(xbasename(argv[0]), logopt, 0, NULL);
	xfree(slurm_conf.priority_type);
	//logopt.stderr_level += 5;
	logopt.prefix_level = 1;
	log_alter(logopt, 0, NULL);
	print_fields_have_header = 0;
	print_fields_parsable_print = PRINT_FIELDS_PARSABLE_ENDING;

	conf = slurm_conf_lock();
	/* force priority type to be multifactor */
	xfree(conf->priority_type);
	conf->priority_type = xstrdup("priority/multifactor");
	conf->priority_flags = PRIORITY_FLAGS_FAIR_TREE;
	/* force accounting type to be slurmdbd (It doesn't really talk
	 * to any database, but needs this to work with fairshare
	 * calculation). */
	xfree(conf->accounting_storage_type);
	conf->accounting_storage_type = xstrdup("accounting_storage/slurmdbd");
	/* set up a known environment to test against.  Since we are
	 * only concerned about the fairshare we won't look at the other
	 * factors here. */
	conf->priority_decay_hl = 1;
	conf->priority_favor_small = 0;
	conf->priority_max_age = conf->priority_decay_hl;
	conf->priority_reset_period = 0;
	conf->priority_weight_age = 0;
	conf->priority_weight_fs = 10000;
	conf->priority_weight_js = 0;
	conf->priority_weight_part = 0;
	conf->priority_weight_qos = 0;
	slurm_conf_unlock();

	/* we don't want to do any decay here so make the save state
	 * to /dev/null */
	xfree(slurm_conf.state_save_location);
	slurm_conf.state_save_location = "/dev/null";
	/* now set up the association tree */
	_setup_assoc_list();
	/* now set up the job list */
	job_list = list_create(_list_delete_job);

	/* now init the priorities of the associations */
	if (priority_g_init() != SLURM_SUCCESS)
		fatal("failed to initialize priority plugin");
	priority_g_thread_start();
	/* on some systems that don't have multiple cores we need to
	 * sleep to make sure the thread gets started. */
	sleep(1);
	memset(&resp, 0, sizeof(shares_response_msg_t));
	assoc_mgr_get_shares(NULL, 0, NULL, &resp);

	/*
	 * This is the global var from sshare.h to tell we want the long format
	 */
	long_flag = 1;

	process(&resp, 0);

	/* free memory */
	if (priority_g_fini() != SLURM_SUCCESS)
		fatal("failed to finalize priority plugin");
	if (job_list)
		list_destroy(job_list);
	if (resp.assoc_shares_list)
		list_destroy(resp.assoc_shares_list);
	if (assoc_mgr_assoc_list)
		list_destroy(assoc_mgr_assoc_list);
	if (assoc_mgr_qos_list)
		list_destroy(assoc_mgr_qos_list);
	xfree(assoc_mgr_tres_array);
	return 0;
}
