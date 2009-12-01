/*****************************************************************************\
 *  test24.1.prog.c - link and test algo of the multifactor plugin.
 *
 *  Usage: test24.1.prog
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
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
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif			/* HAVE_INTTYPES_H */

#include <time.h>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "src/common/slurm_priority.h"
#include "src/common/assoc_mgr.h"
#include "src/common/xstring.h"
#include "src/common/log.h"
#include "src/sshare/sshare.h"

/* set up some fake system */
int cluster_procs = 50;
int long_flag = 1;
int exit_code = 0;
sshare_time_format_t time_format = SSHARE_TIME_MINS;
char *time_format_string = "Minutes";

List   job_list = NULL;		/* job_record list */
static pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;

/* this will leak memory, but we don't care really */
static void _list_delete_job(void *job_entry)
{
	struct job_record *job_ptr = (struct job_record *) job_entry;

	xfree(job_ptr);
}

int _setup_assoc_list()
{
	acct_update_object_t update;
	acct_association_rec_t *assoc = NULL;
	/* make the main list */
	assoc_mgr_association_list = list_create(destroy_acct_association_rec);

	/* we just want make it so we setup_childern so just pretend
	   we are running off cache */
	running_cache = 1;
	assoc_mgr_init(NULL, NULL);

	/* Here we make the associations we want to add to the
	   system.  We do this as an update to avoid having to do
	   setup.
	*/
	memset(&update, 0, sizeof(acct_update_object_t));
	update.type = ACCT_ADD_ASSOC;
	update.objects = list_create(destroy_acct_association_rec);

	/* Since we don't want to worry about lft and rgt's here we
	 * need to put the assocs in hierarchical order using push
	 * not append. */

	/* First only add the accounts */
	/* root association */
	assoc = xmalloc(sizeof(acct_association_rec_t));
	assoc->id = 1;
	assoc->acct = xstrdup("root");
	list_push(update.objects, assoc);

	/* sub of root id 1 */
	assoc = xmalloc(sizeof(acct_association_rec_t));
	assoc->id = 2;
	assoc->parent_id = 1;
	assoc->shares_raw = 40;
	assoc->acct = xstrdup("AccountA");
	list_push(update.objects, assoc);

	/* sub of AccountA id 2 */
	assoc = xmalloc(sizeof(acct_association_rec_t));
	assoc->id = 21;
	assoc->parent_id = 2;
	assoc->shares_raw = 30;
	assoc->acct = xstrdup("AccountB");
	list_push(update.objects, assoc);

	/* sub of AccountB id 21 */
	assoc = xmalloc(sizeof(acct_association_rec_t));
	assoc->id = 211;
	assoc->parent_id = 21;
	assoc->shares_raw = 1;
	assoc->usage_raw = 20;
	assoc->acct = xstrdup("AccountB");
	assoc->user = xstrdup("User1");
	list_push(update.objects, assoc);

	/* sub of AccountA id 2 */
	assoc = xmalloc(sizeof(acct_association_rec_t));
	assoc->id = 22;
	assoc->parent_id = 2;
	assoc->shares_raw = 10;
	assoc->acct = xstrdup("AccountC");
	list_push(update.objects, assoc);

	/* sub of AccountC id 22 */
	assoc = xmalloc(sizeof(acct_association_rec_t));
	assoc->id = 221;
	assoc->parent_id = 22;
	assoc->shares_raw = 1;
	assoc->usage_raw = 25;
	assoc->acct = xstrdup("AccountC");
	assoc->user = xstrdup("User2");
	list_push(update.objects, assoc);

	assoc = xmalloc(sizeof(acct_association_rec_t));
	assoc->id = 222;
	assoc->parent_id = 22;
	assoc->shares_raw = 1;
	assoc->usage_raw = 0;
	assoc->acct = xstrdup("AccountC");
	assoc->user = xstrdup("User3");
	list_push(update.objects, assoc);

	/* sub of root id 1 */
	assoc = xmalloc(sizeof(acct_association_rec_t));
	assoc->id = 3;
	assoc->parent_id = 1;
	assoc->shares_raw = 60;
	assoc->acct = xstrdup("AccountD");
	list_push(update.objects, assoc);

	/* sub of AccountD id 3 */
	assoc = xmalloc(sizeof(acct_association_rec_t));
	assoc->id = 31;
	assoc->parent_id = 3;
	assoc->shares_raw = 25;
	assoc->acct = xstrdup("AccountE");
	list_push(update.objects, assoc);

	/* sub of AccountE id 31 */
	assoc = xmalloc(sizeof(acct_association_rec_t));
	assoc->id = 311;
	assoc->parent_id = 31;
	assoc->shares_raw = 1;
	assoc->usage_raw = 25;
	assoc->acct = xstrdup("AccountE");
	assoc->user = xstrdup("User4");
	list_push(update.objects, assoc);

	/* sub of AccountD id 3 */
	assoc = xmalloc(sizeof(acct_association_rec_t));
	assoc->id = 32;
	assoc->parent_id = 3;
	assoc->shares_raw = 35;
	assoc->acct = xstrdup("AccountF");
	list_push(update.objects, assoc);

	/* sub of AccountF id 32 */
	assoc = xmalloc(sizeof(acct_association_rec_t));
	assoc->id = 321;
	assoc->parent_id = 32;
	assoc->shares_raw = 1;
	assoc->usage_raw = 0;
	assoc->acct = xstrdup("AccountF");
	assoc->user = xstrdup("User5");
	list_push(update.objects, assoc);

	assoc_mgr_update_assocs(&update);
	list_destroy(update.objects);
	return SLURM_SUCCESS;
}

int main (int argc, char **argv)
{
	log_options_t logopt = LOG_OPTS_STDERR_ONLY;
	slurm_ctl_conf_t *conf = NULL;
	shares_response_msg_t resp;

	log_init(xbasename(argv[0]), logopt, 0, NULL);
	xfree(slurmctld_conf.priority_type);
	//logopt.stderr_level += 5;
	logopt.prefix_level = 1;
	log_alter(logopt, 0, NULL);
	print_fields_have_header = 0;
	print_fields_parsable_print = PRINT_FIELDS_PARSABLE_ENDING;

	conf = slurm_conf_lock();
	/* force priority type to be multifactor */
	xfree(conf->priority_type);
	conf->priority_type = xstrdup("priority/multifactor");
	/* force accounting type to be slurmdbd (It doesn't really talk
	 * to any database, but needs this to work with fairshare
	 * calculation). */
	xfree(conf->accounting_storage_type);
	conf->accounting_storage_type = xstrdup("accounting_storage/slurmdbd");
	/* set up a known environment to test against.  Since we are
	   only concerned about the fairshare we won't look at the other
	   factors here.
	*/
	conf->priority_decay_hl = 1;
	conf->priority_favor_small = 0;
	conf->priority_max_age = conf->priority_decay_hl;
	conf->priority_weight_age = 0;
	conf->priority_weight_fs = 10000;
	conf->priority_weight_js = 0;
	conf->priority_weight_part = 0;
	conf->priority_weight_qos = 0;
	slurm_conf_unlock();

	/* we don't want to do any decay here so make the save state
	 * to /dev/null */
	xfree(slurmctld_conf.state_save_location);
	slurmctld_conf.state_save_location = "/dev/null";
	/* now set up the association tree */
	_setup_assoc_list();
	/* now set up the job list */
	job_list = list_create(_list_delete_job);

	/* now init the priorities of the associations */
	if (slurm_priority_init() != SLURM_SUCCESS)
		fatal("failed to initialize priority plugin");
	/* on some systems that don't have multiple cores we need to
	   sleep to make sure the tread get started. */
	sleep(1);
	memset(&resp, 0, sizeof(shares_response_msg_t));
	resp.assoc_shares_list = assoc_mgr_get_shares(NULL, 0, NULL, NULL);
	process(&resp);

	/* free memory */
	if (slurm_priority_fini() != SLURM_SUCCESS)
		fatal("failed to finalize priority plugin");
	if(job_list)
		list_destroy(job_list);
	if(resp.assoc_shares_list)
		list_destroy(resp.assoc_shares_list);
	if(assoc_mgr_association_list)
		list_destroy(assoc_mgr_association_list);
	return 0;
}
