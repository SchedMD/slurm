/*****************************************************************************\
 *  update_powercap.c - powercapping update function for scontrol.
 *****************************************************************************
 *  Copyright (C) 2013 CEA/DAM/DIF
 *  Written by Matthieu Hautreux <matthieu.hautreux@cea.fr>
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

#include "src/common/proc_args.h"
#include "src/scontrol/scontrol.h"

static uint32_t _parse_watts(char * watts_str)
{
	uint32_t watts_num = 0;
	char *end_ptr = NULL;

	if (!xstrcasecmp(watts_str, "n/a") || !xstrcasecmp(watts_str, "none"))
		return watts_num;
	if (!xstrcasecmp(watts_str, "INFINITE"))
		return INFINITE;
	watts_num = strtol(watts_str, &end_ptr, 10);
	if ((end_ptr[0] == 'k') || (end_ptr[0] == 'K'))
		watts_num *= 1000;
	else if ((end_ptr[0] == 'm') || (end_ptr[0] == 'M'))
		watts_num *= 1000000;
	else if (end_ptr[0] != '\0')
		watts_num = NO_VAL;
	return watts_num;
}

/*
 * scontrol_update_powercap - update the slurm powercapping configuration per the
 *	supplied arguments
 * IN argc - count of arguments
 * IN argv - list of arguments
 * RET 0 if no slurm error, errno otherwise. parsing error prints
 *			error message and returns 0
 */
extern int
scontrol_update_powercap (int argc, char **argv)
{
	update_powercap_msg_t powercap_msg;
	int i;
	char *tag, *val;
	int taglen;

	memset(&powercap_msg, 0, sizeof(update_powercap_msg_t));
	powercap_msg.power_cap = NO_VAL;
	powercap_msg.min_watts = NO_VAL;
	powercap_msg.cur_max_watts = NO_VAL;
	powercap_msg.adj_max_watts = NO_VAL;
	powercap_msg.max_watts = NO_VAL;

	for (i = 0; i < argc; i++) {
		tag = argv[i];
		val = strchr(argv[i], '=');
		if (val) {
			taglen = val - argv[i];
			val++;
		} else {
			exit_code = 1;
			error("Invalid input: %s  Request aborted", argv[i]);
			return -1;
		}

		if (strncasecmp(tag, "PowerCap", MAX(taglen, 8)) == 0) {
			powercap_msg.power_cap = _parse_watts(val);
			break;
		}
	}

	if (powercap_msg.power_cap == NO_VAL) {
		exit_code = 1;
		error("Invalid PowerCap value.");
		return 0;
	}

	if (slurm_update_powercap(&powercap_msg)) {
		exit_code = 1;
		return slurm_get_errno ();
	} else
		return 0;
}
