/*****************************************************************************\
 *  pmi.c - Process Management Interface for MPICH2
 *  See http://www-unix.mcs.anl.gov/mpi/mpich2/
 *
 *  NOTE: Dynamic Process Management functions (PMI part 2) are not supported
 *  at this time. Functions required for MPI-1 (PMI part 1) are supported.
 *****************************************************************************
 *  COPYRIGHT: For the function definitions
 *
 *  The following is a notice of limited availability of the code, and
 *  disclaimer which must be included in the prologue of the code and in all
 *  source listings of the code.
 *
 *  Copyright Notice + 2002 University of Chicago
 *
 *  Permission is hereby granted to use, reproduce, prepare derivative
 *  works, and to redistribute to others. This software was authored by:
 *
 *  Argonne National Laboratory Group
 *  W. Gropp: (630) 252-4318; FAX: (630) 252-5986; e-mail: gropp@mcs.anl.gov
 *  E. Lusk: (630) 252-7852; FAX: (630) 252-5986; e-mail: lusk@mcs.anl.gov
 *  Mathematics and Computer Science Division Argonne National Laboratory,
 *  Argonne IL 60439
 *
 *  GOVERNMENT LICENSE
 *
 *  Portions of this material resulted from work developed under a U.S.
 *  Government Contract and are subject to the following license: the
 *  Government is granted for itself and others acting on its behalf a
 *  paid-up, nonexclusive, irrevocable worldwide license in this computer
 *  software to reproduce, prepare derivative works, and perform publicly
 *  and display publicly.
 *
 *  DISCLAIMER
 *
 *  This computer code material was prepared, in part, as an account of work
 *  sponsored by an agency of the United States Government. Neither the
 *  United States, nor the University of Chicago, nor any of their
 *  employees, makes any warranty express or implied, or assumes any legal
 *  liability or responsibility for the accuracy, completeness, or
 *  usefulness of any information, apparatus, product, or process disclosed,
 *  or represents that its use would not infringe privately owned rights.
 *
 *  MCS Division <http://www.mcs.anl.gov>        Argonne National Laboratory
 *  <http://www.anl.gov>     University of Chicago <http://www.uchicago.edu>
 *****************************************************************************
 *  COPYRIGHT: For the implementation of the functions
 *
 *  Copyright (C) 2005-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  LLNL-CODE-402394.
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
#ifndef   _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <slurm/pmi.h>
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "src/api/slurm_pmi.h"
#include "src/common/macros.h"
#include "src/common/malloc.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

#define KVS_STATE_LOCAL    0
#define KVS_STATE_DEFUNCT  1

#define KVS_KEY_STATE_GLOBAL   0
#define KVS_KEY_STATE_LOCAL    1

/* default key names form is jobid.stepid[.taskid.sequence] */
struct kvs_rec {
	char *		kvs_name;
	uint16_t	kvs_state;	/* see KVS_STATE_* */
	uint16_t	kvs_cnt;	/* count of key-pairs */
	uint16_t	kvs_inx;	/* iteration index */
	uint16_t *	kvs_key_states;	/* see KVS_KEY_STATE_* */
	char **		kvs_keys;
	char **		kvs_values;
};

#define _DEBUG 0

static void _del_kvs_rec( struct kvs_rec *kvs_ptr );
static void _init_kvs( char kvsname[] );
static void inline _kvs_dump(void);
static int  _kvs_put( const char kvsname[], const char key[], 
		const char value[], int local);
static void _kvs_swap(struct kvs_rec *kvs_ptr, int inx1, int inx2);

/* Global variables */
long pmi_jobid;
long pmi_stepid;

int pmi_init = 0;
int pmi_size;
int pmi_spawned;
int pmi_rank;
int pmi_debug;

static pthread_mutex_t kvs_mutex = PTHREAD_MUTEX_INITIALIZER;

int kvs_rec_cnt = 0;
struct kvs_rec *kvs_recs;
int kvs_name_sequence = 0;

static char *pmi_opt_str =
  "pmi command line options \n"
  "        \n"
  "        \n"
  "        \n";

static int IsPmiKey(char *);

/* PMI Group functions */

/*@
PMI_Init - initialize the Process Manager Interface

Output Parameter:
. spawned - spawned flag

Return values:
+ PMI_SUCCESS - initialization completed successfully
. PMI_ERR_INVALID_ARG - invalid argument
- PMI_FAIL - initialization failed

Notes:
Initialize PMI for this process group. The value of spawned indicates whether
this process was created by 'PMI_Spawn_multiple'.  'spawned' will be 'PMI_TRUE' 
if this process group has a parent and 'PMI_FALSE' if it does not.

@*/
int PMI_Init( int *spawned )
{
	char *env;

	env = getenv("PMI_DEBUG");
	if (env)
		pmi_debug = atoi(env);
	else
		pmi_debug = 0;
	if (pmi_debug)
		fprintf(stderr, "In: PMI_Init\n");

	if (spawned == NULL)
		return PMI_ERR_INVALID_ARG;

	if (pmi_init)
		goto replay;

	env = getenv("SLURM_JOBID");
	if (env)
		pmi_jobid = atoi(env);
	else
		pmi_jobid = 1;

	env = getenv("SLURM_STEPID");
	if (env)
		pmi_stepid = atoi(env);
	else
		pmi_stepid = 1;

	env = getenv("PMI_SPAWNED");
	if (env)
		pmi_spawned = atoi(env);
	else
		pmi_spawned = 0;

	env = getenv("SLURM_NPROCS");
	if (!env)
		env = getenv("PMI_SIZE");
	if (env)
		pmi_size = atoi(env);
	else
		pmi_size = 1;

	env = getenv("SLURM_PROCID");
	if (!env)
		env = getenv("PMI_RANK");
	if (env)
		pmi_rank = atoi(env);
	else
		pmi_rank = 0;

	pmi_init = 1;

replay:	if (pmi_spawned)
		*spawned = PMI_TRUE;
	else
		*spawned = PMI_FALSE;
	return PMI_SUCCESS;
}

/*@
PMI_Initialized - check if PMI has been initialized

Output Parameter:
. initialized - boolean value

Return values:
+ PMI_SUCCESS - initialized successfully set
. PMI_ERR_INVALID_ARG - invalid argument
- PMI_FAIL - unable to set the variable

Notes:
On successful output, initialized will either be 'PMI_TRUE' or 'PMI_FALSE'.

+ PMI_TRUE - initialize has been called.
- PMI_FALSE - initialize has not been called or previously failed.

@*/
int PMI_Initialized( PMI_BOOL *initialized )
{
	if (pmi_debug)
		fprintf(stderr, "In: PMI_Initialized\n");

	if (initialized == NULL)
		return PMI_ERR_INVALID_ARG;

	if (pmi_init)
		*initialized = PMI_TRUE;
	else
		*initialized = PMI_FALSE;

	return PMI_SUCCESS;
}

/*@
PMI_Finalize - finalize the Process Manager Interface

Return values:
+ PMI_SUCCESS - finalization completed successfully
- PMI_FAIL - finalization failed

Notes:
 Finalize PMI for this process group.

@*/
int PMI_Finalize( void )
{
	int i;

	if (pmi_debug)
		fprintf(stderr, "In: PMI_Finalize\n");

	pmi_init = 0;
	pthread_mutex_lock(&kvs_mutex);
	for (i=0; i<kvs_rec_cnt; i++)
		_del_kvs_rec(&kvs_recs[i]);
	if (kvs_recs)
		free(kvs_recs);
	kvs_recs = NULL;
	kvs_rec_cnt = 0;
	pthread_mutex_unlock(&kvs_mutex);
	slurm_pmi_finalize();

	return PMI_SUCCESS;
}

static void _del_kvs_rec(struct kvs_rec *kvs_ptr)
{
	int i;

	if (kvs_ptr == NULL)
		return;

	for (i=0; i<kvs_ptr->kvs_cnt; i++) {
		if (kvs_ptr->kvs_keys[i])
			free(kvs_ptr->kvs_keys[i]);
		if (kvs_ptr->kvs_values[i])
			free(kvs_ptr->kvs_values[i]);
	}
	if (kvs_ptr->kvs_name)
		free(kvs_ptr->kvs_name);
	return;
}

/*@
PMI_Get_size - obtain the size of the process group

Output Parameters:
. size - pointer to an integer that receives the size of the process group

Return values:
+ PMI_SUCCESS - size successfully obtained
. PMI_ERR_INVALID_ARG - invalid argument
- PMI_FAIL - unable to return the size

Notes:
This function returns the size of the process group to which the local process
belongs.

@*/
int PMI_Get_size( int *size )
{
	if (pmi_debug)
		fprintf(stderr, "In: PMI_Get_size\n");

	if (size == NULL)
		return PMI_ERR_INVALID_ARG;

	if (!pmi_init) {
		int spawned;
		PMI_Init(&spawned);
		if (!pmi_init)
			return PMI_FAIL;
	}
 
	*size = pmi_size;
	return PMI_SUCCESS;
}

/*@
PMI_Get_rank - obtain the rank of the local process in the process group

Output Parameters:
. rank - pointer to an integer that receives the rank in the process group

Return values:
+ PMI_SUCCESS - rank successfully obtained
. PMI_ERR_INVALID_ARG - invalid argument
- PMI_FAIL - unable to return the rank

Notes:
This function returns the rank of the local process in its process group.

@*/
int PMI_Get_rank( int *rank )
{
	if (pmi_debug)
		fprintf(stderr, "In: PMI_Get_rank\n");

	if (rank == NULL)
		return PMI_ERR_INVALID_ARG;

	if (!pmi_init) {
		int spawned;
		PMI_Init(&spawned);
		if (!pmi_init)
			return PMI_FAIL;
	}

	*rank = pmi_rank;
	return PMI_SUCCESS;
}

/*@
PMI_Get_universe_size - obtain the universe size
(NOTE: "universe size" indicates the maximum recommended 
process count for the job.)

Output Parameters:
. size - pointer to an integer that receives the size

Return values:
+ PMI_SUCCESS - size successfully obtained
. PMI_ERR_INVALID_ARG - invalid argument
- PMI_FAIL - unable to return the size


@*/
int PMI_Get_universe_size( int *size )
{
	char *env;

	if (pmi_debug)
		fprintf(stderr, "In: PMI_Get_universe_size\n");

	if (size == NULL)
		return PMI_ERR_INVALID_ARG;

	env = getenv("SLURM_NPROCS");
	if (env) {
		*size = atoi(env);
		return PMI_SUCCESS;
	}

	env = getenv("SLURM_NNODES");
	if (env) {
		/* FIXME: We want a processor count here */
		*size = atoi(env);
		return PMI_SUCCESS;
	}

	*size = 1;
	return PMI_SUCCESS;
}

/*@
PMI_Get_appnum - obtain the application number

Output parameters:
. appnum - pointer to an integer that receives the appnum

Return values:
+ PMI_SUCCESS - appnum successfully obtained
. PMI_ERR_INVALID_ARG - invalid argument
- PMI_FAIL - unable to return the size


@*/
int PMI_Get_appnum( int *appnum )
{
	char *env;

	if (pmi_debug)
		fprintf(stderr, "In: PMI_Get_appnum\n");

	if (appnum == NULL)
		return PMI_ERR_INVALID_ARG;

	env = getenv("SLURM_JOBID");
	if (env) {
		*appnum = atoi(env);
		return PMI_SUCCESS;
	}

	*appnum =1;
	return PMI_SUCCESS;
}

/*@
PMI_Publish_name - publish a name 

Input parameters:
. service_name - string representing the service being published
. port - string representing the port on which to contact the service

Return values:
+ PMI_SUCCESS - port for service successfully published
. PMI_ERR_INVALID_ARG - invalid argument
- PMI_FAIL - unable to publish service


@*/
int PMI_Publish_name( const char service_name[], const char port[] )
{
	if (pmi_debug)
		fprintf(stderr, "In: PMI_Publish_name - NOT SUPPORTED\n");

	if ((service_name == NULL) || (port == NULL))
		return PMI_ERR_INVALID_ARG;

	/* FIXME */
	return PMI_FAIL;
}

/*@
PMI_Unpublish_name - unpublish a name

Input parameters:
. service_name - string representing the service being unpublished

Return values:
+ PMI_SUCCESS - port for service successfully published
. PMI_ERR_INVALID_ARG - invalid argument
- PMI_FAIL - unable to unpublish service


@*/
int PMI_Unpublish_name( const char service_name[] )
{
	if (pmi_debug)
		fprintf(stderr, "In: PMI_Unpublish_name - NOT SUPPORTED\n");

	if (service_name == NULL)
		return PMI_ERR_INVALID_ARG;

	/* FIXME */
	return PMI_FAIL;
}

/*@
PMI_Lookup_name - lookup a service by name

Input parameters:
. service_name - string representing the service being published

Output parameters:
. port - string representing the port on which to contact the service

Return values:
+ PMI_SUCCESS - port for service successfully obtained
. PMI_ERR_INVALID_ARG - invalid argument
- PMI_FAIL - unable to lookup service


@*/
int PMI_Lookup_name( const char service_name[], char port[] )
{
	if (pmi_debug)
		fprintf(stderr, "In: PMI_Lookup_name - NOT SUPPORTED\n");

	if ((service_name == NULL) || (port == NULL))
		return PMI_ERR_INVALID_ARG;

	/* FIXME */
	return PMI_FAIL;
}

/*@
PMI_Get_id - obtain the id of the process group

Input Parameter:
. length - length of the id_str character array

Output Parameter:
. id_str - character array that receives the id of the process group

Return values:
+ PMI_SUCCESS - id successfully obtained
. PMI_ERR_INVALID_ARG - invalid id_str argument
. PMI_ERR_INVALID_LENGTH - invalid length argument
- PMI_FAIL - unable to return the id

Notes:
This function returns a string that uniquely identifies the process group
that the local process belongs to.  The string passed in must be at least
as long as the number returned by 'PMI_Get_id_length_max()'.

@*/
int PMI_Get_id( char id_str[], int length )
{
	if (pmi_debug)
		fprintf(stderr, "In: PMI_Get_id\n");

	if (length < PMI_MAX_ID_LEN)
		return PMI_ERR_INVALID_LENGTH;
	if (id_str == NULL)
		return PMI_ERR_INVALID_ARG;
	if (pmi_init == 0)
		return PMI_FAIL;

	snprintf(id_str, length, "%ld.%ld", pmi_jobid, pmi_stepid); 
	return PMI_SUCCESS;
}

/*@
PMI_Get_kvs_domain_id - obtain the id of the PMI domain

Input Parameter:
. length - length of id_str character array

Output Parameter:
. id_str - character array that receives the id of the PMI domain

Return values:
+ PMI_SUCCESS - id successfully obtained
. PMI_ERR_INVALID_ARG - invalid argument
. PMI_ERR_INVALID_LENGTH - invalid length argument
- PMI_FAIL - unable to return the id

Notes:
This function returns a string that uniquely identifies the PMI domain
where keyval spaces can be shared.  The string passed in must be at least
as long as the number returned by 'PMI_Get_id_length_max()'.

@*/
int PMI_Get_kvs_domain_id( char id_str[], int length )
{
	if (pmi_debug)
		fprintf(stderr, "In: PMI_Get_kvs_domain_id\n");

	if (length < PMI_MAX_ID_LEN)
		return PMI_ERR_INVALID_LENGTH;
	if (id_str == NULL)
		return PMI_ERR_INVALID_ARG;
	if (pmi_init == 0)
		return PMI_FAIL;

	snprintf(id_str, length, "%ld.%ld", pmi_jobid, pmi_stepid);
	return PMI_SUCCESS;
}

/*@
PMI_Get_id_length_max - obtain the maximum length of an id string

Output Parameters:
. length - the maximum length of an id string

Return values:
+ PMI_SUCCESS - length successfully set
. PMI_ERR_INVALID_ARG - invalid argument
- PMI_FAIL - unable to return the maximum length

Notes:
This function returns the maximum length of a process group id string.

@*/
int PMI_Get_id_length_max( int *length )
{
	if (pmi_debug)
		fprintf(stderr, "In: PMI_Get_id_length_max\n");

	if (length == NULL)
		return PMI_ERR_INVALID_ARG;

	*length = PMI_MAX_ID_LEN;
	return PMI_SUCCESS;
}

/*@
PMI_Barrier - barrier across the process group

Return values:
+ PMI_SUCCESS - barrier successfully finished
- PMI_FAIL - barrier failed

Notes:
This function is a collective call across all processes in the process group
the local process belongs to.  It will not return until all the processes
have called 'PMI_Barrier()'.

@*/
int PMI_Barrier( void )
{
	struct kvs_comm_set *kvs_set_ptr = NULL;
	struct kvs_comm *kvs_ptr;
	int i, j, k, rc = PMI_SUCCESS;

	if (pmi_debug)
		fprintf(stderr, "In: PMI_Barrier\n");

	/* Issue the RPC */
	if (slurm_get_kvs_comm_set(&kvs_set_ptr, pmi_rank, pmi_size) 
			!= SLURM_SUCCESS)
		return PMI_FAIL;
	if (kvs_set_ptr == NULL)
		return PMI_SUCCESS;
	if (pmi_debug)
		fprintf(stderr, "Past PMI_Barrier\n");

	for (i=0; i<kvs_set_ptr->kvs_comm_recs; i++) {
		kvs_ptr = kvs_set_ptr->kvs_comm_ptr[i];
		for (j=0; j<kvs_ptr->kvs_cnt; j++) {
			k = _kvs_put(kvs_ptr->kvs_name, 
				kvs_ptr->kvs_keys[j], 
				kvs_ptr->kvs_values[j],
				0);
			if (k != PMI_SUCCESS)
				rc = k;
		}
	}

	/* Release temporary storage from RPC */
	slurm_free_kvs_comm_set(kvs_set_ptr);
	return rc;
}

/*@
PMI_Get_clique_size - obtain the number of processes on the local node

Output Parameters:
. size - pointer to an integer that receives the size of the clique

Return values:
+ PMI_SUCCESS - size successfully obtained
. PMI_ERR_INVALID_ARG - invalid argument
- PMI_FAIL - unable to return the clique size

Notes:
This function returns the number of processes in the local process group that
are on the local node along with the local process.  This is a simple topology
function to distinguish between processes that can communicate through IPC
mechanisms (e.g., shared memory) and other network mechanisms.

@*/
int PMI_Get_clique_size( int *size )
{
	char *env;

	if (pmi_debug)
		fprintf(stderr, "In: PMI_Get_clique_size\n");

	if (size == NULL)
		return PMI_ERR_INVALID_ARG;

	env = getenv("SLURM_CPUS_ON_NODE");
	if (env) {
		*size = atoi(env);
		return PMI_SUCCESS;
	}
	return PMI_FAIL;
}

/*@
PMI_Get_clique_ranks - get the ranks of the local processes in the process group

Input Parameters:
. length - length of the ranks array

Output Parameters:
. ranks - pointer to an array of integers that receive the local ranks

Return values:
+ PMI_SUCCESS - ranks successfully obtained
. PMI_ERR_INVALID_ARG - invalid argument
. PMI_ERR_INVALID_LENGTH - invalid length argument
- PMI_FAIL - unable to return the ranks

Notes:
This function returns the ranks of the processes on the local node.  The array
must be at least as large as the size returned by 'PMI_Get_clique_size()'.  This
is a simple topology function to distinguish between processes that can
communicate through IPC mechanisms (e.g., shared memory) and other network
mechanisms.

@*/
int PMI_Get_clique_ranks( char ranks[], int length )
{
	char *env;

	if (pmi_debug)
		fprintf(stderr, "In: PMI_Get_clique_ranks\n");

	if (ranks == NULL)
		return PMI_ERR_INVALID_ARG;

	env = getenv("SLURM_GTIDS");
	if (env) {
		strcpy(ranks, env);
		return PMI_SUCCESS;
	}

	return PMI_FAIL;
}

/*@
PMI_Abort - abort the process group associated with this process

Input Parameters:
+ exit_code - exit code to be returned by this process
- error_msg - error message to be printed

Return values:
. none - this function should not return
@*/
int PMI_Abort(int exit_code, const char error_msg[])
{
	if ((pmi_debug) || (error_msg != NULL)) {
		if (error_msg == NULL)
			error_msg = "NULL";
		fprintf(stderr, "In: PMI_Abort(%d, %s)\n", exit_code, error_msg);
	}

	if (pmi_init) {
		slurm_kill_job_step((uint32_t) pmi_jobid, (uint32_t) pmi_stepid,
				SIGKILL);
	}
	exit(exit_code);
}

/* PMI Keymap functions */
/*@
PMI_KVS_Get_my_name - obtain the name of the keyval space the local process 
group has access to

Input Parameters:
. length - length of the kvsname character array

Output Parameters:
. kvsname - a string that receives the keyval space name

Return values:
+ PMI_SUCCESS - kvsname successfully obtained
. PMI_ERR_INVALID_ARG - invalid argument
. PMI_ERR_INVALID_LENGTH - invalid length argument
- PMI_FAIL - unable to return the kvsname

Notes:
This function returns the name of the keyval space that this process and all
other processes in the process group have access to.  The output parameter,
kvsname, must be at least as long as the value returned by
'PMI_KVS_Get_name_length_max()'.

@*/
int PMI_KVS_Get_my_name( char kvsname[], int length )
{
	int size;

	if (pmi_debug)
		fprintf(stderr, "In: PMI_KVS_Get_my_name\n");

	if (kvsname == NULL)
		return PMI_ERR_INVALID_ARG;
	if (pmi_init == 0)
		return PMI_FAIL;

	size = snprintf(kvsname, length, "%ld.%ld", pmi_jobid, pmi_stepid);
	if (size >= length)	/* truncated */
		return PMI_ERR_INVALID_LENGTH;

	pthread_mutex_lock(&kvs_mutex);
	_init_kvs(kvsname);
	pthread_mutex_unlock(&kvs_mutex);
	return PMI_SUCCESS;
}

static void _init_kvs( char kvsname[] )
{
	int i;

	i = kvs_rec_cnt;
	kvs_rec_cnt++;
	kvs_recs = realloc(kvs_recs, (sizeof(struct kvs_rec) * kvs_rec_cnt));
	/* DO NOT CHANGE TO STRNDUP(), NOT SUPPORTED ON AIX */
	kvs_recs[i].kvs_name = malloc(PMI_MAX_KVSNAME_LEN);
	if (kvs_recs[i].kvs_name)
		strncpy(kvs_recs[i].kvs_name, kvsname, PMI_MAX_KVSNAME_LEN);
	kvs_recs[i].kvs_state = KVS_STATE_LOCAL;
	kvs_recs[i].kvs_cnt = 0;
	kvs_recs[i].kvs_inx = 0;
	kvs_recs[i].kvs_key_states = NULL;
	kvs_recs[i].kvs_keys = NULL;
	kvs_recs[i].kvs_values = NULL;
}

/*@
PMI_KVS_Get_name_length_max - obtain the length necessary to store a kvsname

Output Parameter:
. length - maximum length required to hold a keyval space name

Return values:
+ PMI_SUCCESS - length successfully set
. PMI_ERR_INVALID_ARG - invalid argument
- PMI_FAIL - unable to set the length

Notes:
This function returns the string length required to store a keyval space name.

A routine is used rather than setting a maximum value in 'pmi.h' to allow
different implementations of PMI to be used with the same executable.  These
different implementations may allow different maximum lengths; by using a 
routine here, we can interface with a variety of implementations of PMI.

@*/
int PMI_KVS_Get_name_length_max( int *length )
{
	if (pmi_debug)
		fprintf(stderr, "In: PMI_KVS_Get_name_length_max\n");

	if (length == NULL)
		return PMI_ERR_INVALID_ARG;

	*length = PMI_MAX_KVSNAME_LEN;
	return PMI_SUCCESS;
}

/*@
PMI_KVS_Get_key_length_max - obtain the length necessary to store a key

Output Parameter:
. length - maximum length required to hold a key string.

Return values:
+ PMI_SUCCESS - length successfully set
. PMI_ERR_INVALID_ARG - invalid argument
- PMI_FAIL - unable to set the length

Notes:
This function returns the string length required to store a key.

@*/
int PMI_KVS_Get_key_length_max( int *length )
{
	if (pmi_debug)
		fprintf(stderr, "In: PMI_KVS_Get_key_length_max\n");

	if (length == NULL)
		return PMI_ERR_INVALID_ARG;

	*length = PMI_MAX_KEY_LEN;
	return PMI_SUCCESS;
}

/*@
PMI_KVS_Get_value_length_max - obtain the length necessary to store a value

Output Parameter:
. length - maximum length required to hold a keyval space value

Return values:
+ PMI_SUCCESS - length successfully set
. PMI_ERR_INVALID_ARG - invalid argument
- PMI_FAIL - unable to set the length

Notes:
This function returns the string length required to store a value from a
keyval space.

@*/
int PMI_KVS_Get_value_length_max( int *length )
{
	if (pmi_debug)
		fprintf(stderr, "In: PMI_KVS_Get_value_length_max\n");

	if (length == NULL)
		return PMI_ERR_INVALID_ARG;

	*length = PMI_MAX_VAL_LEN;
	return PMI_SUCCESS;
}

/*@
PMI_KVS_Create - create a new keyval space

Input Parameter:
. length - length of the kvsname character array

Output Parameters:
. kvsname - a string that receives the keyval space name

Return values:
+ PMI_SUCCESS - keyval space successfully created
. PMI_ERR_INVALID_ARG - invalid argument
. PMI_ERR_INVALID_LENGTH - invalid length argument
- PMI_FAIL - unable to create a new keyval space

Notes:
This function creates a new keyval space.  Everyone in the same process group
can access this keyval space by the name returned by this function.  The
function is not collective.  Only one process calls this function.  The output
parameter, kvsname, must be at least as long as the value returned by
'PMI_KVS_Get_name_length_max()'.

@*/
int PMI_KVS_Create( char kvsname[], int length )
{
	int size, rc;

	if (pmi_debug)
		fprintf(stderr, "In: PMI_KVS_Create\n");

	if (kvsname == NULL)
		return PMI_ERR_INVALID_ARG;
	if ((pmi_jobid < 0) || (pmi_stepid < 0))
		return PMI_FAIL;

	pthread_mutex_lock(&kvs_mutex);
	size = snprintf(kvsname, length, "%ld.%ld.%d.%d", pmi_jobid, 
			pmi_stepid, pmi_rank, kvs_name_sequence);
	if (size >= length)	/* truncated */
		rc = PMI_ERR_INVALID_LENGTH;
	else {
		kvs_name_sequence++;
		_init_kvs(kvsname);
		rc = PMI_SUCCESS;
	}
	pthread_mutex_unlock(&kvs_mutex);
	return rc;
}

/*@
PMI_KVS_Destroy - destroy keyval space

Input Parameters:
. kvsname - keyval space name

Return values:
+ PMI_SUCCESS - keyval space successfully destroyed
. PMI_ERR_INVALID_ARG - invalid argument
- PMI_FAIL - unable to destroy the keyval space

Notes:
This function destroys a keyval space created by 'PMI_KVS_Create()'.

@*/
int PMI_KVS_Destroy( const char kvsname[] )
{
	int i, found = 0;

	if  (pmi_debug)
		fprintf(stderr, "In: PMI_KVS_Destroy - NOT FULLY SUPPORTED\n");

	if (kvsname == NULL)
		return PMI_ERR_INVALID_ARG;

	pthread_mutex_lock(&kvs_mutex);
	for (i=0; i<kvs_rec_cnt; i++) {
		if (strncmp(kvs_recs[i].kvs_name, kvsname, PMI_MAX_KVSNAME_LEN))
			continue;
		kvs_recs[i].kvs_state = KVS_STATE_DEFUNCT;
		found = 1;
		break;
	}
	pthread_mutex_unlock(&kvs_mutex);
	if (found == 0)
		return PMI_ERR_INVALID_ARG;
	/* FIXME: We need to add mechanism to remove these keys from srun's master copy */
	return PMI_SUCCESS;
}

/*@
PMI_KVS_Put - put a key/value pair in a keyval space

Input Parameters:
+ kvsname - keyval space name
. key - key
- value - value

Return values:
+ PMI_SUCCESS - keyval pair successfully put in keyval space
. PMI_ERR_INVALID_KVS - invalid kvsname argument
. PMI_ERR_INVALID_KEY - invalid key argument
. PMI_ERR_INVALID_VAL - invalid val argument
- PMI_FAIL - put failed

Notes:
This function puts the key/value pair in the specified keyval space.  The
value is not visible to other processes until 'PMI_KVS_Commit()' is called.  
The function may complete locally.  After 'PMI_KVS_Commit()' is called, the
value may be retrieved by calling 'PMI_KVS_Get()'.  All keys put to a keyval
space must be unique to the keyval space.  You may not put more than once
with the same key.

@*/
int PMI_KVS_Put( const char kvsname[], const char key[], const char value[])
{
	if (pmi_debug)
		fprintf(stderr, "In: PMI_KVS_Put(%s:%s)\n", key, value);

	if ((kvsname == NULL) || (strlen(kvsname) > PMI_MAX_KVSNAME_LEN))
		return PMI_ERR_INVALID_KVS;
	if ((key == NULL) || (strlen(key) >PMI_MAX_KEY_LEN))
		return PMI_ERR_INVALID_KEY;
	if ((value == NULL) || (strlen(value) > PMI_MAX_VAL_LEN))
		return PMI_ERR_INVALID_VAL;

	return _kvs_put(kvsname, key, value, 1);
}

static int _kvs_put( const char kvsname[], const char key[], const char value[], 
		int local)
{
	int i, j, rc;

	/* find the proper kvs record */
	pthread_mutex_lock(&kvs_mutex);
	for (i=0; i<kvs_rec_cnt; i++) {
		if (strncmp(kvs_recs[i].kvs_name, kvsname, PMI_MAX_KVSNAME_LEN))
			continue;
		/* search for duplicate key */
		for (j=0; j<kvs_recs[i].kvs_cnt; j++) {
			if (strncmp(kvs_recs[i].kvs_keys[j], key, 
					PMI_MAX_KEY_LEN))
				continue;
			if (local)
				kvs_recs[i].kvs_key_states[j] = KVS_KEY_STATE_LOCAL;
			/* else leave unchanged */
			/* replace the existing value */
			/* DO NOT CHANGE TO STRNDUP(), NOT SUPPORTED ON AIX */
			if (kvs_recs[i].kvs_values[j] == NULL)
				kvs_recs[i].kvs_values[j] = malloc(PMI_MAX_VAL_LEN);
			if (kvs_recs[i].kvs_values[j] == NULL)
				rc = PMI_FAIL;	/* malloc error */
			else {
				rc = PMI_SUCCESS;
				strncpy(kvs_recs[i].kvs_values[j], value, 
					PMI_MAX_VAL_LEN);
			}
			goto fini;
		}
		/* create new key */
		kvs_recs[i].kvs_cnt++;
		kvs_recs[i].kvs_key_states = realloc(kvs_recs[i].kvs_key_states,
			(sizeof (uint16_t) * kvs_recs[i].kvs_cnt));
		kvs_recs[i].kvs_values = realloc(kvs_recs[i].kvs_values, 
			(sizeof (char *) * kvs_recs[i].kvs_cnt));
		kvs_recs[i].kvs_keys = realloc(kvs_recs[i].kvs_keys,
			(sizeof (char *) * kvs_recs[i].kvs_cnt));
		if ((kvs_recs[i].kvs_key_states == NULL)
		||  (kvs_recs[i].kvs_values     == NULL)
		||  (kvs_recs[i].kvs_keys       == NULL)) {
			rc = PMI_FAIL;	/* malloc error */
			goto fini;
		}
		if (local)
			kvs_recs[i].kvs_key_states[j] = KVS_KEY_STATE_LOCAL;
		else
			kvs_recs[i].kvs_key_states[j] = KVS_KEY_STATE_GLOBAL;
		/* DO NOT CHANGE TO STRNDUP(), NOT SUPPORTED ON AIX */
		kvs_recs[i].kvs_values[j] = malloc(PMI_MAX_VAL_LEN);
		kvs_recs[i].kvs_keys[j]   = malloc(PMI_MAX_KEY_LEN);
		if ((kvs_recs[i].kvs_values[j] == NULL)
		||  (kvs_recs[i].kvs_keys[j] == NULL))
			rc = PMI_FAIL;	/* malloc error */
		else {
			rc = PMI_SUCCESS;
			strncpy(kvs_recs[i].kvs_values[j], value, PMI_MAX_VAL_LEN);
			strncpy(kvs_recs[i].kvs_keys[j],   key,   PMI_MAX_KEY_LEN);
		}
		goto fini;
	}
	rc = PMI_ERR_INVALID_KVS;

fini:	pthread_mutex_unlock(&kvs_mutex);
	_kvs_dump();
	return rc;
}

/*@
PMI_KVS_Commit - commit all previous puts to the keyval space

Input Parameters:
. kvsname - keyval space name

Return values:
+ PMI_SUCCESS - commit succeeded
. PMI_ERR_INVALID_ARG - invalid argument
- PMI_FAIL - commit failed

Notes:
This function commits all previous puts since the last 'PMI_KVS_Commit()' into
the specified keyval space. It is a process local operation.

@*/
int PMI_KVS_Commit( const char kvsname[] )
{
	struct kvs_comm_set kvs_set;
	int i, j, rc, local_pairs;

	if (pmi_debug)
		fprintf(stderr, "In: PMI_KVS_Commit\n");

	if ((kvsname == NULL) || (strlen(kvsname) > PMI_MAX_KVSNAME_LEN))
		return PMI_ERR_INVALID_ARG;

	/* Pack records into RPC for sending to slurmd_step
	 * NOTE: For performance reasons, we only send key-pairs
	 * which have been locally set rather than the full key-pair
	 * space. We do this by moving the local key-pairs to the 
	 * head of the list and sending the count of local entries
	 * rather than the full set. */
	kvs_set.host_cnt      = 1;
	kvs_set.kvs_host_ptr  = malloc(sizeof(struct kvs_hosts));
	kvs_set.kvs_host_ptr->task_id  = pmi_rank;
	kvs_set.kvs_host_ptr->port     = 0;
	kvs_set.kvs_host_ptr->hostname = NULL;
	kvs_set.kvs_comm_recs = 0;
	kvs_set.kvs_comm_ptr  = NULL;

	pthread_mutex_lock(&kvs_mutex);
	for (i=0; i<kvs_rec_cnt; i++) {
		if (kvs_recs[i].kvs_state == KVS_STATE_DEFUNCT)
			continue;
		local_pairs = 0;
		for (j=0; j<kvs_recs[i].kvs_cnt; j++) {
			if (kvs_recs[i].kvs_key_states[j] == 
					KVS_KEY_STATE_GLOBAL)
				continue;
			if (local_pairs != j)
				_kvs_swap(&kvs_recs[i], j, local_pairs);
			local_pairs++;
		}
		if (local_pairs == 0)
			continue;

		kvs_set.kvs_comm_ptr = realloc(kvs_set.kvs_comm_ptr, 
			(sizeof(struct kvs_comm *) *
			(kvs_set.kvs_comm_recs+1)));
		kvs_set.kvs_comm_ptr[kvs_set.kvs_comm_recs] = 
			malloc(sizeof(struct kvs_comm));
		kvs_set.kvs_comm_ptr[kvs_set.kvs_comm_recs]->kvs_name   =
			kvs_recs[i].kvs_name;
		kvs_set.kvs_comm_ptr[kvs_set.kvs_comm_recs]->kvs_cnt    =
			local_pairs;
		kvs_set.kvs_comm_ptr[kvs_set.kvs_comm_recs]->kvs_keys   =
			kvs_recs[i].kvs_keys;
		kvs_set.kvs_comm_ptr[kvs_set.kvs_comm_recs]->kvs_values =
			kvs_recs[i].kvs_values;
		kvs_set.kvs_comm_recs++;
	}

	/* Send the RPC */
	if (slurm_send_kvs_comm_set(&kvs_set, pmi_rank, pmi_size) 
			!= SLURM_SUCCESS)
		rc = PMI_FAIL;
	else
		rc = PMI_SUCCESS;
	pthread_mutex_unlock(&kvs_mutex);

	/* Free any temporary storage */
	free(kvs_set.kvs_host_ptr);
	for (i=0; i<kvs_set.kvs_comm_recs; i++)
		free(kvs_set.kvs_comm_ptr[i]);
	if (kvs_set.kvs_comm_ptr)
		free(kvs_set.kvs_comm_ptr);

	return rc;
}

static void _kvs_swap(struct kvs_rec *kvs_ptr, int inx1, int inx2)
{
	char *tmp_char;
	uint16_t tmp_16;

	tmp_16 = kvs_ptr->kvs_key_states[inx1];
	kvs_ptr->kvs_key_states[inx1] = kvs_ptr->kvs_key_states[inx2];
	kvs_ptr->kvs_key_states[inx2] = tmp_16;

	tmp_char = kvs_ptr->kvs_keys[inx1];
	kvs_ptr->kvs_keys[inx1] =  kvs_ptr->kvs_keys[inx2];
	kvs_ptr->kvs_keys[inx2] = tmp_char;

	tmp_char = kvs_ptr->kvs_values[inx1];
	kvs_ptr->kvs_values[inx1] =  kvs_ptr->kvs_values[inx2];
	kvs_ptr->kvs_values[inx2] = tmp_char;
}

/*@
PMI_KVS_Get - get a key/value pair from a keyval space

Input Parameters:
+ kvsname - keyval space name
. key - key
- length - length of value character array

Output Parameters:
. value - value

Return values:
+ PMI_SUCCESS - get succeeded
. PMI_ERR_INVALID_KVS - invalid kvsname argument
. PMI_ERR_INVALID_KEY - invalid key argument
. PMI_ERR_INVALID_VAL - invalid val argument
. PMI_ERR_INVALID_LENGTH - invalid length argument
- PMI_FAIL - get failed

Notes:
This function gets the value of the specified key in the keyval space.

@*/
int PMI_KVS_Get( const char kvsname[], const char key[], char value[], int length)
{
	int i, j, rc;

	if (pmi_debug)
		fprintf(stderr, "In: PMI_KVS_Get(%s)\n", key);
	
	if ((kvsname == NULL) || (strlen(kvsname) > PMI_MAX_KVSNAME_LEN))
		return PMI_ERR_INVALID_KVS;
	if ((key == NULL) || (strlen(key) >PMI_MAX_KEY_LEN))
		return PMI_ERR_INVALID_KEY;
	if (value == NULL)
		return PMI_ERR_INVALID_VAL;

	/* find the proper kvs record */
	pthread_mutex_lock(&kvs_mutex);
	for (i=0; i<kvs_rec_cnt; i++) {
		if (kvs_recs[i].kvs_state == KVS_STATE_DEFUNCT)
			continue;
		if (strncmp(kvs_recs[i].kvs_name, kvsname, PMI_MAX_KVSNAME_LEN))
			continue;
		for (j=0; j<kvs_recs[i].kvs_cnt; j++) {
			if (strncmp(kvs_recs[i].kvs_keys[j], key, PMI_MAX_KEY_LEN))
				continue;
			if (strlen(kvs_recs[i].kvs_values[j]) > (length-1))
				rc = PMI_ERR_INVALID_LENGTH;
			else {
				strncpy(value, kvs_recs[i].kvs_values[j], 
					PMI_MAX_VAL_LEN);
				rc = PMI_SUCCESS;
			}
			goto fini;
		}
		rc = PMI_ERR_INVALID_KEY;
		goto fini;
	}
	rc = PMI_ERR_INVALID_KVS;

fini:	pthread_mutex_unlock(&kvs_mutex);
	return rc;
}

/*@
PMI_KVS_Iter_first - initialize the iterator and get the first value

Input Parameters:
+ kvsname - keyval space name
. key_len - length of key character array
- val_len - length of val character array

Output Parameters:
+ key - key
- value - value

Return values:
+ PMI_SUCCESS - keyval pair successfully retrieved from the keyval space
. PMI_ERR_INVALID_KVS - invalid kvsname argument
. PMI_ERR_INVALID_KEY - invalid key argument
. PMI_ERR_INVALID_KEY_LENGTH - invalid key length argument
. PMI_ERR_INVALID_VAL - invalid val argument
. PMI_ERR_INVALID_VAL_LENGTH - invalid val length argument
- PMI_FAIL - failed to initialize the iterator and get the first keyval pair

Notes:
This function initializes the iterator for the specified keyval space and
retrieves the first key/val pair.  The end of the keyval space is specified
by returning an empty key string.  key and val must be at least as long as
the values returned by 'PMI_KVS_Get_key_length_max()' and
'PMI_KVS_Get_value_length_max()'.

@*/
int PMI_KVS_Iter_first(const char kvsname[], char key[], int key_len, char val[], int val_len)
{
	int i, rc;

	if (pmi_debug)
		fprintf(stderr, "In: PMI_KVS_Iter_first\n");

	if ((kvsname == NULL) || (strlen(kvsname) > PMI_MAX_KVSNAME_LEN))
		return PMI_ERR_INVALID_KVS;
	if (key == NULL)
		return PMI_ERR_INVALID_KEY;
	if (val == NULL)
		return PMI_ERR_INVALID_VAL;

	/* find the proper kvs record */
	pthread_mutex_lock(&kvs_mutex);
	for (i=0; i<kvs_rec_cnt; i++) {
		if (kvs_recs[i].kvs_state == KVS_STATE_DEFUNCT)
			continue;
		if (strncmp(kvs_recs[i].kvs_name, kvsname, PMI_MAX_KVSNAME_LEN))
			continue;
		kvs_recs[i].kvs_inx = 0;
		if (kvs_recs[i].kvs_inx >= kvs_recs[i].kvs_cnt) {
			key[0] = '\0';
			val[0] = '\0';
			rc = PMI_SUCCESS;
		} else if (strlen(kvs_recs[i].kvs_keys[kvs_recs[i].kvs_inx]) >
				(key_len-1)) {
			rc = PMI_ERR_INVALID_KEY_LENGTH;
		} else if (strlen(kvs_recs[i].kvs_values[kvs_recs[i].kvs_inx]) >
				(val_len-1)) {
			rc = PMI_ERR_INVALID_VAL_LENGTH;
		} else {
			strncpy(key, kvs_recs[i].kvs_keys[kvs_recs[i].kvs_inx], 
				PMI_MAX_KEY_LEN);
			strncpy(val, kvs_recs[i].kvs_values[kvs_recs[i].kvs_inx],
				PMI_MAX_VAL_LEN);
			rc = PMI_SUCCESS;
		}
		goto fini;
	} 
	rc = PMI_ERR_INVALID_KVS;

fini:	pthread_mutex_unlock(&kvs_mutex);
	return rc;
}

/*@
PMI_KVS_Iter_next - get the next keyval pair from the keyval space

Input Parameters:
+ kvsname - keyval space name
. key_len - length of key character array
- val_len - length of val character array

Output Parameters:
+ key - key
- value - value

Return values:
+ PMI_SUCCESS - keyval pair successfully retrieved from the keyval space
. PMI_ERR_INVALID_KVS - invalid kvsname argument
. PMI_ERR_INVALID_KEY - invalid key argument
. PMI_ERR_INVALID_KEY_LENGTH - invalid key length argument
. PMI_ERR_INVALID_VAL - invalid val argument
. PMI_ERR_INVALID_VAL_LENGTH - invalid val length argument
- PMI_FAIL - failed to get the next keyval pair

Notes:
This function retrieves the next keyval pair from the specified keyval space.  
'PMI_KVS_Iter_first()' must have been previously called.  The end of the keyval
space is specified by returning an empty key string.  The output parameters,
key and val, must be at least as long as the values returned by
'PMI_KVS_Get_key_length_max()' and 'PMI_KVS_Get_value_length_max()'.

@*/
int PMI_KVS_Iter_next(const char kvsname[], char key[], int key_len, 
		char val[], int val_len)
{
	int i, rc;

	if (pmi_debug)
		fprintf(stderr, "In: PMI_KVS_Iter_next\n");

	if ((kvsname == NULL) || (strlen(kvsname) > PMI_MAX_KVSNAME_LEN))
		return PMI_ERR_INVALID_KVS;
	if (key == NULL)
		return PMI_ERR_INVALID_KEY;
	if (val == NULL)
		return PMI_ERR_INVALID_VAL;

	/* find the proper kvs record */
	pthread_mutex_lock(&kvs_mutex);
	for (i=0; i<kvs_rec_cnt; i++) {
		if (kvs_recs[i].kvs_state == KVS_STATE_DEFUNCT)
			continue;
		if (strncmp(kvs_recs[i].kvs_name, kvsname, PMI_MAX_KVSNAME_LEN))
			continue;
		kvs_recs[i].kvs_inx++;
		if (kvs_recs[i].kvs_inx >= kvs_recs[i].kvs_cnt) {
			key[0] = '\0';
			val[0] = '\0';
			rc = PMI_SUCCESS;
		} else if (strlen(kvs_recs[i].kvs_keys[kvs_recs[i].kvs_inx]) >
				(key_len-1)) {
			rc = PMI_ERR_INVALID_KEY_LENGTH;
		} else if (strlen(kvs_recs[i].kvs_values[kvs_recs[i].kvs_inx]) >
				(val_len-1)) {
			rc = PMI_ERR_INVALID_VAL_LENGTH;
		} else {
			strncpy(key, kvs_recs[i].kvs_keys[kvs_recs[i].kvs_inx],
				PMI_MAX_KEY_LEN);
			strncpy(val, kvs_recs[i].kvs_values[kvs_recs[i].kvs_inx],
				PMI_MAX_VAL_LEN);
			rc = PMI_SUCCESS;
		}
		goto fini;
	}
	rc = PMI_ERR_INVALID_KVS;

fini:	pthread_mutex_unlock(&kvs_mutex);
	return rc;
}

/* PMI Process Creation functions */

/*@
PMI_Spawn_multiple - spawn a new set of processes

Input Parameters:
+ count - count of commands
. cmds - array of command strings
. argvs - array of argv arrays for each command string
. maxprocs - array of maximum processes to spawn for each command string
. info_keyval_sizes - array giving the number of elements in each of the 
  'info_keyval_vectors'
. info_keyval_vectors - array of keyval vector arrays
. preput_keyval_size - Number of elements in 'preput_keyval_vector'
- preput_keyval_vector - array of keyvals to be pre-put in the spawned keyval space

Output Parameter:
. errors - array of errors for each command

Return values:
+ PMI_SUCCESS - spawn successful
. PMI_ERR_INVALID_ARG - invalid argument
- PMI_FAIL - spawn failed

Notes:
This function spawns a set of processes into a new process group.  The 'count'
field refers to the size of the array parameters - 'cmd', 'argvs', 'maxprocs',
'info_keyval_sizes' and 'info_keyval_vectors'.  The 'preput_keyval_size' refers
to the size of the 'preput_keyval_vector' array.  The 'preput_keyval_vector'
contains keyval pairs that will be put in the keyval space of the newly
created process group before the processes are started.  The 'maxprocs' array
specifies the desired number of processes to create for each 'cmd' string.  
The actual number of processes may be less than the numbers specified in
maxprocs.  The acceptable number of processes spawned may be controlled by
``soft'' keyvals in the info arrays.  The ``soft'' option is specified by
mpiexec in the MPI-2 standard.  Environment variables may be passed to the
spawned processes through PMI implementation specific 'info_keyval' parameters.
@*/
int PMI_Spawn_multiple(int count,
                       const char * cmds[],
                       const char ** argvs[],
                       const int maxprocs[],
                       const int info_keyval_sizesp[],
                       const PMI_keyval_t * info_keyval_vectors[],
                       int preput_keyval_size,
                       const PMI_keyval_t preput_keyval_vector[],
                       int errors[])
{
	if (pmi_debug)
		fprintf(stderr, "In: PMI_Spawn_multiple - NOT SUPPORTED\n");

	if (cmds == NULL)
		return PMI_ERR_INVALID_ARG;

	/* FIXME */
	return PMI_FAIL;
}

/*@
PMI_Parse_option - create keyval structures from a single command line argument

Input Parameters:
+ num_args - length of args array
- args - array of command line arguments starting with the argument to be parsed

Output Parameters:
+ num_parsed - number of elements of the argument array parsed
. keyvalp - pointer to an array of keyvals
- size - size of the allocated array

Return values:
+ PMI_SUCCESS - success
. PMI_ERR_INVALID_NUM_ARGS - invalid number of arguments
. PMI_ERR_INVALID_ARGS - invalid args argument
. PMI_ERR_INVALID_NUM_PARSED - invalid num_parsed length argument
. PMI_ERR_INVALID_KEYVALP - invalid keyvalp argument
. PMI_ERR_INVALID_SIZE - invalid size argument
- PMI_FAIL - fail

Notes:
This function removes one PMI specific argument from the command line and
creates the corresponding 'PMI_keyval_t' structure for it.  It returns
an array and size to the caller.  The array must be freed by 'PMI_Free_keyvals()'.
If the first element of the args array is not a PMI specific argument, the 
function returns success and sets num_parsed to zero.  If there are multiple PMI 
specific arguments in the args array, this function may parse more than one 
argument as long as the options are contiguous in the args array.

@*/
int PMI_Parse_option(int num_args, char *args[], int *num_parsed, PMI_keyval_t **keyvalp, 
		int *size)
{
	int i, n, s, len;
	char *cp, *kp, *vp;
	PMI_keyval_t *temp;

	if (pmi_debug)
		fprintf(stderr, "In: PMI_Parse_option - \n");

	if (num_parsed == NULL)
		return PMI_ERR_INVALID_NUM_PARSED;
	if (keyvalp == NULL)
		return PMI_ERR_INVALID_KEYVALP;
	if (size == NULL)
		return PMI_ERR_INVALID_SIZE;

	i = 0;
	n = 0;
	s = 0;

	cp = args[0];
	temp = (PMI_keyval_t *) malloc(num_args * (sizeof (PMI_keyval_t)));
	if (temp == NULL)
		return PMI_FAIL;

	cp = args[0];
	while (i < num_args) {

		while (*cp == ' ') cp++;
		n++; // number of array elements processed
		kp = cp;	// keyword start here
		while (*cp != ' ' && *cp != '=' && *cp != '\n' && *cp != '\0')
			cp++;
		if (*cp != '=')  {
			n++;
			break;
		}
		len = cp - kp;
		temp[s].key = (char *) malloc((len+1) * sizeof (char));
		if (temp[s].key == NULL)
			return PMI_FAIL;
		strncpy(temp[s].key, kp, len);
		temp[s].key[len] = '\0';
		if (!IsPmiKey(temp[s].key)) {
			free(temp[s].key);
			temp[s].key=NULL;
			break;
		}
		vp = ++cp;
		while (*cp != ' ' && *cp != '\n' && *cp != '\0')
			cp++;
		len = cp - vp + 1;
		temp[s].val = (char *) malloc((len+1) * sizeof (char));
		if (temp[s].val == NULL)
			return PMI_FAIL;
		strncpy(temp[s].val, vp, len);
		temp[s].val[len] = '\0';
		s++;
		i++;  // try next args
		cp = args[i];

	}

	if (s == 0) {
		free(temp);
		temp = NULL;
	}
	*keyvalp = temp;
	*num_parsed = n;
	*size = s;
	
	return PMI_SUCCESS;
}

/*@
PMI_Args_to_keyval - create keyval structures from command line arguments

Input Parameters:
+ argcp - pointer to argc
- argvp - pointer to argv

Output Parameters:
+ keyvalp - pointer to an array of keyvals
- size - size of the allocated array

Return values:
+ PMI_SUCCESS - success
. PMI_ERR_INVALID_ARG - invalid argument
- PMI_FAIL - fail

Notes:
This function removes PMI specific arguments from the command line and
creates the corresponding 'PMI_keyval_t' structures for them.  It returns
an array and size to the caller that can then be passed to 'PMI_Spawn_multiple()'.
The array can be freed by 'PMI_Free_keyvals()'.  The routine 'free()' should 
not be used to free this array as there is no requirement that the array be
allocated with 'malloc()'.

@*/

/* Assume it is the standard c input argument format, i.e.,
   argcp points to number of arguments
   argvp points to the number of array of arguments, with argv[0] is the cmd
   argv[1], argv[2]... are the keyword/argument pair.

*/

int PMI_Args_to_keyval(int *argcp, char *((*argvp)[]), PMI_keyval_t **keyvalp, 
		int *size)
{
	int i, j, cnt;
	PMI_keyval_t *temp;
	char **argv;

	if  (pmi_debug)
		fprintf(stderr, "In: PMI_Args_to_keyval \n");

	if ((keyvalp == NULL) || (size == NULL) || (argcp == NULL) || (argvp == NULL))
		return PMI_ERR_INVALID_ARG;

	cnt=*argcp;
	argv = *argvp;

	temp = (PMI_keyval_t *) malloc(cnt * (sizeof (PMI_keyval_t)));
	if (temp == NULL)
		return PMI_FAIL;

	if (cnt == 0)
		return PMI_ERR_INVALID_ARG;
	j = 0;
	i = 0;

	if (argv[i][0] != '-') {
		temp[j].val = (char *) malloc((strlen(argv[i])+1) * sizeof (char));
		if (temp[j].val == NULL)
			return PMI_FAIL;
		strcpy(temp[j].val, argv[i]);
		temp[i].key=NULL;
		--cnt;
		++j;
		++i;
	}

	while (cnt) {
		if (argv[i][0] == '-') {
			temp[j].key = (char *) malloc((strlen(argv[i])+1) * 
					sizeof (char));
			if (temp[j].key == NULL)
				return PMI_FAIL;
			strcpy(temp[j].key, argv[i]);
			++i;
			--cnt;
			if ((cnt) && (argv[i][0] != '-')){
				temp[j].val = (char *) malloc(
						(strlen(argv[i])+1) * 
						sizeof (char));
				if (temp[j].val == NULL)
					return PMI_FAIL;
				strcpy(temp[j].val, argv[i]);
				i++;
				--cnt;
			} else {
				temp[j].val = NULL;
			}
			j++;
		} else {
			return PMI_ERR_INVALID_ARG;
		}
	}
	*size = j;
	*keyvalp = temp;

	return PMI_SUCCESS;
}

/*@
PMI_Free_keyvals - free the keyval structures created by PMI_Args_to_keyval

Input Parameters:
+ keyvalp - array of keyvals
- size - size of the array

Return values:
+ PMI_SUCCESS - success
. PMI_ERR_INVALID_ARG - invalid argument
- PMI_FAIL - fail

Notes:
 This function frees the data returned by 'PMI_Args_to_keyval' and 'PMI_Parse_option'.
 Using this routine instead of 'free' allows the PMI package to track 
 allocation of storage or to use interal storage as it sees fit.
@*/
int PMI_Free_keyvals(PMI_keyval_t keyvalp[], int size)
{
	int i;

	if (pmi_debug)
		fprintf(stderr, "In: PMI_Free_keyvals \n");

	if (((keyvalp == NULL) && size)  || (size < 0))
		return PMI_ERR_INVALID_ARG;

	if (size == 0) {
		if (keyvalp != NULL)
			free(keyvalp);
		return PMI_SUCCESS;
	}

	for (i=0; i<size; i++) {
		if ((keyvalp[i].key) != NULL)
			free(keyvalp[i].key);
		if ((keyvalp[i].val) != NULL)
			free(keyvalp[i].val);
	}
	free(keyvalp);
	return PMI_SUCCESS;
}

/*@
PMI_Get_options - get a string of command line argument descriptions that may be printed 
	to the user

Input Parameters:
. length - length of str

Output Parameters:
+ str - description string
- length - length of string or necessary length if input is not large enough

Return values:
+ PMI_SUCCESS - success
. PMI_ERR_INVALID_ARG - invalid argument
. PMI_ERR_INVALID_LENGTH - invalid length argument
. PMI_ERR_NOMEM - input length too small
- PMI_FAIL - fail

Notes:
 This function returns the command line options specific to the pmi implementation
@*/
int PMI_Get_options(char *str, int *length)
{
	int optlen;

	if (pmi_debug)
		fprintf(stderr, "In: PMI_Get_options \n");

	if ((str == NULL) || (length == NULL))
		return PMI_ERR_INVALID_ARG;

	optlen = strlen(pmi_opt_str);
	if (*length <= optlen) {
		strncpy(str, pmi_opt_str, *length-1);
		str[*length-1] = '\0';
		return PMI_ERR_NOMEM;
	}

    strcpy(str, pmi_opt_str);
    return PMI_SUCCESS;
}

static int IsPmiKey(char * key) {
	char strh[5];

	if (pmi_debug)
		fprintf(stderr, "In: IsPmiKey \n");

	strncpy(strh, key, 4);
	strh[4]='\0';
 	if (!strcmp(strh, "PMI_") && (strlen(key) > 4)) {
		return 1;
	}

	/* add code to test special key if needed */
	return 0;
}

static void inline _kvs_dump(void)
{
#if _DEBUG
	int i, j;

	for (i=0; i<kvs_rec_cnt; i++) {
		info("name=%s state=%u cnt=%u inx=%u",
			kvs_recs[i].kvs_name, kvs_recs[i].kvs_state,
			kvs_recs[i].kvs_cnt, kvs_recs[i].kvs_inx);
		for (j=0; j<kvs_recs[i].kvs_cnt; j++) {
			info("  state=%u key=%s value=%s",
				kvs_recs[i].kvs_key_states[j],
				kvs_recs[i].kvs_keys[j],
				kvs_recs[i].kvs_values[j]);
		}
	}
#endif
}
