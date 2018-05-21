/*****************************************************************************\
 *  msg_aggr.h - Message Aggregator for sending messages to the
 *               slurmctld, if a reply is expected this also will wait
 *               and get that reply when received.
 *****************************************************************************
 *  Copyright (C) 2015 Bull S. A. S.
 *		Bull, Rue Jean Jaures, B.P.68, 78340, Les Clayes-sous-Bois.
 *  Copyright (C) 2015 SchedMD LLC.
 *  Written by Martin Perry <martin.perry@bull.com>
 *             Danny Auble <da@schedmd.com>
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

#ifndef _MSG_AGGR_H
#define _MSG_AGGR_H

#include "src/common/slurm_protocol_defs.h"

extern void msg_aggr_sender_init(char *host, uint16_t port, uint64_t window,
				 uint64_t max_msg_cnt);
extern void msg_aggr_sender_reconfig(uint64_t window, uint64_t max_msg_cnt);
extern void msg_aggr_sender_fini(void);

/* add a message that needs to be sent.
 * IN: msg - message to be sent
 * IN: wait - whether or not we need to wait for a response
 * IN: resp_callback - function to process response
 */
extern void msg_aggr_add_msg(slurm_msg_t *msg, bool wait,
			     void (*resp_callback) (slurm_msg_t *msg));
extern void msg_aggr_add_comp(Buf buffer, void *auth_cred, header_t *header);
extern void msg_aggr_resp(slurm_msg_t *msg);

#endif
