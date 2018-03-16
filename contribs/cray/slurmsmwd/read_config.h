/*****************************************************************************\
 *  read_config.h - Define symbols used to read configuration file for
 *  slurmsmwd
 *****************************************************************************
 *  Copyright (C) 2017 Regents of the University of California
 *  Written by Douglas Jacobsen <dmjacobsen@lbl.gov>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com>.
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

#ifndef _HAVE_SLURMSMWD_READ_CONFIG_H
#define _HAVE_SLURMSMWD_READ_CONFIG_H

#include <inttypes.h>
#include <sys/types.h>
#include <unistd.h>

extern uint16_t slurmsmwd_cabinets_per_row;
extern uint16_t slurmsmwd_debug_level;
extern char *   slurmsmwd_log_file;

/* Configuration functions */

/* Load configuration file contents into global variables. */
extern void slurmsmwd_read_config(void);
extern void slurmsmwd_print_config(void);

#endif	/* _HAVE_SLURMSMWD_READ_CONFIG_H */
