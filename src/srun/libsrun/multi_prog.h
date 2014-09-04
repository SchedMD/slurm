/*****************************************************************************\
 *  multi_prog.h - executing program according to task rank
 *                 set MPIR_PROCDESC accordingly
 *****************************************************************************
 *  Produced at National University of Defense Technology (China)
 *  Written by Hongjia Cao <hjcao@nudt.edu.cn>
 *  and
 *  Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
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
#ifndef _SRUN_MULTI_PROG_H
#define _SRUN_MULTI_PROG_H

/* set global MPIR_PROCDESC executable names based upon multi-program
 * configuration file */
extern int mpir_set_multi_name(int ntasks, const char *config_fname);
extern void mpir_init(int num_tasks);
extern void mpir_cleanup(void);
extern void mpir_set_executable_names(const char *executable_name);
extern void mpir_dump_proctable(void);

/*
 * Verify that we have a valid executable program specified for each task
 *	when the --multi-prog option is used.
 * IN config_name - MPMD configuration file name
 * IN/OUT ntasks - number of tasks to launch
 * IN/OUT ntasks_set - true if task count explicitly set by user
 * OUT ncmds - number of commands
 * RET 0 on success, -1 otherwise
 */
extern int verify_multi_name(char *config_fname, int *ntasks, bool *ntasks_set,
			     int32_t *ncmds);

#endif

