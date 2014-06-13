/*****************************************************************************\
 *  step_terminate_monitor.h - Run an external program if there are 
 *    unkillable processes at step termination.
 *****************************************************************************
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher J. Morrone <morrone2@llnl.gov>
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifndef _SLURMD_TERM_MONITOR_H
#define _SLURMD_TERM_MONITOR_H

/*
 * Start a monitor pthread that will wait for a period of time,
 * as defined in the slurm.conf variable UnkillableStepTimeout,
 * and then execute the program specified in the slurm.conf
 * variable UnkillableStepProgram.  (If UnkillableStepProgram is
 * not defined, the monitor thread and associated timer will not
 * be started.)
 *
 * The idea is to call this start function just before beginning
 * step termination.  Then, if processes in the job step are unkillable,
 * an external program will be called that may be able to deal with
 * the situation.
 *
 * If step_terminate_monitor_stop() is called before the time runs
 * out, the external program will not be called.
 */
void step_terminate_monitor_start(uint32_t jobid, uint32_t stepid);

/*
 * Stop the timer in the step terminate monitor pthread, and kill
 * said pthread.
 */
void step_terminate_monitor_stop(void);

#endif /* !_SLURMD_TERM_MONITOR_H */
