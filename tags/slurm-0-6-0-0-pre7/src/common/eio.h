/*****************************************************************************\
 *  src/common/eio.h Event-based I/O for slurm
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
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

#ifndef _EIO_H
#define _EIO_H 1

#include "src/common/list.h"
#include "src/common/macros.h"

typedef struct io_obj io_obj_t;

typedef struct eio_handle_components * eio_t;

/* Possible I/O operations on an I/O object
 * Each takes the io_obj being operated on as an argument
 *
 * handle_*() functions also pass the List of io_obj's from the event loop
 *
 */
struct io_operations {
	bool (*readable    )(io_obj_t *);       
	bool (*writable    )(io_obj_t *);      
	int  (*handle_read )(io_obj_t *, List);
	int  (*handle_write)(io_obj_t *, List);
	int  (*handle_error)(io_obj_t *, List);
	int  (*handle_close)(io_obj_t *, List);
};

struct io_obj {
	int fd;                           /* fd to operate on                */
	void *arg;                        /* application-specific data       */
	struct io_operations *ops;        /* pointer to ops struct for obj   */
};

/* passed a list of struct io_obj's, this routine will watch for activtiy
 * on the fd's as long as obj->readable() or obj->writable() returns >0
 *
 * routine returns 0 when either list is empty or no objects in list are
 * readable() or writable().
 *
 * returns -1 on error.
 */
int io_handle_events(eio_t eio, List io_obj_list);


eio_t eio_handle_create(void);
void  eio_handle_destroy(eio_t eio);
int   eio_handle_signal(eio_t eio);

#endif /* !_EIO_H */
