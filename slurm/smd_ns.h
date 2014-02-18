/*****************************************************************************\
 *  smd_ns.h - Library for fault tolerant application support
 *****************************************************************************
 *  Copyright (C) 2013-2014 SchedMD LLC
 *  Written by Morris Jette and David Bigagli (SchedMD LLC)
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

#ifndef _HAVE_SMD_NS_H
#define _HAVE_SMD_NS_H

#if HAVE_CONFIG_H
#  include "config.h"
#endif
#if HAVE_STDINT_H
#  include <stdint.h>		/* for uint16_t, uint32_t definitions */
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>		/* for uint16_t, uint32_t definitions */
#endif
#if HAVE_STDBOOL_H
#  include <stdbool.h>
#else
typedef enum {false, true} bool;
#endif /* !HAVE_STDBOOL_H */

#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <ctype.h>
#include <time.h>
#include <stdarg.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/param.h>
#include <sys/time.h>

/* Faulty can be in state FAILED or FAILING
 * these flags tell the controller which one
 * the caller is interested in.
 */
#define FAILED_NODES   (1 << 1)
#define FAILING_NODES  (1 << 2)

/* These are the events sent from slurm to the client that
 * has registered for any of these events.
 * We use define as user can subscribe to more than one
 * events.
 */
#define	SMD_EVENT_NODE_FAILED  (1 << 1)	/* node has failed */
#define SMD_EVENT_NODE_FAILING (1 << 2)	/* node failing can be drained */
#define	SMD_EVENT_NODE_REPLACE (1 << 3)	/* replacement ready */

#endif	/* _HAVE_SMD_NS_H */
