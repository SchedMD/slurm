/* 
 * partition_info.c - get the partition information of slurm
 * see slurm.h for documentation on external functions and data structures
 *
 * author: moe jette, jette@llnl.gov
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "slurm.h"
#include "slurmlib.h"

static char *build_api_buffer = NULL;
static int build_api_buffer_size = 0;
static pthread_mutex_t build_api_mutex = PTHREAD_MUTEX_INITIALIZER;

#if DEBUG_MODULE
/* main is used here for module testing purposes only */
main (int argc, char *argv[]) {
	char req_name[BUILD_SIZE], next_name[BUILD_SIZE], value[BUILD_SIZE];
	int error_code;

	error_code = slurm_load_build ();
	if (error_code) {
		printf ("slurm_load_build error %d\n", error_code);
		exit (1);
	}			
	strcpy (req_name, "");	/* start at beginning of build configuration list */
	while (error_code == 0) {
		error_code = slurm_load_build_name (req_name, next_name, value);
		if (error_code != 0) {
			printf ("slurm_load_build_name error %d finding %s\n",
				error_code, req_name);
			break;
		}		

		printf ("%s=%s\n", req_name, value);
		if (strlen (next_name) == 0)
			break;
		strcpy (req_name, next_name);
	}			
	slurm_free_build_info ();
	exit (0);
}
#endif


/*
 * slurm_free_build_info - free the build information buffer (if allocated)
 * NOTE: buffer is loaded by load_build and used by load_build_name.
 */
void slurm_free_build_info (void) {
	pthread_mutex_lock(&build_api_mutex);
	if (build_api_buffer)
		free (build_api_buffer);
	build_api_buffer = NULL;
	pthread_mutex_unlock(&build_api_mutex);
}


/*
 * slurm_load_build - update the build information buffer for use by info gathering APIs
 * output: returns 0 if no error, EINVAL if the buffer is invalid, ENOMEM if malloc failure.
 * NOTE: buffer is used by load_build_name and freed by free_build_info.
 */
int slurm_load_build () {
	int buffer_offset, buffer_size, error_code, in_size, version;
	char *buffer, *my_line;
	int sockfd;
	struct sockaddr_in serv_addr;
	unsigned long my_time;

	if (build_api_buffer)
		return 0;	/* already loaded */

	if ((sockfd = socket (AF_INET, SOCK_STREAM, 0)) < 0)
		return EINVAL;
	serv_addr.sin_family = PF_INET;
	serv_addr.sin_addr.s_addr = inet_addr (SLURMCTLD_HOST);
	serv_addr.sin_port = htons (SLURMCTLD_PORT);
	if (connect
	    (sockfd, (struct sockaddr *) &serv_addr,
	     sizeof (serv_addr)) < 0) {
		close (sockfd);
		return EINVAL;
	}			
	if (send (sockfd, "DumpBuild", 10, 0) < 10) {
		close (sockfd);
		return EINVAL;
	}			
	buffer = NULL;
	buffer_offset = 0;
	buffer_size = 1024;
	while (1) {
		buffer = realloc (buffer, buffer_size);
		if (buffer == NULL) {
			close (sockfd);
			return ENOMEM;
		}		
		in_size =
			recv (sockfd, &buffer[buffer_offset],
			      (buffer_size - buffer_offset), 0);
		if (in_size <= 0) {	/* end if input */
			in_size = 0;
			break;
		}		
		buffer_offset += in_size;
		buffer_size += in_size;
	}			
	close (sockfd);
	buffer_size = buffer_offset + in_size;
	buffer = realloc (buffer, buffer_size);
	if (buffer == NULL)
		return ENOMEM;

	buffer_offset = 0;
	error_code =
		read_buffer (buffer, &buffer_offset, buffer_size, &my_line);
	if ((error_code) || (strlen (my_line) < strlen (HEAD_FORMAT))) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "load_build: node buffer lacks valid header\n");
#else
		syslog (LOG_ERR,
			"load_build: node buffer lacks valid header\n");
#endif
		free (buffer);
		return EINVAL;
	}			
	sscanf (my_line, HEAD_FORMAT, &my_time, &version);
	if (version != BUILD_STRUCT_VERSION) {
#if DEBUG_SYSTEM
		fprintf (stderr, "load_build: expect version %d, read %d\n",
			 BUILD_STRUCT_VERSION, version);
#else
		syslog (LOG_ERR, "load_build: expect version %d, read %d\n",
			BUILD_STRUCT_VERSION, version);
#endif
		free (buffer);
		return EINVAL;
	}			

	pthread_mutex_lock(&build_api_mutex);
	if (build_api_buffer) free(build_api_buffer);
	build_api_buffer = buffer;
	build_api_buffer_size = buffer_size;
	pthread_mutex_unlock(&build_api_mutex);
	return 0;
}


/* 
 * slurm_load_build_name - load the state information about the named build parameter
 * input: req_name - name of the parameter for which information is requested
 *		     if "", then get info for the first parameter in list
 *        next_name - location into which the name of the next parameter is 
 *                   stored, "" if no more
 *        value - pointer to location into which the information is to be stored
 * output: req_name - the parameter's name is stored here
 *         next_name - the name of the next parameter in the list is stored here
 *         value - the parameter's state information
 *         returns 0 on success, ENOENT if not found, or EINVAL if buffer is bad
 * NOTE:  req_name, next_name, and value must be declared by caller with have 
 *        length BUILD_SIZE or larger
 * NOTE: buffer is loaded by load_build and freed by free_build_info.
 */
int slurm_load_build_name (char *req_name, char *next_name, char *value) {
	int i, error_code, version, buffer_offset;
	static char next_build_name[BUILD_SIZE] = "";
	static last_buffer_offset;
	unsigned long my_time;
	char *my_line;
	char my_build_name[BUILD_SIZE], my_build_value[BUILD_SIZE];

	/* load buffer's header (data structure version and time) */
	pthread_mutex_lock(&build_api_mutex);
	buffer_offset = 0;
	error_code =
		read_buffer (build_api_buffer, &buffer_offset,
			     build_api_buffer_size, &my_line);
	if (error_code) {
		pthread_mutex_unlock(&build_api_mutex);
		return error_code;
	}
	sscanf (my_line, HEAD_FORMAT, &my_time, &version);

	if ((strcmp (req_name, next_build_name) == 0) &&
	    (strlen (req_name) != 0))
		buffer_offset = last_buffer_offset;

	while (1) {
		/* load all info for next parameter */
		error_code =
			read_buffer (build_api_buffer, &buffer_offset,
				     build_api_buffer_size, &my_line);
		if (error_code == EFAULT)
			break;	/* end of buffer */
		if (error_code) {
			pthread_mutex_unlock(&build_api_mutex);
			return error_code;
		}

		i = sscanf (my_line, BUILD_STRUCT_FORMAT, my_build_name,
			    my_build_value);
		if (i == 1)
			strcpy (my_build_value, "");	/* empty string passed */
		if (strlen (req_name) == 0)
			strncpy (req_name, my_build_name, BUILD_SIZE);

		/* check if this is requested parameter */
		if (strcmp (req_name, my_build_name) != 0)
			continue;

		/*load values to be returned */
		strncpy (value, my_build_value, BUILD_SIZE);

		last_buffer_offset = buffer_offset;
		error_code =
			read_buffer (build_api_buffer, &buffer_offset,
				     build_api_buffer_size, &my_line);
		if (error_code) {	/* no more records */
			strcpy (next_build_name, "");
			strcpy (next_name, "");
		}
		else {
			sscanf (my_line, BUILD_STRUCT_FORMAT, my_build_name,
				my_build_value);
			strncpy (next_build_name, my_build_name, BUILD_SIZE);
			strncpy (next_name, my_build_name, BUILD_SIZE);
		}		
		pthread_mutex_unlock(&build_api_mutex);
		return 0;
	}			
	pthread_mutex_unlock(&build_api_mutex);
	return ENOENT;
}
