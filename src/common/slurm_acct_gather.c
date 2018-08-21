/*****************************************************************************\
 *  slurm_acct_gather.c - generic interface needed for some
 *                        acct_gather plugins.
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC.
 *  Written by Danny Auble <da@schedmd.com>
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

#include <sys/stat.h>
#include <stdlib.h>

#include "src/common/slurm_acct_gather.h"
#include "slurm_acct_gather_energy.h"
#include "slurm_acct_gather_interconnect.h"
#include "slurm_acct_gather_filesystem.h"
#include "src/common/xstring.h"

static bool acct_gather_suspended = false;
static pthread_mutex_t suspended_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t conf_mutex = PTHREAD_MUTEX_INITIALIZER;

static bool inited = 0;

static int _get_int(const char *my_str)
{
	char *end = NULL;
	int value;

	if (!my_str)
		return -1;
	value = strtol(my_str, &end, 10);
	//info("from %s I get %d and %s: %m", my_str, value, end);
	/* means no numbers */
	if (my_str == end)
		return -1;

	return value;
}

extern int acct_gather_conf_init(void)
{
	s_p_hashtbl_t *tbl = NULL;
	char *conf_path = NULL;
	s_p_options_t *full_options = NULL;
	int full_options_cnt = 0, i;
	struct stat buf;
	int rc = SLURM_SUCCESS;

	if (inited)
		return SLURM_SUCCESS;
	inited = 1;

	/* get options from plugins using acct_gather.conf */

	rc += acct_gather_energy_g_conf_options(&full_options,
						&full_options_cnt);
	rc += acct_gather_profile_g_conf_options(&full_options,
						 &full_options_cnt);
	rc += acct_gather_interconnect_g_conf_options(&full_options,
						      &full_options_cnt);
	rc += acct_gather_filesystem_g_conf_options(&full_options,
						    &full_options_cnt);
	/* ADD MORE HERE */

	/* for the NULL at the end */
	xrealloc(full_options,
		 ((full_options_cnt + 1) * sizeof(s_p_options_t)));

	/**************************************************/

	/* Get the acct_gather.conf path and validate the file */
	conf_path = get_extra_conf_path("acct_gather.conf");
	if ((conf_path == NULL) || (stat(conf_path, &buf) == -1)) {
		debug2("No acct_gather.conf file (%s)", conf_path);
	} else {
		debug2("Reading acct_gather.conf file %s", conf_path);

		tbl = s_p_hashtbl_create(full_options);
		if (s_p_parse_file(tbl, NULL, conf_path, false) ==
		    SLURM_ERROR) {
			fatal("Could not open/read/parse acct_gather.conf file "
			      "%s.  Many times this is because you have "
			      "defined options for plugins that are not "
			      "loaded.  Please check your slurm.conf file "
			      "and make sure the plugins for the options "
			      "listed are loaded.",
			      conf_path);
		}
	}

	for (i=0; i<full_options_cnt; i++)
		xfree(full_options[i].key);
	xfree(full_options);
	xfree(conf_path);

	/* handle acct_gather.conf in each plugin */
	slurm_mutex_lock(&conf_mutex);
	rc += acct_gather_energy_g_conf_set(tbl);
	rc += acct_gather_profile_g_conf_set(tbl);
	rc += acct_gather_interconnect_g_conf_set(tbl);
	rc += acct_gather_filesystem_g_conf_set(tbl);
	/*********************************************************************/
	/* ADD MORE HERE AND FREE MEMORY IN acct_gather_conf_destroy() BELOW */
	/*********************************************************************/
	slurm_mutex_unlock(&conf_mutex);

	s_p_hashtbl_destroy(tbl);

	return rc;
}

extern int acct_gather_conf_destroy(void)
{
	int rc, rc2;

	if (!inited)
		return SLURM_SUCCESS;

	rc = acct_gather_energy_fini();

	rc2 = acct_gather_filesystem_fini();
	rc = MAX(rc, rc2);
	rc2 = acct_gather_interconnect_fini();
	rc = MAX(rc, rc2);
	rc2 = acct_gather_profile_fini();
	rc = MAX(rc, rc2);

	slurm_mutex_destroy(&conf_mutex);
	return rc;
}

extern List acct_gather_conf_values(void)
{
	List acct_list = list_create(destroy_config_key_pair);

	/* get acct_gather.conf in each plugin */
	slurm_mutex_lock(&conf_mutex);
	acct_gather_profile_g_conf_values(&acct_list);
	acct_gather_interconnect_g_conf_values(&acct_list);
	acct_gather_energy_g_conf_values(&acct_list);
	acct_gather_filesystem_g_conf_values(&acct_list);
	/* ADD MORE HERE */
	slurm_mutex_unlock(&conf_mutex);
	/******************************************/

	list_sort(acct_list, (ListCmpF) sort_key_pairs);

	return acct_list;
}

extern int acct_gather_parse_freq(int type, char *freq)
{
	int freq_int = -1;
	char *sub_str = NULL;

	if (!freq)
		return freq_int;

	switch (type) {
	case PROFILE_ENERGY:
		if ((sub_str = xstrcasestr(freq, "energy=")))
			freq_int = _get_int(sub_str + 7);
		break;
	case PROFILE_TASK:
		/* backwards compatibility for when the freq was only
		   for task.
		*/
		freq_int = _get_int(freq);
		if ((freq_int == -1)
		    && (sub_str = xstrcasestr(freq, "task=")))
			freq_int = _get_int(sub_str + 5);
		break;
	case PROFILE_FILESYSTEM:
		if ((sub_str = xstrcasestr(freq, "filesystem=")))
			freq_int = _get_int(sub_str + 11);
		break;
	case PROFILE_NETWORK:
		if ((sub_str = xstrcasestr(freq, "network=")))
			freq_int = _get_int(sub_str + 8);
		break;
	default:
		fatal("Unhandled profile option %d please update "
		      "slurm_acct_gather.c "
		      "(acct_gather_parse_freq)", type);
	}

	return freq_int;
}

extern int acct_gather_check_acct_freq_task(uint64_t job_mem_lim,
					    char *acctg_freq)
{
	int task_freq;
	static uint32_t acct_freq_task = NO_VAL;

	if (acct_freq_task == NO_VAL) {
		char *acct_freq = slurm_get_jobacct_gather_freq();
		int i = acct_gather_parse_freq(PROFILE_TASK, acct_freq);
		xfree(acct_freq);

		/* If the value is -1 lets set the freq to something
		   really high so we don't check this again.
		*/
		if (i == -1)
			acct_freq_task = NO_VAL16;
		else
			acct_freq_task = i;
	}

	if (!job_mem_lim || !acct_freq_task)
		return 0;

	task_freq = acct_gather_parse_freq(PROFILE_TASK, acctg_freq);

	if (task_freq == -1)
		return 0;

	if (task_freq == 0) {
		error("Can't turn accounting frequency off.  "
		      "We need it to monitor memory usage.");
		slurm_seterrno(ESLURMD_INVALID_ACCT_FREQ);
		return 1;
	} else if (task_freq > acct_freq_task) {
		error("Can't set frequency to %d, it is higher than %u.  "
		      "We need it to be at least at this level to "
		      "monitor memory usage.",
		      task_freq, acct_freq_task);
		slurm_seterrno(ESLURMD_INVALID_ACCT_FREQ);
		return 1;
	}

	return 0;
}

extern void acct_gather_suspend_poll(void)
{
	slurm_mutex_lock(&suspended_mutex);
	acct_gather_suspended = true;
	slurm_mutex_unlock(&suspended_mutex);
}

extern void acct_gather_resume_poll(void)
{
	slurm_mutex_lock(&suspended_mutex);
	acct_gather_suspended = false;
	slurm_mutex_unlock(&suspended_mutex);
}

extern bool acct_gather_suspend_test(void)
{
	bool rc;
	slurm_mutex_lock(&suspended_mutex);
	rc = acct_gather_suspended;
	slurm_mutex_unlock(&suspended_mutex);
	return rc;
}
