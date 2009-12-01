/*****************************************************************************\
 *  initialize.c - Initialization handshake between Slurm and Moab
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
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

#include "./msg.h"
#include <strings.h>
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"

/*
 * initialize_wiki - used by Moab to communication desired format information
 * cmd_ptr IN   - CMD=INITIALIZE EPORT=<port> USEHOSTEXP=[N|T|F]
 *                USEHOSTEXP=N : use hostlist expression for GETNODES messages
 *                USEHOSTEXP=T : use hostlist expression for GETJOBS messages
 *                USEHOSTEXP=F : use no hostlist expressions
 * err_code OUT - 0 or an error code
 * err_msg OUT  - response message
 * RET 0 on success, -1 on failure
 */
extern int	initialize_wiki(char *cmd_ptr, int *err_code, char **err_msg)
{
	char *arg_ptr, *eport_ptr, *exp_ptr, *use_ptr;
	static char reply_msg[128];

	arg_ptr = strstr(cmd_ptr, "ARG=");
	if (arg_ptr == NULL) {
		*err_code = -300;
		*err_msg = "INITIALIZE lacks ARG=";
		error("wiki: INITIALIZE lacks ARG=");
		return -1;
	}
	eport_ptr = strstr(cmd_ptr, "EPORT=");
	exp_ptr   = strstr(cmd_ptr, "USEHOSTEXP=");
	if (eport_ptr) {
		eport_ptr += 6;
		e_port = (uint16_t) strtoul(eport_ptr, NULL, 10);
	}
	if (exp_ptr) {
		exp_ptr += 11;
		if (exp_ptr[0] == 'T')
			use_host_exp = 1;
		else if (exp_ptr[0] == 'F')
			use_host_exp = 0;
		else if (exp_ptr[0] == 'N')
			use_host_exp = 2;
		else {
			*err_code = -300;
			*err_msg = "INITIALIZE has invalid USEHOSTEXP";
			error("wiki: INITIALIZE has invalid USEHOSTEXP");
			return -1;
		}
	}

	if      (use_host_exp == 2)
		use_ptr = "N";
	else if (use_host_exp == 1)
		use_ptr = "T";
	else
		use_ptr = "F";
	snprintf(reply_msg, sizeof(reply_msg),
		"EPORT=%u USEHOSTEXP=%s",
		e_port, use_ptr);
	*err_msg = reply_msg;
	return 0;
}
