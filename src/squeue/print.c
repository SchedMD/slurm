/*****************************************************************************\
 *  print.c - squeue print job functions
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Joey Ekstrom <ekstrom1@llnl.gov>
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

#include <time.h>
#include <stdio.h>
#include <string.h>
#include <pwd.h>
#include <sys/types.h>

#include <src/common/list.h>
#include <src/common/xmalloc.h>

#include <src/squeue/print.h>
#include <src/squeue/squeue.h>

static int filter_job( job_info_t * job );
static int filter_step( job_step_info_t * step );

/*****************************************************************************
 * Global Print Functions
 *****************************************************************************/
int print_jobs( List jobs, List format )
{
	if ( list_count( jobs ) > 0 )
	{
		job_info_t* job = NULL;
		ListIterator i = list_iterator_create( jobs );

		print_job_from_format( NULL, format );
		while ( ( job = (job_info_t*)list_next(i) ) != NULL )
		{
			print_job_from_format( job, format );
		}
	}
	else 
		printf("No jobs found in system\n");
	
	return SLURM_SUCCESS;
}

int print_steps( List steps, List format )
{
	if ( list_count( steps ) > 0 )
	{
		job_step_info_t* step = NULL;
		ListIterator i = list_iterator_create( steps );

		print_step_from_format( NULL, format );
		while ( ( step = (job_step_info_t*)list_next(i) ) != NULL )
		{
			print_step_from_format( step, format );
		}
	}
	else 
		printf("No job steps found in system\n" );
	
	return SLURM_SUCCESS;
}

int print_jobs_array( job_info_t* jobs, int size, List format )
{
	if ( size > 0 )
	{
		int i = 0;
		print_job_from_format( NULL, format );
		for (; i<size; i++)
		{
			if (filter_job( &jobs[i] ))
				continue;
			print_job_from_format( &jobs[i], format );
		}
	}
	else 
		printf("No jobs found in system\n" );
	
	return SLURM_SUCCESS;
}

int print_steps_array( job_step_info_t* steps, int size, List format )
{

	if ( size > 0 )
	{
		int i=0;

		print_step_from_format( NULL, format );
		for (; i<size; i++ )
		{
			if (filter_step( &steps[i] ))
				continue;
			print_step_from_format( &steps[i], format );
		}
	}
	else 
		printf("No job steps found in system\n" );
	
	return SLURM_SUCCESS;
}


int
_print_str( char* str, int width, bool right, bool cut_output )
{
	char format[64];
	int printed = 0;

	if ( right == true && width != 0 )
		snprintf( format, 64, "%%%ds", width );
	else if ( width != 0 )
		snprintf( format, 64, "%%.%ds", width-1 );
	else
	{
		format[0] = '%';
		format[1] = 's';
		format[2] = '\0';
	}

	if ( cut_output == false )
	{
		if ( ( printed = printf( format, str ) ) < 0  )
			return printed;
	}
	else
	{
		char temp[width+1];
		snprintf( temp, width+1, format, str );
		if ( ( printed = printf( temp ) ) < 0  )
			return printed;
	}

	while ( printed++ < width )
		printf(" ");

	return printed;
}

int
_print_int( int number, int width, bool right, bool cut_output )
{
	char buf[32];

	snprintf( buf, 32, "%d", number );
	return _print_str( buf, width, right, cut_output );
}


int
_create_format( char* buffer, const char* type, int width, bool right_justify )
{
	if ( snprintf( buffer, FORMAT_STRING_SIZE, (right_justify ? " %%-%d.%d%s": "%%%d.%d%s " ),
			width, width - 1, type ) == -1 )
		return SLURM_ERROR;
	return SLURM_SUCCESS;
}

int
_print_time( time_t t, int level, int width, bool right_justify )
{
	struct tm time;
	char str[FORMAT_STRING_SIZE];
	
	localtime_r( &t, &time );
	
	switch ( level )
	{
		case 1:
		case 2:
		default:
			snprintf(str, FORMAT_STRING_SIZE, "%2.2u/%2.2u-%2.2u:%2.2u", 
				(time.tm_mon+1) ,time.tm_mday ,time.tm_hour, time.tm_min);
			break;
	}

	_print_str( str, width, right_justify, true );

	return SLURM_SUCCESS;
}

/*****************************************************************************
 * Job Print Functions
 *****************************************************************************/
int 
print_job_from_format( job_info_t* job,  List list )
{
	ListIterator i = list_iterator_create( list );
	job_format_t* current;

	while ( (current = (job_format_t*) list_next(i)) != NULL)
	{
		if ( current->function( job, current->width, current->right_justify ) 
				!= SLURM_SUCCESS )
			return SLURM_ERROR;
		printf(" ");
		
	}
	printf("\n");
	return SLURM_SUCCESS;
}

int
job_format_add_function ( List list, int width, bool right_justify, 
		int (*function)(job_info_t*,int,bool) )
{
	job_format_t* tmp = (job_format_t*) xmalloc(sizeof(job_format_t));	
	tmp->function = function;
	tmp->width = width;
	tmp->right_justify = right_justify;

	/* FIXME: Check for error */
	list_append( list, tmp );
	return SLURM_SUCCESS;
}


int
_print_job_job_id( job_info_t* job, int width, bool right )
{
	char id[FORMAT_STRING_SIZE];
	if ( job == NULL ) /* Print the Header instead */
		_print_str( "JOB_ID", width, right, true );
	else
	{
		snprintf( id, FORMAT_STRING_SIZE, "%s.%d", job->partition, job->job_id );
		_print_str( id, width, right, true );
	}
	return SLURM_SUCCESS;
}

int
_print_job_name( job_info_t* job, int width, bool right )
{
	if ( job == NULL ) /* Print the Header instead */
		_print_str( "NAME", width, right, true );
	else 
		_print_str( job->name, width, right, true );

	return SLURM_SUCCESS;
}

int
_print_job_user_name( job_info_t* job, int width, bool right )
{
	struct passwd* user_info = NULL;

	if ( job == NULL ) /* Print the Header instead */
	{
		_print_str( "USER", width, right, true );
		return SLURM_SUCCESS;
	}
	user_info = getpwuid( (uid_t) job->user_id );
	if ( user_info != NULL && user_info->pw_name[0] != '\0' )
		_print_str( user_info->pw_name, width, right, true );
	else
		_print_int( job->user_id, width, right, true );
	return SLURM_SUCCESS;
}
	
int
_print_job_job_state( job_info_t* job, int width, bool right )
{
	if ( job == NULL ) /* Print the Header instead */
		_print_str( "JOB_STATE", width, right, true );
	else
		_print_str( job_state_string( job->job_state ), width, right, true );

	return SLURM_SUCCESS;
}

int
_print_job_job_state_compact( job_info_t* job, int width, bool right )
{
	if ( job == NULL ) /* Print the Header instead */
		_print_str( "ST", width, right, true);
	else
		_print_str( job_state_string_compact( job->job_state ), width, right, true );
	return SLURM_SUCCESS;
}

int
_print_job_time_limit( job_info_t* job, int width, bool right )
{
	char time[FORMAT_STRING_SIZE];
	if ( job == NULL ) /* Print the Header instead */
		_print_str( "WC", width, right, true );
	else
	{
		snprintf( time, FORMAT_STRING_SIZE, "%d:%2.2d:%2.2d", 
					job->time_limit/(60*24), 
					(job->time_limit/60) %24 , 
					job->time_limit % 60 ) ;

		_print_str( time, width, right, true );
	}

	return SLURM_SUCCESS;
}

int
_print_job_start_time( job_info_t* job, int width, bool right )
{
	if ( job == NULL ) /* Print the Header instead */
		_print_str( "START", width, right, true );
	else
		_print_time( job->start_time, 0, width, right );

	return SLURM_SUCCESS;
}

int
_print_job_end_time( job_info_t* job, int width, bool right )
{
	if ( job == NULL ) /* Print the Header instead */
		_print_str( "END", width, right, true );
	else
		_print_time( job->end_time, 0, width, right );

	return SLURM_SUCCESS;
}

int
_print_job_priority( job_info_t* job, int width, bool right)
{
	char temp[FORMAT_STRING_SIZE];
	if ( job == NULL ) /* Print the Header instead */
		_print_str( "PRI", width, right, true );
	else
	{
		sprintf(temp, "%d", job->priority );
		_print_str( temp, width, right, true );
	}

	return SLURM_SUCCESS;
}

int
_print_job_nodes( job_info_t* job, int width, bool right_justify )
{
	if ( job == NULL ) /* Print the Header instead */
		_print_str( "NODES", width, right_justify, true );
	else
		_print_str( job->nodes, width, right_justify, true );

	return SLURM_SUCCESS;
}

int
_print_job_node_inx( job_info_t* job, int width, bool right )
{
	if ( job == NULL ) /* Print the Header instead */
		_print_str( "NODE_INX", width, right, true );
	else
	{
		int* current = job->node_inx;
		int curr_width = 0;
		while ( *current != -1 && curr_width < width )
		{
			curr_width += _print_int( *current, width, right, true );
			printf(",");
		}
		while (curr_width < width )
			curr_width += printf(" ");
	}
	return SLURM_SUCCESS;
}

int
_print_job_partition( job_info_t* job, int width, bool right )
{
	if ( job == NULL ) /* Print the Header instead */
		_print_str( "PARTITION", width, right, true );
	else
		printf( job->partition, width, right, true );

	return SLURM_SUCCESS;
}

int
_print_job_num_procs( job_info_t* job, int width, bool right_justify )
{
	if ( job == NULL ) /* Print the Header instead */
		_print_str( "NUM_PROCS", width, right_justify, true );
	else
		_print_int( job->num_procs, width, right_justify, true );

	return SLURM_SUCCESS;
}

int
_print_job_num_nodes( job_info_t* job, int width, bool right_justify )
{
	if ( job == NULL ) /* Print the Header instead */
		_print_str( "NUM_NODES", width, right_justify, true );
	else
		_print_int( job->num_nodes, width, right_justify, true );

	return SLURM_SUCCESS;
}

int
_print_job_shared( job_info_t* job, int width, bool right_justify )
{
	if ( job == NULL ) /* Print the Header instead */
		_print_str( "SHARED", width, right_justify, true );
	else
		_print_int( job->shared, width, right_justify, true );

	return SLURM_SUCCESS;
}

int
_print_job_contiguous( job_info_t* job, int width, bool right_justify )
{
	if ( job == NULL ) /* Print the Header instead */
		_print_str( "CONTIGUOUS", width, right_justify, true );
	else
		_print_int( job->contiguous, width, right_justify, true );

	return SLURM_SUCCESS;
}

int
_print_job_min_procs( job_info_t* job, int width, bool right_justify )
{
	if ( job == NULL ) /* Print the Header instead */
		_print_str( "MIN_PROCS", width, right_justify, true );
	else
		_print_int( job->min_procs, width, right_justify, true );

	return SLURM_SUCCESS;
}

int
_print_job_min_memory( job_info_t* job, int width, bool right_justify )
{
	if ( job == NULL ) /* Print the Header instead */
		_print_str( "MIN_MEMORY", width, right_justify, true );
	else
		_print_int( job->min_memory, width, right_justify, true );

	return SLURM_SUCCESS;
}

int
_print_job_min_tmp_disk( job_info_t* job, int width, bool right_justify )
{
	if ( job == NULL ) /* Print the Header instead */
		_print_str( "MIN_TMP_DISK", width, right_justify, true );
	else
		_print_int( job->min_tmp_disk, width, right_justify, true );

	return SLURM_SUCCESS;
}

int
_print_job_req_nodes( job_info_t* job, int width, bool right_justify )
{
	if ( job == NULL ) /* Print the Header instead */
		_print_str( "REQ_NODES", width, right_justify, true );
	else
		_print_str( job->req_nodes, width, right_justify, true );

	return SLURM_SUCCESS;
}

int
_print_job_req_node_inx( job_info_t* job, int width, bool right_justify )
{
	if ( job == NULL ) /* Print the Header instead */
		_print_str( "REQ_NODE_INX", width, right_justify, true );
	else
	{
		int* current = job->req_node_inx;
		int curr_width = 0;
		while ( *current != -1 && curr_width < width )
		{
			curr_width += _print_int( *current, width, right_justify, true );
			printf(",");
		}
		while (curr_width < width )
			curr_width += printf(" ");
	}
	return SLURM_SUCCESS;
}

int
_print_job_features( job_info_t* job, int width, bool right_justify )
{
	if ( job == NULL ) /* Print the Header instead */
		_print_str( "Features", width, right_justify, true );
	else
		_print_str( job->features, width, right_justify, true );

	return SLURM_SUCCESS;
}


/*****************************************************************************
 * Job Step  Print Functions
 *****************************************************************************/
int 
print_step_from_format( job_step_info_t* job_step, List list )
{
	ListIterator i = list_iterator_create( list );
	step_format_t* current;

	while ( (current = (step_format_t*) list_next(i)) != NULL)
	{
		if ( current->function( job_step, current->width, current->right_justify ) 
				!= SLURM_SUCCESS )
			return SLURM_ERROR;
		
	}
	printf("\n");
	return SLURM_SUCCESS;
}

int
step_format_add_function ( List list, int width, bool right_justify, 
		int (*function)(job_step_info_t*,int,bool) )
{
	step_format_t* tmp = (step_format_t*) xmalloc(sizeof(step_format_t));	
	tmp->function = function;
	tmp->width = width;
	tmp->right_justify = right_justify;

	/* FIXME: Check for error */
	list_append( list, tmp );
	return SLURM_SUCCESS;
}

int _print_step_id( job_step_info_t* step, int width, bool right_justify )
{
	char format[FORMAT_STRING_SIZE];
    char id[FORMAT_STRING_SIZE];
	if ( step == NULL ) /* Print the Header instead */
	{
		if ( _create_format( format, "s", width, right_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, "ID" );
		return SLURM_SUCCESS;
	}

	if ( _create_format( format, "s", width, right_justify ) == SLURM_ERROR )
		return SLURM_ERROR;
	snprintf( id, FORMAT_STRING_SIZE, "%s.%u.%u", step->partition, step->job_id, step->step_id );
	printf( format, id );

	return SLURM_SUCCESS;
}

int _print_step_user_id( job_step_info_t* step, int width, bool right_justify )
{
	char format[FORMAT_STRING_SIZE];
	struct passwd* user_info = NULL;
	if ( step == NULL ) /* Print the Header instead */
	{
		if ( _create_format( format, "s", width, right_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, "USER" );
		return SLURM_SUCCESS;
	}

	user_info = getpwuid( (uid_t) step->user_id );
	if ( user_info != NULL || user_info->pw_name[0] == '\0' )
	{
		if ( _create_format( format, "s", width, right_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, user_info->pw_name );
	}
	else
	{
		if ( _create_format( format, "d", width, right_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, step->user_id );
	}

	if ( _create_format( format, "s", width, right_justify ) == SLURM_ERROR )
		return SLURM_ERROR;
	return SLURM_SUCCESS;
}

int _print_step_start_time( job_step_info_t* step, int width, bool right_justify )
{
	char format[FORMAT_STRING_SIZE];
	if ( step == NULL ) /* Print the Header instead */
	{
		if ( _create_format( format, "s", width, right_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, "START" );
		return SLURM_SUCCESS;
	}

	_print_time( step->start_time, 0, width, right_justify );
	return SLURM_SUCCESS;
}

int _print_step_nodes( job_step_info_t* step, int width, bool right_justify )
{
	char format[FORMAT_STRING_SIZE];
	if ( step == NULL ) /* Print the Header instead */
	{
		if ( _create_format( format, "s", width, right_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, "NODES" );
		return SLURM_SUCCESS;
	}

	if ( _create_format( format, "s", width, right_justify ) == SLURM_ERROR )
		return SLURM_ERROR;
	printf( format, step->nodes );


	return SLURM_SUCCESS;
}

/* filter job records per input specifications, returns 1 if job should be filter out (not printed) */
static int filter_job( job_info_t * job )
{
	int filter;
	ListIterator iterator;
	uint32_t *job_id, *user;
	enum job_states *state_id;
	char *part;

	if (params.job_list) {
		filter = 1;
		iterator = list_iterator_create( params.job_list );
		while ( (job_id = list_next( iterator )) ) {
			if (*job_id == job->job_id) {
				filter = 0;
				break;
			}
		}
		list_iterator_destroy( iterator );
		if (filter == 1)
			return 1;
	}

	if (params.part_list) {
		filter = 1;
		iterator = list_iterator_create( params.part_list );
		while ( (part = list_next( iterator )) ) {
			if (strcmp (part, job->partition) == 0) {
				filter = 0;
				break;
			}
		}
		list_iterator_destroy( iterator );
		if (filter == 1)
			return 2;
	}

	if (params.state_list) {
		filter = 1;
		iterator = list_iterator_create( params.state_list );
		while ( (state_id = list_next( iterator )) ) {
			if (*state_id == job->job_state) {
				filter = 0;
				break;
			}
		}
		list_iterator_destroy( iterator );
		if (filter == 1)
			return 3;
	} else {
		if ((job->job_state != JOB_PENDING) &&
		    (job->job_state != JOB_STAGE_IN) && 
		    (job->job_state != JOB_RUNNING) && 
		    (job->job_state != JOB_STAGE_OUT)) 
			return 4;
	}

	if (params.user_list) {
		filter = 1;
		iterator = list_iterator_create( params.user_list );
		while ( (user = list_next( iterator )) ) {
			if (*user == job->user_id) {
				filter = 0;
				break;
			}
		}
		list_iterator_destroy( iterator );
		if (filter == 1)
			return 5;
	}

	return 0;
}

/* filter step records per input specifications, returns 1 if step should be filter out (not printed) */
static int filter_step( job_step_info_t * step )
{
	int filter;
	ListIterator iterator;
	uint32_t *job_id, *user;
	char *part;
	squeue_job_step_t *job_step_id;

	if (params.job_list) {
		filter = 1;
		iterator = list_iterator_create( params.job_list );
		while ( (job_id = list_next( iterator )) ) {
			if (*job_id == step->job_id) {
				filter = 0;
				break;
			}
		}
		list_iterator_destroy( iterator );
		if (filter == 1)
			return 1;
	}

	if (params.part_list) {
		filter = 1;
		iterator = list_iterator_create( params.part_list );
		while ( (part = list_next( iterator )) ) {
			if (strcmp (part, step->partition) == 0) {
				filter = 0;
				break;
			}
		}
		list_iterator_destroy( iterator );
		if (filter == 1)
			return 2;
	}

	if (params.step_list) {
		filter = 1;
		iterator = list_iterator_create( params.step_list );
		while ( (job_step_id = list_next( iterator )) ) {
			if ((job_step_id->job_id  == step->job_id) &&
			    (job_step_id->step_id == step->step_id)) {
				filter = 0;
				break;
			}
		}
		list_iterator_destroy( iterator );
		if (filter == 1)
			return 3;
	}

	if (params.user_list) {
		filter = 1;
		iterator = list_iterator_create( params.user_list );
		while ( (user = list_next( iterator )) ) {
			if (*user == step->user_id) {
				filter = 0;
				break;
			}
		}
		list_iterator_destroy( iterator );
		if (filter == 1)
			return 5;
	}

	return 0;
}
