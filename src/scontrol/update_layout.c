/*****************************************************************************\
 *  update_layout.c - layout update functions for scontrol.
 *****************************************************************************
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

#include "scontrol.h"
#include "src/common/pack.h"
#include "src/common/slurm_protocol_defs.h"

/*
 * scontrol_print_layout - print information about the supplied layout
 * IN argc - count of arguments
 * IN argv - list of arguments
 * RET 0 if no slurm error, errno otherwise. parsing error prints
 *			error message and returns 0
 */
extern int
scontrol_update_layout (int argc, char **argv)
{
	int rc = 0;
	int i = 0, tag_len = 0;
	char *tag = NULL, *val = NULL;
	update_layout_msg_t msg;
	char *opt = NULL;

	opt = xstrdup_printf(" ");
	memset(&msg, 0, sizeof(update_layout_msg_t));
	while (i < argc) {
		tag = argv[i];
		val = strchr(argv[i], '=');
		if (val) {
			tag_len = val - argv[i];
			val++;
		} else {
			exit_code = 1;
			fprintf (stderr,
				 "invalid option:%s for layouts "
				 "(\"=\" mandatory)\n",
				 tag);
			goto done;
		}
		if (xstrncasecmp(tag, "layouts", MAX(tag_len, 2)) == 0) {
			msg.layout = val;
		} else if (xstrncasecmp(tag, "entity", MAX(tag_len, 2)) == 0) {
			msg.arg = xstrdup_printf("Entity=%s", val);
		} else {
			xstrcat(opt, tag);
			xstrcat(opt, " ");
		}
		i++;
	}

	if (msg.layout == NULL) {
		exit_code = 1;
		fprintf (stderr,
			 "No valid layout name in update command\n");
		goto done;
	}
	if (msg.arg == NULL) {
		exit_code = 1;
		fprintf (stderr,
			 "No valid layout enity in update command\n");
		goto done;
	}
	if ( strlen(opt) <= 1 ) {
		exit_code = 1;
		fprintf (stderr,
			 "No valid updates arguments in update command\n");
		goto done;
	}

	xstrcat(msg.arg, opt);

	rc = slurm_update_layout(&msg);

done:	xfree(msg.arg);
	xfree(opt);
	if (rc) {	
		exit_code = 1;
		return slurm_get_errno ();
	} else
		return 0;
}
