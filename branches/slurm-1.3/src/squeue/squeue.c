/*****************************************************************************\
 *  squeue.c - Report jobs in the slurm system
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Joey Ekstrom <ekstrom1@llnl.gov>, 
 *             Morris Jette <jette1@llnl.gov>, et. al.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef HAVE_TERMCAP_H
#  include <termcap.h>
#endif

#include <sys/ioctl.h>
#include <termios.h>

#include "src/common/xstring.h"
#include "src/squeue/squeue.h"

/********************
 * Global Variables *
 ********************/
struct squeue_parameters params;
int quiet_flag = 0;
int max_line_size;

/************
 * Funtions *
 ************/
static int  _get_window_width( void );
static void _print_date( void );
static int _print_job (void);
static int _print_job_steps( void );

int 
main (int argc, char *argv[]) 
{
	log_options_t opts = LOG_OPTS_STDERR_ONLY ;
	int error_code = SLURM_SUCCESS;

	log_init(xbasename(argv[0]), opts, SYSLOG_FACILITY_USER, NULL);
	parse_command_line( argc, argv );
	if (params.verbose) {
		opts.stderr_level += params.verbose;
		log_alter(opts, SYSLOG_FACILITY_USER, NULL);
	}
	max_line_size = _get_window_width( );
	
	while (1) 
	{
		if ((!params.no_header)
		 && (params.iterate || params.verbose || params.long_list))
			_print_date ();
		
		if ( params.step_flag )
			error_code = _print_job_steps( );
		else
			error_code = _print_job( );
		
		if ( params.iterate ) {
			printf( "\n");
			sleep( params.iterate );
		}
		else
			break;
	}

	if ( error_code != SLURM_SUCCESS )
		exit (error_code);
	else
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
static int
_print_job ( void ) 
{
	static job_info_msg_t * old_job_ptr = NULL, * new_job_ptr;
	int error_code;
	uint16_t show_flags = 0;
	uint32_t job_id = 0;

	if (params.all_flag)
		show_flags |= SHOW_ALL;
	if (params.job_list && (list_count(params.job_list) == 1)) {
		ListIterator iterator;
		uint32_t *job_id_ptr;
		iterator = list_iterator_create(params.job_list);
		job_id_ptr = list_next(iterator);
		job_id = *job_id_ptr;
		list_iterator_destroy(iterator);
	}

	if (old_job_ptr) {
		if (job_id) {
			error_code = slurm_load_job(&new_job_ptr, job_id);
		} else {
			error_code = slurm_load_jobs(old_job_ptr->last_update,
						     &new_job_ptr, show_flags);
		}
		if (error_code ==  SLURM_SUCCESS)
			slurm_free_job_info_msg( old_job_ptr );
		else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_job_ptr = old_job_ptr;
		}
	} else if (job_id) {
		error_code = slurm_load_job(&new_job_ptr, job_id);
	} else {
		error_code = slurm_load_jobs((time_t) NULL, &new_job_ptr,
				show_flags);
	}

	if (error_code) {
		slurm_perror ("slurm_load_jobs error");
		return SLURM_ERROR;
	}
	old_job_ptr = new_job_ptr;
	if (job_id)
		old_job_ptr->last_update = (time_t) 0;
	
	if (quiet_flag == -1)
		printf ("last_update_time=%ld\n", 
		        (long) new_job_ptr->last_update);

	if (params.format == NULL) {
		if (params.long_list)
			params.format = "%.7i %.9P %.8j %.8u %.8T %.10M %.9l "
			                "%.6D %R";
		else
			params.format = "%.7i %.9P %.8j %.8u  %.2t %.10M %.6D %R";
	}
	if (params.format_list == NULL)
		parse_format(params.format);

	print_jobs_array( new_job_ptr->job_array, new_job_ptr->record_count , 
			params.format_list ) ;
	return SLURM_SUCCESS;
}


/* _print_job_step - print the specified job step's information */
static int
_print_job_steps( void )
{
	int error_code;
	static job_step_info_response_msg_t * old_step_ptr = NULL;
	static job_step_info_response_msg_t  * new_step_ptr;
	uint16_t show_flags = 0;

	if (params.all_flag)
		show_flags |= SHOW_ALL;

	if (old_step_ptr) {
		error_code = slurm_get_job_steps (old_step_ptr->last_update, 
				0, 0, &new_step_ptr, show_flags);
		if (error_code ==  SLURM_SUCCESS)
			slurm_free_job_step_info_response_msg( old_step_ptr );
		else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_step_ptr = old_step_ptr;
		}
	}
	else
		error_code = slurm_get_job_steps ((time_t) NULL, 0, 0, 
				&new_step_ptr, show_flags);
	if (error_code) {
		slurm_perror ("slurm_get_job_steps error");
		return SLURM_ERROR;
	}
	old_step_ptr = new_step_ptr;

	if (quiet_flag == -1)
		printf ("last_update_time=%ld\n", 
		        (long) new_step_ptr->last_update);
	
	if (params.format == NULL)
		params.format = "%10i %.8j %.9P %.8u %.9M %N";
	if (params.format_list == NULL)
		parse_format(params.format);

	print_steps_array( new_step_ptr->job_steps, 
			   new_step_ptr->job_step_count, 
			   params.format_list );
	return SLURM_SUCCESS;
}


static void 
_print_date( void )
{
	time_t now;

	now = time( NULL );
	printf("%s", ctime( &now ));
}
