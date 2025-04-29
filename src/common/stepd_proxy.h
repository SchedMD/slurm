/*****************************************************************************\
 *  stepd_proxy.h
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#ifndef _STEPD_PROXY_H
#define _STEPD_PROXY_H

/*
 * Initialize listening socket on slurmd for stepd proxy
 */
extern void stepd_proxy_slurmd_init(char *spooldir);

/*
 * Initialize slurmd address for creating connections
 */
extern void stepd_proxy_stepd_init(char *spooldir);

/*
 * Send message to slurmctld via slurmd. Do not get response.
 *
 * See slurm_send_only_controller_msg() for function description
 */
extern int stepd_proxy_send_only_ctld_msg(slurm_msg_t *req);

/*
 * Send message to and receive response from slurmctld via slurmd
 *
 * See slurm_send_only_controller_msg() for function description
 */
extern int stepd_proxy_send_recv_ctld_msg(slurm_msg_t *req, slurm_msg_t *resp);

/*
 * Send message to another node via slurmd. Do not get response.
 *
 * See slurm_send_only_node_msg() for function description
 */
extern int stepd_proxy_send_only_node_msg(slurm_msg_t *req);

/*
 * Send message to and receive response from another node via slurmd
 *
 * See slurm_send_recv_node_msg() for function description
 */
extern int stepd_proxy_send_recv_node_msg(slurm_msg_t *req, slurm_msg_t *resp,
					  int timeout);

/*
 * Send message to slurmstepd, and receive response
 *
 * IN req - message to send to slurmstepd
 * OUT resp_buf - response from slurmstep (if reply is true)
 * IN step_id - stepd step id
 * IN stepd_fd - open connection to stepd's unix socket
 * IN reply - true if stepd needs to send a response
 */
extern int stepd_proxy_send_recv_to_stepd(slurm_msg_t *req, buf_t **resp_buf,
					  slurm_step_id_t *step_id,
					  int stepd_fd, bool reply);
/*
 * Send response message to slurmd
 *
 * Use this when message was sent to slurmstepd via
 * stepd_proxy_send_recv_to_stepd(). This is meant to replace
 * send_msg_response() for slurmstepd sending response to slurmd.
 *
 * IN source_msg - Message to send response about
 * IN msg_type - RPC message type
 * IN data - pointer to message data which corresponds to msg_type
 * RET SLURM_SUCCESS or error
 */
extern int stepd_proxy_send_resp_to_slurmd(int fd, slurm_msg_t *source_msg,
					   slurm_msg_type_t msg_type,
					   void *data);

#endif
