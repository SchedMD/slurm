/*****************************************************************************\
 *  squeue.c - Report jobs in the slurm system
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Joey Ekstrom <ekstrom1@llnl.gov>, Moe Jette <jette1@llnl.gov>
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

#include <sys/ioctl.h>
#include <termcap.h>
#include <termios.h>

#include "src/squeue/squeue.h"

static char *command_name;
struct squeue_parameters params;
int quiet_flag = 0;
int max_line_size;

static int  _get_window_width( void );
static void _print_date( void );
static void _print_job (void);
static void _print_job_steps( void );

int 
main (int argc, char *argv[]) 
{
	log_options_t opts = LOG_OPTS_STDERR_ONLY ;

	command_name = argv[0];
	quiet_flag = 0;

	log_init(argv[0], opts, SYSLOG_FACILITY_DAEMON, NULL);
	parse_command_line( argc, argv );
	max_line_size = _get_window_width( );
	
	while (1) 
	{
		if ( params.iterate && (params.verbose || params.long_list) )
			_print_date ();

		if ( params.step_flag )
			_print_job_steps( );
		else 
			_print_job( );

		if ( params.iterate ) {
			printf( "\n");
			sleep( params.iterate );
		}
		else
			break;
	}

	exit (0);
}

/* get_window_width - return the size of the window STDOUT goes to */
static int  
_get_window_width( void )
{
	int width = 80;

#ifdef TIOCGSIZE
	struct ttysize win;
	if (ioctl (STDOUT_FILENO, TIOCGSIZE, &win) == 0)
		width = win.ts_cols;
#elif defined TIOCGWINSZ
	struct winsize win;
	if (ioctl (STDOUT_FILENO, TIOCGWINSZ, &win) == 0)
		width = win.ws_col;
#else
	const char *s;
	s = getenv("COLUMNS");
	if (s)
		width = strtol(s,NULL,10);
#endif
	return width;
}


/* _print_job - print the specified job's information */
static void 
_print_job ( void ) 
{
	static job_info_msg_t * old_job_ptr = NULL, * new_job_ptr;
	int error_code;

	if (old_job_ptr) {
		error_code = slurm_load_jobs (old_job_ptr->last_update, 
		                              &new_job_ptr);
		if (error_code ==  SLURM_SUCCESS)
			slurm_free_job_info_msg( old_job_ptr );
		else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_job_ptr = old_job_ptr;
		}
	}
	else
		error_code = slurm_load_jobs ((time_t) NULL, &new_job_ptr);
	if (error_code) {
		slurm_perror ("slurm_load_jobs error:");
		return;
	}
	old_job_ptr = new_job_ptr;
	
	if (quiet_flag == -1)
		printf ("last_update_time=%ld\n", 
		        (long) new_job_ptr->last_update);

	if (params.format_list == NULL) {
		int out_size = 0;
		params.format_list = list_create( NULL );
		job_format_add_job_id( params.format_list, 7, true, " " );
		out_size += (7 + 1);
		job_format_add_partition( params.format_list, 9, false, " " );
		out_size += (9 + 1);
		job_format_add_name( params.format_list, 8, false, " " );
		out_size += (8 + 1);
		job_format_add_user_name( params.format_list, 8, false, " " );
		out_size += (8 + 1);
		if (params.long_list) {
			job_format_add_job_state( params.format_list, 
			                          8, false, " " );
			out_size += (8 + 1);
		} else {
			job_format_add_job_state_compact( params.format_list, 
			                                  2, false, " " );
			out_size += (2 + 1);
		}
		job_format_add_time_used( params.format_list, 9, true, " " );
		out_size += (9 + 1);
		if (params.long_list) {
			job_format_add_time_limit( params.format_list, 8, 
						   true, " " );
			out_size += (8 + 1);
		}
		job_format_add_num_nodes( params.format_list, 6, true, " " );
		out_size += (6 + 1);
		/* Leave node list at the end, length is highly variable */
#ifdef		LINE_LENGTH_LIMITED
		out_size  = max_line_size - out_size - 1;
		job_format_add_nodes( params.format_list, out_size, false, 
		                      NULL );
#else
		job_format_add_nodes( params.format_list, 0, false, NULL );
#endif
	}

	print_jobs_array( new_job_ptr->job_array, new_job_ptr->record_count , 
			params.format_list ) ;
	return;
}


/* _print_job_step - print the specified job step's information */
static void
_print_job_steps( void )
{
	int error_code;
	static job_step_info_response_msg_t * old_step_ptr = NULL;
	static job_step_info_response_msg_t  * new_step_ptr;

	if (old_step_ptr) {
		error_code = slurm_get_job_steps (old_step_ptr->last_update, 
		                                  0, 0, &new_step_ptr);
		if (error_code ==  SLURM_SUCCESS)
			slurm_free_job_step_info_response_msg( old_step_ptr );
		else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_step_ptr = old_step_ptr;
		}
	}
	else
		error_code = slurm_get_job_steps ((time_t) NULL, 0, 0, 
		                                  &new_step_ptr);
	if (error_code) {
		slurm_perror ("slurm_get_job_steps error:");
		return;
	}
	old_step_ptr = new_step_ptr;

	if (quiet_flag == -1)
		printf ("last_update_time=%ld\n", 
		        (long) new_step_ptr->last_update);
	
	if (params.format_list == NULL) {
		int out_size = 0;
		params.format_list = list_create( NULL );
		step_format_add_id( params.format_list, 10, false, " " );
		out_size += (10 + 1);
		step_format_add_partition( params.format_list, 9, false, " " );
		out_size += (9 + 1);
		step_format_add_user_name( params.format_list, 8, false, " " );
		out_size += (8 + 1);
		step_format_add_time_used( params.format_list, 9, 
					    true, " " );
		out_size += (11 + 1);
#ifdef		LINE_LENGTH_LIMITED
		out_size  = max_line_size - out_size - 1;
		step_format_add_nodes( params.format_list, out_size, false, 
			               NULL );
#else
		step_format_add_nodes( params.format_list, 0, false, NULL );
#endif
	}
		
	print_steps_array( new_step_ptr->job_steps, 
			   new_step_ptr->job_step_count, 
			   params.format_list );
	return;
}


static void 
_print_date( void )
{
	time_t now;

	now = time( NULL );
	printf("%s", ctime( &now ));
}
