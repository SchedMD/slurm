/*****************************************************************************\
 *  gold_interface.h - interface to the gold daemon commands.
 *
 *  $Id: storage_filetxt.c 10893 2007-01-29 21:53:48Z da $
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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
#ifndef _HAVE_GOLD_INTERFACE_H
#define _HAVE_GOLD_INTERFACE_H


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_STDINT_H
#  include <stdint.h>
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>
#endif

#include <netdb.h>

#include <stdio.h>
#include <slurm/slurm_errno.h>

#include "src/common/xmalloc.h"
#include "src/common/list.h"
#include "src/common/xstring.h"

#define	GOLD_ACTION_QUERY_STR "Query"
#define GOLD_ACTION_CREATE_STR "Create"
#define GOLD_ACTION_MODIFY_STR "Modify"
#define GOLD_ACTION_DELETE_STR "Delete"

#define GOLD_OBJECT_ACCOUNT_STR "Account"
#define GOLD_OBJECT_USER_STR "User"
#define GOLD_OBJECT_PROJECT_STR "Project"
#define GOLD_OBJECT_MACHINE_STR "Machine"
#define GOLD_OBJECT_JOB_STR "Job"
#define GOLD_OBJECT_ROLEUSER_STR "RoleUser"

typedef enum {
	GOLD_ACTION_QUERY,
	GOLD_ACTION_CREATE,
	GOLD_ACTION_MODIFY,
	GOLD_ACTION_DELETE
} gold_action_t;

typedef enum {
	GOLD_OBJECT_ACCOUNT,
	GOLD_OBJECT_USER,
	GOLD_OBJECT_PROJECT,
	GOLD_OBJECT_MACHINE,
	GOLD_OBJECT_JOB,
	GOLD_OBJECT_ROLEUSER
} gold_object_t;

typedef struct {
	char *name;
	char *value;
} gold_name_value_t;

typedef struct {
	gold_object_t object;
	gold_action_t action;
	List assignments; // List of gold_name_value_t's
	List conditions; // List of gold_name_value_t's
	List selections; // List of char *'s
	char *body;
	unsigned char *digest;
	unsigned char *signature;
} gold_request_t;

typedef struct {
	List name_val; // List of gold_name_value_t's
} gold_response_entry_t;

typedef struct {
	List entries; // List of gold_response_entry_t's
	int entry_cnt;
	char *message;
	int rc;
} gold_response_t;


extern int init_gold(char *machine, char *keyfile, char *host, uint16_t port);
extern int fini_gold();

extern gold_request_t *create_gold_request(gold_object_t object,
					   gold_object_t action);
extern int destroy_gold_request(gold_request_t *gold_request);

extern int gold_request_add_assignment(gold_request_t *gold_request, 
				       char *name, char *value);
extern int gold_request_add_condition(gold_request_t *gold_request, 
				      char *name, char *value);
extern int gold_request_add_selection(gold_request_t *gold_request, char *name);

extern gold_response_t *get_gold_response(gold_request_t *gold_request);
extern int destroy_gold_response(gold_response_t *gold_response);

extern void destroy_gold_name_value(void *object);
extern void destroy_gold_char(void *object);
extern void destroy_gold_response_entry(void *object);

#endif
