/*****************************************************************************\
 *  x11_util.h - x11 forwarding support functions
 *		 also see src/slurmd/slurmstepd/x11_forwarding.[ch]
 *****************************************************************************
 *  Copyright (C) 2017 SchedMD LLC.
 *  Written by Tim Wickberg <tim@schedmd.com>
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

#ifndef _X11_UTIL_H_
#define _X11_UTIL_H_

/*
 * X11 displays use a TCP port that is 6000 + $display_number.
 * E.g., DISPLAY=localhost:10.0 is TCP port 6010.
 */
#define X11_TCP_PORT_OFFSET 6000

/* convert a --x11 argument into flags */
uint16_t x11_str2flags(const char *str);

/*
 * Get local TCP port for X11 from DISPLAY environment variable.
 *
 * Warning - will call exit(-1) if not able to retrieve.
 */
extern int x11_get_display_port(void);

/*
 * Retrieve the X11 magic cookie for the local DISPLAY
 * so we can use it on the remote end point.
 *
 * Warning - will call exit(-1) if not able to retrieve.
 */
extern char *x11_get_xauth(void);

extern int x11_set_xauth(char *xauthority, char *cookie,
			 char *host, uint16_t display);

extern int x11_delete_xauth(char *xauthority, char *host, uint16_t display);

#endif /* _X11_UTIL_H_ */
