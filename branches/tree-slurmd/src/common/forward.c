/*****************************************************************************\
 *  submit.c - submit a job with supplied contraints
 *  $Id: submit.c 6636 2005-11-17 19:50:10Z jette $
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by moe jette <jette1@llnl.gov>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

#include <slurm/slurm.h>

#include "forward.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"

/*
 * set_forward_addrs - add to the message possible forwards to go to
 * IN: msg         - slurm_msg_t * - message to add forwards to
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
			      char *forward_names)
{
        int span = 0;
	int j = 1;
	
	if((total - *pos) > (FORWARD_COUNT-thr_count)) {
		/* FIXME:!!!!! */
		/* I think this is the way to go, but am not sure */
	        span = FORWARD_COUNT;
	} else 
		span = 0;
	if(span > 0) {
		forward->addr = 
			xmalloc(sizeof(struct sockaddr_in) * span);
		forward->name = 
			xmalloc(sizeof(char) * (MAX_NAME_LEN * span));
					
		while(j<span 
		      && ((*pos+j) < total)) {
			forward->addr[j-1] = forward_addr[*pos+j];
			strncpy(&forward->name[(j-1) * MAX_NAME_LEN], 
				&forward_names[(*pos+j) * MAX_NAME_LEN], 
				MAX_NAME_LEN);
			
			j++;
		}
		j--;
		forward->cnt = j;
		*pos += j;
	} else {
		forward->cnt = 0;
		forward->addr = NULL;
		forward->name = NULL;
	}
	
	return SLURM_SUCCESS;
}
