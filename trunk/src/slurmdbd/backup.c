/*****************************************************************************\
 *  backup.c - backup slurm dbd
 *****************************************************************************
 *  Copyright (C) 2009  Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#include <sys/poll.h>

#include "src/common/xmalloc.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/slurmdbd_defs.h"

#include "src/slurmdbd/backup.h"

bool primary_resumed = false;
bool backup = false;
bool have_control = false;

static slurm_fd  slurmdbd_fd         = -1;

/* Open a connection to the Slurm DBD and set slurmdbd_fd */
static void _open_slurmdbd_fd(slurm_addr dbd_addr)
{
	if(dbd_addr.sin_port == 0) {
		error("sin_port == 0 in the slurmdbd backup");
		return;
	}

       	slurmdbd_fd = slurm_open_msg_conn(&dbd_addr);
	
	if (slurmdbd_fd >= 0)
		fd_set_nonblocking(slurmdbd_fd);
}

/* Close the SlurmDbd connection */
static void _close_slurmdbd_fd(void)
{
	if (slurmdbd_fd >= 0) {
		close(slurmdbd_fd);
		slurmdbd_fd = -1;
	}
}

/* Reopen the Slurm DBD connection due to some error */
static void _reopen_slurmdbd_fd(slurm_addr dbd_addr)
{
	_close_slurmdbd_fd();
	_open_slurmdbd_fd(dbd_addr);
}

/* run_backup - this is the backup controller, it should run in standby 
 *	mode, assuming control when the primary controller stops responding */
extern void run_backup(void)
{
	slurm_addr dbd_addr;
		
	primary_resumed = false;

	/* get a connection */
	slurm_set_addr(&dbd_addr, slurmdbd_conf->dbd_port,
		       slurmdbd_conf->dbd_host);

	if (dbd_addr.sin_port == 0)
		error("Unable to locate SlurmDBD host %s:%u", 
		      slurmdbd_conf->dbd_host, slurmdbd_conf->dbd_port);
	else 
		_open_slurmdbd_fd(dbd_addr);
	

	/* repeatedly ping Primary */
	while (!shutdown_time) {
		bool writeable = fd_writeable(slurmdbd_fd);
		//info("%d %d", have_control, writeable);

		if (have_control && writeable) {
			info("Primary has come back");
			primary_resumed = true;
			shutdown_threads();
			have_control = false;
			break;
		} else if(!have_control && !writeable) {
			have_control = true;
			info("Taking Control");
			break;
		}
		
		sleep(1);
		if(!writeable) 
			_reopen_slurmdbd_fd(dbd_addr);
	}

	_close_slurmdbd_fd();

	return;
}
