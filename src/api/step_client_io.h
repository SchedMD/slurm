/*****************************************************************************\
 * src/api/step_client_io.h - job-step client-side I/O routines
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher J. Morrone <morrone2@llnl.gov>
 *  UCRL-CODE-217948.
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
#ifndef _HAVE_CLIENT_IO_H
#define _HAVE_CLIENT_IO_H

#include <stdint.h>

#include "src/common/eio.h"
#include "src/common/list.h"
#include "src/common/dist_tasks.h"

typedef struct client_io {
	/* input parameters - set (indirectly) by user */
	int num_tasks;
	int num_nodes;
	bool label;
	int label_width;
	char *io_key;

	/* internal variables */
	pthread_t ioid;		/* stdio thread id 		  */
	int num_listen;		/* Number of stdio listen sockets */
	int *listensock;	/* Array of stdio listen sockets  */
	int *listenport;	/* Array of stdio listen ports 	  */

	eio_handle_t *eio;      /* Event IO handle for stdio traffic */
	int ioservers_ready;    /* Number of servers that established contact */
	eio_obj_t **ioserver;	/* Array of nhosts pointers to eio_obj_t */
	eio_obj_t *stdin_obj;
	eio_obj_t *stdout_obj;
	eio_obj_t *stderr_obj;
	List free_incoming;     /* List of free struct io_buf * for incoming
				 * traffic. "incoming" means traffic from the
				 * client to the tasks.
				 */
	List free_outgoing;     /* List of free struct io_buf * for outgoing
				 * traffic "outgoing" means traffic from the
				 * tasks to the client.
				 */
	int incoming_count;     /* Count of total incoming message buffers
			         * including free_incoming buffers and
			         * buffers in use.
			         */
	int outgoing_count;     /* Count of total incoming message buffers
			         * including free_incoming buffers and
			         * buffers in use.
			         */
} client_io_t;

typedef struct client_io_fds {
	struct {
		int fd;
		uint32_t taskid;
		uint32_t nodeid;
	} in, out, err;
} client_io_fds_t;

#define CLIENT_IO_FDS_INITIALIZER {{0, (uint32_t)-1, (uint32_t)-1},\
                                   {1, (uint32_t)-1, (uint32_t)-1},\
                                   {2, (uint32_t)-1, (uint32_t)-1}}

client_io_t *
client_io_handler_create(client_io_fds_t fds,
			 int num_tasks,
			 int num_nodes,
			 char *io_key,
			 bool label);
int client_io_handler_start(client_io_t *cio);
int client_io_handler_finish(client_io_t *cio);
void client_io_handler_destroy(client_io_t *cio);

#endif /* !_HAVE_CLIENT_IO_H */
