/*****************************************************************************\
 *  src/common/eio.h Event-based I/O for slurm
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifndef _EIO_H
#define _EIO_H 1

#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/slurm_protocol_defs.h"
typedef struct eio_obj eio_obj_t;

typedef struct eio_handle_components eio_handle_t;

/* Possible I/O operations on an I/O object
 * Each takes the io_obj being operated on as an argument
 *
 * handle_*() functions also pass the List of io_obj's from the event loop
 *
 * If either "handle_error" (for POLLERR and POLLNVAL) or "handle_close"
 * (for POLLHUP) are not defined, the eio server will fallback to handle_read
 * if defined, and fallback to handle_write if handle_read is not defined
 * either.
 *
 * If there are no handlers at all when POLLERR or POLLNVAL occurs, the eio
 * server will set the eio_obj_t shutdown flag to "true".  Keep in mind
 * that the shutdown flag is essentially just an advisory flag.  The
 * "readable" and "writable" functions have the final say over whether a
 * file descriptor will continue to be polled.
 */
struct io_operations {
	bool (*readable    )(eio_obj_t *);
	bool (*writable    )(eio_obj_t *);
	void (*handle_msg  )(void *arg, slurm_msg_t *msg);
	int  (*handle_read )(eio_obj_t *, List);
	int  (*handle_write)(eio_obj_t *, List);
	int  (*handle_error)(eio_obj_t *, List);
	int  (*handle_close)(eio_obj_t *, List);
	int  timeout;
};

struct eio_obj {
	int fd;                           /* fd to operate on                */
	void *arg;                        /* application-specific data       */
	struct io_operations *ops;        /* pointer to ops struct for obj   */
	bool shutdown;
};

eio_handle_t *eio_handle_create(void);
void eio_handle_destroy(eio_handle_t *eio);

/*
 * Add an eio_obj_t "obj" to an eio_handle_t "eio"'s internal object list.
 *
 * This function can only be used to initialize "eio"'s list before
 * calling eio_handle_mainloop.  If it is used after the eio engine's
 * mainloop has started, segfaults are likely.
 */
void eio_new_initial_obj(eio_handle_t *eio, eio_obj_t *obj);

/*
 * Queue an eio_obj_t "obj" for inclusion in an already running
 * eio_handle_t "eio"'s internal object list.
 */
void eio_new_obj(eio_handle_t *eio, eio_obj_t *obj);

/*
 * DeQueue an eio_obj_t "obj" from the running
 * eio_handle_t eio's internal object list "objs".
 * Supposed to be called from read/write handlers
 * (have "objs" as one of their arguments).
 */
bool eio_remove_obj(eio_obj_t *obj, List objs);


/* This routine will watch for activtiy on the fd's as long
 * as obj->readable() or obj->writable() returns >0
 *
 * routine returns 0 when either list is empty or no objects in list are
 * readable() or writable().
 *
 * returns -1 on error.
 */
int eio_handle_mainloop(eio_handle_t *eio);

bool eio_message_socket_readable(eio_obj_t *obj);
int eio_message_socket_accept(eio_obj_t *obj, List objs);

int eio_signal_wakeup(eio_handle_t *eio);
int eio_signal_shutdown(eio_handle_t *eio);

eio_obj_t *eio_obj_create(int fd, struct io_operations *ops, void *arg);
void eio_obj_destroy(void *arg);

#endif /* !_EIO_H */
