/****************************************************************************\
 *  slurmdbd_defs.c - functions for use with Slurm DBD RPCs
 *****************************************************************************
 *  Copyright (C) 2011-2018 SchedMD LLC.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#include "src/common/slurmdbd_defs.h"
#include "src/common/slurm_jobacct_gather.h"

/*
 * Define slurm-specific aliases for use by plugins, see slurm_xlator.h
 * for details.
 */
strong_alias(slurmdbd_free_buffer,	slurm_slurmdbd_free_buffer);
strong_alias(slurmdbd_free_list_msg,	slurm_slurmdbd_free_list_msg);
strong_alias(slurmdbd_free_usage_msg,	slurm_slurmdbd_free_usage_msg);
strong_alias(slurmdbd_free_id_rc_msg,	slurm_slurmdbd_free_id_rc_msg);

extern slurmdbd_msg_type_t str_2_slurmdbd_msg_type(char *msg_type)
{
	if (!msg_type) {
		return NO_VAL;
	} else if (!xstrcasecmp(msg_type, "Init")) {
		return DBD_INIT;
	} else if (!xstrcasecmp(msg_type, "Fini")) {
		return DBD_FINI;
	} else if (!xstrcasecmp(msg_type, "Add Accounts")) {
		return DBD_ADD_ACCOUNTS;
	} else if (!xstrcasecmp(msg_type, "Add Account Coord")) {
		return DBD_ADD_ACCOUNT_COORDS;
	} else if (!xstrcasecmp(msg_type, "Add TRES")) {
		return DBD_ADD_TRES;
	} else if (!xstrcasecmp(msg_type, "Add Associations")) {
		return DBD_ADD_ASSOCS;
	} else if (!xstrcasecmp(msg_type, "Add Clusters")) {
		return DBD_ADD_CLUSTERS;
	} else if (!xstrcasecmp(msg_type, "Add Federations")) {
		return DBD_ADD_FEDERATIONS;
	} else if (!xstrcasecmp(msg_type, "Add Resources")) {
		return DBD_ADD_RES;
	} else if (!xstrcasecmp(msg_type, "Add Users")) {
		return DBD_ADD_USERS;
	} else if (!xstrcasecmp(msg_type, "Cluster TRES")) {
		return DBD_CLUSTER_TRES;
	} else if (!xstrcasecmp(msg_type, "Flush Jobs")) {
		return DBD_FLUSH_JOBS;
	} else if (!xstrcasecmp(msg_type, "Get Accounts")) {
		return DBD_GET_ACCOUNTS;
	} else if (!xstrcasecmp(msg_type, "Get TRES")) {
		return DBD_GET_TRES;
	} else if (!xstrcasecmp(msg_type, "Get Associations")) {
		return DBD_GET_ASSOCS;
	} else if (!xstrcasecmp(msg_type, "Get Association Usage")) {
		return DBD_GET_ASSOC_USAGE;
	} else if (!xstrcasecmp(msg_type, "Get Clusters")) {
		return DBD_GET_CLUSTERS;
	} else if (!xstrcasecmp(msg_type, "Get Cluster Usage")) {
		return DBD_GET_CLUSTER_USAGE;
	} else if (!xstrcasecmp(msg_type, "Get Events")) {
		return DBD_GET_EVENTS;
	} else if (!xstrcasecmp(msg_type, "Get Federations")) {
		return DBD_GET_FEDERATIONS;
	} else if (!xstrcasecmp(msg_type, "Reconfigure")) {
		return DBD_RECONFIG;
	} else if (!xstrcasecmp(msg_type, "Get Problems")) {
		return DBD_GET_PROBS;
	} else if (!xstrcasecmp(msg_type, "Get Resources")) {
		return DBD_GET_RES;
	} else if (!xstrcasecmp(msg_type, "Get Users")) {
		return DBD_GET_USERS;
	} else if (!xstrcasecmp(msg_type, "Got Accounts")) {
		return DBD_GOT_ACCOUNTS;
	} else if (!xstrcasecmp(msg_type, "Got TRES")) {
		return DBD_GOT_TRES;
	} else if (!xstrcasecmp(msg_type, "Got Associations")) {
		return DBD_GOT_ASSOCS;
	} else if (!xstrcasecmp(msg_type, "Got Association Usage")) {
		return DBD_GOT_ASSOC_USAGE;
	} else if (!xstrcasecmp(msg_type, "Got Clusters")) {
		return DBD_GOT_CLUSTERS;
	} else if (!xstrcasecmp(msg_type, "Got Cluster Usage")) {
		return DBD_GOT_CLUSTER_USAGE;
	} else if (!xstrcasecmp(msg_type, "Got Events")) {
		return DBD_GOT_EVENTS;
	} else if (!xstrcasecmp(msg_type, "Got Federations")) {
		return DBD_GOT_FEDERATIONS;
	} else if (!xstrcasecmp(msg_type, "Got Jobs")) {
		return DBD_GOT_JOBS;
	} else if (!xstrcasecmp(msg_type, "Got List")) {
		return DBD_GOT_LIST;
	} else if (!xstrcasecmp(msg_type, "Got Problems")) {
		return DBD_GOT_PROBS;
	} else if (!xstrcasecmp(msg_type, "Got Resources")) {
		return DBD_GOT_RES;
	} else if (!xstrcasecmp(msg_type, "Got Users")) {
		return DBD_GOT_USERS;
	} else if (!xstrcasecmp(msg_type, "Job Complete")) {
		return DBD_JOB_COMPLETE;
	} else if (!xstrcasecmp(msg_type, "Job Start")) {
		return DBD_JOB_START;
	} else if (!xstrcasecmp(msg_type, "ID RC")) {
		return DBD_ID_RC;
	} else if (!xstrcasecmp(msg_type, "Job Suspend")) {
		return DBD_JOB_SUSPEND;
	} else if (!xstrcasecmp(msg_type, "Modify Accounts")) {
		return DBD_MODIFY_ACCOUNTS;
	} else if (!xstrcasecmp(msg_type, "Modify Associations")) {
		return DBD_MODIFY_ASSOCS;
	} else if (!xstrcasecmp(msg_type, "Modify Clusters")) {
		return DBD_MODIFY_CLUSTERS;
	} else if (!xstrcasecmp(msg_type, "Modify Federations")) {
		return DBD_MODIFY_FEDERATIONS;
	} else if (!xstrcasecmp(msg_type, "Modify Job")) {
		return DBD_MODIFY_JOB;
	} else if (!xstrcasecmp(msg_type, "Modify QOS")) {
		return DBD_MODIFY_QOS;
	} else if (!xstrcasecmp(msg_type, "Modify Resources")) {
		return DBD_MODIFY_RES;
	} else if (!xstrcasecmp(msg_type, "Modify Users")) {
		return DBD_MODIFY_USERS;
	} else if (!xstrcasecmp(msg_type, "Node State")) {
		return DBD_NODE_STATE;
	} else if (!xstrcasecmp(msg_type, "Register Cluster")) {
		return DBD_REGISTER_CTLD;
	} else if (!xstrcasecmp(msg_type, "Remove Accounts")) {
		return DBD_REMOVE_ACCOUNTS;
	} else if (!xstrcasecmp(msg_type, "Remove Account Coords")) {
		return DBD_REMOVE_ACCOUNT_COORDS;
	} else if (!xstrcasecmp(msg_type, "Archive Dump")) {
		return DBD_ARCHIVE_DUMP;
	} else if (!xstrcasecmp(msg_type, "Archive Load")) {
		return DBD_ARCHIVE_LOAD;
	} else if (!xstrcasecmp(msg_type, "Remove Associations")) {
		return DBD_REMOVE_ASSOCS;
	} else if (!xstrcasecmp(msg_type, "Remove Clusters")) {
		return DBD_REMOVE_CLUSTERS;
	} else if (!xstrcasecmp(msg_type, "Remove Federations")) {
		return DBD_REMOVE_FEDERATIONS;
	} else if (!xstrcasecmp(msg_type, "Remove Resources")) {
		return DBD_REMOVE_RES;
	} else if (!xstrcasecmp(msg_type, "Remove Users")) {
		return DBD_REMOVE_USERS;
	} else if (!xstrcasecmp(msg_type, "Roll Usage")) {
		return DBD_ROLL_USAGE;
	} else if (!xstrcasecmp(msg_type, "Step Complete")) {
		return DBD_STEP_COMPLETE;
	} else if (!xstrcasecmp(msg_type, "Step Start")) {
		return DBD_STEP_START;
	} else if (!xstrcasecmp(msg_type, "Get Jobs Conditional")) {
		return DBD_GET_JOBS_COND;
	} else if (!xstrcasecmp(msg_type, "Get Transactions")) {
		return DBD_GET_TXN;
	} else if (!xstrcasecmp(msg_type, "Got Transactions")) {
		return DBD_GOT_TXN;
	} else if (!xstrcasecmp(msg_type, "Add QOS")) {
		return DBD_ADD_QOS;
	} else if (!xstrcasecmp(msg_type, "Get QOS")) {
		return DBD_GET_QOS;
	} else if (!xstrcasecmp(msg_type, "Got QOS")) {
		return DBD_GOT_QOS;
	} else if (!xstrcasecmp(msg_type, "Remove QOS")) {
		return DBD_REMOVE_QOS;
	} else if (!xstrcasecmp(msg_type, "Add WCKeys")) {
		return DBD_ADD_WCKEYS;
	} else if (!xstrcasecmp(msg_type, "Get WCKeys")) {
		return DBD_GET_WCKEYS;
	} else if (!xstrcasecmp(msg_type, "Got WCKeys")) {
		return DBD_GOT_WCKEYS;
	} else if (!xstrcasecmp(msg_type, "Remove WCKeys")) {
		return DBD_REMOVE_WCKEYS;
	} else if (!xstrcasecmp(msg_type, "Get WCKey Usage")) {
		return DBD_GET_WCKEY_USAGE;
	} else if (!xstrcasecmp(msg_type, "Got WCKey Usage")) {
		return DBD_GOT_WCKEY_USAGE;
	} else if (!xstrcasecmp(msg_type, "Add Reservation")) {
		return DBD_ADD_RESV;
	} else if (!xstrcasecmp(msg_type, "Remove Reservation")) {
		return DBD_REMOVE_RESV;
	} else if (!xstrcasecmp(msg_type, "Modify Reservation")) {
		return DBD_MODIFY_RESV;
	} else if (!xstrcasecmp(msg_type, "Get Reservations")) {
		return DBD_GET_RESVS;
	} else if (!xstrcasecmp(msg_type, "Got Reservations")) {
		return DBD_GOT_RESVS;
	} else if (!xstrcasecmp(msg_type, "Get Config")) {
		return DBD_GET_CONFIG;
	} else if (!xstrcasecmp(msg_type, "Got Config")) {
		return DBD_GOT_CONFIG;
	} else if (!xstrcasecmp(msg_type, "Send Multiple Job Starts")) {
		return DBD_SEND_MULT_JOB_START;
	} else if (!xstrcasecmp(msg_type, "Got Multiple Job Starts")) {
		return DBD_GOT_MULT_JOB_START;
	} else if (!xstrcasecmp(msg_type, "Send Multiple Messages")) {
		return DBD_SEND_MULT_MSG;
	} else if (!xstrcasecmp(msg_type, "Got Multiple Message Returns")) {
		return DBD_GOT_MULT_MSG;
	} else if (!xstrcasecmp(msg_type,
				"Persistent Connection Initialization")) {
		return SLURM_PERSIST_INIT;
	} else {
		return NO_VAL;
	}

	return NO_VAL;
}

extern char *slurmdbd_msg_type_2_str(slurmdbd_msg_type_t msg_type, int get_enum)
{
	static char unk_str[64];

	switch (msg_type) {
	case DBD_INIT:
		if (get_enum) {
			return "DBD_INIT";
		} else
			return "Init";
		break;
	case DBD_FINI:
		if (get_enum) {
			return "DBD_FINI";
		} else
			return "Fini";
		break;
	case DBD_ADD_ACCOUNTS:
		if (get_enum) {
			return "DBD_ADD_ACCOUNTS";
		} else
			return "Add Accounts";
		break;
	case DBD_ADD_ACCOUNT_COORDS:
		if (get_enum) {
			return "DBD_ADD_ACCOUNT_COORDS";
		} else
			return "Add Account Coord";
		break;
	case DBD_ADD_TRES:
		if (get_enum) {
			return "DBD_ADD_TRES";
		} else
			return "Add TRES";
		break;
	case DBD_ADD_ASSOCS:
		if (get_enum) {
			return "DBD_ADD_ASSOCS";
		} else
			return "Add Associations";
		break;
	case DBD_ADD_CLUSTERS:
		if (get_enum) {
			return "DBD_ADD_CLUSTERS";
		} else
			return "Add Clusters";
		break;
	case DBD_ADD_FEDERATIONS:
		if (get_enum) {
			return "DBD_ADD_FEDERATIONS";
		} else
			return "Add Clusters";
		break;
	case DBD_ADD_RES:
		if (get_enum) {
			return "DBD_ADD_RES";
		} else
			return "Add Resources";
		break;
	case DBD_ADD_USERS:
		if (get_enum) {
			return "DBD_ADD_USERS";
		} else
			return "Add Users";
		break;
	case DBD_CLUSTER_TRES:
		if (get_enum) {
			return "DBD_CLUSTER_TRES";
		} else
			return "Cluster TRES";
		break;
	case DBD_FLUSH_JOBS:
		if (get_enum) {
			return "DBD_FLUSH_JOBS";
		} else
			return "Flush Jobs";
		break;
	case DBD_GET_ACCOUNTS:
		if (get_enum) {
			return "DBD_GET_ACCOUNTS";
		} else
			return "Get Accounts";
		break;
	case DBD_GET_TRES:
		if (get_enum) {
			return "DBD_GET_TRES";
		} else
			return "Get TRES";
		break;
	case DBD_GET_ASSOCS:
		if (get_enum) {
			return "DBD_GET_ASSOCS";
		} else
			return "Get Associations";
		break;
	case DBD_GET_ASSOC_USAGE:
		if (get_enum) {
			return "DBD_GET_ASSOC_USAGE";
		} else
			return "Get Association Usage";
		break;
	case DBD_GET_CLUSTERS:
		if (get_enum) {
			return "DBD_GET_CLUSTERS";
		} else
			return "Get Clusters";
		break;
	case DBD_GET_CLUSTER_USAGE:
		if (get_enum) {
			return "DBD_GET_CLUSTER_USAGE";
		} else
			return "Get Cluster Usage";
		break;
	case DBD_GET_EVENTS:
		if (get_enum) {
			return "DBD_GET_EVENTS";
		} else
			return "Get Events";
		break;
	case DBD_GET_FEDERATIONS:
		if (get_enum) {
			return "DBD_GET_FEDERATIONS";
		} else
			return "Get Federations";
		break;
	case DBD_RECONFIG:
		if (get_enum) {
			return "DBD_RECONFIG";
		} else
			return "Reconfigure";
		break;
	case DBD_GET_PROBS:
		if (get_enum) {
			return "DBD_GET_PROBS";
		} else
			return "Get Problems";
		break;
	case DBD_GET_RES:
		if (get_enum) {
			return "DBD_GET_RES";
		} else
			return "Get Resources";
		break;
	case DBD_GET_USERS:
		if (get_enum) {
			return "DBD_GET_USERS";
		} else
			return "Get Users";
		break;
	case DBD_GOT_ACCOUNTS:
		if (get_enum) {
			return "DBD_GOT_ACCOUNTS";
		} else
			return "Got Accounts";
		break;
	case DBD_GOT_TRES:
		if (get_enum) {
			return "DBD_GOT_TRES";
		} else
			return "Got TRES";
		break;
	case DBD_GOT_ASSOCS:
		if (get_enum) {
			return "DBD_GOT_ASSOCS";
		} else
			return "Got Associations";
		break;
	case DBD_GOT_ASSOC_USAGE:
		if (get_enum) {
			return "DBD_GOT_ASSOC_USAGE";
		} else
			return "Got Association Usage";
		break;
	case DBD_GOT_CLUSTERS:
		if (get_enum) {
			return "DBD_GOT_CLUSTERS";
		} else
			return "Got Clusters";
		break;
	case DBD_GOT_CLUSTER_USAGE:
		if (get_enum) {
			return "DBD_GOT_CLUSTER_USAGE";
		} else
			return "Got Cluster Usage";
		break;
	case DBD_GOT_EVENTS:
		if (get_enum) {
			return "DBD_GOT_EVENTS";
		} else
			return "Got Events";
		break;
	case DBD_GOT_FEDERATIONS:
		if (get_enum) {
			return "DBD_GOT_FEDERATIONS";
		} else
			return "Got Federations";
		break;
	case DBD_GOT_JOBS:
		if (get_enum) {
			return "DBD_GOT_JOBS";
		} else
			return "Got Jobs";
		break;
	case DBD_GOT_LIST:
		if (get_enum) {
			return "DBD_GOT_LIST";
		} else
			return "Got List";
		break;
	case DBD_GOT_PROBS:
		if (get_enum) {
			return "DBD_GOT_PROBS";
		} else
			return "Got Problems";
		break;
	case DBD_GOT_RES:
		if (get_enum) {
			return "DBD_GOT_RES";
		} else
			return "Got Resources";
		break;
	case DBD_GOT_USERS:
		if (get_enum) {
			return "DBD_GOT_USERS";
		} else
			return "Got Users";
		break;
	case DBD_JOB_COMPLETE:
		if (get_enum) {
			return "DBD_JOB_COMPLETE";
		} else
			return "Job Complete";
		break;
	case DBD_JOB_START:
		if (get_enum) {
			return "DBD_JOB_START";
		} else
			return "Job Start";
		break;
	case DBD_ID_RC:
		if (get_enum) {
			return "DBD_ID_RC";
		} else
			return "ID RC";
		break;
	case DBD_JOB_SUSPEND:
		if (get_enum) {
			return "DBD_JOB_SUSPEND";
		} else
			return "Job Suspend";
		break;
	case DBD_MODIFY_ACCOUNTS:
		if (get_enum) {
			return "DBD_MODIFY_ACCOUNTS";
		} else
			return "Modify Accounts";
		break;
	case DBD_MODIFY_ASSOCS:
		if (get_enum) {
			return "DBD_MODIFY_ASSOCS";
		} else
			return "Modify Associations";
		break;
	case DBD_MODIFY_CLUSTERS:
		if (get_enum) {
			return "DBD_MODIFY_CLUSTERS";
		} else
			return "Modify Clusters";
		break;
	case DBD_MODIFY_FEDERATIONS:
		if (get_enum) {
			return "DBD_MODIFY_FEDERATIONS";
		} else
			return "Modify Federations";
		break;
	case DBD_MODIFY_JOB:
		if (get_enum) {
			return "DBD_MODIFY_JOB";
		} else
			return "Modify Job";
		break;
	case DBD_MODIFY_QOS:
		if (get_enum) {
			return "DBD_MODIFY_QOS";
		} else
			return "Modify QOS";
		break;
	case DBD_MODIFY_RES:
		if (get_enum) {
			return "DBD_MODIFY_RES";
		} else
			return "Modify Resources";
		break;
	case DBD_MODIFY_USERS:
		if (get_enum) {
			return "DBD_MODIFY_USERS";
		} else
			return "Modify Users";
		break;
	case DBD_NODE_STATE:
		if (get_enum) {
			return "DBD_NODE_STATE";
		} else
			return "Node State";
		break;
	case DBD_REGISTER_CTLD:
		if (get_enum) {
			return "DBD_REGISTER_CTLD";
		} else
			return "Register Cluster";
		break;
	case DBD_REMOVE_ACCOUNTS:
		if (get_enum) {
			return "DBD_REMOVE_ACCOUNTS";
		} else
			return "Remove Accounts";
		break;
	case DBD_REMOVE_ACCOUNT_COORDS:
		if (get_enum) {
			return "DBD_REMOVE_ACCOUNT_COORDS";
		} else
			return "Remove Account Coords";
		break;
	case DBD_ARCHIVE_DUMP:
		if (get_enum) {
			return "DBD_ARCHIVE_DUMP";
		} else
			return "Archive Dump";
		break;
	case DBD_ARCHIVE_LOAD:
		if (get_enum) {
			return "DBD_ARCHIVE_LOAD";
		} else
			return "Archive Load";
		break;
	case DBD_REMOVE_ASSOCS:
		if (get_enum) {
			return "DBD_REMOVE_ASSOCS";
		} else
			return "Remove Associations";
		break;
	case DBD_REMOVE_CLUSTERS:
		if (get_enum) {
			return "DBD_REMOVE_CLUSTERS";
		} else
			return "Remove Clusters";
		break;
	case DBD_REMOVE_FEDERATIONS:
		if (get_enum) {
			return "DBD_REMOVE_FEDERATIONS";
		} else
			return "Remove Federations";
		break;
	case DBD_REMOVE_RES:
		if (get_enum) {
			return "DBD_REMOVE_RES";
		} else
			return "Remove Resources";
		break;
	case DBD_REMOVE_USERS:
		if (get_enum) {
			return "DBD_REMOVE_USERS";
		} else
			return "Remove Users";
		break;
	case DBD_ROLL_USAGE:
		if (get_enum) {
			return "DBD_ROLL_USAGE";
		} else
			return "Roll Usage";
		break;
	case DBD_STEP_COMPLETE:
		if (get_enum) {
			return "DBD_STEP_COMPLETE";
		} else
			return "Step Complete";
		break;
	case DBD_STEP_START:
		if (get_enum) {
			return "DBD_STEP_START";
		} else
			return "Step Start";
		break;
	case DBD_GET_JOBS_COND:
		if (get_enum) {
			return "DBD_GET_JOBS_COND";
		} else
			return "Get Jobs Conditional";
		break;
	case DBD_GET_TXN:
		if (get_enum) {
			return "DBD_GET_TXN";
		} else
			return "Get Transactions";
		break;
	case DBD_GOT_TXN:
		if (get_enum) {
			return "DBD_GOT_TXN";
		} else
			return "Got Transactions";
		break;
	case DBD_ADD_QOS:
		if (get_enum) {
			return "DBD_ADD_QOS";
		} else
			return "Add QOS";
		break;
	case DBD_GET_QOS:
		if (get_enum) {
			return "DBD_GET_QOS";
		} else
			return "Get QOS";
		break;
	case DBD_GOT_QOS:
		if (get_enum) {
			return "DBD_GOT_QOS";
		} else
			return "Got QOS";
		break;
	case DBD_REMOVE_QOS:
		if (get_enum) {
			return "DBD_REMOVE_QOS";
		} else
			return "Remove QOS";
		break;
	case DBD_ADD_WCKEYS:
		if (get_enum) {
			return "DBD_ADD_WCKEYS";
		} else
			return "Add WCKeys";
		break;
	case DBD_GET_WCKEYS:
		if (get_enum) {
			return "DBD_GET_WCKEYS";
		} else
			return "Get WCKeys";
		break;
	case DBD_GOT_WCKEYS:
		if (get_enum) {
			return "DBD_GOT_WCKEYS";
		} else
			return "Got WCKeys";
		break;
	case DBD_REMOVE_WCKEYS:
		if (get_enum) {
			return "DBD_REMOVE_WCKEYS";
		} else
			return "Remove WCKeys";
		break;
	case DBD_GET_WCKEY_USAGE:
		if (get_enum) {
			return "DBD_GET_WCKEY_USAGE";
		} else
			return "Get WCKey Usage";
		break;
	case DBD_GOT_WCKEY_USAGE:
		if (get_enum) {
			return "DBD_GOT_WCKEY_USAGE";
		} else
			return "Got WCKey Usage";
		break;
	case DBD_ADD_RESV:
		if (get_enum) {
			return "DBD_ADD_RESV";
		} else
			return "Add Reservation";
		break;
	case DBD_REMOVE_RESV:
		if (get_enum) {
			return "DBD_REMOVE_RESV";
		} else
			return "Remove Reservation";
		break;
	case DBD_MODIFY_RESV:
		if (get_enum) {
			return "DBD_MODIFY_RESV";
		} else
			return "Modify Reservation";
		break;
	case DBD_GET_RESVS:
		if (get_enum) {
			return "DBD_GET_RESVS";
		} else
			return "Get Reservations";
		break;
	case DBD_GOT_RESVS:
		if (get_enum) {
			return "DBD_GOT_RESVS";
		} else
			return "Got Reservations";
		break;
	case DBD_GET_CONFIG:
		if (get_enum) {
			return "DBD_GET_CONFIG";
		} else
			return "Get Config";
		break;
	case DBD_GOT_CONFIG:
		if (get_enum) {
			return "DBD_GOT_CONFIG";
		} else
			return "Got Config";
		break;
	case DBD_SEND_MULT_JOB_START:
		if (get_enum) {
			return "DBD_SEND_MULT_JOB_START";
		} else
			return "Send Multiple Job Starts";
		break;
	case DBD_GOT_MULT_JOB_START:
		if (get_enum) {
			return "DBD_GOT_MULT_JOB_START";
		} else
			return "Got Multiple Job Starts";
		break;
	case DBD_SEND_MULT_MSG:
		if (get_enum) {
			return "DBD_SEND_MULT_MSG";
		} else
			return "Send Multiple Messages";
		break;
	case DBD_GOT_MULT_MSG:
		if (get_enum) {
			return "DBD_GOT_MULT_MSG";
		} else
			return "Got Multiple Message Returns";
		break;
	case DBD_GET_STATS:
		if (get_enum) {
			return "DBD_GET_STATS";
		} else
			return "Get daemon statistics";
		break;
	case DBD_GOT_STATS:
		if (get_enum) {
			return "DBD_GOT_STATS";
		} else
			return "Got daemon statistics data";
		break;
	case DBD_CLEAR_STATS:
		if (get_enum) {
			return "DBD_CLEAR_STATS";
		} else
			return "Clear daemon statistics";
		break;
	case DBD_SHUTDOWN:
		if (get_enum) {
			return "DBD_SHUTDOWN";
		} else
			return "Shutdown daemon";
		break;
	case SLURM_PERSIST_INIT:
		if (get_enum) {
			return "SLURM_PERSIST_INIT";
		} else
			return "Persistent Connection Initialization";
		break;
	default:
		snprintf(unk_str, sizeof(unk_str), "MsgType=%d", msg_type);
		return unk_str;
		break;
	}
}


/****************************************************************************\
 * Free data structures
\****************************************************************************/
extern void slurmdbd_free_buffer(void *x)
{
	Buf buffer = (Buf) x;
	if (buffer)
		free_buf(buffer);
}

extern void slurmdbd_free_acct_coord_msg(dbd_acct_coord_msg_t *msg)
{
	if (msg) {
		FREE_NULL_LIST(msg->acct_list);
		slurmdb_destroy_user_cond(msg->cond);
		xfree(msg);
	}
}

extern void slurmdbd_free_cluster_tres_msg(dbd_cluster_tres_msg_t *msg)
{
	if (msg) {
		xfree(msg->cluster_nodes);
		xfree(msg->tres_str);
		xfree(msg);
	}
}

extern void slurmdbd_free_msg(slurmdbd_msg_t *msg)
{
	switch (msg->msg_type) {
	case DBD_ADD_ACCOUNTS:
	case DBD_ADD_TRES:
	case DBD_ADD_ASSOCS:
	case DBD_ADD_CLUSTERS:
	case DBD_ADD_FEDERATIONS:
	case DBD_ADD_RES:
	case DBD_ADD_USERS:
	case DBD_GOT_ACCOUNTS:
	case DBD_GOT_TRES:
	case DBD_GOT_ASSOCS:
	case DBD_GOT_CLUSTERS:
	case DBD_GOT_EVENTS:
	case DBD_GOT_FEDERATIONS:
	case DBD_GOT_JOBS:
	case DBD_GOT_LIST:
	case DBD_GOT_PROBS:
	case DBD_GOT_RES:
	case DBD_ADD_QOS:
	case DBD_GOT_QOS:
	case DBD_GOT_RESVS:
	case DBD_ADD_WCKEYS:
	case DBD_GOT_WCKEYS:
	case DBD_GOT_TXN:
	case DBD_GOT_USERS:
	case DBD_GOT_CONFIG:
	case DBD_SEND_MULT_JOB_START:
	case DBD_GOT_MULT_JOB_START:
	case DBD_SEND_MULT_MSG:
	case DBD_GOT_MULT_MSG:
	case DBD_FIX_RUNAWAY_JOB:
		slurmdbd_free_list_msg(msg->data);
		break;
	case DBD_ADD_ACCOUNT_COORDS:
	case DBD_REMOVE_ACCOUNT_COORDS:
		slurmdbd_free_acct_coord_msg(msg->data);
		break;
	case DBD_ARCHIVE_LOAD:
		slurmdb_destroy_archive_rec(msg->data);
		break;
	case DBD_CLUSTER_TRES:
	case DBD_FLUSH_JOBS:
		slurmdbd_free_cluster_tres_msg(msg->data);
		break;
	case DBD_GET_ACCOUNTS:
	case DBD_GET_TRES:
	case DBD_GET_ASSOCS:
	case DBD_GET_CLUSTERS:
	case DBD_GET_EVENTS:
	case DBD_GET_FEDERATIONS:
	case DBD_GET_JOBS_COND:
	case DBD_GET_PROBS:
	case DBD_GET_QOS:
	case DBD_GET_RESVS:
	case DBD_GET_RES:
	case DBD_GET_TXN:
	case DBD_GET_USERS:
	case DBD_GET_WCKEYS:
	case DBD_REMOVE_ACCOUNTS:
	case DBD_REMOVE_ASSOCS:
	case DBD_REMOVE_CLUSTERS:
	case DBD_REMOVE_FEDERATIONS:
	case DBD_REMOVE_QOS:
	case DBD_REMOVE_RES:
	case DBD_REMOVE_WCKEYS:
	case DBD_REMOVE_USERS:
	case DBD_ARCHIVE_DUMP:
		slurmdbd_free_cond_msg(msg->data, msg->msg_type);
		break;
	case DBD_GET_ASSOC_USAGE:
	case DBD_GOT_ASSOC_USAGE:
	case DBD_GET_CLUSTER_USAGE:
	case DBD_GOT_CLUSTER_USAGE:
	case DBD_GET_WCKEY_USAGE:
	case DBD_GOT_WCKEY_USAGE:
		slurmdbd_free_usage_msg(msg->data, msg->msg_type);
		break;
	case DBD_INIT:
		slurmdbd_free_init_msg(msg->data);
		break;
	case DBD_FINI:
		slurmdbd_free_fini_msg(msg->data);
		break;
	case DBD_JOB_COMPLETE:
		slurmdbd_free_job_complete_msg(msg->data);
		break;
	case DBD_JOB_START:
		slurmdbd_free_job_start_msg(msg->data);
		break;
	case DBD_JOB_SUSPEND:
		slurmdbd_free_job_suspend_msg(msg->data);
		break;
	case DBD_MODIFY_ACCOUNTS:
	case DBD_MODIFY_ASSOCS:
	case DBD_MODIFY_CLUSTERS:
	case DBD_MODIFY_FEDERATIONS:
	case DBD_MODIFY_JOB:
	case DBD_MODIFY_QOS:
	case DBD_MODIFY_RES:
	case DBD_MODIFY_USERS:
		slurmdbd_free_modify_msg(msg->data, msg->msg_type);
		break;
	case DBD_NODE_STATE:
		slurmdbd_free_node_state_msg(msg->data);
		break;
	case DBD_STEP_COMPLETE:
		slurmdbd_free_step_complete_msg(msg->data);
		break;
	case DBD_STEP_START:
		slurmdbd_free_step_start_msg(msg->data);
		break;
	case DBD_REGISTER_CTLD:
		slurmdbd_free_register_ctld_msg(msg->data);
		break;
	case DBD_ROLL_USAGE:
		slurmdbd_free_roll_usage_msg(msg->data);
		break;
	case DBD_ADD_RESV:
	case DBD_REMOVE_RESV:
	case DBD_MODIFY_RESV:
		slurmdbd_free_rec_msg(msg->data, msg->msg_type);
		break;
	case DBD_GET_CONFIG:
	case DBD_RECONFIG:
	case DBD_GET_STATS:
	case DBD_CLEAR_STATS:
	case DBD_SHUTDOWN:
		break;
	case SLURM_PERSIST_INIT:
		slurm_free_msg(msg->data);
		break;
	default:
		error("%s: Unknown rec type %d(%s)",
		      __func__, msg->msg_type,
		      slurmdbd_msg_type_2_str(msg->msg_type, true));
		return;
	}
}

extern void slurmdbd_free_rec_msg(dbd_rec_msg_t *msg,
				  slurmdbd_msg_type_t type)
{
	void (*my_destroy) (void *object);

	if (msg) {
		switch (type) {
		case DBD_ADD_RESV:
		case DBD_REMOVE_RESV:
		case DBD_MODIFY_RESV:
			my_destroy = slurmdb_destroy_reservation_rec;
			break;
		default:
			fatal("Unknown rec type");
			return;
		}
		if (msg->rec)
			(*(my_destroy))(msg->rec);
		xfree(msg);
	}
}

extern void slurmdbd_free_cond_msg(dbd_cond_msg_t *msg,
				   slurmdbd_msg_type_t type)
{
	void (*my_destroy) (void *object);

	if (msg) {
		switch (type) {
		case DBD_GET_ACCOUNTS:
		case DBD_REMOVE_ACCOUNTS:
			my_destroy = slurmdb_destroy_account_cond;
			break;
		case DBD_GET_TRES:
			my_destroy = slurmdb_destroy_tres_cond;
			break;
		case DBD_GET_ASSOCS:
		case DBD_GET_PROBS:
		case DBD_REMOVE_ASSOCS:
			my_destroy = slurmdb_destroy_assoc_cond;
			break;
		case DBD_GET_CLUSTERS:
		case DBD_REMOVE_CLUSTERS:
			my_destroy = slurmdb_destroy_cluster_cond;
			break;
		case DBD_GET_FEDERATIONS:
		case DBD_REMOVE_FEDERATIONS:
			my_destroy = slurmdb_destroy_federation_cond;
			break;
		case DBD_GET_JOBS_COND:
			my_destroy = slurmdb_destroy_job_cond;
			break;
		case DBD_GET_QOS:
		case DBD_REMOVE_QOS:
			my_destroy = slurmdb_destroy_qos_cond;
			break;
		case DBD_GET_RES:
		case DBD_REMOVE_RES:
			my_destroy = slurmdb_destroy_res_cond;
			break;
		case DBD_GET_WCKEYS:
		case DBD_REMOVE_WCKEYS:
			my_destroy = slurmdb_destroy_wckey_cond;
			break;
		case DBD_GET_TXN:
			my_destroy = slurmdb_destroy_txn_cond;
			break;
		case DBD_GET_USERS:
		case DBD_REMOVE_USERS:
			my_destroy = slurmdb_destroy_user_cond;
			break;
		case DBD_ARCHIVE_DUMP:
			my_destroy = slurmdb_destroy_archive_cond;
			break;
		case DBD_GET_RESVS:
			my_destroy = slurmdb_destroy_reservation_cond;
			break;
		case DBD_GET_EVENTS:
			my_destroy = slurmdb_destroy_event_cond;
			break;
		default:
			fatal("Unknown cond type");
			return;
		}
		if (msg->cond)
			(*(my_destroy))(msg->cond);
		xfree(msg);
	}
}

extern void slurmdbd_free_init_msg(dbd_init_msg_t *msg)
{
	if (msg) {
		xfree(msg->cluster_name);
		xfree(msg);
	}
}

extern void slurmdbd_free_fini_msg(dbd_fini_msg_t *msg)
{
	xfree(msg);
}

extern void slurmdbd_free_job_complete_msg(dbd_job_comp_msg_t *msg)
{
	if (msg) {
		xfree(msg->admin_comment);
		xfree(msg->comment);
		xfree(msg->nodes);
		xfree(msg->system_comment);
		xfree(msg->tres_alloc_str);
		xfree(msg);
	}
}

extern void slurmdbd_free_job_start_msg(void *in)
{
	dbd_job_start_msg_t *msg = (dbd_job_start_msg_t *)in;
	if (msg) {
		xfree(msg->account);
		xfree(msg->array_task_str);
		xfree(msg->constraints);
		xfree(msg->gres_alloc);
		xfree(msg->gres_req);
		xfree(msg->gres_used);
		xfree(msg->mcs_label);
		xfree(msg->name);
		xfree(msg->nodes);
		xfree(msg->node_inx);
		xfree(msg->partition);
		xfree(msg->tres_alloc_str);
		xfree(msg->tres_req_str);
		xfree(msg->wckey);
		xfree(msg->work_dir);
		xfree(msg);
	}
}

extern void slurmdbd_free_id_rc_msg(void *in)
{
	dbd_id_rc_msg_t *msg = (dbd_id_rc_msg_t *)in;
	xfree(msg);
}

extern void slurmdbd_free_job_suspend_msg(dbd_job_suspend_msg_t *msg)
{
	xfree(msg);
}

extern void slurmdbd_free_list_msg(dbd_list_msg_t *msg)
{
	if (msg) {
		FREE_NULL_LIST(msg->my_list);
		xfree(msg);
	}
}

extern void slurmdbd_free_modify_msg(dbd_modify_msg_t *msg,
				     slurmdbd_msg_type_t type)
{
	void (*destroy_cond) (void *object);
	void (*destroy_rec) (void *object);

	if (msg) {
		switch (type) {
		case DBD_MODIFY_ACCOUNTS:
			destroy_cond = slurmdb_destroy_account_cond;
			destroy_rec = slurmdb_destroy_account_rec;
			break;
		case DBD_MODIFY_ASSOCS:
			destroy_cond = slurmdb_destroy_assoc_cond;
			destroy_rec = slurmdb_destroy_assoc_rec;
			break;
		case DBD_MODIFY_CLUSTERS:
			destroy_cond = slurmdb_destroy_cluster_cond;
			destroy_rec = slurmdb_destroy_cluster_rec;
			break;
		case DBD_MODIFY_FEDERATIONS:
			destroy_cond = slurmdb_destroy_federation_cond;
			destroy_rec = slurmdb_destroy_federation_rec;
			break;
		case DBD_MODIFY_JOB:
			destroy_cond = slurmdb_destroy_job_modify_cond;
			destroy_rec = slurmdb_destroy_job_rec;
			break;
		case DBD_MODIFY_QOS:
			destroy_cond = slurmdb_destroy_qos_cond;
			destroy_rec = slurmdb_destroy_qos_rec;
			break;
		case DBD_MODIFY_RES:
			destroy_cond = slurmdb_destroy_res_cond;
			destroy_rec = slurmdb_destroy_res_rec;
			break;
		case DBD_MODIFY_USERS:
			destroy_cond = slurmdb_destroy_user_cond;
			destroy_rec = slurmdb_destroy_user_rec;
			break;
		default:
			fatal("Unknown modify type");
			return;
		}

		if (msg->cond)
			(*(destroy_cond))(msg->cond);
		if (msg->rec)
			(*(destroy_rec))(msg->rec);
		xfree(msg);
	}
}

extern void slurmdbd_free_node_state_msg(dbd_node_state_msg_t *msg)
{
	if (msg) {
		xfree(msg->hostlist);
		xfree(msg->reason);
		xfree(msg->tres_str);
		xfree(msg);
	}
}

extern void slurmdbd_free_register_ctld_msg(dbd_register_ctld_msg_t *msg)
{
	xfree(msg);
}

extern void slurmdbd_free_roll_usage_msg(dbd_roll_usage_msg_t *msg)
{
	xfree(msg);
}

extern void slurmdbd_free_step_complete_msg(dbd_step_comp_msg_t *msg)
{
	if (msg) {
		jobacctinfo_destroy(msg->jobacct);
		xfree(msg->job_tres_alloc_str);
		xfree(msg);
	}
}

extern void slurmdbd_free_step_start_msg(dbd_step_start_msg_t *msg)
{
	if (msg) {
		xfree(msg->name);
		xfree(msg->nodes);
		xfree(msg->node_inx);
		xfree(msg->tres_alloc_str);
		xfree(msg);
	}
}

extern void slurmdbd_free_usage_msg(dbd_usage_msg_t *msg,
				    slurmdbd_msg_type_t type)
{
	void (*destroy_rec) (void *object);
	if (msg) {
		switch (type) {
		case DBD_GET_ASSOC_USAGE:
		case DBD_GOT_ASSOC_USAGE:
			destroy_rec = slurmdb_destroy_assoc_rec;
			break;
		case DBD_GET_CLUSTER_USAGE:
		case DBD_GOT_CLUSTER_USAGE:
			destroy_rec = slurmdb_destroy_cluster_rec;
			break;
		case DBD_GET_WCKEY_USAGE:
		case DBD_GOT_WCKEY_USAGE:
			destroy_rec = slurmdb_destroy_wckey_rec;
			break;
		default:
			fatal("Unknown usuage type");
			return;
		}

		if (msg->rec)
			(*(destroy_rec))(msg->rec);
		xfree(msg);
	}
}
