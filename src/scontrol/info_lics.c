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

static void print_license_info(const char *,
                               struct license_info_msg *);

/* scontrol_print_licenses()
 *
 * Retrieve and display the license information
 * from the controller
 *
 */
void
scontrol_print_licenses(const char *feature)
{
	int cc;
	struct license_info_msg *msg;
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
	print_license_info(feature, msg);

	/* free at last
	 */
	slurm_free_license_info_msg(msg);

	return;
}

/* print_license_info()
 *
 * Print the license information.
 */
static void
print_license_info(const char *feature, struct license_info_msg *msg)
{
	int cc;

	if (msg->num_features == 0) {
		printf("No licenses configured in SLURM.\n");
		return;
	}

	for (cc = 0; cc < msg->num_features; cc++) {
		if (one_liner) {
			printf("LicenseName=%s ", msg->lic_array[cc].feature);
			printf("Total=%d ", msg->lic_array[cc].total);
		} else {
			printf("LicenseName=%s\n", msg->lic_array[cc].feature);
			printf("    Total=%d ", msg->lic_array[cc].total);
		}
		printf("Used=%d ", msg->lic_array[cc].in_use);
		printf("Free=%d\n", msg->lic_array[cc].available);
	}
}
