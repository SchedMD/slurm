
#include <time.h>
#include <stdio.h>
#include <pwd.h>
#include <sys/types.h>

#include <src/common/list.h>
#include <src/common/xmalloc.h>

#include "print.h"

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
	else printf("No jobs found in system" );
	
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
	else printf("No job steps found in system" );
	
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
			print_job_from_format( &jobs[i], format );
		}
	}
	else printf("No jobs found in system" );
	
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
			print_step_from_format( &steps[i], format );
		}
	}
	else printf("No job steps found in system" );
	
	return SLURM_SUCCESS;
}


int
_create_format( char* buffer, const char* type, int width, bool left_justify )
{
	if ( snprintf( buffer, FORMAT_STRING_SIZE, (left_justify ? " %%-%d.%d%s": "%%%d.%d%s " ),
			width, width - 1, type ) == -1 )
		return SLURM_ERROR;
	return SLURM_SUCCESS;
}

int
_print_time( time_t t, int level, int width, bool left_justify )
{
	struct tm time;
	char format[FORMAT_STRING_SIZE];
	char str[FORMAT_STRING_SIZE];
	
	gmtime_r( &t, &time );
	
	switch ( level )
	{
		case 1:
		case 2:
		default:
			snprintf(str, FORMAT_STRING_SIZE, "%.2u-%.2u-%.2u:%.2u", time.tm_mon ,time.tm_mday ,time.tm_hour, time.tm_min);
			break;
	}
	if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
		return SLURM_ERROR;

	printf( format, str );

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
		if ( current->function( job, current->width, current->left_justify ) 
				!= SLURM_SUCCESS )
			return SLURM_ERROR;
		
	}
	printf("\n");
	return SLURM_SUCCESS;
}

int
job_format_add_function ( List list, int width, bool left_justify, 
		int (*function)(job_info_t*,int,bool) )
{
	job_format_t* tmp = (job_format_t*) xmalloc(sizeof(job_format_t));	
	tmp->function = function;
	tmp->width = width;
	tmp->left_justify = left_justify;

	/* FIXME: Check for error */
	list_append( list, tmp );
	return SLURM_SUCCESS;
}


int
_print_job_job_id( job_info_t* job, int width, bool left_justify )
{
	char format[FORMAT_STRING_SIZE];
	char id[FORMAT_STRING_SIZE];
	if ( job == NULL ) /* Print the Header instead */
	{
		if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, "JOBID" );
		return SLURM_SUCCESS;
	}

	if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
		return SLURM_ERROR;
	snprintf( id, FORMAT_STRING_SIZE, "%s.%d", job->partition, job->job_id );
	printf( format, id );
	return SLURM_SUCCESS;
}

int
_print_job_name( job_info_t* job, int width, bool left_justify )
{
	char format[FORMAT_STRING_SIZE];
	if ( job == NULL ) /* Print the Header instead */
	{
		if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, "NAME" );
		return SLURM_SUCCESS;
	}

	if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
		return SLURM_ERROR;
	printf( format, job->name );


	return SLURM_SUCCESS;
}

int
_print_job_user_name( job_info_t* job, int width, bool left_justify )
{
	char format[FORMAT_STRING_SIZE];
	struct passwd* user_info = NULL;

	
	if ( job == NULL ) /* Print the Header instead */
	{
		if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, "USER" );
		return SLURM_SUCCESS;
	}
	user_info = getpwuid( (uid_t) job->user_id );
	if ( user_info != NULL || user_info->pw_name[0] == '\0' )
	{
		if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, user_info->pw_name );
	}
	else
	{
		if ( _create_format( format, "d", width, left_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, job->user_id );
	}
	return SLURM_SUCCESS;
}
	
int
_print_job_job_state( job_info_t* job, int width, bool left_justify )
{
	char format[FORMAT_STRING_SIZE];
	if ( job == NULL ) /* Print the Header instead */
	{
		if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, "JOB_STATE" );
		return SLURM_SUCCESS;
	}

	if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
		return SLURM_ERROR;
	printf( format, job_state_string( job->job_state ) );

	return SLURM_SUCCESS;
}

int
_print_job_job_state_compact( job_info_t* job, int width, bool left_justify )
{
	char format[FORMAT_STRING_SIZE];
	if ( job == NULL ) /* Print the Header instead */
	{
		if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, "ST" );
		return SLURM_SUCCESS;
	}

	if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
		return SLURM_ERROR;
	printf( format, job_state_string_compact( job->job_state ) );

	return SLURM_SUCCESS;
}

int
_print_job_time_limit( job_info_t* job, int width, bool left_justify )
{
	char format[FORMAT_STRING_SIZE];
	char time[FORMAT_STRING_SIZE];
	if ( job == NULL ) /* Print the Header instead */
	{
		if ( _create_format( format, "s", MAX(width,9), left_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, "WC" );
		return SLURM_SUCCESS;
	}

	snprintf( time, FORMAT_STRING_SIZE, "%d:%2.2d:%2.2d", job->time_limit/(60*24), (job->time_limit/60) %24 , job->time_limit % 60 ) ;

	if ( _create_format( format, "s", MAX(width,9), left_justify ) == SLURM_ERROR )
		return SLURM_ERROR;
	printf( format, time );


	return SLURM_SUCCESS;
}

int
_print_job_start_time( job_info_t* job, int width, bool left_justify )
{
	char format[FORMAT_STRING_SIZE];
	if ( job == NULL ) /* Print the Header instead */
	{
		if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, "START" );
		return SLURM_SUCCESS;
	}
	_print_time( job->start_time, 0, width, left_justify );
	return SLURM_SUCCESS;
}

int
_print_job_end_time( job_info_t* job, int width, bool left_justify )
{
	char format[FORMAT_STRING_SIZE];
	if ( job == NULL ) /* Print the Header instead */
	{
		if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, "END" );
		return SLURM_SUCCESS;
	}
	_print_time( job->end_time, 0, width, left_justify );
	return SLURM_SUCCESS;
}

int
_print_job_priority( job_info_t* job, int width, bool left_justify )
{
	char format[FORMAT_STRING_SIZE];
	char temp[FORMAT_STRING_SIZE];
	if ( job == NULL ) /* Print the Header instead */
	{
		if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, "PRI" );
		return SLURM_SUCCESS;
	}

	if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
		return SLURM_ERROR;
	sprintf(temp, "%d", job->priority );
	printf( format, temp );


	return SLURM_SUCCESS;
}

int
_print_job_nodes( job_info_t* job, int width, bool left_justify )
{
	char format[FORMAT_STRING_SIZE];
	if ( job == NULL ) /* Print the Header instead */
	{
		if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, "NODES" );
		return SLURM_SUCCESS;
	}

	if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
		return SLURM_ERROR;
	printf( format, job->nodes );


	return SLURM_SUCCESS;
}

int
_print_job_node_inx( job_info_t* job, int width, bool left_justify )
{
	char format[FORMAT_STRING_SIZE];
	if ( job == NULL ) /* Print the Header instead */
	{
		if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, "NODE_INX" );
		return SLURM_SUCCESS;
	}

	if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
		return SLURM_ERROR;
	printf( format, job->node_inx );


	return SLURM_SUCCESS;
}

int
_print_job_partition( job_info_t* job, int width, bool left_justify )
{
	char format[FORMAT_STRING_SIZE];
	if ( job == NULL ) /* Print the Header instead */
	{
		if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, "PARTITION" );
		return SLURM_SUCCESS;
	}

	if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
		return SLURM_ERROR;
	printf( format, job->partition );


	return SLURM_SUCCESS;
}

int
_print_job_num_procs( job_info_t* job, int width, bool left_justify )
{
	char format[FORMAT_STRING_SIZE];
	if ( job == NULL ) /* Print the Header instead */
	{
		if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, "NUM_PROCS" );
		return SLURM_SUCCESS;
	}

	if ( _create_format( format, "d", width, left_justify ) == SLURM_ERROR )
		return SLURM_ERROR;
	printf( format, job->num_procs );


	return SLURM_SUCCESS;
}

int
_print_job_num_nodes( job_info_t* job, int width, bool left_justify )
{
	char format[FORMAT_STRING_SIZE];
	if ( job == NULL ) /* Print the Header instead */
	{
		if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, "NUM_NODES" );
		return SLURM_SUCCESS;
	}

	if ( _create_format( format, "d", width, left_justify ) == SLURM_ERROR )
		return SLURM_ERROR;
	printf( format, job->num_nodes );


	return SLURM_SUCCESS;
}

int
_print_job_shared( job_info_t* job, int width, bool left_justify )
{
	char format[FORMAT_STRING_SIZE];
	if ( job == NULL ) /* Print the Header instead */
	{
		if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, "SHARED" );
		return SLURM_SUCCESS;
	}

	if ( _create_format( format, "d", width, left_justify ) == SLURM_ERROR )
		return SLURM_ERROR;
	printf( format, job->shared );


	return SLURM_SUCCESS;
}

int
_print_job_contiguous( job_info_t* job, int width, bool left_justify )
{
	char format[FORMAT_STRING_SIZE];
	if ( job == NULL ) /* Print the Header instead */
	{
		if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, "CONTIGUOUS" );
		return SLURM_SUCCESS;
	}

	if ( _create_format( format, "d", width, left_justify ) == SLURM_ERROR )
		return SLURM_ERROR;
	printf( format, job->contiguous );


	return SLURM_SUCCESS;
}

int
_print_job_min_procs( job_info_t* job, int width, bool left_justify )
{
	char format[FORMAT_STRING_SIZE];
	if ( job == NULL ) /* Print the Header instead */
	{
		if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, "MIN_PROCS" );
		return SLURM_SUCCESS;
	}

	if ( _create_format( format, "d", width, left_justify ) == SLURM_ERROR )
		return SLURM_ERROR;
	printf( format, job->min_procs );


	return SLURM_SUCCESS;
}

int
_print_job_min_memory( job_info_t* job, int width, bool left_justify )
{
	char format[FORMAT_STRING_SIZE];
	if ( job == NULL ) /* Print the Header instead */
	{
		if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, "MIN_MEMORY" );
		return SLURM_SUCCESS;
	}

	if ( _create_format( format, "u", width, left_justify ) == SLURM_ERROR )
		return SLURM_ERROR;
	printf( format, job->min_memory );

	return SLURM_SUCCESS;
}

int
_print_job_min_tmp_disk( job_info_t* job, int width, bool left_justify )
{
	char format[FORMAT_STRING_SIZE];
	if ( job == NULL ) /* Print the Header instead */
	{
		if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, "MIN_TMP_DISK" );
		return SLURM_SUCCESS;
	}

	if ( _create_format( format, "u", width, left_justify ) == SLURM_ERROR )
		return SLURM_ERROR;
	printf( format, job->min_tmp_disk );


	return SLURM_SUCCESS;
}

int
_print_job_req_nodes( job_info_t* job, int width, bool left_justify )
{
	char format[FORMAT_STRING_SIZE];
	if ( job == NULL ) /* Print the Header instead */
	{
		if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, "REQ_NODES" );
		return SLURM_SUCCESS;
	}

	if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
		return SLURM_ERROR;
	printf( format, job->req_nodes );


	return SLURM_SUCCESS;
}

int
_print_job_req_node_inx( job_info_t* job, int width, bool left_justify )
{
	char format[FORMAT_STRING_SIZE];
	if ( job == NULL ) /* Print the Header instead */
	{
		if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, "REQ_NODE_INX" );
		return SLURM_SUCCESS;
	}

	if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
		return SLURM_ERROR;
	printf( format, job->req_node_inx );


	return SLURM_SUCCESS;
}

int
_print_job_features( job_info_t* job, int width, bool left_justify )
{
	char format[FORMAT_STRING_SIZE];
	if ( job == NULL ) /* Print the Header instead */
	{
		if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, "Features" );
		return SLURM_SUCCESS;
	}

	if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
		return SLURM_ERROR;
	printf( format, job->features );


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
		if ( current->function( job_step, current->width, current->left_justify ) 
				!= SLURM_SUCCESS )
			return SLURM_ERROR;
		
	}
	printf("\n");
	return SLURM_SUCCESS;
}

int
step_format_add_function ( List list, int width, bool left_justify, 
		int (*function)(job_step_info_t*,int,bool) )
{
	step_format_t* tmp = (step_format_t*) xmalloc(sizeof(step_format_t));	
	tmp->function = function;
	tmp->width = width;
	tmp->left_justify = left_justify;

	/* FIXME: Check for error */
	list_append( list, tmp );
	return SLURM_SUCCESS;
}

int _print_step_id( job_step_info_t* step, int width, bool left_justify )
{
	char format[FORMAT_STRING_SIZE];
    char id[FORMAT_STRING_SIZE];
	if ( step == NULL ) /* Print the Header instead */
	{
		if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, "ID" );
		return SLURM_SUCCESS;
	}

	if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
		return SLURM_ERROR;
	snprintf( id, FORMAT_STRING_SIZE, "%s.%u.%u", step->partition, step->job_id, step->step_id );
	printf( format, id );

	return SLURM_SUCCESS;
}

int _print_step_user_id( job_step_info_t* step, int width, bool left_justify )
{
	char format[FORMAT_STRING_SIZE];
	struct passwd* user_info = NULL;
	if ( step == NULL ) /* Print the Header instead */
	{
		if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, "USER" );
		return SLURM_SUCCESS;
	}

	user_info = getpwuid( (uid_t) step->user_id );
	if ( user_info != NULL || user_info->pw_name[0] == '\0' )
	{
		if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, user_info->pw_name );
	}
	else
	{
		if ( _create_format( format, "d", width, left_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, step->user_id );
	}

	if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
		return SLURM_ERROR;
	return SLURM_SUCCESS;
}

int _print_step_start_time( job_step_info_t* step, int width, bool left_justify )
{
	char format[FORMAT_STRING_SIZE];
	if ( step == NULL ) /* Print the Header instead */
	{
		if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, "START" );
		return SLURM_SUCCESS;
	}

	_print_time( step->start_time, 0, width, left_justify );
	return SLURM_SUCCESS;
}

int _print_step_nodes( job_step_info_t* step, int width, bool left_justify )
{
	char format[FORMAT_STRING_SIZE];
	if ( step == NULL ) /* Print the Header instead */
	{
		if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
			return SLURM_ERROR;
		printf( format, "NODES" );
		return SLURM_SUCCESS;
	}

	if ( _create_format( format, "s", width, left_justify ) == SLURM_ERROR )
		return SLURM_ERROR;
	printf( format, step->nodes );


	return SLURM_SUCCESS;
}

