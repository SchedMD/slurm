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
#include <src/common/list.h>


/********************
 * Global Variables *
 ********************/
static char *command_name;
struct sinfo_parameters params = { partition_flag:false, partition:NULL, state_flag:false, node_flag:false, node:NULL, summarize:false, long_output:false, line_wrap:false, verbose:false };

/************
 * Funtions *
 ************/
void usage ();

int query_server( partition_info_msg_t ** part_pptr, node_info_msg_t ** node_pptr  );


/* Node Functions */
void display_all_nodes( node_info_msg_t* node_msg );
void display_nodes_list( List nodes );
void display_nodes_list_long( List nodes );
char* node_name_string_from_list( List nodes, char* buffer );

List group_node_list( node_info_msg_t* msg );

/* Partition Functions */
void display_all_partition_summary( partition_info_msg_t* part_ptr, node_info_msg_t* node_ptr );
void display_partition_info_long( struct partition_summary* partition );
void display_partition_node_info( struct partition_summary* partition, bool print_name );
void display_all_partition_summary( partition_info_msg_t* part_ptr, node_info_msg_t* node_ptr );
void display_all_partition_info_long( List partitions );
void display_partition_summarys ( List partitions );

/* Misc Display functions */
int build_min_max_string( char* buffer, int max, int min );
int print_int( int number, int width, bool right );
int print_str( char* number, int width, bool right );
char* int_to_str( int num );

int 
main (int argc, char *argv[]) 
{
	log_options_t opts = LOG_OPTS_STDERR_ONLY ;
	partition_info_msg_t* partition_msg = NULL;
	node_info_msg_t* node_msg = NULL;

	command_name = argv[0];

	log_init(argv[0], opts, SYSLOG_FACILITY_DAEMON, NULL);
	parse_command_line( argc, argv );
	print_options();

	if ( query_server( &partition_msg, &node_msg ) != 0 )
		exit (1);

	if ( params.node_flag )
		display_all_nodes( node_msg );
	
	else if ( params.partition_flag )
		display_all_partition_summary( partition_msg, node_msg );
	else
		display_all_partition_summary( partition_msg, node_msg );
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

/*****************************************************************************
 *                        DISPLAY NODE INFO FUNCTIONS
 *****************************************************************************/
static const char display_line[] = "--------------------------------------------------------------------------------\n";
static const char display_node_format[] = "%15.15s %10.10s %4.4s %7.7s %7.7s %5.5s %10.10s %s";

void display_all_nodes( node_info_msg_t* node_msg )
{
	printf( "%15s %10s %4s %7s %7s %5s %10s %s\n",
			"Name", "State","CPUS", "MEMORY", "DISK", "WGHT", "PARTITION", "FEATURES");
	printf( display_line );


	if ( params.long_output == true || params.node != NULL )
	{
		List nodes = list_create( NULL );
		int i = 0;

		for ( ; i < node_msg->record_count; i++ )
			if ( ( params.node == NULL || strcmp( node_msg->node_array[i].name, params.node) == 0 )
					&& ( params.state_flag == false || node_msg->node_array[i].node_state == params.state ) )
				list_append( nodes, &node_msg->node_array[i] );
		
		display_nodes_list_long( nodes );

		list_destroy( nodes );
	}
	else 
	{
		List nodes = group_node_list( node_msg );
		ListIterator i = list_iterator_create( nodes );
		List current;

		while ( (current= list_next(i)) != NULL )
		{
			display_nodes_list( current );
			list_destroy( current );
		}
		list_destroy( nodes );
	}
}

void
display_nodes_list( List nodes )
{
	/*int console_width = atoi( getenv( "COLUMNS" ) );*/
	int console_width = 80;
	char line[BUFSIZ];
	char format[32];
	char node_names[32];
	node_info_t* curr = list_peek( nodes ) ;
	node_name_string_from_list( nodes, node_names );


	snprintf(line, BUFSIZ, display_node_format,
			node_names,
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
	
void
display_nodes_list_long( List nodes )
{
	/*int console_width = atoi( getenv( "COLUMNS" ) );*/
	int console_width = 80;
	int count = 0;
	char line[BUFSIZ];
	char format[32];
	ListIterator i = list_iterator_create( nodes );
	node_info_t* curr = NULL ;

	while ( ( curr = list_next( i ) ) != NULL )
	{
		if ( params.partition != NULL && strcmp( params.partition, curr-> partition) )
			continue;

		snprintf(line, BUFSIZ, display_node_format,
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
		count++;	
	}
	printf( "-- %.8s NODES LISTED --\n\n",
			 int_to_str( count ));

}

List
group_node_list( node_info_msg_t* msg )
{
	List node_lists = list_create( NULL );
	node_info_t* nodes = msg->node_array;
	int i;
	
	for ( i=0; i < msg->record_count; i++ )
	{
		ListIterator list_i = list_iterator_create( node_lists );
		List curr_list = NULL;

		if (  params.partition != NULL && strcmp( params.partition, nodes[i].partition ) )
			continue;
		if ( params.state_flag == true && nodes[i].node_state != params.state )
			continue;

		while ( (curr_list = list_next(list_i)) != NULL )
		{
			node_info_t* curr = list_peek( curr_list ); 		
			bool feature_test; 
			bool part_test; 
			
			if ( curr->features != NULL && nodes[i].features != NULL )
				feature_test = strcmp(nodes[i].features, curr->features) ? false : true ;
			else if ( curr->features == nodes[i].features )
				feature_test = true;
			else feature_test = false;
			if ( curr->partition != NULL && nodes[i].partition != NULL )
				part_test = strcmp(nodes[i].partition, curr->partition) ? false : true ;	
			else if ( curr->partition == nodes[i].partition )
				part_test = true;
			else part_test = false;
			
			if ( feature_test && part_test &&
					nodes[i].node_state == curr->node_state &&
					nodes[i].cpus == curr->cpus  &&
					nodes[i].real_memory == curr->real_memory  &&
					nodes[i].tmp_disk == curr->tmp_disk )
			{
				list_append( curr_list, &( nodes[i] ));
				break;
			}
		}
		
		if ( curr_list == NULL )
		{
			List temp = list_create( NULL ) ;
			list_append( temp,  &( nodes[i]) );
			list_append( node_lists, temp ); 
		}
	}

	return node_lists;
}

/*****************************************************************************
 *                     DISPLAY PARTITION INFO FUNCTIONS
 *****************************************************************************/

struct partition_summary*
find_partition_summary( List l, char* name )
{
	ListIterator i = list_iterator_create(l);
	struct partition_summary* current;
	
	while ( (current = list_next(i) ) != NULL )
	{
		if ( strcmp( current->info->name, name ) == 0 )
			return current;
	}
	return NULL;
}

struct node_state_summary*
find_node_state_summary( List l, enum node_states state )
{
	ListIterator i = list_iterator_create(l);
	struct node_state_summary* current;
	
	while ( (current = list_next(i) ) != NULL )
	{
		if ( state == current->state )
			return current;
	}
	return NULL;
}

List
setup_partition_summary( partition_info_msg_t* part_ptr, node_info_msg_t* node_ptr )
{
	int i=0;
	List partitions = list_create( NULL );

	for ( i=0; i < part_ptr->record_count; i++ )
	{
		struct partition_summary* sum = (struct partition_summary*) malloc( sizeof(struct partition_summary));
		sum->info = &part_ptr->partition_array[i];
		sum->states = list_create( NULL );
		list_append( partitions, sum );
	}

	for ( i=0; i < node_ptr->record_count; i++ )
	{
		node_info_t* ninfo = &node_ptr->node_array[i];
		struct partition_summary* part_sum = find_partition_summary( partitions, ninfo->partition );
		struct node_state_summary* node_sum = NULL;
		
		if ( part_sum == NULL )
		{
			/* This should never happen */
			printf("Couldn't find the partition... this is bad news\n");
			continue;
		}
	
		if ( (node_sum = find_node_state_summary( part_sum->states, (enum node_states)ninfo->node_state ) ) != NULL )
		{
			node_sum->state = (enum node_states)ninfo->node_state;
			node_sum->cpu_max = MAX( ninfo->cpus, node_sum->cpu_max );
			node_sum->cpu_min = MIN( ninfo->cpus, node_sum->cpu_min );
			node_sum->ram_max = MAX( ninfo->real_memory, node_sum->ram_max );
			node_sum->ram_min = MIN( ninfo->real_memory, node_sum->ram_min );
			node_sum->disk_max = MAX( ninfo->tmp_disk, node_sum->disk_max);
			node_sum->disk_min = MIN( ninfo->tmp_disk, node_sum->disk_min);
			hostlist_push( node_sum->nodes, ninfo->name );
			node_sum->node_count++;
		}
		else
		{
			node_sum = (struct node_state_summary*) malloc( sizeof(struct node_state_summary));
			node_sum->state = (enum node_states)ninfo->node_state;
			node_sum->cpu_max = ninfo->cpus;
			node_sum->cpu_min = ninfo->cpus;
			node_sum->ram_max = ninfo->real_memory;
			node_sum->ram_min = ninfo->real_memory;
			node_sum->disk_max = ninfo->tmp_disk;
			node_sum->disk_min = ninfo->tmp_disk;
			node_sum->node_count = 1;
			node_sum->nodes = hostlist_create( ninfo->name );
			list_append( part_sum->states, node_sum );
		}
	}
	
	return partitions;
}

void 
display_all_partition_summary( partition_info_msg_t* part_ptr, node_info_msg_t* node_ptr )
{
	List partitions = setup_partition_summary( part_ptr, node_ptr );
	if ( params.long_output )
		display_all_partition_info_long( partitions );		
	else display_partition_summarys( partitions );
	list_destroy( partitions );
}

/* Formating for partiton display headers... */
int part_sz_part  = 10;
int part_sz_num   = 6;
int part_sz_state = 10;
int part_sz_cpus  = 4;
int part_sz_ram   = 8;
int part_sz_disk  = 8;
int part_sz_nodes = 0;

void 
print_partition_header( bool no_name )
{
	if ( no_name == true )
		printf("\t");
	else
	{
		print_str( "NAME", part_sz_part, false ); 
		printf(" ");
	}
	print_str( "NODES", part_sz_num, true );
	printf("  ");
	print_str( "STATE", part_sz_state, false);
	printf(" ");
	print_str( "CPUS", part_sz_cpus, true);
	printf(" ");
	print_str( "MEM", part_sz_ram, true);
	printf(" ");
	print_str( "DISK", part_sz_disk, true);
	print_str( "  NODES", part_sz_nodes, false );
	printf("\n");
	if ( no_name == true )
		printf( "\t------------------------------------------------------------------------\n");
	else
		printf( "--------------------------------------------------------------------------------\n");
}

void 
display_partition_summarys ( List partitions ) 
{
	struct partition_summary* partition ;
	ListIterator part_i = list_iterator_create( partitions );

	print_partition_header( false );
	while ( (partition = list_next( part_i ) ) != NULL )
	{
		if ( params.partition == NULL || strcmp( partition->info->name, params.partition ) == 0 )
			display_partition_node_info( partition, true );
	}
}


void
display_partition_node_info( struct partition_summary* partition, bool print_name )
{
	char* no_name = "";
	char cpu_buf[64];
	char ram_buf[64];
	char disk_buf[64];
	char name_buf[64];


	ListIterator node_i = list_iterator_create( partition->states );
	struct node_state_summary* state_sum = NULL;
	char* part_name = partition->info->name ;

	while ( ( state_sum = list_next(node_i) ) != NULL )
	{
		build_min_max_string( ram_buf, state_sum->ram_min, state_sum->ram_max );
		build_min_max_string( disk_buf, state_sum->disk_min, state_sum->disk_max );
		build_min_max_string( cpu_buf, state_sum->cpu_min, state_sum->cpu_max );
		hostlist_ranged_string( state_sum->nodes, 64, name_buf );

		if ( print_name  == true )
		{
			print_str( part_name, part_sz_part, false ); 
			printf(" ");
		}
		else printf("\t");

		print_int( state_sum->node_count, part_sz_num, true );
		printf("  ");
		print_str( node_state_string( state_sum->state), part_sz_state, false);
		printf(" ");
		print_str( cpu_buf, part_sz_cpus, true);
		printf(" ");
		print_str( ram_buf, part_sz_ram, true);
		printf(" ");
		print_str( disk_buf, part_sz_disk, true);
		printf("  ");
		print_str( name_buf, part_sz_nodes, false );
		printf("\n");
		part_name = no_name;
	}
}


void
display_all_partition_info_long( List partitions )
{
	struct partition_summary* partition ;
	ListIterator part_i = list_iterator_create( partitions );

	while ( (partition = list_next( part_i ) ) != NULL )
	{
		if ( params.partition == NULL || strcmp( partition->info->name, params.partition ) == 0 )
			display_partition_info_long( partition );
		printf("\n");
	}
}

void
display_partition_info_long( struct partition_summary* partition )
{
	partition_info_t* part = partition->info ;
	
	printf( "================================================================================\n" );
	printf("%s\n", part->name );
	printf("\tcurrent state     = %s\n", part->state_up ? "UP" : "DOWN" );
	printf("\tdefault partition = %s\n", part->default_part ? "YES" : "NO" );
	printf("\ttotal nodes       = %d\n", part->total_nodes );
	printf("\ttotal cpus        = %d\n", part->total_cpus );
	if ( part->max_time == -1 ) 
		printf("\tmax jobtime       = NONE\n" );
	else printf("\tmax jobtime       = %d\n", part->max_time );
	printf("\tmax nodes/job     = %d\n", part->max_nodes );
	printf("\troot only         = %s\n", part->root_only ? "YES" : "NO" );
	printf("\tshare nodes       = %s\n", part->shared == 2 ? "ALWAYS" : part->shared ? "YES" : "NO" );

	printf("\n");
	print_partition_header( true );
	display_partition_node_info( partition, false );
	
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


int
build_min_max_string( char* buffer, int min, int max )
{
	if ( max == min )
		return sprintf( buffer, "%d", max );
	
	return sprintf( buffer, "%d-%d", min, max );
}

int
print_str( char* str, int width, bool right )
{
	char format[64];
	int printed = 0;

	if ( right == true && width != 0 )
		snprintf( format, 64, "%%%ds", width );
	else if ( width != 0 )
		snprintf( format, 64, "%%.%ds", width );
	else
		snprintf( format, 64, "%%s", width );

	if ( ( printed = printf( format, str ) ) < 0  )
		return printed;

	while ( printed++ < width )
		printf(" ");

	return printed;


}

int
print_int( int number, int width, bool right )
{
	char buf[32];
	char format[64];
	int printed = 0;

	snprintf( buf, 32, "%d", number );
	return print_str( buf, width, right );
}

/* node_name_string_from_list - analyzes a list of node_info_t* and 
 * 		fills in a buffer with the appropriate nodename in a prefix[001-100]
 * 		type format.
 * @nodes - list of node_info_t* to analyze
 * @buffer - a char buffer to store the string in.
 *
 * return - return on success return buffer, NULL on failure
 */

char* 
node_name_string_from_list( List nodes, char* buffer )
{
	node_info_t* curr_node = NULL;
	ListIterator i = list_iterator_create( nodes );
	hostlist_t list = hostlist_create( NULL );

	while( (curr_node = list_next(i) ) != NULL )
		hostlist_push( list, curr_node->name );
	
	hostlist_ranged_string( list, 32, buffer );
	hostlist_destroy( list );
	return buffer;
}
