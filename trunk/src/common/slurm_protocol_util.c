/*****************************************************************************\
 *  slurm_protocol_util.c - communication infrastructure functions
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov> et. al.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <src/common/slurm_protocol_defs.h>
#include <src/common/slurm_protocol_api.h>
#include <src/common/slurm_protocol_common.h>
#include <src/common/slurm_protocol_util.h>
#include <src/common/log.h>
#include <src/common/xmalloc.h>

/* checks to see that the specified header was sent from a node running the same version of the protocol as the current node */
uint32_t check_header_version(header_t * header)
{
	if (header->version != SLURM_PROTOCOL_VERSION) {
		debug("Invalid Protocol Version %d", header->version);
		slurm_seterrno_ret(SLURM_PROTOCOL_VERSION_ERROR);
	}
	return SLURM_PROTOCOL_SUCCESS;
}

/* simple function to create a header, always insuring that an accurate version string is inserted */
void init_header(header_t * header, slurm_msg_type_t msg_type,
		 uint16_t flags)
{
	header->version = SLURM_PROTOCOL_VERSION;
	header->flags = flags;
	header->msg_type = msg_type;
}

void update_header(header_t * header, uint32_t cred_length,
		   uint32_t msg_length)
{
	header->cred_length = cred_length;
	header->body_length = msg_length;
}

/* checks to see that the specified header was sent from a node running the same version of the protocol as the current node */
uint32_t check_io_stream_header_version(slurm_io_stream_header_t * header)
{
	if (header->version != SLURM_PROTOCOL_VERSION) {
		debug("Invalid IO Stream Protocol Version %d ",
		      header->version);
		slurm_seterrno_ret(SLURM_PROTOCOL_IO_STREAM_VERSION_ERROR);
	}
	return SLURM_PROTOCOL_SUCCESS;
}

/* simple function to create a header, always insuring that an accurate version string is inserted */
void init_io_stream_header(slurm_io_stream_header_t * header, char *key,
			   uint32_t task_id, uint16_t type)
{
	assert(key != NULL);
	header->version = SLURM_PROTOCOL_VERSION;
	memcpy(header->key, key, SLURM_SSL_SIGNATURE_LENGTH);
	header->task_id = task_id;
	header->type = type;
}

int read_io_stream_header(slurm_io_stream_header_t * header, int fd)
{
	char *data;
	int  buf_size, rsize;
	Buf my_buf;

	buf_size = sizeof(slurm_io_stream_header_t);
	data = xmalloc(buf_size);
	rsize = slurm_read_stream(fd, data, buf_size);
	my_buf = create_buf (data, buf_size);
	if (rsize == buf_size)
		unpack_io_stream_header(header, my_buf);
	free_buf(my_buf);
	return rsize;
}

int write_io_stream_header(slurm_io_stream_header_t * header, int fd)
{
	int  buf_size, wsize;
	Buf my_buf;

	buf_size = sizeof(slurm_io_stream_header_t);
	my_buf = init_buf(buf_size);
	pack_io_stream_header(header, my_buf);
	wsize = slurm_write_stream(fd, get_buf_data(my_buf), get_buf_offset(my_buf));
	free_buf(my_buf);
	return wsize;
}

int read_io_stream_header2(slurm_io_stream_header_t * header, int fd)
{
	int rsize;

	rsize = slurm_read_stream(fd, (char *)&header->version, 
			          sizeof(header->version));
	if (rsize != sizeof(header->version)) 
		return rsize;
	header->version = ntohs(header->version);

	rsize = slurm_read_stream(fd, (char *) &header->key, sizeof(header->key));
	if (rsize != sizeof(header->key)) 
		return rsize;

	rsize = slurm_read_stream(fd, (char *) &header->task_id, 
			          sizeof(header->task_id));
	if (rsize != sizeof(header->task_id)) 
		return rsize;
	header->task_id = ntohl(header->task_id);

	rsize = slurm_read_stream(fd, (char *) &header->type, sizeof(header->type));
	if (rsize != sizeof(header->type))
		return rsize;
	header->type = ntohs(header->type);

	return SLURM_SUCCESS;
}

int write_io_stream_header2(slurm_io_stream_header_t * header, int fd)
{
	int write_size;
	slurm_io_stream_header_t header2 = *header;

	header->version = htons(header2.version);
	if ((write_size =
	     slurm_write_stream(fd, (char *) &header2.version,
				sizeof(header2.version))) !=
	    sizeof(header2.version)) {
		return write_size;
	}

	if ((write_size =
	     slurm_write_stream(fd, header2.key,
				sizeof(header2.key))) !=
	    sizeof(header2.key)) {
		return write_size;
	}

	header->task_id = htonl(header2.task_id);
	if ((write_size =
	     slurm_write_stream(fd, (char *) &header2.version,
				sizeof(header2.task_id))) !=
	    sizeof(header2.task_id)) {
		return write_size;
	}

	header->type = htons(header2.type);
	if ((write_size =
	     slurm_write_stream(fd, (char *) &header2.version,
				sizeof(header2.type))) !=
	    sizeof(header2.type)) {
		return write_size;
	}

	return SLURM_SUCCESS;
}

void slurm_print_job_credential(FILE * stream,
				slurm_job_credential_t * credential)
{
	debug3("credential.job_id: %i", credential->job_id);
	debug3("credential.user_id: %i", credential->user_id);
	debug3("credential.node_list: %s", credential->node_list);
	debug3("credential.expiration_time: %lu",
	       credential->expiration_time);
	debug3("credential.signature: %#x", credential->signature);
}

void slurm_print_launch_task_msg(launch_tasks_request_msg_t * msg)
{
	int i;
	debug3("job_id: %i", msg->job_id);
	debug3("job_step_id: %i", msg->job_step_id);
	debug3("uid: %i", msg->uid);
	slurm_print_job_credential(stderr, msg->credential);
	debug3("tasks_to_launch: %i", msg->tasks_to_launch);
	debug3("envc: %i", msg->envc);
	for (i = 0; i < msg->envc; i++) {
		debug3("env[%i]: %s", i, msg->env[i]);
	}
	debug3("cwd: %s", msg->cwd);
	debug3("argc: %i", msg->argc);
	for (i = 0; i < msg->argc; i++) {
		debug3("argv[%i]: %s", i, msg->argv[i]);
	}
	debug3("msg -> resp_port = %d", msg->resp_port);
	debug3("msg -> io_port   = %d", msg->io_port);

	for (i = 0; i < msg->tasks_to_launch; i++) {
		debug3("global_task_id[%i]: %i ", i,
		       msg->global_task_ids[i]);
	}
}
