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

#define SLURM_PHP_VERSION "1.0.1"
#define SLURM_PHP_EXTNAME "slurm"
/*
 * Adjust this value to change the format of the returned string
 * values.
 *
 * For more information on formatting options :
 * http://www.java2s.com/Tutorial/C/0460__time.h/strftime.htm
 */
#define TIME_FORMAT_STRING "%c"

#include <php.h>
#include <slurm/slurm.h>
#include <slurm/slurmdb.h>

#include <time.h>
#include <string.h>
#include <stdlib.h>

#include "src/common/xmalloc.h"

extern zend_module_entry slurm_php_module_entry;

/*****************************************************************************\
 *	TYPEDEFS
\*****************************************************************************/

typedef struct key_value {
	char *name;		/* key */
	char *value;		/* value */
} key_pair_t;

/* define functions needed to avoid warnings (they are defined in
 * src/common/xstring.h)  If you can figure out a way to make it so we
 * don't have to make these declarations that would be awesome.  I
 * didn't have time to spend on it when I was working on it. -da
 */

/*
** strdup which uses xmalloc routines
*/
char *slurm_xstrdup(const char *str);

/*
** strdup formatted which uses xmalloc routines
*/
char *slurm_xstrdup_printf(const char *fmt, ...)
  __attribute__ ((format (printf, 1, 2)));


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
 * slurm_array_to_hostlist - convert an array of nodenames into a hostlist
 * 	string
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
 * NOTE : the error codes and their meaning are described in the section
 * 	labelled EXTRA
 */
PHP_FUNCTION(slurm_ping);

/*
 * slurm_slurmd_status - Issues the slurm interface to return the
 *	status of the slave daemon ( running on this machine )
 *
 * RET associative array containing the status or a negative long variable
 *	containing an error code
 * NOTE : the error codes and their meaning are described in the section
 * 	labelled EXTRA
 */
PHP_FUNCTION(slurm_slurmd_status);

/*
 * slurm_version - Returns the slurm version number in the requested format
 *
 * IN option - long/integer value linking to the formatting of the version
 *	number
 * RET long value containing the specific formatted version number a numeric
 *	array containing the version number or a negative long variable
 *	containing an error code.
 * NOTE : the possible cases and their meaning are described in the section
 * 	labelled EXTRA
 */
PHP_FUNCTION(slurm_version);


/*****************************************************************************\
 *	SLURM PARTITION READ FUNCTIONS
\*****************************************************************************/

/*
 * slurm_print_partition_names - Creates and returns a numerically
 *	indexed array containing the names of the partitions
 *
 * RET numerically indexed array containing the partitionnames or a
 *	negative long variable containing an error code NOTE : the
 *	error codes and their meaning are described in the section
 *	labelled EXTRA
 */
PHP_FUNCTION(slurm_print_partition_names);
/*
 * slurm_get_specific_partition_info - Searches for the requested
 *	partition and if found it returns an associative array
 *	containing the information about this specific partition
 *
 * IN name - a string variable containing the partitionname
 * OPTIONAL IN lngth - a long variable containing the length of the
 *      partitionname
 * RET an associative array containing the information about a
 *	specific partition, or a negative long value containing an
 *	error code
 * NOTE : the error codes and their meaning are described in the
 *	section labelled EXTRA
 */
PHP_FUNCTION(slurm_get_specific_partition_info);

/*
 * slurm_get_partition_node_names - Searches for the requested partition and
 *	if found it parses the nodes into a numerically indexed array, which is
 *	then returned to the calling function.
 *
 * IN name - a string variable containing the partitionname
 *
 * OPTIONAL IN lngth - a long variable containing the length of the
 * partitionname
 *
 * RET a numerically indexed array containing the names of all the
 *	nodes connected to this partition, or a negative long value
 *	containing an error code
 *
 * NOTE : the error codes and their meaning are described in the
 *	section labelled EXTRA
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
 * slurm_get_node_states - Creates a numerically indexed array
 *	containing the state of each node ( only the state ! ) as a
 *	long value. This function could be used to create a summary of
 *	the node states without having to do a lot of processing ( or
 *	having to deal with overlapping nodes between partitions ).
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
 * slurm_get_control_configuration_keys - Retreives the configuration
 *	from the slurm daemon and parses it into a numerically indexed
 *	array containg the keys that link to the values ( the values
 *	are retreived by the slurm_get_control_configuration_values
 *	function )
 *
 * RET a numerically indexed array containing keys that describe the
 *	values of the configuration of the slurm daemon, or a long
 *	value containing an error code
 *
 *  NOTE : the error codes and their meaning are described in the
 *	section labelled EXTRA
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
PHP_FUNCTION(slurm_load_job_information);

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
PHP_FUNCTION(slurm_load_partition_jobs);


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
