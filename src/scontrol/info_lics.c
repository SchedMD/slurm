/*****************************************************************************\
 *  info_lics.c - licenses information functions for scontrol.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) 2013 SchedMD
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by David Bigagli david@schemd.com
 *  CODE-OCEC-09-009. All rights reserved.
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

static void _print_license_info(const char *, license_info_msg_t *);

/* scontrol_print_licenses()
 *
 * Retrieve and display the license information
 * from the controller
 *
 */
void
scontrol_print_licenses(const char *name)
{
	int cc;
	license_info_msg_t *msg;
	uint16_t show_flags;
	static time_t last_update;

	show_flags = 0;
	/* call the controller to get the meat
	 */
	cc = slurm_load_licenses(last_update, &msg, show_flags);
	if (cc != SLURM_PROTOCOL_SUCCESS) {
		/* Hosed, crap out.
		 */
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror ("slurm_load_license error");
		return;
	}

	last_update = time(NULL);
	/* print the info
	 */
	_print_license_info(name, msg);

	/* free at last
	 */
	slurm_free_license_info_msg(msg);

	return;
}

/* _print_license_info()
 *
 * Print the license information.
 */
static void _print_license_info(const char *name, license_info_msg_t *msg)
{
	int cc;

	if (!msg->num_lic) {
		printf("No licenses configured in Slurm.\n");
		return;
	}

	for (cc = 0; cc < msg->num_lic; cc++) {
		if (name && strcmp(msg->lic_array[cc].name, name))
			continue;
		printf("LicenseName=%s%sTotal=%d Used=%u Free=%u Remote=%s\n",
		       msg->lic_array[cc].name,
		       one_liner ? " " : "\n    ",
		       msg->lic_array[cc].total,
		       msg->lic_array[cc].in_use,
		       msg->lic_array[cc].available,
		       msg->lic_array[cc].remote ? "yes" : "no");
		if (name)
			break;
	}
}
