/*****************************************************************************\
 *  ports.c - Library for managing a switch on a Cray system.
 *****************************************************************************
 *  Copyright (C) 2014 SchedMD LLC
 *  Copyright 2014 Cray Inc. All Rights Reserved.
 *  Written by David Gloe <c16817@cray.com>
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

#include "switch_cray.h"

#ifdef HAVE_NATIVE_CRAY
#include <unistd.h>

// Global variables
bitstr_t *port_resv = NULL;
uint32_t last_alloc_port = 0;
pthread_mutex_t port_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * Function: assign_ports
 * Description:
 *  Looks for and assigns the next contiguous block of num_ports ports.
 *  These ports are used by Cray's PMI for its communications to
 *  manage its control tree.
 *
 *  To avoid port conflicts, this function selects a large range of
 *  ports within the middle of the port range where it assumes no
 *  ports are used.  No special precautions are taken to handle a
 *  selected port already in use by some other non-SLURM component
 *  on the node.
 *
 *  If there are no free ports, then it loops through the entire table
 *  ATTEMPTS number of times before declaring a failure.
 *
 *  real_port is an output variable that on success holds the port
 *  number of the first port in the block of ports.
 *
 * Returns:
 *  0 on success and -1 on failure.
 */
int assign_ports(uint32_t *real_port, int num_ports)
{
	int port, attempts = 0, block_size;

	// Sanity check arguments
	if (real_port == NULL) {
		CRAY_ERR("real_port address was NULL");
		return -1;
	} else if (num_ports < 1) {
		CRAY_ERR("Asking for %d < 1 port", num_ports);
		return -1;
	} else if (num_ports > PORT_CNT) {
		CRAY_ERR("Asking for %d > %d ports", num_ports, PORT_CNT);
		return -1;
	}

	/*
	 * Find an unreserved port to assign.
	 * Abandon the attempt if we've been through the available ports ATTEMPT
	 * number of times
	 */
	pthread_mutex_lock(&port_mutex);
	port = (last_alloc_port + 1) % PORT_CNT;
	block_size = 0;
	for (attempts = 0; attempts < (PORT_CNT + 1) * ATTEMPTS;
	     attempts++, port++) {
		// Give some other threads a chance
		if (attempts > 0 && attempts % PORT_CNT == 0) {
			CRAY_INFO("Unable to find free port block, retrying");
			pthread_mutex_unlock(&port_mutex);
			usleep(100000);
			pthread_mutex_lock(&port_mutex);
		}

		// If we've overflowed, reset
		if (port > PORT_CNT) {
			port = -1;
			block_size = 0;
			continue;
		}

		// If this is allocated, continue
		if (bit_test(port_resv, port)) {
			block_size = 0;
			continue;
		}

		// This port is free, expand the current block
		block_size++;

		// If the current block is large enough, reserve it
		if (block_size >= num_ports) {
			bit_nset(port_resv, port - block_size + 1, port);
			last_alloc_port = port;
			pthread_mutex_unlock(&port_mutex);
			*real_port = (port + MIN_PORT);
			return 0;
		}
	}
	pthread_mutex_unlock(&port_mutex);

	CRAY_ERR("Couldn't find an open port block");
	return -1;
}

/*
 * Function: release_ports
 * Description:
 *  Release the block of ports starting at real_port.
 *
 * Returns:
 *  0 on success and -1 on failure.
 */
int release_ports(uint32_t real_port, int num_ports)
{
	uint32_t port;

	if (real_port < MIN_PORT || real_port + num_ports - 1 >= MAX_PORT) {
		if (num_ports == 1) {
			CRAY_ERR("Port %"PRIu32" outside of valid range %"PRIu32
			 "-%"PRIu32, real_port, MIN_PORT, MAX_PORT);
		} else {
			CRAY_ERR("Port block %"PRIu32"-%"PRIu32" outside of"
				 "valid range %"PRIu32"-%"PRIu32,
				 real_port, real_port + num_ports - 1,
				 MIN_PORT, MAX_PORT);
		}
		return -1;
	}

	port = real_port - MIN_PORT;

	pthread_mutex_lock(&port_mutex);
	bit_nclear(port_resv, port, port + num_ports - 1);
	pthread_mutex_unlock(&port_mutex);
	return 0;
}

#endif
