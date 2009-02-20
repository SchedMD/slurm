/*****************************************************************************\
 *  config_functions.c - functions dealing with system configuration.
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#include "src/common/list.h"
#include "src/common/read_config.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"
#include "src/sacctmgr/sacctmgr.h"

static char    *acct_storage_host = NULL;
static char    *acct_storage_loc  = NULL;
static char    *acct_storage_pass = NULL;
static uint32_t acct_storage_port;
static char    *acct_storage_type = NULL;
static char    *acct_storage_user = NULL;
static char    *auth_type = NULL;
static uint16_t msg_timeout;
static char    *plugin_dir = NULL;
static uint16_t private_data;
static uint32_t slurm_user_id;
static uint16_t track_wckey;

static List dbd_config_list = NULL;


static void _load_dbd_config(void)
{
	dbd_config_list = acct_storage_g_get_config(db_conn);
}

static void _print_dbd_config(void)
{
	ListIterator iter = NULL;
	config_key_pair_t *key_pair;

	if (!dbd_config_list)
		return;

	printf("\nSlurmDBD configuration:\n");
	iter = list_iterator_create(dbd_config_list);
	while((key_pair = list_next(iter))) {
		printf("%-22s = %s\n", key_pair->name, key_pair->value);
	}
	list_iterator_destroy(iter);
}

static void _free_dbd_config(void)
{
	if (!dbd_config_list)
		return;

	list_destroy(dbd_config_list);
	dbd_config_list = NULL;
}

static void _load_slurm_config(void)
{
	acct_storage_host = slurm_get_accounting_storage_host();
	acct_storage_loc  = slurm_get_accounting_storage_loc();
	acct_storage_pass = slurm_get_accounting_storage_pass();
	acct_storage_port = slurm_get_accounting_storage_port();
	acct_storage_type = slurm_get_accounting_storage_type();
	acct_storage_user = slurm_get_accounting_storage_user();
	auth_type = slurm_get_auth_type();
	msg_timeout = slurm_get_msg_timeout();
	plugin_dir = slurm_get_plugin_dir();
	private_data = slurm_get_private_data();
	slurm_user_id = slurm_get_slurm_user_id();
	track_wckey = slurm_get_track_wckey();
}

static void _free_slurm_config(void)
{
	xfree(acct_storage_host);
	xfree(acct_storage_loc);
	xfree(acct_storage_pass);
	xfree(acct_storage_type);
	xfree(acct_storage_user);
	xfree(auth_type);
	xfree(plugin_dir);
}

static void _print_slurm_config(void)
{
	time_t now = time(NULL);
	char tmp_str[128], *user_name = NULL;

	slurm_make_time_str(&now, tmp_str, sizeof(tmp_str));
	printf("Configuration data as of %s\n", tmp_str);
	printf("AccountingStorageHost  = %s\n", acct_storage_host);
	printf("AccountingStorageLoc   = %s\n", acct_storage_loc);
	printf("AccountingStoragePass  = %s\n", acct_storage_pass);
	printf("AccountingStoragePort  = %u\n", acct_storage_port);
	printf("AccountingStorageType  = %s\n", acct_storage_type);
	printf("AccountingStorageUser  = %s\n", acct_storage_user);
	printf("AuthType               = %s\n", auth_type);
	printf("MessageTimeout         = %u sec\n", msg_timeout);
	printf("PluginDir              = %s\n", plugin_dir);
	private_data_string(private_data, tmp_str, sizeof(tmp_str));
	printf("PrivateData            = %s\n", tmp_str);
	user_name = uid_to_string(slurm_user_id);
	printf("SlurmUserId            = %s(%u)\n", user_name, slurm_user_id);
	xfree(user_name);
	printf("SLURM_CONF             = %s\n", default_slurm_config_file);
	printf("SLURM_VERSION          = %s\n", SLURM_VERSION);
	printf("TrackWCKey             = %u\n", track_wckey);
}

extern int sacctmgr_list_config(bool have_db_conn)
{
	_load_slurm_config();
	_print_slurm_config();
	_free_slurm_config();

	if (have_db_conn) {
		_load_dbd_config();
		_print_dbd_config();
		_free_dbd_config();
	}

	return SLURM_SUCCESS;
}
