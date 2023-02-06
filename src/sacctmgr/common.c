/*****************************************************************************\
 *  common.c - definitions for functions common to all modules in sacctmgr.
 *****************************************************************************
 *  Copyright (C) 2010-2015 SchedMD LLC.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Copyright (C) 2002-2007 The Regents of the University of California.
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

#include "src/sacctmgr/sacctmgr.h"
#include "src/common/slurmdbd_defs.h"
#include "src/interfaces/auth.h"
#include "src/common/slurm_protocol_defs.h"

#include <unistd.h>
#include <termios.h>

static pthread_t lock_warning_thread;

static void *_print_lock_warn(void *no_data)
{
	sleep(5);
	printf(" Database is busy or waiting for lock from other user.\n");

	return NULL;
}

static void _nonblock(int state)
{
	struct termios ttystate;

	//get the terminal state
	tcgetattr(STDIN_FILENO, &ttystate);

	switch(state) {
	case 1:
		//turn off canonical mode
		ttystate.c_lflag &= ~ICANON;
		//minimum of number input read.
		ttystate.c_cc[VMIN] = 1;
		break;
	default:
		//turn on canonical mode
		ttystate.c_lflag |= ICANON;
	}
	//set the terminal attributes.
	tcsetattr(STDIN_FILENO, TCSANOW, &ttystate);

}

extern int parse_option_end(char *option)
{
	int end = 0;

	if (!option)
		return 0;

	while(option[end]) {
		if ((option[end] == '=')
		   || (option[end] == '+' && option[end+1] == '=')
		   || (option[end] == '-' && option[end+1] == '='))
			break;
		end++;
	}

	if (!option[end])
		return 0;

	end++;
	return end;
}

/* you need to xfree whatever is sent from here */
extern char *strip_quotes(char *option, int *increased, bool make_lower)
{
	int end = 0;
	int i=0, start=0;
	char *meat = NULL;
	char quote_c = '\0';
	int quote = 0;

	if (!option)
		return NULL;

	/* first strip off the ("|')'s */
	if (option[i] == '\"' || option[i] == '\'') {
		quote_c = option[i];
		quote = 1;
		i++;
	}
	start = i;

	while(option[i]) {
		if (quote && option[i] == quote_c) {
			end++;
			break;
		} else if (option[i] == '\"' || option[i] == '\'')
			option[i] = '`';
		else if (make_lower) {
			char lower = tolower(option[i]);
			if (lower != option[i])
				option[i] = lower;
		}

		i++;
	}
	end += i;

	meat = xmalloc((i-start)+1);
	memcpy(meat, option+start, (i-start));

	if (increased)
		(*increased) += end;

	return meat;
}

static print_field_t *_get_print_field(char *object)
{
	/* This should be kept in alpha order to avoid picking the
	   wrong field name.
	*/
	print_field_t *field = xmalloc(sizeof(print_field_t));
	char *tmp_char = NULL;
	int command_len, field_len = 0;

	if ((tmp_char = strstr(object, "\%"))) {
		field_len = atoi(tmp_char+1);
		tmp_char[0] = '\0';
	}
	command_len = strlen(object);

	if (!xstrncasecmp("Account", object, MAX(command_len, 3))
	    || !xstrncasecmp("Acct", object, MAX(command_len, 4))) {
		field->type = PRINT_ACCT;
		field->name = xstrdup("Account");
		if (tree_display)
			field->len = -20;
		else
			field->len = 10;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("ActionRaw", object, MAX(command_len, 7))) {
		field->type = PRINT_ACTIONRAW;
		field->name = xstrdup("ActionRaw");
		field->len = 10;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("Action", object, MAX(command_len, 4))) {
		field->type = PRINT_ACTION;
		field->name = xstrdup("Action");
		field->len = 20;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("Actor", object, MAX(command_len, 4))) {
		field->type = PRINT_ACTOR;
		field->name = xstrdup("Actor");
		field->len = 10;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("AdminLevel", object, MAX(command_len, 2))) {
		field->type = PRINT_ADMIN;
		field->name = xstrdup("Admin");
		field->len = 9;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("Allowed", object, MAX(command_len, 2))) {
		field->type = PRINT_ALLOWED;
		field->name = xstrdup("Allowed");
		field->len = 8;
		field->print_routine = print_fields_uint32;
	} else if (!xstrncasecmp("Associations", object, MAX(command_len, 2))) {
		field->type = PRINT_ASSOC_NAME;
		field->name = xstrdup("Assocs");
		field->len = 10;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("TRES", object, MAX(command_len, 2))) {
		field->type = PRINT_TRES;
		field->name = xstrdup("TRES");
		field->len = 20;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("Classification", object,
				 MAX(command_len, 3))) {
		field->type = PRINT_CLASS;
		field->name = xstrdup("Class");
		field->len = 9;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("ClusterNodes", object, MAX(command_len, 8))) {
		field->type = PRINT_CLUSTER_NODES;
		field->name = xstrdup("Cluster Nodes");
		field->len = 20;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("Clusters", object, MAX(command_len, 2))) {
		field->type = PRINT_CLUSTER;
		field->name = xstrdup("Cluster");
		field->len = 10;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("Coordinators", object, MAX(command_len, 3))) {
		field->type = PRINT_COORDS;
		field->name = xstrdup("Coord Accounts");
		field->len = 20;
		field->print_routine = sacctmgr_print_coord_list;
	} else if (!xstrncasecmp("Comment", object, MAX(command_len, 3))) {
		field->type = PRINT_COMMENT;
		field->name = xstrdup("Comment");
		field->len = 20;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("ControlHost", object, MAX(command_len, 8))) {
		field->type = PRINT_CHOST;
		field->name = xstrdup("ControlHost");
		field->len = 15;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("ControlPort", object, MAX(command_len, 8))) {
		field->type = PRINT_CPORT;
		field->name = xstrdup("ControlPort");
		field->len = 12;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("Count", object, MAX(command_len, 3))) {
		field->type = PRINT_COUNT;
		field->name = xstrdup("Count");
		field->len = 6;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("CountAllowed", object, MAX(command_len, 6))) {
		field->type = PRINT_CALLOWED;
		field->name = xstrdup("# Allowed");
		field->len = 10;
		field->print_routine = print_fields_uint32;
	} else if (!xstrncasecmp("CountUsed", object, MAX(command_len, 6))) {
		field->type = PRINT_CALLOWED;
		field->name = xstrdup("# Used");
		field->len = 10;
		field->print_routine = print_fields_uint32;
	} else if (!xstrncasecmp("CPUCount", object,
				MAX(command_len, 2))) {
		field->type = PRINT_CPUS;
		field->name = xstrdup("CPU Cnt");
		field->len = 7;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("DefaultAccount", object,
				 MAX(command_len, 8))) {
		field->type = PRINT_DACCT;
		field->name = xstrdup("Def Acct");
		field->len = 10;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("DefaultQOS", object, MAX(command_len, 8))) {
		field->type = PRINT_DQOS;
		field->name = xstrdup("Def QOS");
		field->len = 9;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("DefaultWCKey", object, MAX(command_len, 8))) {
		field->type = PRINT_DWCKEY;
		field->name = xstrdup("Def WCKey");
		field->len = 10;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("Description", object, MAX(command_len, 3))) {
		field->type = PRINT_DESC;
		field->name = xstrdup("Descr");
		field->len = 20;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("Duration", object, MAX(command_len, 2))) {
		field->type = PRINT_DURATION;
		field->name = xstrdup("Duration");
		field->len = 13;
		field->print_routine = print_fields_time_from_secs;
	} else if (!xstrncasecmp("EventRaw", object, MAX(command_len, 6))) {
		field->type = PRINT_EVENTRAW;
		field->name = xstrdup("EventRaw");
		field->len = 8;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("Event", object, MAX(command_len, 2))) {
		field->type = PRINT_EVENT;
		field->name = xstrdup("Event");
		field->len = 7;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("Features", object, MAX(command_len, 3))) {
		field->type = PRINT_FEATURES;
		field->name = xstrdup("Features");
		field->len = 20;
		field->print_routine = print_fields_char_list;
	} else if (!xstrncasecmp("Federation", object, MAX(command_len, 3))) {
		field->type = PRINT_FEDERATION;
		field->name = xstrdup("Federation");
		field->len = 10;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("FedState", object, MAX(command_len, 4))) {
		field->type = PRINT_FEDSTATE;
		field->name = xstrdup("FedState");
		field->len = 12;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("FedStateRaw", object, MAX(command_len, 9))) {
		field->type = PRINT_FEDSTATERAW;
		field->name = xstrdup("FedStateRaw");
		field->len = 11;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("Flags", object, MAX(command_len, 2))) {
		field->type = PRINT_FLAGS;
		field->name = xstrdup("Flags");
		field->len = 20;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("GraceTime", object, MAX(command_len, 3))) {
		field->type = PRINT_GRACE;
		field->name = xstrdup("GraceTime");
		field->len = 10;
		field->print_routine = print_fields_time_from_secs;
	} else if (!xstrncasecmp("GrpCPUs", object, MAX(command_len, 6))) {
		field->type = PRINT_GRPC;
		field->name = xstrdup("GrpCPUs");
		field->len = 8;
		field->print_routine = print_fields_uint64;
	} else if (!xstrncasecmp("GrpCPUMins", object, MAX(command_len, 7))) {
		field->type = PRINT_GRPCM;
		field->name = xstrdup("GrpCPUMins");
		field->len = 11;
		field->print_routine = print_fields_uint64;
	} else if (!xstrncasecmp("GrpCPURunMins", object, MAX(command_len, 7))) {
		field->type = PRINT_GRPCRM;
		field->name = xstrdup("GrpCPURunMins");
		field->len = 13;
		field->print_routine = print_fields_uint64;
	} else if (!xstrncasecmp("GrpTRES", object, MAX(command_len, 7))) {
		field->type = PRINT_GRPT;
		field->name = xstrdup("GrpTRES");
		field->len = 13;
		field->print_routine = sacctmgr_print_tres;
	} else if (!xstrncasecmp("GrpTRESMins", object, MAX(command_len, 7))) {
		field->type = PRINT_GRPTM;
		field->name = xstrdup("GrpTRESMins");
		field->len = 13;
		field->print_routine = sacctmgr_print_tres;
	} else if (!xstrncasecmp("GrpTRESRunMins", object,
				 MAX(command_len, 7))) {
		field->type = PRINT_GRPTRM;
		field->name = xstrdup("GrpTRESRunMins");
		field->len = 13;
		field->print_routine = sacctmgr_print_tres;
	} else if (!xstrncasecmp("GrpJobs", object, MAX(command_len, 4))) {
		field->type = PRINT_GRPJ;
		field->name = xstrdup("GrpJobs");
		field->len = 7;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("GrpJobsAccrue",
				 object, MAX(command_len, 8))) {
		field->type = PRINT_GRPJA;
		field->name = xstrdup("GrpJobsAccrue");
		field->len = 13;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("GrpMemory", object, MAX(command_len, 4))) {
		field->type = PRINT_GRPMEM;
		field->name = xstrdup("GrpMem");
		field->len = 7;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("GrpNodes", object, MAX(command_len, 4))) {
		field->type = PRINT_GRPN;
		field->name = xstrdup("GrpNodes");
		field->len = 8;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("GrpSubmitJobs", object, MAX(command_len, 4))) {
		field->type = PRINT_GRPS;
		field->name = xstrdup("GrpSubmit");
		field->len = 9;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("GrpWall", object, MAX(command_len, 4))) {
		field->type = PRINT_GRPW;
		field->name = xstrdup("GrpWall");
		field->len = 11;
		field->print_routine = print_fields_time;
	} else if (!xstrncasecmp("ID", object, MAX(command_len, 2))) {
		field->type = PRINT_ID;
		field->name = xstrdup("ID");
		field->len = 6;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("Info", object, MAX(command_len, 2))) {
		field->type = PRINT_INFO;
		field->name = xstrdup("Info");
		field->len = 20;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("LFT", object, MAX(command_len, 1))) {
		field->type = PRINT_LFT;
		field->name = xstrdup("LFT");
		field->len = 6;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("servertype", object, MAX(command_len, 10))) {
		field->type = PRINT_SERVERTYPE;
		field->name = xstrdup("ServerType");
		field->len = 10;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("MaxCPUMinsPerJob", object,
				MAX(command_len, 7))) {
		field->type = PRINT_MAXCM;
		field->name = xstrdup("MaxCPUMins");
		field->len = 11;
		field->print_routine = print_fields_uint64;
	} else if (!xstrncasecmp("MaxCPURunMinsPerUser", object,
				 MAX(command_len, 7)) ||
		   !xstrncasecmp("MaxCPURunMinsPU", object,
				MAX(command_len, 7))) {
		field->type = PRINT_MAXCRM;
		field->name = xstrdup("MaxCPURunMinsPU");
		field->len = 15;
		field->print_routine = print_fields_uint64;
	} else if (!xstrncasecmp("MaxCPUsPerJob", object,
				 MAX(command_len, 7))) {
		field->type = PRINT_MAXC;
		field->name = xstrdup("MaxCPUs");
		field->len = 8;
		field->print_routine = print_fields_uint64;
	} else if (!xstrncasecmp("MaxCPUsPerUser", object,
				MAX(command_len, 11)) ||
		   !xstrncasecmp("MaxCPUsPU", object,
				MAX(command_len, 9))) {
		field->type = PRINT_MAXCU;
		field->name = xstrdup("MaxCPUsPU");
		field->len = 9;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("MaxTRES", object,
				 MAX(command_len, 7)) ||
		   !xstrncasecmp("MaxTRESPerJob", object,
				 MAX(command_len, 11))) {
		field->type = PRINT_MAXT;
		field->name = xstrdup("MaxTRES");
		field->len = 13;
		field->print_routine = sacctmgr_print_tres;
	} else if (!xstrncasecmp("MaxTRESPerNode", object,
				 MAX(command_len, 11))) {
		field->type = PRINT_MAXTN;
		field->name = xstrdup("MaxTRESPerNode");
		field->len = 14;
		field->print_routine = sacctmgr_print_tres;
	} else if (!xstrncasecmp("MaxTRESMinsPerJob", object,
				 MAX(command_len, 8))) {
		field->type = PRINT_MAXTM;
		field->name = xstrdup("MaxTRESMins");
		field->len = 13;
		field->print_routine = sacctmgr_print_tres;
	} else if (!xstrncasecmp("MaxTRESRunMinsPerAccount", object,
				 MAX(command_len, 18)) ||
		   !xstrncasecmp("MaxTRESRunMinsPerAcct", object,
				 MAX(command_len, 18)) ||
		   !xstrncasecmp("MaxTRESRunMinsPA", object,
				 MAX(command_len, 15))) {
		field->type = PRINT_MAXTRMA;
		field->name = xstrdup("MaxTRESRunMinsPA");
		field->len = 15;
		field->print_routine = sacctmgr_print_tres;
	} else if (!xstrncasecmp("MaxTRESRunMinsPerUser", object,
				 MAX(command_len, 8)) ||
		   !xstrncasecmp("MaxTRESRunMinsPU", object,
				 MAX(command_len, 8))) {
		field->type = PRINT_MAXTRM;
		field->name = xstrdup("MaxTRESRunMinsPU");
		field->len = 15;
		field->print_routine = sacctmgr_print_tres;
	} else if (!xstrncasecmp("MaxTRESPerAccount", object,
				 MAX(command_len, 11)) ||
		   !xstrncasecmp("MaxTRESPerAcct", object,
				 MAX(command_len, 11)) ||
		   !xstrncasecmp("MaxTRESPA", object,
				 MAX(command_len, 9))) {
		field->type = PRINT_MAXTA;
		field->name = xstrdup("MaxTRESPA");
		field->len = 13;
		field->print_routine = sacctmgr_print_tres;
	} else if (!xstrncasecmp("MaxTRESPerUser", object,
				 MAX(command_len, 11)) ||
		   !xstrncasecmp("MaxTRESPU", object,
				 MAX(command_len, 9))) {
		field->type = PRINT_MAXTU;
		field->name = xstrdup("MaxTRESPU");
		field->len = 13;
		field->print_routine = sacctmgr_print_tres;
	} else if (!xstrncasecmp("MaxJobs", object, MAX(command_len, 4))) {
		field->type = PRINT_MAXJ;
		field->name = xstrdup("MaxJobs");
		field->len = 7;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("MaxJobsAccrue",
				 object, MAX(command_len, 4))) {
		field->type = PRINT_MAXJA;
		field->name = xstrdup("MaxJobsAccrue");
		field->len = 13;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("MaxJobsAccruePerAccount", object,
				 MAX(command_len, 17)) ||
		   !xstrncasecmp("MaxJobsAccruePerAcct", object,
				 MAX(command_len, 17)) ||
		   !xstrncasecmp("MaxJobsAccruePA", object,
				 MAX(command_len, 15))) {
		field->type = PRINT_MAXJAA;
		field->name = xstrdup("MaxJobsAccruePA");
		field->len = 15;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("MaxJobsAccruePerUser", object,
				 MAX(command_len, 17)) ||
		   !xstrncasecmp("MaxJobsAccruePU", object,
				 MAX(command_len, 15))) {
		field->type = PRINT_MAXJAU;
		field->name = xstrdup("MaxJobsAccruePU");
		field->len = 15;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("MaxJobsPerAccount", object,
				 MAX(command_len, 11)) ||
		   !xstrncasecmp("MaxJobsPerAcct", object,
				 MAX(command_len, 11)) ||
		   !xstrncasecmp("MaxJobsPA", object,
				 MAX(command_len, 9))) {
		field->type = PRINT_MAXJPA;
		field->name = xstrdup("MaxJobsPA");
		field->len = 9;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("MaxJobsPerUser", object,
				 MAX(command_len, 11)) ||
		   !xstrncasecmp("MaxJobsPU", object,
				 MAX(command_len, 9))) {
		field->type = PRINT_MAXJ; /* used same as MaxJobs */
		field->name = xstrdup("MaxJobsPU");
		field->len = 9;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("MaxNodesPerJob", object,
				 MAX(command_len, 4))) {
		field->type = PRINT_MAXN;
		field->name = xstrdup("MaxNodes");
		field->len = 8;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("MaxNodesPerUser", object,
				 MAX(command_len, 12)) ||
		   !xstrncasecmp("MaxNodesPU", object,
				 MAX(command_len, 10))) {
		field->type = PRINT_MAXNU;
		field->name = xstrdup("MaxNodesPU");
		field->len = 10;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("MinPrioThreshold", object,
				 MAX(command_len, 4))) {
		field->type = PRINT_MINPT;
		field->name = xstrdup("MinPrioThres");
		field->len = 12;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("MaxSubmitJobs", object,
				 MAX(command_len, 4))) {
		field->type = PRINT_MAXS;
		field->name = xstrdup("MaxSubmit");
		field->len = 9;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("MaxSubmitJobsPerAccount", object,
				 MAX(command_len, 17)) ||
		   !xstrncasecmp("MaxSubmitJobsPerAcct", object,
				 MAX(command_len, 17)) ||
		   !xstrncasecmp("MaxSubmitJobsPA", object,
				 MAX(command_len, 15)) ||
		   !xstrncasecmp("MaxSubmitPA", object,
				 MAX(command_len, 11))) {
		field->type = PRINT_MAXSA;
		field->name = xstrdup("MaxSubmitPA");
		field->len = 11;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("MaxSubmitJobsPerUser", object,
				 MAX(command_len, 10)) ||
		   !xstrncasecmp("MaxSubmitJobsPU", object,
				 MAX(command_len, 10)) ||
		   !xstrncasecmp("MaxSubmitPU", object,
				 MAX(command_len, 6))) {
		field->type = PRINT_MAXS; /* used same as MaxSubmitJobs */
		field->name = xstrdup("MaxSubmitPU");
		field->len = 11;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("MaxWallDurationPerJob", object,
				 MAX(command_len, 4))) {
		field->type = PRINT_MAXW;
		field->name = xstrdup("MaxWall");
		field->len = 11;
		field->print_routine = print_fields_time;
	} else if (!xstrncasecmp("MinCPUsPerJob", object,
				 MAX(command_len, 7))) {
		field->type = PRINT_MINC;
		field->name = xstrdup("MinCPUs");
		field->len = 8;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("MinTRESPerJob", object,
				 MAX(command_len, 7))) {
		field->type = PRINT_MINT;
		field->name = xstrdup("MinTRES");
		field->len = 13;
		field->print_routine = sacctmgr_print_tres;
	} else if (!xstrncasecmp("Name", object, MAX(command_len, 2))) {
		field->type = PRINT_NAME;
		field->name = xstrdup("Name");
		field->len = 10;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("NodeCount", object, MAX(command_len, 5))) {
		field->type = PRINT_NODECNT;
		field->name = xstrdup("NodeCount");
		field->len = 9;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("NodeInx", object, MAX(command_len, 5))) {
		field->type = PRINT_NODEINX;
		field->name = xstrdup("NodeInx");
		field->len = 9;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("NodeNames", object, MAX(command_len, 5))) {
		field->type = PRINT_NODENAME;
		field->name = xstrdup("NodeName");
		field->len = -15;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("Organization", object, MAX(command_len, 1))) {
		field->type = PRINT_ORG;
		field->name = xstrdup("Org");
		field->len = 20;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("ParentID", object, MAX(command_len, 7))) {
		field->type = PRINT_PID;
		field->name = xstrdup("ParentID");
		field->len = 8;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("ParentName", object, MAX(command_len, 7))) {
		field->type = PRINT_PNAME;
		field->name = xstrdup("ParentName");
		field->len = 10;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("Partition", object, MAX(command_len, 4))) {
		field->type = PRINT_PART;
		field->name = xstrdup("Partition");
		field->len = 10;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("PluginIDSelect", object,
				 MAX(command_len, 2))) {
		field->type = PRINT_SELECT;
		field->name = xstrdup("PluginIDSelect");
		field->len = 14;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("PreemptMode", object, MAX(command_len, 8))) {
		field->type = PRINT_PREEM;
		field->name = xstrdup("PreemptMode");
		field->len = 11;
		field->print_routine = print_fields_str;
		/* Preempt needs to follow PreemptMode */
	} else if (!xstrncasecmp("Preempt", object, MAX(command_len, 7))) {
		field->type = PRINT_PREE;
		field->name = xstrdup("Preempt");
		field->len = 10;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("PreemptExemptTime", object,
				 MAX(command_len, 8))) {
		field->type = PRINT_PRXMPT;
		field->name = xstrdup("PreemptExemptTime");
		field->len = 19;
		field->print_routine = print_fields_time_from_secs;
	} else if (!xstrncasecmp("Priority", object, MAX(command_len, 3))) {
		field->type = PRINT_PRIO;
		field->name = xstrdup("Priority");
		field->len = 10;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("Problem", object, MAX(command_len, 1))) {
		field->type = PRINT_PROBLEM;
		field->name = xstrdup("Problem");
		field->len = 40;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("QOSLevel", object, MAX(command_len, 3))) {
		field->type = PRINT_QOS;
		field->name = xstrdup("QOS");
		field->len = 20;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("QOSRAWLevel", object, MAX(command_len, 4))) {
		field->type = PRINT_QOS_RAW;
		field->name = xstrdup("QOS_RAW");
		field->len = 10;
		field->print_routine = print_fields_char_list;
	} else if (!xstrncasecmp("Reason", object,
				 MAX(command_len, 1))) {
		field->type = PRINT_REASON;
		field->name = xstrdup("Reason");
		field->len = 30;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("RGT", object, MAX(command_len, 1))) {
		field->type = PRINT_RGT;
		field->name = xstrdup("RGT");
		field->len = 6;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("RPC", object, MAX(command_len, 1))) {
		field->type = PRINT_RPC_VERSION;
		field->name = xstrdup("RPC");
		field->len = 5;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("Server", object, MAX(command_len, 3))) {
		field->type = PRINT_SERVER;
		field->name = xstrdup("Server");
		field->len = 10;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("Share", object, MAX(command_len, 1))
		   || !xstrncasecmp("FairShare", object, MAX(command_len, 2))) {
		field->type = PRINT_FAIRSHARE;
		field->name = xstrdup("Share");
		field->len = 9;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("StateRaw", object,
				MAX(command_len, 6))) {
		field->type = PRINT_STATERAW;
		field->name = xstrdup("StateRaw");
		field->len = 8;
		field->print_routine = print_fields_uint;
	} else if (!xstrncasecmp("State", object, MAX(command_len, 1))) {
		field->type = PRINT_STATE;
		field->name = xstrdup("State");
		field->len = 6;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("TimeStamp", object, MAX(command_len, 2))) {
		field->type = PRINT_TS;
		field->name = xstrdup("Time");
		field->len = 19;
		field->print_routine = print_fields_date;
	} else if (!xstrncasecmp("TimeEligible", object, MAX(command_len, 6)) ||
		   !xstrncasecmp("Eligible", object, MAX(command_len, 2))) {
		field->type = PRINT_TIMEELIGIBLE;
		field->name = xstrdup("TimeEligible");
		field->len = 19;
		field->print_routine = print_fields_date;
	} else if (!xstrncasecmp("TimeEnd", object, MAX(command_len, 6)) ||
		   !xstrncasecmp("End", object, MAX(command_len, 2))) {
		field->type = PRINT_TIMEEND;
		field->name = xstrdup("TimeEnd");
		field->len = 19;
		field->print_routine = print_fields_date;
	} else if (!xstrncasecmp("TimeStart", object, MAX(command_len, 7)) ||
		   !xstrncasecmp("Start", object, MAX(command_len, 3))) {
		field->type = PRINT_TIMESTART;
		field->name = xstrdup("TimeStart");
		field->len = 19;
		field->print_routine = print_fields_date;
	} else if (!xstrncasecmp("TimeSubmit", object, MAX(command_len, 6)) ||
		   !xstrncasecmp("Submit", object, MAX(command_len, 2))) {
		field->type = PRINT_TIMESUBMIT;
		field->name = xstrdup("TimeSubmit");
		field->len = 19;
		field->print_routine = print_fields_date;
	} else if (!xstrncasecmp("TRES", object,
				 MAX(command_len, 2))) {
		field->type = PRINT_TRES;
		field->name = xstrdup("TRES");
		field->len = 20;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("Type", object, MAX(command_len, 2))) {
		field->type = PRINT_TYPE;
		field->name = xstrdup("Type");
		field->len = 8;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("UnusedWall", object, MAX(command_len, 2))) {
		field->type = PRINT_UNUSED;
		field->name = xstrdup("UnusedWall");
		field->len = 10;
		field->print_routine = print_fields_double;
	} else if (!xstrncasecmp("UsageFactor", object, MAX(command_len, 6))) {
		field->type = PRINT_UF;
		field->name = xstrdup("UsageFactor");
		field->len = 11;
		field->print_routine = print_fields_double;
	} else if (!xstrncasecmp("UsageThreshold", object,
				 MAX(command_len, 6))) {
		field->type = PRINT_UT;
		field->name = xstrdup("UsageThres");
		field->len = 10;
		field->print_routine = print_fields_double;
	} else if (!xstrncasecmp("LimitFactor", object, MAX(command_len, 6))) {
		field->type = PRINT_LF;
		field->name = xstrdup("LimitFactor");
		field->len = 11;
		field->print_routine = print_fields_double;
	} else if (!xstrncasecmp("Allocated", object, MAX(command_len, 7))) {
		field->type = PRINT_ALLOCATED;
		field->name = xstrdup("Allocated");
		field->len = 9;
		field->print_routine = print_fields_uint32;
	} else if (!xstrncasecmp("LastConsumed", object, MAX(command_len, 8))) {
		field->type = PRINT_LAST_CONSUMED;
		field->name = xstrdup("LastConsumed");
		field->len = 12;
		field->print_routine = print_fields_uint32;
	} else if (!xstrncasecmp("User", object, MAX(command_len, 1))) {
		field->type = PRINT_USER;
		field->name = xstrdup("User");
		field->len = 10;
		field->print_routine = print_fields_str;
	} else if (!xstrncasecmp("WCKeys", object, MAX(command_len, 2))) {
		field->type = PRINT_WCKEYS;
		field->name = xstrdup("WCKeys");
		field->len = 20;
		field->print_routine = print_fields_char_list;
	} else if (!xstrncasecmp("Where", object, MAX(command_len, 2))) {
		field->type = PRINT_WHERE;
		field->name = xstrdup("Where");
		field->len = 20;
		field->print_routine = print_fields_str;
	} else {
		exit_code=1;
		fprintf(stderr, "Unknown field '%s'\n", object);
		exit(1);
	}

	if (field_len)
		field->len = field_len;
	return field;
}

extern void notice_thread_init()
{
	slurm_thread_create_detached(&lock_warning_thread,
				     _print_lock_warn, NULL);
}

extern void notice_thread_fini()
{
	pthread_cancel(lock_warning_thread);
}

extern int commit_check(char *warning)
{
	int ans = 0;
	char c = '\0';
	int fd = fileno(stdin);
	fd_set rfds;
	struct timeval tv;

	if (!rollback_flag)
		return 1;

	printf("%s (You have 30 seconds to decide)\n", warning);
	_nonblock(1);
	while(c != 'Y' && c != 'y'
	      && c != 'N' && c != 'n'
	      && c != '\n') {
		if (c) {
			printf("Y or N please\n");
		}
		printf("(N/y): ");
		fflush(stdout);
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		/* Wait up to 30 seconds. */
		tv.tv_sec = 30;
		tv.tv_usec = 0;
		if ((ans = select(fd+1, &rfds, NULL, NULL, &tv)) <= 0)
			break;

		c = (char) getchar();
		printf("\n");
	}
	_nonblock(0);
	if (ans <= 0)
		printf("timeout\n");
	else if (c == 'Y' || c == 'y')
		return 1;

	return 0;
}

extern int sacctmgr_remove_assoc_usage(slurmdb_assoc_cond_t *assoc_cond)
{
	List update_list = NULL;
	List local_assoc_list = NULL;
	List local_cluster_list = NULL;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	ListIterator itr3 = NULL;
	char *account = NULL;
	char *cluster = NULL;
	char *user = NULL;
	slurmdb_assoc_rec_t* rec = NULL;
	slurmdb_cluster_rec_t* cluster_rec = NULL;
	slurmdb_update_object_t* update_obj = NULL;
	slurmdb_cluster_cond_t cluster_cond;
	int rc = SLURM_SUCCESS;

	if (!assoc_cond || !assoc_cond->acct_list ||
	    !list_count(assoc_cond->acct_list)) {
		error("An association name is required to remove usage");
		return SLURM_ERROR;
	}

	if (!assoc_cond->cluster_list)
		assoc_cond->cluster_list = list_create(xfree_ptr);

	if (!list_count(assoc_cond->cluster_list)) {
		printf("No cluster specified, resetting on local cluster %s\n",
		       slurm_conf.cluster_name);
		list_append(assoc_cond->cluster_list,
			    xstrdup(slurm_conf.cluster_name));
	}

	if (!commit_check("Would you like to reset usage?")) {
		printf(" Changes Discarded\n");
		return rc;
	}

	local_assoc_list = slurmdb_associations_get(db_conn, assoc_cond);

	slurmdb_init_cluster_cond(&cluster_cond, 0);
	cluster_cond.cluster_list = assoc_cond->cluster_list;
	local_cluster_list = slurmdb_clusters_get(db_conn, &cluster_cond);

	itr = list_iterator_create(assoc_cond->cluster_list);
	itr2 = list_iterator_create(assoc_cond->acct_list);
	if (assoc_cond->user_list && list_count(assoc_cond->user_list))
		itr3 = list_iterator_create(assoc_cond->user_list);
	while((cluster = list_next(itr))) {
		cluster_rec = sacctmgr_find_cluster_from_list(
			local_cluster_list, cluster);
		if (!cluster_rec) {
			error("Failed to find cluster %s in database",
			      cluster);
			rc = SLURM_ERROR;
			goto end_it;
		}

		update_list = list_create(slurmdb_destroy_update_object);
		update_obj = xmalloc(sizeof(slurmdb_update_object_t));
		update_obj->type = SLURMDB_REMOVE_ASSOC_USAGE;
		update_obj->objects = list_create(NULL);

		if (itr3) {
			while ((user = list_next(itr3))) {
				while ((account = list_next(itr2))) {
					rec = sacctmgr_find_assoc_from_list(
						local_assoc_list,
						user, account, cluster, "*");
					if (!rec) {
						error("Failed to find "
						      "cluster %s "
						      "account %s user "
						      "%s association "
						      "in database",
						      cluster, account,
						      user);
						rc = SLURM_ERROR;
						slurmdb_destroy_update_object(
							update_obj);
						goto end_it;
					}
					list_append(update_obj->objects, rec);
				}
				list_iterator_reset(itr2);
			}
			list_iterator_reset(itr3);
		} else {
			while ((account = list_next(itr2))) {
				rec = sacctmgr_find_assoc_from_list(
					local_assoc_list,
					NULL, account, cluster, "*");
				if (!rec) {
					error("Failed to find cluster %s "
					      "account %s association in "
					      "database",
					      cluster, account);
					rc = SLURM_ERROR;
					slurmdb_destroy_update_object(
						update_obj);
					goto end_it;
				}
				list_append(update_obj->objects, rec);
			}
			list_iterator_reset(itr2);
		}

		if (list_count(update_obj->objects)) {
			list_append(update_list, update_obj);
			rc = slurmdb_send_accounting_update(
				update_list, cluster,
				cluster_rec->control_host,
				cluster_rec->control_port,
				cluster_rec->rpc_version);
		} else {
			slurmdb_destroy_update_object(update_obj);
		}
		update_obj = NULL;
		FREE_NULL_LIST(update_list);
	}
end_it:
	list_iterator_destroy(itr);
	list_iterator_destroy(itr2);
	if (itr3)
		list_iterator_destroy(itr3);

	FREE_NULL_LIST(local_assoc_list);
	FREE_NULL_LIST(local_cluster_list);

	return rc;
}

extern int sacctmgr_remove_qos_usage(slurmdb_qos_cond_t *qos_cond)
{
	List update_list = NULL;
	List cluster_list;
	List local_qos_list = NULL;
	List local_cluster_list = NULL;
	ListIterator itr = NULL, itr2 = NULL;
	char *qos_name = NULL, *cluster_name = NULL;
	slurmdb_qos_rec_t* rec = NULL;
	slurmdb_cluster_rec_t* cluster_rec = NULL;
	slurmdb_update_object_t* update_obj = NULL;
	slurmdb_cluster_cond_t cluster_cond;
	int rc = SLURM_SUCCESS;

	cluster_list = qos_cond->description_list;
	qos_cond->description_list = NULL;

	if (!cluster_list)
		cluster_list = list_create(xfree_ptr);

	if (!list_count(cluster_list)) {
		printf("No cluster specified, resetting on local cluster %s\n",
		       slurm_conf.cluster_name);
		list_append(cluster_list, xstrdup(slurm_conf.cluster_name));
	}

	if (!commit_check("Would you like to reset usage?")) {
		printf(" Changes Discarded\n");
		return rc;
	}

	local_qos_list = slurmdb_qos_get(db_conn, qos_cond);

	slurmdb_init_cluster_cond(&cluster_cond, 0);
	cluster_cond.cluster_list = cluster_list;
	local_cluster_list = slurmdb_clusters_get(db_conn, &cluster_cond);

	itr = list_iterator_create(cluster_list);
	itr2 = list_iterator_create(qos_cond->name_list);
	while ((cluster_name = list_next(itr))) {
		cluster_rec = sacctmgr_find_cluster_from_list(
			local_cluster_list, cluster_name);
		if (!cluster_rec) {
			error("Failed to find cluster %s in database",
			      cluster_name);
			rc = SLURM_ERROR;
			goto end_it;
		}

		update_list = list_create(slurmdb_destroy_update_object);
		update_obj = xmalloc(sizeof(slurmdb_update_object_t));
		update_obj->type = SLURMDB_REMOVE_QOS_USAGE;
		update_obj->objects = list_create(NULL);

		while ((qos_name = list_next(itr2))) {
			rec = sacctmgr_find_qos_from_list(
				local_qos_list, qos_name);
			if (!rec) {
				error("Failed to find QOS %s", qos_name);
				rc = SLURM_ERROR;
				slurmdb_destroy_update_object(update_obj);
				goto end_it;
			}
			list_append(update_obj->objects, rec);
		}
		list_iterator_reset(itr2);

		if (list_count(update_obj->objects)) {
			list_append(update_list, update_obj);
			rc = slurmdb_send_accounting_update(
				update_list, cluster_name,
				cluster_rec->control_host,
				cluster_rec->control_port,
				cluster_rec->rpc_version);
		} else {
			slurmdb_destroy_update_object(update_obj);
		}
		FREE_NULL_LIST(update_list);
	}
end_it:
	list_iterator_destroy(itr);
	list_iterator_destroy(itr2);

	FREE_NULL_LIST(update_list);
	FREE_NULL_LIST(local_qos_list);
	xfree(cluster_name);

	return rc;
}

extern slurmdb_assoc_rec_t *sacctmgr_find_account_base_assoc(
	char *account, char *cluster)
{
	slurmdb_assoc_rec_t *assoc = NULL;
	char *temp = "root";
	slurmdb_assoc_cond_t assoc_cond;
	List assoc_list = NULL;

	if (!cluster)
		return NULL;

	if (account)
		temp = account;

	memset(&assoc_cond, 0, sizeof(slurmdb_assoc_cond_t));
	assoc_cond.acct_list = list_create(NULL);
	list_append(assoc_cond.cluster_list, temp);
	assoc_cond.cluster_list = list_create(NULL);
	list_append(assoc_cond.cluster_list, cluster);
	assoc_cond.user_list = list_create(NULL);
	list_append(assoc_cond.user_list, "");

	assoc_list = slurmdb_associations_get(db_conn, &assoc_cond);

	FREE_NULL_LIST(assoc_cond.acct_list);
	FREE_NULL_LIST(assoc_cond.cluster_list);
	FREE_NULL_LIST(assoc_cond.user_list);

	if (assoc_list)
		assoc = list_pop(assoc_list);

	FREE_NULL_LIST(assoc_list);

	return assoc;
}

extern slurmdb_assoc_rec_t *sacctmgr_find_root_assoc(char *cluster)
{
	return sacctmgr_find_account_base_assoc(NULL, cluster);
}

extern slurmdb_user_rec_t *sacctmgr_find_user(char *name)
{
	slurmdb_user_rec_t *user = NULL;
	slurmdb_user_cond_t user_cond;
	slurmdb_assoc_cond_t assoc_cond;
	List user_list = NULL;

	if (!name)
		return NULL;

	memset(&user_cond, 0, sizeof(slurmdb_user_cond_t));
	memset(&assoc_cond, 0, sizeof(slurmdb_assoc_cond_t));
	assoc_cond.user_list = list_create(NULL);
	list_append(assoc_cond.user_list, name);
	user_cond.assoc_cond = &assoc_cond;

	user_list = slurmdb_users_get(db_conn, &user_cond);

	FREE_NULL_LIST(assoc_cond.user_list);

	if (user_list)
		user = list_pop(user_list);

	FREE_NULL_LIST(user_list);

	return user;
}

extern slurmdb_account_rec_t *sacctmgr_find_account(char *name)
{
	slurmdb_account_rec_t *account = NULL;
	slurmdb_account_cond_t account_cond;
	slurmdb_assoc_cond_t assoc_cond;
	List account_list = NULL;

	if (!name)
		return NULL;

	memset(&account_cond, 0, sizeof(slurmdb_account_cond_t));
	memset(&assoc_cond, 0, sizeof(slurmdb_assoc_cond_t));
	assoc_cond.acct_list = list_create(NULL);
	list_append(assoc_cond.acct_list, name);
	account_cond.assoc_cond = &assoc_cond;

	account_list = slurmdb_accounts_get(db_conn, &account_cond);

	FREE_NULL_LIST(assoc_cond.acct_list);

	if (account_list)
		account = list_pop(account_list);

	FREE_NULL_LIST(account_list);

	return account;
}

extern slurmdb_cluster_rec_t *sacctmgr_find_cluster(char *name)
{
	slurmdb_cluster_rec_t *cluster = NULL;
	slurmdb_cluster_cond_t cluster_cond;
	List cluster_list = NULL;

	if (!name)
		return NULL;

	slurmdb_init_cluster_cond(&cluster_cond, 0);
	cluster_cond.cluster_list = list_create(NULL);
	list_append(cluster_cond.cluster_list, name);

	cluster_list = slurmdb_clusters_get(db_conn, &cluster_cond);

	FREE_NULL_LIST(cluster_cond.cluster_list);

	if (cluster_list)
		cluster = list_pop(cluster_list);

	FREE_NULL_LIST(cluster_list);

	return cluster;
}

extern slurmdb_assoc_rec_t *sacctmgr_find_assoc_from_list(
	List assoc_list, char *user, char *account,
	char *cluster, char *partition)
{
	ListIterator itr = NULL;
	slurmdb_assoc_rec_t * assoc = NULL;

	if (!assoc_list)
		return NULL;

	itr = list_iterator_create(assoc_list);
	while((assoc = list_next(itr))) {
		if (((!user && assoc->user)
		     || (user && (!assoc->user
				  || xstrcasecmp(user, assoc->user))))
		    || (account && (!assoc->acct
				    || xstrcasecmp(account, assoc->acct)))
		    || ((!cluster && assoc->cluster)
			|| (cluster && (!assoc->cluster
					|| xstrcasecmp(cluster,
						       assoc->cluster)))))
			continue;
		else if (partition) {
			if (partition[0] != '*'
			    && (!assoc->partition
				|| xstrcasecmp(partition, assoc->partition)))
				continue;
		} else if (assoc->partition)
			continue;

		break;
	}
	list_iterator_destroy(itr);

	return assoc;
}

extern slurmdb_assoc_rec_t *sacctmgr_find_account_base_assoc_from_list(
	List assoc_list, char *account, char *cluster)
{
	ListIterator itr = NULL;
	slurmdb_assoc_rec_t *assoc = NULL;
	char *temp = "root";

	if (!cluster || !assoc_list)
		return NULL;

	if (account)
		temp = account;
	/* info("looking for %s %s in %d", account, cluster, */
/* 	     list_count(assoc_list)); */
	itr = list_iterator_create(assoc_list);
	while((assoc = list_next(itr))) {
		/* info("is it %s %s %s", assoc->user, assoc->acct, assoc->cluster); */
		if (assoc->user
		    || xstrcasecmp(temp, assoc->acct)
		    || xstrcasecmp(cluster, assoc->cluster))
			continue;
		/* 	info("found it"); */
		break;
	}
	list_iterator_destroy(itr);

	return assoc;
}

extern slurmdb_qos_rec_t *sacctmgr_find_qos_from_list(
	List qos_list, char *name)
{
	ListIterator itr = NULL;
	slurmdb_qos_rec_t *qos = NULL;
	char *working_name = NULL;

	if (!name || !qos_list)
		return NULL;

	if (name[0] == '+' || name[0] == '-')
		working_name = name+1;
	else
		working_name = name;

	itr = list_iterator_create(qos_list);
	while((qos = list_next(itr))) {
		if (!xstrcasecmp(working_name, qos->name))
			break;
	}
	list_iterator_destroy(itr);

	return qos;

}

extern slurmdb_res_rec_t *sacctmgr_find_res_from_list(
	List res_list, uint32_t id, char *name, char *server)
{
	ListIterator itr = NULL;
	slurmdb_res_rec_t *res = NULL;

	if ((id == NO_VAL) && (!name || !res_list))
		return NULL;

	itr = list_iterator_create(res_list);
	while ((res = list_next(itr))) {
		if ((id == res->id)
		    || (name && server
			&& !xstrcasecmp(server, res->server)
			&& !xstrcasecmp(name, res->name)))
			break;
	}
	list_iterator_destroy(itr);

	return res;
}

extern slurmdb_user_rec_t *sacctmgr_find_user_from_list(
	List user_list, char *name)
{
	ListIterator itr = NULL;
	slurmdb_user_rec_t *user = NULL;

	if (!name || !user_list)
		return NULL;

	itr = list_iterator_create(user_list);
	while((user = list_next(itr))) {
		if (!xstrcasecmp(name, user->name))
			break;
	}
	list_iterator_destroy(itr);

	return user;

}

extern slurmdb_account_rec_t *sacctmgr_find_account_from_list(
	List acct_list, char *name)
{
	ListIterator itr = NULL;
	slurmdb_account_rec_t *account = NULL;

	if (!name || !acct_list)
		return NULL;

	itr = list_iterator_create(acct_list);
	while((account = list_next(itr))) {
		if (!xstrcasecmp(name, account->name))
			break;
	}
	list_iterator_destroy(itr);

	return account;

}

extern slurmdb_cluster_rec_t *sacctmgr_find_cluster_from_list(
	List cluster_list, char *name)
{
	ListIterator itr = NULL;
	slurmdb_cluster_rec_t *cluster = NULL;

	if (!name || !cluster_list)
		return NULL;

	itr = list_iterator_create(cluster_list);
	while((cluster = list_next(itr))) {
		if (!xstrcasecmp(name, cluster->name))
			break;
	}
	list_iterator_destroy(itr);

	return cluster;
}

extern slurmdb_wckey_rec_t *sacctmgr_find_wckey_from_list(
	List wckey_list, char *user, char *name, char *cluster)
{
	ListIterator itr = NULL;
	slurmdb_wckey_rec_t * wckey = NULL;

	if (!wckey_list)
		return NULL;

	itr = list_iterator_create(wckey_list);
	while((wckey = list_next(itr))) {
		if (((!user && wckey->user)
		     || (user && (!wckey->user
				  || xstrcasecmp(user, wckey->user))))
		    || (name && (!wckey->name
				 || xstrcasecmp(name, wckey->name)))
		    || ((!cluster && wckey->cluster)
			|| (cluster && (!wckey->cluster
					|| xstrcasecmp(cluster,
						       wckey->cluster)))))
			continue;
		break;
	}
	list_iterator_destroy(itr);

	return wckey;
}

extern int get_uint(char *in_value, uint32_t *out_value, char *type)
{
	char *ptr = NULL, *meat = NULL;
	long num;

	if (!(meat = strip_quotes(in_value, NULL, 1))) {
		error("Problem with strip_quotes");
		return SLURM_ERROR;
	}
	num = strtol(meat, &ptr, 10);
	if ((num == 0) && ptr && ptr[0]) {
		error("Invalid value for %s (%s)", type, meat);
		xfree(meat);
		return SLURM_ERROR;
	}
	xfree(meat);

	if (num < 0)
		*out_value = INFINITE;		/* flag to clear */
	else
		*out_value = (uint32_t) num;
	return SLURM_SUCCESS;
}

extern int get_uint16(char *in_value, uint16_t *out_value, char *type)
{
	char *ptr = NULL, *meat = NULL;
	long num;

	if (!(meat = strip_quotes(in_value, NULL, 1))) {
		error("Problem with strip_quotes");
		return SLURM_ERROR;
	}

	num = strtol(meat, &ptr, 10);
	if ((num == 0) && ptr && ptr[0]) {
		error("Invalid value for %s (%s)", type, meat);
		xfree(meat);
		return SLURM_ERROR;
	}
	xfree(meat);

	if (num < 0)
		*out_value = INFINITE16; /* flag to clear */
	else
		*out_value = (uint16_t) num;
	return SLURM_SUCCESS;
}

extern int get_uint64(char *in_value, uint64_t *out_value, char *type)
{
	char *ptr = NULL, *meat = NULL;
	long long num;

	if (!(meat = strip_quotes(in_value, NULL, 1))) {
		error("Problem with strip_quotes");
		return SLURM_ERROR;
	}

	num = strtoll(meat, &ptr, 10);
	if ((num == 0) && ptr && ptr[0]) {
		error("Invalid value for %s (%s)", type, meat);
		xfree(meat);
		return SLURM_ERROR;
	}
	xfree(meat);

	if (num < 0)
		*out_value = INFINITE64; /* flag to clear */
	else
		*out_value = (uint64_t) num;
	return SLURM_SUCCESS;
}

extern int get_double(char *in_value, double *out_value, char *type)
{
	char *ptr = NULL, *meat = NULL;
	double num;

	if (!(meat = strip_quotes(in_value, NULL, 1))) {
		error("Problem with strip_quotes");
		return SLURM_ERROR;
	}
	num = strtod(meat, &ptr);
	if ((num == 0) && ptr && ptr[0]) {
		error("Invalid value for %s (%s)", type, meat);
		xfree(meat);
		return SLURM_ERROR;
	}
	xfree(meat);

	if (num < 0)
		*out_value = (double) INFINITE;		/* flag to clear */
	else
		*out_value = (double) num;
	return SLURM_SUCCESS;
}

static int _addto_action_char_list_internal(List char_list, char *name, void *x)
{
	uint32_t id = 0;
	char *tmp_name = NULL;

	id = str_2_slurmdbd_msg_type(name);
	if (id == NO_VAL) {
		error("You gave a bad action '%s'.", name);
		list_flush(char_list);
		return SLURM_ERROR;
	}

	tmp_name = xstrdup_printf("%u", id);

	if (!list_find_first(char_list, slurm_find_char_in_list, tmp_name)) {
		list_append(char_list, tmp_name);
		return 1;
	} else {
		xfree(tmp_name);
		return 0;
	}
}

extern int addto_action_char_list(List char_list, char *names)
{
	if (!char_list) {
		error("No list was given to fill in");
		return 0;
	}

	return slurm_parse_char_list(char_list, names, NULL,
				     _addto_action_char_list_internal);
}

extern void sacctmgr_print_coord_list(
	print_field_t *field, void *input, int last)
{
	int abs_len = abs(field->len);
	ListIterator itr = NULL;
	char *print_this = NULL;
	List value = *(List *)input;

	slurmdb_coord_rec_t *object = NULL;

	if (!value || !list_count(value)) {
		if (print_fields_parsable_print)
			print_this = xstrdup("");
		else
			print_this = xstrdup(" ");
	} else {
		list_sort(value, (ListCmpF)sort_coord_list);
		itr = list_iterator_create(value);
		while((object = list_next(itr))) {
			if (print_this)
				xstrfmtcat(print_this, ",%s",
					   object->name);
			else
				print_this = xstrdup(object->name);
		}
		list_iterator_destroy(itr);
	}

	if (print_fields_parsable_print == PRINT_FIELDS_PARSABLE_NO_ENDING
	    && last)
		printf("%s", print_this);
	else if (print_fields_parsable_print)
		printf("%s|", print_this);
	else if (print_this) {
		if (strlen(print_this) > abs_len)
			print_this[abs_len-1] = '+';

		if (field->len == abs_len)
			printf("%*.*s ", abs_len, abs_len, print_this);
		else
			printf("%-*.*s ", abs_len, abs_len, print_this);
	}
	xfree(print_this);
}

extern void sacctmgr_print_tres(print_field_t *field, void *input, int last)
{
	int abs_len = abs(field->len);
	char *print_this;
	char *value = *(char **)input;

	sacctmgr_initialize_g_tres_list();

	print_this = slurmdb_make_tres_string_from_simple(
		value, g_tres_list, NO_VAL, CONVERT_NUM_UNIT_EXACT, 0, NULL);

	if (!print_this)
		print_this = xstrdup("");

	if (print_fields_parsable_print == PRINT_FIELDS_PARSABLE_NO_ENDING
	    && last)
		printf("%s", print_this);
	else if (print_fields_parsable_print)
		printf("%s|", print_this);
	else {
		if (strlen(print_this) > abs_len)
			print_this[abs_len-1] = '+';

		if (field->len == abs_len)
			printf("%*.*s ", abs_len, abs_len, print_this);
		else
			printf("%-*.*s ", abs_len, abs_len, print_this);
	}
	xfree(print_this);
}

extern void sacctmgr_print_assoc_limits(slurmdb_assoc_rec_t *assoc)
{
	char *tmp_char;

	if (!assoc)
		return;

	if (assoc->shares_raw == INFINITE)
		printf("  Fairshare     = NONE\n");
	else if (assoc->shares_raw == SLURMDB_FS_USE_PARENT)
		printf("  Fairshare     = parent\n");
	else if (assoc->shares_raw != NO_VAL)
		printf("  Fairshare     = %u\n", assoc->shares_raw);

	if (assoc->grp_jobs == INFINITE)
		printf("  GrpJobs       = NONE\n");
	else if (assoc->grp_jobs != NO_VAL)
		printf("  GrpJobs       = %u\n", assoc->grp_jobs);

	if (assoc->grp_jobs_accrue == INFINITE)
		printf("  GrpJobsAccrue            = None\n");
	else if (assoc->grp_jobs_accrue != NO_VAL)
		printf("  GrpJobsAccrue            = %u\n",
		       assoc->grp_jobs_accrue);

	if (assoc->grp_submit_jobs == INFINITE)
		printf("  GrpSubmitJobs = NONE\n");
	else if (assoc->grp_submit_jobs != NO_VAL)
		printf("  GrpSubmitJobs = %u\n",
		       assoc->grp_submit_jobs);

	if (assoc->grp_tres) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			assoc->grp_tres, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT, 0, NULL);
		printf("  GrpTRES       = %s\n", tmp_char);
		xfree(tmp_char);
	}
	if (assoc->grp_tres_mins) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			assoc->grp_tres_mins, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT, 0, NULL);;
		printf("  GrpTRESMins   = %s\n", tmp_char);
		xfree(tmp_char);
	}
	if (assoc->grp_tres_run_mins) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			assoc->grp_tres_run_mins, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT, 0, NULL);
		printf("  GrpTRESRunMins= %s\n", tmp_char);
		xfree(tmp_char);
	}

	if (assoc->grp_wall == INFINITE)
		printf("  GrpWall       = NONE\n");
	else if (assoc->grp_wall != NO_VAL) {
		char time_buf[32];
		mins2time_str((time_t) assoc->grp_wall,
			      time_buf, sizeof(time_buf));
		printf("  GrpWall       = %s\n", time_buf);
	}

	if (assoc->max_jobs == INFINITE)
		printf("  MaxJobs       = NONE\n");
	else if (assoc->max_jobs != NO_VAL)
		printf("  MaxJobs       = %u\n", assoc->max_jobs);

	if (assoc->max_jobs_accrue == INFINITE)
		printf("  MaxJobsPrioAcc= NONE\n");
	else if (assoc->max_jobs_accrue != NO_VAL)
		printf("  MaxJobsPrioAcc= %u\n", assoc->max_jobs_accrue);

	if (assoc->max_submit_jobs == INFINITE)
		printf("  MaxSubmitJobs = NONE\n");
	else if (assoc->max_submit_jobs != NO_VAL)
		printf("  MaxSubmitJobs = %u\n",
		       assoc->max_submit_jobs);

	if (assoc->max_tres_pj) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			assoc->max_tres_pj, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT, 0, NULL);
		printf("  MaxTRES       = %s\n", tmp_char);
		xfree(tmp_char);
	}
	if (assoc->max_tres_pn) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			assoc->max_tres_pn, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT, 0, NULL);
		printf("  MaxTRESPerNode= %s\n", tmp_char);
		xfree(tmp_char);
	}
	if (assoc->max_tres_mins_pj) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			assoc->max_tres_mins_pj, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT, 0, NULL);
		printf("  MaxTRESMins   = %s\n", tmp_char);
		xfree(tmp_char);
	}
	if (assoc->max_tres_run_mins) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			assoc->max_tres_run_mins, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT, 0, NULL);
		printf("  MaxTRESRUNMins= %s\n", tmp_char);
		xfree(tmp_char);
	}

	if (assoc->max_wall_pj == INFINITE)
		printf("  MaxWall       = NONE\n");
	else if (assoc->max_wall_pj != NO_VAL) {
		char time_buf[32];
		mins2time_str((time_t) assoc->max_wall_pj,
			      time_buf, sizeof(time_buf));
		printf("  MaxWall       = %s\n", time_buf);
	}

	if (assoc->min_prio_thresh == INFINITE)
		printf("  MinPrioThresh = NONE\n");
	else if (assoc->min_prio_thresh != NO_VAL)
		printf("  MinPrioThresh = %u\n", assoc->min_prio_thresh);

	if (assoc->parent_acct)
		printf("  Parent        = %s\n", assoc->parent_acct);

	if (assoc->priority == INFINITE)
		printf("  Priority      = NONE\n");
	else if (assoc->priority != NO_VAL)
		printf("  Priority      = %d\n", assoc->priority);

	if (assoc->qos_list) {
		if (!g_qos_list)
			g_qos_list =
				slurmdb_qos_get(db_conn, NULL);
		char *temp_char = get_qos_complete_str(g_qos_list,
						       assoc->qos_list);
		if (temp_char) {
			printf("  QOS           = %s\n", temp_char);
			xfree(temp_char);
		}
	}

	if (assoc->def_qos_id != NO_VAL) {
		if (!g_qos_list)
			g_qos_list =
				slurmdb_qos_get(db_conn, NULL);
		printf("  DefQOS        = %s\n",
		       slurmdb_qos_str(g_qos_list, assoc->def_qos_id));
	}

	/* This should be last because it might be long */
	if (assoc->comment)
		printf("  Comment       = %s\n", assoc->comment);

}

static int _print_cluster_features(void *object, void *arg)
{
	char *feature = (char *)object;
	if (feature[0] == '+' || feature[0] == '-')
		printf("  Feature     %c= %s\n", feature[0], feature + 1);
	else
		printf("  Feature       = %s\n", feature);

	return SLURM_SUCCESS;
}

extern void sacctmgr_print_cluster(slurmdb_cluster_rec_t *cluster)
{
	if (!cluster)
		return;

	if (cluster->name)
		printf("  Name           = %s\n", cluster->name);
	if (cluster->classification)
		printf("  Classification = %s\n",
		       get_classification_str(cluster->classification));

	if (cluster->fed.feature_list) {
		if (!list_count(cluster->fed.feature_list))
			printf("  Feature     = \n");
		else
			list_for_each(cluster->fed.feature_list,
				      _print_cluster_features, NULL);
	}

	if (cluster->fed.name)
		printf("  Federation     = %s\n", cluster->fed.name);
	if (cluster->fed.state != NO_VAL) {
		char *tmp_str = slurmdb_cluster_fed_states_str(
						cluster->fed.state);
		printf("  FedState       = %s\n", tmp_str);
	}
}

extern void sacctmgr_print_federation(slurmdb_federation_rec_t *fed)
{
	if (!fed)
		return;

	if (fed->name)
		printf("  Name          = %s\n", fed->name);
	if (fed->flags && (fed->flags != FEDERATION_FLAG_NOTSET)) {
		char *mode = NULL;
		char *tmp_flags =
			slurmdb_federation_flags_str(fed->flags);
		if (fed->flags & FEDERATION_FLAG_ADD)
			mode = "+";
		else if (fed->flags & FEDERATION_FLAG_REMOVE)
			mode = "-";
		else
			mode = " ";
		printf("  Flags        %s= %s\n", mode, tmp_flags);
		xfree(tmp_flags);
	}
	if (fed->cluster_list) {
		ListIterator itr = list_iterator_create(fed->cluster_list);
		slurmdb_cluster_rec_t *cluster = NULL;
		while ((cluster = list_next(itr))) {
			char *tmp_name = cluster->name;
			if (tmp_name &&
			    (tmp_name[0] == '+' || tmp_name[0] == '-'))
				printf("  Cluster      %c= %s\n",
				       tmp_name[0], tmp_name + 1);
			else
				printf("  Cluster       = %s\n", tmp_name);

		}
		list_iterator_destroy(itr);
	}
}

extern void sacctmgr_print_qos_limits(slurmdb_qos_rec_t *qos)
{
	char *tmp_char;

	if (!qos)
		return;

	if (qos->preempt_list && !g_qos_list)
		g_qos_list = slurmdb_qos_get(db_conn, NULL);

	if (qos->flags && (qos->flags != QOS_FLAG_NOTSET)) {
		char *tmp_char = slurmdb_qos_flags_str(qos->flags);
		printf("  Flags                    = %s\n", tmp_char);
		xfree(tmp_char);
	}

	if (qos->grace_time == INFINITE)
		printf("  GraceTime                = NONE\n");
	else if (qos->grace_time != NO_VAL)
		printf("  GraceTime                = %d\n", qos->grace_time);

	if (qos->grp_jobs == INFINITE)
		printf("  GrpJobs                  = NONE\n");
	else if (qos->grp_jobs != NO_VAL)
		printf("  GrpJobs                  = %u\n", qos->grp_jobs);

	if (qos->grp_jobs_accrue == INFINITE)
		printf("  GrpJobsAccrue            = None\n");
	else if (qos->grp_jobs_accrue != NO_VAL)
		printf("  GrpJobsAccrue            = %u\n",
		       qos->grp_jobs_accrue);

	if (qos->grp_submit_jobs == INFINITE)
		printf("  GrpSubmitJobs            = NONE\n");
	else if (qos->grp_submit_jobs != NO_VAL)
		printf("  GrpSubmitJobs            = %u\n",
		       qos->grp_submit_jobs);

	if (qos->grp_tres) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			qos->grp_tres, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT, 0, NULL);
		printf("  GrpTRES                  = %s\n", tmp_char);
		xfree(tmp_char);
	}
	if (qos->grp_tres_mins) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			qos->grp_tres_mins, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT, 0, NULL);
		printf("  GrpTRESMins              = %s\n", tmp_char);
		xfree(tmp_char);
	}
	if (qos->grp_tres_run_mins) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			qos->grp_tres_run_mins, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT, 0, NULL);
		printf("  GrpTRESRunMins           = %s\n", tmp_char);
		xfree(tmp_char);
	}

	if (qos->grp_wall == INFINITE)
		printf("  GrpWall                  = NONE\n");
	else if (qos->grp_wall != NO_VAL) {
		char time_buf[32];
		mins2time_str((time_t) qos->grp_wall,
			      time_buf, sizeof(time_buf));
		printf("  GrpWall                  = %s\n", time_buf);
	}

	if (qos->max_jobs_accrue_pa == INFINITE)
		printf("  MaxJobsAccruePerAccount  = NONE\n");
	else if(qos->max_jobs_accrue_pa != NO_VAL)
		printf("  MaxJobsAccruePerAccount  = %u\n",
		       qos->max_jobs_accrue_pa);

	if (qos->max_jobs_accrue_pu == INFINITE)
		printf("  MaxJobsAccruePerUser     = NONE\n");
	else if(qos->max_jobs_accrue_pu != NO_VAL)
		printf("  MaxJobsAccruePerUser     = %u\n",
		       qos->max_jobs_accrue_pu);

	if (qos->max_jobs_pa == INFINITE)
		printf("  MaxJobsPerAccount        = NONE\n");
	else if (qos->max_jobs_pa != NO_VAL)
		printf("  MaxJobsPerAccount        = %u\n",
		       qos->max_jobs_pa);
	if (qos->max_jobs_pu == INFINITE)
		printf("  MaxJobsPerUser = NONE\n");
	else if (qos->max_jobs_pu != NO_VAL)
		printf("  MaxJobsPerUser = %u\n",
		       qos->max_jobs_pu);

	if (qos->max_submit_jobs_pa == INFINITE)
		printf("  MaxSubmitJobsPerAccount  = NONE\n");
	else if (qos->max_submit_jobs_pa != NO_VAL)
		printf("  MaxSubmitJobsPerAccount  = %u\n",
		       qos->max_submit_jobs_pa);

	if (qos->max_submit_jobs_pu == INFINITE)
		printf("  MaxSubmitJobsPerUser     = NONE\n");
	else if (qos->max_submit_jobs_pu != NO_VAL)
		printf("  MaxSubmitJobsPerUser     = %u\n",
		       qos->max_submit_jobs_pu);

	if (qos->max_tres_pa) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			qos->max_tres_pa, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT, 0, NULL);
		printf("  MaxTRESPerAccount        = %s\n", tmp_char);
		xfree(tmp_char);
	}
	if (qos->max_tres_pj) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			qos->max_tres_pj, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT, 0, NULL);
		printf("  MaxTRESPerJob            = %s\n", tmp_char);
		xfree(tmp_char);
	}
	if (qos->max_tres_pn) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			qos->max_tres_pn, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT, 0, NULL);
		printf("  MaxTRESPerNode           = %s\n", tmp_char);
		xfree(tmp_char);
	}
	if (qos->max_tres_pu) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			qos->max_tres_pu, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT, 0, NULL);
		printf("  MaxTRESPerUser           = %s\n", tmp_char);
		xfree(tmp_char);
	}

	if (qos->min_prio_thresh == INFINITE)
		printf("  MinPrioThresh            = NONE\n");
	else if (qos->min_prio_thresh != NO_VAL)
		printf("  MinPrioThresh            = %u\n",
		       qos->min_prio_thresh);

	if (qos->min_tres_pj) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			qos->min_tres_pj, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT, 0, NULL);
		printf("  MinTRESPerJob            = %s\n", tmp_char);
		xfree(tmp_char);
	}
	if (qos->max_tres_mins_pj) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			qos->max_tres_mins_pj, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT, 0, NULL);
		printf("  MaxTRESMins              = %s\n", tmp_char);
		xfree(tmp_char);
	}
	if (qos->max_tres_run_mins_pa) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			qos->max_tres_run_mins_pa, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT, 0, NULL);
		printf("  MaxTRESRUNMinsPerAccount = %s\n", tmp_char);
		xfree(tmp_char);
	}
	if (qos->max_tres_run_mins_pu) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			qos->max_tres_run_mins_pu, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT, 0, NULL);
		printf("  MaxTRESRUNMinsPerUser    = %s\n", tmp_char);
		xfree(tmp_char);
	}

	if (qos->max_wall_pj == INFINITE)
		printf("  MaxWall                  = NONE\n");
	else if (qos->max_wall_pj != NO_VAL) {
		char time_buf[32];
		mins2time_str((time_t) qos->max_wall_pj,
			      time_buf, sizeof(time_buf));
		printf("  MaxWall                  = %s\n", time_buf);
	}

	if (qos->preempt_list) {
		char *temp_char = get_qos_complete_str(g_qos_list,
						       qos->preempt_list);
		if (temp_char) {
			printf("  Preempt                  = %s\n", temp_char);
			xfree(temp_char);
		}
	}

	if (qos->preempt_mode && (qos->preempt_mode != NO_VAL16)) {
		printf("  PreemptMode              = %s\n",
		       preempt_mode_string(qos->preempt_mode));
	}

	if (qos->preempt_exempt_time == INFINITE) {
		printf("  PreemptExemptTime        = NONE\n");
	} else if (qos->preempt_exempt_time != NO_VAL) {
		char time_buf[32];
		secs2time_str((time_t) qos->preempt_exempt_time, time_buf,
			      sizeof(time_buf));
		printf("  PreemptExemptTime        = %s\n",
	       		time_buf);
	}

	if (qos->priority == INFINITE)
		printf("  Priority                 = NONE\n");
	else if (qos->priority != NO_VAL)
		printf("  Priority                 = %d\n", qos->priority);

	if (qos->usage_factor == INFINITE)
		printf("  UsageFactor              = NONE\n");
	else if(qos->usage_factor != NO_VAL)
		printf("  UsageFactor              = %.4lf\n", qos->usage_factor);

	if (qos->usage_thres == INFINITE)
		printf("  UsageThreshold           = NONE\n");
	else if (qos->usage_thres != NO_VAL)
		printf("  UsageThreshold           = %.4lf\n", qos->usage_thres);

	if (qos->limit_factor == INFINITE)
		printf("  LimitFactor              = NONE\n");
	else if(qos->limit_factor != NO_VAL)
		printf("  LimitFactor              = %.4lf\n", qos->limit_factor);

}

extern int sort_coord_list(void *a, void *b)
{
	slurmdb_coord_rec_t *coord_a = *(slurmdb_coord_rec_t **)a;
	slurmdb_coord_rec_t *coord_b = *(slurmdb_coord_rec_t **)b;
	int diff;

	diff = xstrcmp(coord_a->name, coord_b->name);

	if (diff < 0)
		return -1;
	else if (diff > 0)
		return 1;

	return 0;
}

extern List sacctmgr_process_format_list(List format_list)
{
	List print_fields_list = list_create(destroy_print_field);
	ListIterator itr = list_iterator_create(format_list);
	print_field_t *field = NULL;
	char *object = NULL;

	while((object = list_next(itr))) {
		if (!(field = _get_print_field(object)))
			exit(1);

		list_append(print_fields_list, field);
	}
	list_iterator_destroy(itr);

	return print_fields_list;
}

extern int sacctmgr_validate_cluster_list(List cluster_list)
{
	List temp_list = NULL;
	char *cluster = NULL;
	int rc = SLURM_SUCCESS;
	ListIterator itr = NULL, itr_c = NULL;

	xassert(cluster_list);

	slurmdb_cluster_cond_t cluster_cond;
	slurmdb_init_cluster_cond(&cluster_cond, 0);
	cluster_cond.cluster_list = cluster_list;

	temp_list = slurmdb_clusters_get(db_conn, &cluster_cond);

	itr_c = list_iterator_create(cluster_list);
	itr = list_iterator_create(temp_list);
	while ((cluster = list_next(itr_c))) {
		slurmdb_cluster_rec_t *cluster_rec = NULL;

		list_iterator_reset(itr);
		while ((cluster_rec = list_next(itr))) {
			if (!xstrcasecmp(cluster_rec->name, cluster)) {
				if (cluster_rec->flags & CLUSTER_FLAG_EXT) {
					fprintf(stderr, " The cluster '%s' is an external cluster. Can't work with it.\n",
						cluster);
					list_delete_item(itr_c);
				}
				break;
			}
		}
		if (!cluster_rec) {
			exit_code=1;
			fprintf(stderr, " This cluster '%s' "
				"doesn't exist.\n"
				"        Contact your admin "
				"to add it to accounting.\n",
				cluster);
			list_delete_item(itr_c);
		}
	}
	list_iterator_destroy(itr);
	list_iterator_destroy(itr_c);
	FREE_NULL_LIST(temp_list);

	if (!list_count(cluster_list))
		rc = SLURM_ERROR;

	return rc;
}


extern void sacctmgr_initialize_g_tres_list(void)
{
	if (!g_tres_list) {
		slurmdb_tres_cond_t tres_cond;
		memset(&tres_cond, 0, sizeof(slurmdb_tres_cond_t));
		tres_cond.with_deleted = 1;
		g_tres_list = slurmdb_tres_get(db_conn, &tres_cond);
	}
}
