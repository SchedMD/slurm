/*****************************************************************************\
 *  forward.h - get/print the job state information of slurm
 *
 *  $Id: job_info.h 4911 2005-05-19 00:59:52Z jette $
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifndef _FORWARD_H
#define _FORWARD_H

#include <stdint.h>
#include "src/common/slurm_protocol_api.h"

#define FORWARD_COUNT	50	/* maximum number of 
				   forwards per node */

/* STRUCTURES */

extern int forward_msg(forward_struct_t *forward_struct, 
		       header_t *header);

/*
 * set_forward_addrs - add to the message possible forwards to go to
 * IN: forward     - forward_t *   - struct to store forward info
 * IN: thr_count   - int           - number of messages already done
 * IN: pos         - int *         - posistion in the forward_addr and names
 *                                   will change to update to set the 
 *				     correct start after forwarding 
 *      			     information has been added.
 * IN: forward_addr- sockaddr_in * - list of address structures to forward to
 * IN: forward_names - char *      - list of names in MAX_NAME_LEN increments
 * RET: SLURM_SUCCESS - int
 */
extern int set_forward_addrs (forward_t *forward, 
			      int thr_count,
			      int *pos,
			      int total,
			      struct sockaddr_in *forward_addr,
			      char *forward_names);

extern int *set_span(int total);

#endif
