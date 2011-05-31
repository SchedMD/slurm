/* *mode: c; c-basic-offset: 8; indent-tabs-mode: nil;*
* vim:expandtab:shiftwidth=8:tabstop=8:
*/

/*****************************************************************************\
 *  slurm_php.h - php interface to slurm.
 *
 *****************************************************************************
 *  Copyright (C) 2011 - Trinity Centre for High Performance Computing
 *  Copyright (C) 2011 - Trinity College Dublin
 *  Written By : Vermeulen Peter <HoWest><Belgium>
 *  
 *  This file is part of php-slurm, a resource management program.
 *  Please also read the included file: DISCLAIMER.
 *  
 *  php-slurm is free software; you can redistribute it and/or modify it under
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
 *  php-slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with php-slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifndef SLURM_PHP_H
#define SLURM_PHP_H 1

#define SLURM_PHP_VERSION "1.0"
#define SLURM_PHP_EXTNAME "slurm"
/*
 *	Adjust this value to change the format of the returned string
 *	values. 
 *	
 *	For more information on formatting options : 
 *		http://www.java2s.com/Tutorial/C/0460__time.h/strftime.htm
 */
#define TIME_FORMAT_STRING "%c"

#include "php.h"
#include "slurm/slurm.h"

#include <time.h>
#include <stdlib.h>

extern zend_module_entry slurm_php_module_entry;

/*****************************************************************************\
 *	TYPEDEFS
\*****************************************************************************/

typedef struct key_value {
	char *name;		/* key */
	char *value;		/* value */
} key_pair;

/*****************************************************************************\
 *	HELPER FUNCTION PROTOTYPES
\*****************************************************************************/

/*
 * now - Get the current time 
 * RET time_t * : Pointer to the memory holding the current time
 */
time_t now();

/*
 * ld_partition_info - Issue slurm to load the partition info into part_pptr
 *
 * IN part_pptr - place to store a partition configuration pointer
 * IN show_flags - partition filtering options
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_partition_info_msg
 */
int ld_partition_info(partition_info_msg_t ** part_pptr, uint16_t show_flags);

/*
 * ld_node_info - Issue slurm to load the node info into node_pptr
 *
 * IN node_pptr - place to store a node configuration pointer
 * IN show_flags - partition filtering options
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_node_info_msg
 */
int ld_node_info(node_info_msg_t ** node_pptr, uint16_t show_flags);

/*
 * ld_job_info - Issue slurm to load the job info into job_pptr
 *
 * IN job_pptr - place to store a node configuration pointer
 * IN show_flags - partition filtering options
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_job_info_msg
 */
int ld_job_info(job_info_msg_t ** job_pptr, uint16_t show_flags);

/*
 * parse_node_pointer - Parse a node pointer's contents into an
 *	assocative zval array where the key is descriptive to the
 *	value
 *
 * IN sub_arr - array to store the contents of the node pointer
 * IN node_arr - node pointer that needs parsing
 */
void parse_node_pointer(zval * sub_arr, node_info_t * node_arr);

/*
 * parse_assoc_array - Parse a character array where the elements are
 *	key-value pairs separated by delimiters into an associative
 *	array
 * 
 * IN char_arr - character array that needs parsing
 * IN delims - character array that contains the delimeters used in parsing
 * IN result_arr - associative array used to store the key_value pairs in
 */
void parse_assoc_array(char * char_arr, char * delims, zval * result_arr);

/*
 * parse_array - Parse a character array where the elements are values
 *	 separated by delimiters into a numerically indexed array
 * 
 * IN char_arr - character array that needs parsing
 * IN delims - character array that contains the delimeters used in parsing
 * IN result_arr - numerically indexed array used to store the values in
 */
void parse_array(char * char_arr, char * delims, zval * rslt_arr);

/*
 * get_partition_from_name - Load the information about a specific partition
 *	 by passing on a character array containing the partition name
 * 
 * IN name - character array containing the partition name
 * IN prt_data - pointer to store the partition information in (if a
 *	partition is found with that specific name)
 * IN prt_ptr - pointer containing all the partition information of all the arrays
 * RET partition_info_t pointer that contains the partition data, or
 *      null if the partition wasn't found
 */
partition_info_t * get_partition_from_name(char * name, partition_info_t * prt_data, partition_info_msg_t * prt_ptr);

/*
 * zend_add_valid_assoc_string - checks a character array to see if it's NULL or
 *	not, if so an associative null is added, if not an associative string is
 *	added.
 *
 * IN rstl_arr - array to store the associative key_value pairs in
 * IN key - character array used as the associative key
 * IN val - character array to be validated and added as value if valid
 */
void zend_add_valid_assoc_string(zval * rstl_arr, char *key, char *val);

/*
 * zend_add_valid_assoc_time_string - checks a unix timestamp to see if it's 0 or
 *	not, if so an associative null is added, if not a formatted string
 *	is added.
 *
 * IN rstl_arr - array to store the associative key_value pairs in
 * IN key - character array used as the associative key
 * IN val - time_t unix timestamp to be validated and added if valid
 * NOTE : If you'd like to change the format in which the valid strings are returned
 *	, you can change the TIME_FORMAT_STRING macro to the needed format
 */
void zend_add_valid_assoc_time_string(zval * rstl_arr, char *key, time_t * val);

/*****************************************************************************\
 *	SLURM PHP HOSTLIST FUNCTIONS
\*****************************************************************************/

/*
 * slurm_hostlist_to_array - converts a hostlist string to 
 * a numerically indexed array.
 *
 * IN host_list - string value containing the hostlist
 * RET numerically indexed array containing the names of the nodes
 */
PHP_FUNCTION(slurm_hostlist_to_array);

/*
 * slurm_array_to_hostlist - convert an array of nodenames into a hostlist string
 *
 * IN node_arr - Numerically indexed array containing a nodename on each index
 * RET String variable containing the hostlist string
 */
PHP_FUNCTION(slurm_array_to_hostlist);

/*****************************************************************************\
 *	SLURM STATUS FUNCTIONS
\*****************************************************************************/

/*
 * slurm_ping - Issues the slurm interface to return the status of the slurm 
 *	primary and secondary controller
 *
 * RET associative array containing the status ( status = 0 if online, = -1 if 
 *	offline ) of both controllers
 * NOTE : the error codes and their meaning are described in the section labelled
 *	EXTRA
 */
PHP_FUNCTION(slurm_ping);

/*
 * slurm_slurmd_status - Issues the slurm interface to return the 
 *	status of the slave daemon ( running on this machine )
 *
 * RET associative array containing the status or a negative long variable
 *	containing an error code
 * NOTE : the error codes and their meaning are described in the section labelled
 *	EXTRA
 */
PHP_FUNCTION(slurm_slurmd_status);

/*
 * slurm_version - Returns the slurm version number in the requested format
 *
 * IN option - long/integer value linking to the formatting of the version number
 * RET long value containing the specific formatted version number a numeric
 *	array containing the version number or a negative long variable containing
 *	an error code.
 * NOTE : the possible cases and their meaning are described in the section labelled
 *	EXTRA
 */
PHP_FUNCTION(slurm_version);

/*****************************************************************************\
 *	SLURM PARTITION READ FUNCTIONS
\*****************************************************************************/

/*
 * slurm_print_partition_names - Creates and returns a numerically indexed array
 *	containing the names of the partitions
 *
 * RET numerically indexed array containing the partitionnames or a negative long
 *	variable containing an error code
 * NOTE : the error codes and their meaning are described in the section labelled
 *	EXTRA
 */
PHP_FUNCTION(slurm_print_partition_names);
/*
 * slurm_get_specific_partition_info - Searches for the requested partition and 
 *	if found it returns an associative array containing the information about 
 *	this specific partition
 *
 * IN name - a string variable containing the partitionname
 * OPTIONAL IN lngth - a long variable containing the length of the partitionname
 * RET an associative array containing the information about a specific partition,
 *	or a negative long value containing an error code
 * NOTE : the error codes and their meaning are described in the section labelled
 *	EXTRA
 */
PHP_FUNCTION(slurm_get_specific_partition_info);
/*
 * slurm_get_partition_node_names - Searches for the requested partition and 
 *	if found it parses the nodes into a numerically indexed array, which is
 *	then returned to the calling function.
 *
 * IN name - a string variable containing the partitionname
 * OPTIONAL IN lngth - a long variable containing the length of the partitionname
 * RET a numerically indexed array containing the names of all the nodes connected
 *	to this partition, or a negative long value containing an error code
 * NOTE : the error codes and their meaning are described in the section labelled
 *	EXTRA
 */
PHP_FUNCTION(slurm_get_partition_node_names);

/*****************************************************************************\
 *	SLURM NODE CONFIGURATION READ FUNCTIONS
\*****************************************************************************/

/*
 * slurm_get_node_names - Creates and returns a numerically index array
 *	containing the nodenames.
 *
 * RET a numerically indexed array containing the requested nodenames,
 *	or a negative long value containing an error code
 * NOTE : the error codes and their meaning are described in the
 *	section labelled EXTRA
 */
PHP_FUNCTION(slurm_get_node_names);
/*
 * slurm_get_node_elements - Creates and returns an associative array
 *	containing all the nodes indexed by nodename and as value an
 *	associative array containing their information.
 *
 * RET an associative array containing the nodes as keys and their
 *	information as value, or a long value containing an error code
 * NOTE : the error codes and their meaning are described in the
 *	section labelled EXTRA
 */
PHP_FUNCTION(slurm_get_node_elements);

/*
 * slurm_get_node_element_by_name - Searches for the requested node
 *	and if found it parses its information into an associative
 *	array, which is then returned to the calling function.
 *
 * IN name - a string variable containing the nodename
 * OPTIONAL IN lngth - a long variable containing the length of the nodename
 * RET an assocative array containing the requested information or a
 *	long value containing an error code
 * NOTE : the error codes and their meaning are described in the
 *	section labelled EXTRA
 */
PHP_FUNCTION(slurm_get_node_element_by_name);

/*
 * slurm_get_node_state_by_name - Searches for the requested node and
 *	if found it returns the state of that node
 *
 * IN name - a string variable containing the nodename
 * OPTIONAL IN lngth - a long variable containing the length of the nodename
 * RET a long value containing the state of the node [0-7] or a
 *	negative long value containing the error code
 * NOTE : the error codes and their meaning are described in the
 *	section labelled EXTRA
 */
PHP_FUNCTION(slurm_get_node_state_by_name);

/*
 * slurm_get_node_states - Creates a numerically indexed array containing the 
 *	state of each node ( only the state ! ) as a long value. This function
 *	could be used to create a summary of the node states without having to 
 *	do a lot of processing ( or having to deal with overlapping nodes between
 *	partitions ).
 *
 * RET a numerically indexed array containing node states
 */
PHP_FUNCTION(slurm_get_node_states);

/*****************************************************************************\
 *	SLURM CONFIGURATION READ FUNCTIONS
\*****************************************************************************/

/*
 * Due to the configuration being quite large, i decided to create 2 functions
 * to return the keys and values separately. ( to prevent a buffer overflow )
 */

/*
 * slurm_get_control_configuration_keys - Retreives the configuration from the slurm
 *	daemon and parses it into a numerically indexed array containg the keys that
 *	link to the values ( the values are retreived by the 
 *	slurm_get_control_configuration_values function )
 * RET a numerically indexed array containing keys that describe the values of the 
 *	configuration of the slurm daemon, or a long value containing an error code
 * NOTE : the error codes and their meaning are described in the section labelled
 *	EXTRA
 */
PHP_FUNCTION(slurm_get_control_configuration_keys);
/*
 * slurm_get_control_configuration_values - Retreives the
 *	configuration from the slurm daemon and parses it into a
 *	numerically indexed array containg the values that link to the
 *	keys ( the keys are retreived by the
 *	slurm_get_control_configuration_keys function )
 *
 * RET a numerically indexed array containing the values of the
 *	configuration of the slurm daemon, or a long value containing
 *	an error code
 * NOTE : the error codes and their meaning are described in the
 *	section labelled EXTRA
 */
PHP_FUNCTION(slurm_get_control_configuration_values);

/*****************************************************************************\
 *	SLURM JOB READ FUNCTIONS
\*****************************************************************************/

/*
 * slurm_load_job_information - Loads the information of all the jobs,
 *	parses it and returns the values as an associative array where
 *	each key is the job id linking to an associative array with
 *	the information of the job
 *
 * RET an associative array containing the information of all jobs, or
 *	a long value containing an error code.
 *
 * NOTE : the error codes and their meaning are described in the
 *	section labelled EXTRA
 */
PHP_FUNCTION(slurm_load_partition_jobs);

/*
 * slurm_load_partition_jobs - Retreive the information of all the
 *	jobs running on a single partition.
 *
 * IN pname - The partition name as a string value
 * OPTIONAL IN lngth - a long variable containing the length of the
 * partitionname
 * RET an associative array containing the information of all the jobs
 *	running on this partition. Or a long value containing an error
 *	code
 * NOTE : the error codes and their meaning are described in the
 *	section labelled EXTRA
 */
PHP_FUNCTION(slurm_load_job_information);

/*****************************************************************************\
 *	EXTRA
 *****************************************************************************
 *
 *	[ERROR CODES]
 *
 *		-3	:	no/incorrect variables where passed on
 *		-2	:	An error occurred whilst trying to communicate
 *				with the daemon
 *		-1	:	Your query produced no results
 *
 *	[VERSION FORMATTING OPTIONS]
 *
 *		0	:	major of the version number
 *		1	:	minor of the version number
 *		2	:	micro of the version number
 *		default	:	full version number
 *		
 *		[EXPLANATION]
 *
 *			Consider the version number 2.2.3,
 *			if we were to split this into an array 
 *			where the "." sign is the delimiter
 *			we would receive the following 
 *
 *				[2]	=>	MAJOR
 *				[2]	=>	MINOR
 *				[3]	=>	MICRO
 *
 *			When requesting the major you would
 *			only receive the major, when requesting
 *			the full version you would receive the array
 *			as depicted above. 
 *
\*****************************************************************************/

#define phpext_slurm_php_ptr &slurm_php_module_entry

#endif
