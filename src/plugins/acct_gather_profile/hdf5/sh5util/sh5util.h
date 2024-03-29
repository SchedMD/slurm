/*****************************************************************************\
 *  sh5util.h - slurm profile accounting plugin for io and energy using hdf5.
 *            - Utility to merge node-step files into a job file
 *            - or extract data from an job file
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com>.
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
 *
\*****************************************************************************/

#ifndef __ACCT_SH5UTIL_H__
#define __ACCT_SH5UTIL_H__

typedef enum {
	SH5UTIL_MODE_MERGE,
	SH5UTIL_MODE_EXTRACT,
	SH5UTIL_MODE_ITEM_EXTRACT,
	SH5UTIL_MODE_ITEM_LIST,
} sh5util_mode_t;

typedef struct {
	char *dir;
	int help;
	char *input;
	int job_id;
	bool keepfiles;
	char *level;
	sh5util_mode_t mode;
	char *node;
	char *output;
	char *series;
	char *data_item;
	int step_id;
	char *user;
	int verbose;
} sh5util_opts_t;

extern sh5util_opts_t params;

#endif // __ACCT_SH5UTIL_H__
