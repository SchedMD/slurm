/*****************************************************************************\
 *  power_save.h - Support node power saving mode. Nodes which have been
 *  idle for an extended period of time will be placed into a power saving
 *  mode by running an arbitrary script. This script can lower the voltage
 *  or frequency of the nodes or can completely power the nodes off.
 *  When the node is restored to normal operation, another script will be
 *  executed. Many parameters are available to control this mode of operation.
 *****************************************************************************
 *  Copyright (C) 2016 SchedMD LLC
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

#include "src/slurmctld/slurmctld.h"

#ifndef _HAVE_POWER_SAVE_H
#define _HAVE_POWER_SAVE_H

/*
 * config_power_mgr - Read power management configuration
 */
extern void config_power_mgr(void);

/* start_power_mgr - Start power management thread as needed. The thread
 *	terminates automatically at slurmctld shutdown time.
 * IN thread_id - pointer to thread ID of the started pthread.
 */
extern void start_power_mgr(pthread_t *thread_id);

/* Report if node power saving is enabled */
extern bool power_save_test(void);

/* power_job_reboot - Reboot compute nodes for a job from the head node */
extern int power_job_reboot(struct job_record *job_ptr);

#endif /* _HAVE_POWER_SAVE_H */
