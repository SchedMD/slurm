/*****************************************************************************\
 *  squeue.c - Report jobs in the system
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by moe jette <jette1@llnl.gov>
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

#include <src/sinfo/sinfo.h>


/********************
 * Global Variables *
 ********************/
static char *command_name;
struct sinfo_parameters params;
int quiet_flag=0;

/************
 * Funtions *
 ************/
void usage ();
char* int_to_str( int num );

int query_server( partition_info_msg_t ** part_pptr, node_info_msg_t ** node_pptr  );

void display_partition_info_long( List partitions );
void display_partition_info( List partitions );
void display_partition_summary (char* partition);
void display_nodes_list( List nodes );

void display_all_nodes( node_info_msg_t* node_msg );


int 
main (int argc, char *argv[]) 
{
	log_options_t opts = LOG_OPTS_STDERR_ONLY ;
	partition_info_msg_t* partition_msg = NULL;
	node_info_msg_t* node_msg = NULL;

	command_name = argv[0];
	quiet_flag = 0;

	log_init(argv[0], opts, SYSLOG_FACILITY_DAEMON, NULL);
	parse_command_line( argc, argv );
	print_options();

	if ( query_server( &partition_msg, &node_msg ) != 0 )
		exit (1);

	display_all_nodes( node_msg );
	
	exit (0);
}


int
query_server( partition_info_msg_t ** part_pptr, node_info_msg_t ** node_pptr  )
{
	static time_t last_update_time = (time_t) NULL;
	int error_code;

	error_code = slurm_load_partitions (last_update_time, part_pptr);
	if (error_code) {
		slurm_perror ("slurm_load_part");
		return (error_code);
	}

	error_code = slurm_load_node (last_update_time, node_pptr);
	if (error_code) {
		slurm_perror("slurm_load_node");
		return (error_code);
	}

	return 0;
}

void display_all_nodes( node_info_msg_t* node_msg )
{
	List nodes = list_create( NULL );
	int i = 0;
	for (; i < node_msg-> record_count; i++ )
		list_append( nodes, &(node_msg->node_array[i]) );

	display_nodes_list( nodes );
}


/*
 */
void 
display_partition_summary (char* partition) 
{

}

void
display_nodes_list( List nodes )
{
	/*int console_width = atoi( getenv( "COLUMNS" ) );*/
	int console_width = 80;
	char line[BUFSIZ];
	char format[32];
	ListIterator i = list_iterator_create( nodes );
	node_info_t* curr = NULL ;

	printf( "%10s %10s %4s %10s %10s %5s %10s %s\n",
			"Name", "State","CPUS", "MEMORY", "DISK", "WGHT", "PARTITION", "FEATURES");
	printf( "--------------------------------------------------------------------------------\n");
	while ( ( curr = list_next( i ) ) != NULL )
	{

		snprintf(line, BUFSIZ, "%10.10s %10.10s %4.4s %10.10s %10.10s %5.5s %10.10s %s",
				curr->name,
				node_state_string( curr->node_state ), 
				int_to_str( curr-> cpus ),
				int_to_str( curr-> real_memory ),
				int_to_str( curr-> tmp_disk ),
				int_to_str( curr-> weight ),
				curr-> partition,
				curr-> features );

		if ( params.line_wrap )
			printf( "%s\n", line );
		else
		{
			snprintf(format, 32, "%%.%ds\n", MAX(80,console_width) );
			printf( format, line );
		}
		
	}
	printf( "-- %.8s NODES LISTED --\n\n",
			 int_to_str( list_count( nodes ) ) );

}

void
display_partition_info( List partitions )
{

}



void
display_partition_info_long( List partitions )
{

}


/* int_to_str - returns an int as a string to allow formatting with printf better
 * IN      int num - the number to convert to a string.
 * RETURN  char* - string representation
 * 
 * NOT THREAD SAFE, but this doesn't use threads... Also a circular buffer is used,
 * so this data that this method returns should only be used before too many 
 * subsequent calls.
 */
char int_to_str_buffer[BUFSIZ];
char* int_to_str_current = int_to_str_buffer;
const char* int_to_str_end = int_to_str_buffer + BUFSIZ - 1;

char* 
int_to_str( int num )
{
	char* temp = int_to_str_current;
	int size = snprintf(temp, int_to_str_end - temp, "%d", num );
	if ( size < 0 )
	{
		int_to_str_current = int_to_str_buffer;
		return int_to_str( num );
	}

	int_to_str_current = int_to_str_current + size+1;
	return temp;	
}
