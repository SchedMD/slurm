/* 
 * controller.c - main control machine daemon for slurm
 * see slurm.h for documentation on external functions and data structures
 *
 * NOTE: DEBUG_MODULE of read_config requires that it be loaded with 
 *       bits_bytes, partition_mgr, read_config, and node_mgr
 *
 * author: moe jette, jette@llnl.gov
 */

#ifdef have_config_h
#  include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include "slurm.h"
#include "slurmlib.h"

#define BUF_SIZE 1024

int msg_from_root (void);
void slurmctld_req (int sockfd);

int 
main (int argc, char *argv[]) {
	int error_code;
	int cli_len, newsockfd, sockfd;
	struct sockaddr_in cli_addr, serv_addr;
	char node_name[MAX_NAME_LEN];
	log_options_t opts = LOG_OPTS_STDERR_ONLY;

	log_init(argv[0], opts, SYSLOG_FACILITY_DAEMON, NULL);

	error_code = init_slurm_conf ();
	if (error_code)
		fatal ("slurmctld: init_slurm_conf error %d", error_code);

	error_code = read_slurm_conf (SLURM_CONF);
	if (error_code)
		fatal ("slurmctld: error %d from read_slurm_conf reading %s",
			 error_code, SLURM_CONF);

	error_code = gethostname (node_name, MAX_NAME_LEN);
	if (error_code != 0) 
		fatal ("slurmctld: error %d from gethostname", error_code);
	
	if (strcmp (node_name, control_machine) != 0)
		fatal ("slurmctld: this machine (%s) is not the primary control machine (%s)",
			 node_name, control_machine);

	if ((sockfd = socket (AF_INET, SOCK_STREAM, 0)) < 0) 
		fatal ("slurmctld: error %d from socket", errno);

	memset (&serv_addr, 0, sizeof (serv_addr));
	serv_addr.sin_family = PF_INET;
	serv_addr.sin_addr.s_addr = htonl (INADDR_ANY);
	serv_addr.sin_port = htons (SLURMCTLD_PORT);
	error_code = bind (sockfd, (struct sockaddr *) &serv_addr, sizeof (serv_addr));
	if ((error_code < 0) && (errno == EADDRINUSE)) {
		printf("waiting to bind\n");
		sleep (10);
		error_code = bind (sockfd, (struct sockaddr *) &serv_addr, 
			sizeof (serv_addr));
	}
	if (error_code < 0)
		fatal ("slurmctld: error %d from bind\n", errno);
		
	listen (sockfd, 5);
	while (1) {
		cli_len = sizeof (cli_addr);
		if ((newsockfd =
		     accept (sockfd, (struct sockaddr *) &cli_addr,
			     &cli_len)) < 0)
			fatal ("slurmctld: error %d from accept", errno);

/* convert to pthread, tbd */
		slurmctld_req (newsockfd);	/* process the request */
		close (newsockfd);		/* close the new socket */

	}			
}


/* 
 * dump_build - dump all build parameters to a buffer
 * input: buffer_ptr - location into which a pointer to the data is to be stored.
 *                     the data buffer is actually allocated by dump_part and the 
 *                     calling function must xfree the storage.
 *         buffer_size - location into which the size of the created buffer is in bytes
 * output: buffer_ptr - the pointer is set to the allocated buffer.
 *         buffer_size - set to size of the buffer in bytes
 *         returns 0 if no error, errno otherwise
 * NOTE: the buffer at *buffer_ptr must be xfreed by the caller
 * NOTE: if you make any changes here be sure to increment the value of BUILD_STRUCT_VERSION
 *       and make the corresponding changes to load_build_name in api/build_info.c
 */
int
dump_build (char **buffer_ptr, int *buffer_size)
{
	char *buffer;
	int buffer_offset, buffer_allocated;
	char out_line[BUILD_SIZE * 2];

	buffer_ptr[0] = NULL;
	*buffer_size = 0;
	buffer = NULL;
	buffer_offset = 0;
	buffer_allocated = 0;

	/* write haeader, version and time */
	sprintf (out_line, HEAD_FORMAT, (unsigned long) time (NULL),
		 BUILD_STRUCT_VERSION);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	/* write paramter records */
	sprintf (out_line, BUILD_STRUCT2_FORMAT, "BACKUP_INTERVAL",
		 BACKUP_INTERVAL);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	sprintf (out_line, BUILD_STRUCT_FORMAT, "BACKUP_LOCATION",
		 BACKUP_LOCATION);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	sprintf (out_line, BUILD_STRUCT_FORMAT, "CONTROL_DAEMON",
		 CONTROL_DAEMON);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	sprintf (out_line, BUILD_STRUCT2_FORMAT, "CONTROLLER_TIMEOUT",
		 CONTROLLER_TIMEOUT);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	sprintf (out_line, BUILD_STRUCT_FORMAT, "EPILOG", EPILOG);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	sprintf (out_line, BUILD_STRUCT2_FORMAT, "FAST_SCHEDULE", FAST_SCHEDULE);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	sprintf (out_line, BUILD_STRUCT2_FORMAT, "HASH_BASE", HASH_BASE);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	sprintf (out_line, BUILD_STRUCT2_FORMAT, "HEARTBEAT_INTERVAL",
		 HEARTBEAT_INTERVAL);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	sprintf (out_line, BUILD_STRUCT_FORMAT, "INIT_PROGRAM", INIT_PROGRAM);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	sprintf (out_line, BUILD_STRUCT2_FORMAT, "KILL_WAIT", KILL_WAIT);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	sprintf (out_line, BUILD_STRUCT_FORMAT, "PRIORITIZE", PRIORITIZE);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	sprintf (out_line, BUILD_STRUCT_FORMAT, "PROLOG", PROLOG);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	sprintf (out_line, BUILD_STRUCT_FORMAT, "SERVER_DAEMON",
		 SERVER_DAEMON);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	sprintf (out_line, BUILD_STRUCT2_FORMAT, "SERVER_TIMEOUT",
		 SERVER_TIMEOUT);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	sprintf (out_line, BUILD_STRUCT_FORMAT, "SLURM_CONF", SLURM_CONF);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	sprintf (out_line, BUILD_STRUCT_FORMAT, "TMP_FS", TMP_FS);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	xrealloc (buffer, buffer_offset);

	buffer_ptr[0] = buffer;
	*buffer_size = buffer_offset;
	return 0;

      cleanup:
	if (buffer)
		xfree (buffer);
	return EINVAL;
}


/*
 * slurmctld_req - process a slurmctld request from the given socket
 * input: sockfd - the socket with a request to be processed
 */
void
slurmctld_req (int sockfd) {
	int error_code, detail, in_size, i;
	char in_line[BUF_SIZE], node_name[MAX_NAME_LEN];
	int cpus, real_memory, tmp_disk;
	char *job_id_ptr, *node_name_ptr, *part_name, *time_stamp;
	time_t last_update;
	clock_t start_time;
	char *dump;
	int dump_size, dump_loc;

	in_size = recv (sockfd, in_line, sizeof (in_line), 0);
	start_time = clock ();

	/* Allocate:  allocate resources for a job */
	if (strncmp ("Allocate", in_line, 8) == 0) {
		node_name_ptr = NULL;
		error_code = job_allocate(&in_line[8], 	/* skip "Allocate" */
			&job_id_ptr, &node_name_ptr);
		if (error_code)
			info ("slurmctld_req: error %d allocating resources for %s, time=%ld",
				 error_code, &in_line[8], (long) (clock () - start_time));
		else
			info ("slurmctld_req: allocated nodes %s to %s, JobId=%s, time=%ld",
				 node_name_ptr, &in_line[8], job_id_ptr, 
				(long) (clock () - start_time));

		if (error_code == 0) {
			i = strlen(node_name_ptr) + strlen (job_id_ptr) + 2;
			dump = xmalloc(i);
			sprintf(dump, "%s %s", node_name_ptr, job_id_ptr);
			send (sockfd, dump, i, 0);
			xfree(dump);
		}
		else if (error_code == EAGAIN)
			send (sockfd, "EAGAIN", 7, 0);
		else
			send (sockfd, "EINVAL", 7, 0);

		if (job_id_ptr)
			xfree (job_id_ptr);
		if (node_name_ptr)
			xfree (node_name_ptr);
	}

	/* DumpBuild - dump the SLURM build parameters */
	else if (strncmp ("DumpBuild", in_line, 9) == 0) {
		error_code = dump_build (&dump, &dump_size);
		if (error_code)
			info ("slurmctld_req: dump_build error %d, time=%ld",
				 error_code, (long) (clock () - start_time));
		else
			info ("slurmctld_req: dump_build returning %d bytes, time=%ld",
				 dump_size, (long) (clock () - start_time));
		if (error_code == 0) {
			dump_loc = 0;
			while (dump_size > 0) {
				i = send (sockfd, &dump[dump_loc], dump_size, 0);
				dump_loc += i;
				dump_size -= i;
			}	
		}
		else
			send (sockfd, "EINVAL", 7, 0);
		if (dump)
			xfree (dump);
	}

	/* DumpJob - dump the job state information */
	else if (strncmp ("DumpJob", in_line, 7) == 0) {
		time_stamp = NULL;
		error_code =
			load_string (&time_stamp, "LastUpdate=", in_line);
		if (time_stamp) {
			last_update = strtol (time_stamp, (char **) NULL, 10);
			xfree (time_stamp);
		}
		else
			last_update = (time_t) 0;
		if (in_line[7] == 'L')
			detail = 1;
		else
			detail = 0;
		error_code = dump_all_job (&dump, &dump_size, 
					   &last_update, detail);
		if (error_code)
			info ("slurmctld_req: dump_all_job error %d, time=%ld",
				 error_code, (long) (clock () - start_time));
		else
			info ("slurmctld_req: dump_all_job returning %d bytes, time=%ld",
				 dump_size, (long) (clock () - start_time));
		if (dump_size == 0)
			send (sockfd, "nochange", 9, 0);
		else if (error_code == 0) {
			dump_loc = 0;
			while (dump_size > 0) {
				i = send (sockfd, &dump[dump_loc], dump_size,
					  0);
				dump_loc += i;
				dump_size -= i;
			}	
		}
		else
			send (sockfd, "EINVAL", 7, 0);
		if (dump)
			xfree (dump);
	}

	/* DumpNode - dump the node configurations */
	else if (strncmp ("DumpNode", in_line, 8) == 0) {
		time_stamp = NULL;
		error_code =
			load_string (&time_stamp, "LastUpdate=", in_line);
		if (time_stamp) {
			last_update = strtol (time_stamp, (char **) NULL, 10);
			xfree (time_stamp);
		}
		else
			last_update = (time_t) 0;
		error_code = dump_all_node (&dump, &dump_size, &last_update);
		if (error_code)
			info ("slurmctld_req: dump_all_node error %d, time=%ld",
				 error_code, (long) (clock () - start_time));
		else
			info ("slurmctld_req: dump_all_node returning %d bytes, time=%ld",
				 dump_size, (long) (clock () - start_time));
		if (dump_size == 0)
			send (sockfd, "nochange", 9, 0);
		else if (error_code == 0) {
			dump_loc = 0;
			while (dump_size > 0) {
				i = send (sockfd, &dump[dump_loc], dump_size,
					  0);
				dump_loc += i;
				dump_size -= i;
			}	
		}
		else
			send (sockfd, "EINVAL", 7, 0);
		if (dump)
			xfree (dump);
	}

	/* DumpPart - dump the partition configurations */
	else if (strncmp ("DumpPart", in_line, 8) == 0) {
		time_stamp = NULL;
		error_code =
			load_string (&time_stamp, "LastUpdate=", in_line);
		if (time_stamp) {
			last_update = strtol (time_stamp, (char **) NULL, 10);
			xfree (time_stamp);
		}
		else
			last_update = (time_t) 0;
		error_code = dump_all_part (&dump, &dump_size, &last_update);
		if (error_code)
			info ("slurmctld_req: dump_part error %d, time=%ld",
				 error_code, (long) (clock () - start_time));
		else
			info ("slurmctld_req: dump_part returning %d bytes, time=%ld",
				 dump_size, (long) (clock () - start_time));
		if (dump_size == 0)
			send (sockfd, "nochange", 9, 0);
		else if (error_code == 0) {
			dump_loc = 0;
			while (dump_size > 0) {
				i = send (sockfd, &dump[dump_loc], dump_size,
					  0);
				dump_loc += i;
				dump_size -= i;
			}	
		}
		else
			send (sockfd, "EINVAL", 7, 0);
		if (dump)
			xfree (dump);
	}

	/* JobCancel - cancel a slurm job or reservation */
	else if (strncmp ("JobCancel", in_line, 9) == 0) {
		time_stamp = NULL;
		error_code = job_cancel(&in_line[10]);
		if (error_code)
			info ("slurmctld_req: job_cancel error %d, time=%ld",
				 error_code, (long) (clock () - start_time));
		else
			info ("slurmctld_req: job_cancel success for %s, time=%ld",
				 &in_line[10], (long) (clock () - start_time));
		if (error_code == 0)
			send (sockfd, "Job killed", 11, 0);
		else
			send (sockfd, "EINVAL", 7, 0);
	}

	/* JobSubmit - submit a job to the slurm queue */
	else if (strncmp ("JobSubmit", in_line, 9) == 0) {
		time_stamp = NULL;
		error_code = job_create(&in_line[9], &job_id_ptr);	/* skip "JobSubmit" */
		if (error_code)
			info ("slurmctld_req: job_submit error %d, time=%ld",
				 error_code, (long) (clock () - start_time));
		else
			info ("slurmctld_req: job_submit success for %s, id=%s, time=%ld",
				 &in_line[9], job_id_ptr, 
				(long) (clock () - start_time));
		if (error_code == 0)
			send (sockfd, job_id_ptr, strlen(job_id_ptr) + 1, 0);
		else
			send (sockfd, "EINVAL", 7, 0);
		if (job_id_ptr)
			xfree (job_id_ptr);
		schedule();
	}

	/* JobWillRun - determine if job with given configuration can be initiated now */
	else if (strncmp ("JobWillRun", in_line, 10) == 0) {
		time_stamp = NULL;
		error_code = EINVAL;
		if (error_code)
			info ("slurmctld_req: job_will_run error %d, time=%ld",
				 error_code, (long) (clock () - start_time));
		else
			info ("slurmctld_req: job_will_run success for %s, time=%ld",
				 &in_line[10], (long) (clock () - start_time));
		if (error_code == 0)
			send (sockfd, dump, dump_size, 0);
		else
			send (sockfd, "EINVAL", 7, 0);
	}

	/* NodeConfig - determine if a node's actual configuration satisfies the
	 * configured specification */
	else if (strncmp ("NodeConfig", in_line, 10) == 0) {
		time_stamp = NULL;
		node_name_ptr = NULL;
		cpus = real_memory = tmp_disk = NO_VAL;
		error_code = load_string (&node_name_ptr, "NodeName=", in_line);
		if (node_name == NULL)
			error_code = EINVAL;
		if (error_code == 0)
			error_code = load_integer (&cpus, "CPUs=", in_line);
		if (error_code == 0)
			error_code =
				load_integer (&real_memory, "RealMemory=",
					      in_line);
		if (error_code == 0)
			error_code =
				load_integer (&tmp_disk, "TmpDisk=",
					      in_line);
		if (error_code == 0)
			error_code =
				validate_node_specs (node_name_ptr, cpus,
						     real_memory, tmp_disk);
		if (error_code)
			error ("slurmctld_req: node_config error %d for %s, time=%ld",
				 error_code, node_name_ptr, (long) (clock () - start_time));
		else
			info ("slurmctld_req: node_config for %s, time=%ld",
				 node_name_ptr, (long) (clock () - start_time));
		if (error_code == 0)
			send (sockfd, dump, dump_size, 0);
		else
			send (sockfd, "EINVAL", 7, 0);
		if (node_name_ptr)
			xfree (node_name_ptr);
	}

	/* Reconfigure - re-initialized from configuration files */
	else if (strncmp ("Reconfigure", in_line, 11) == 0) {
		time_stamp = NULL;
		error_code = init_slurm_conf ();
		if (error_code == 0)
			error_code = read_slurm_conf (SLURM_CONF);

		if (error_code)
			error ("slurmctld_req: reconfigure error %d, time=%ld",
				 error_code, (long) (clock () - start_time));
		else
			info ("slurmctld_req: reconfigure completed successfully, time=%ld", 
				(long) (clock () - start_time));
		sprintf (in_line, "%d", error_code);
		send (sockfd, in_line, strlen (in_line) + 1, 0);
	}

	/* Update - modify node or partition configuration */
	else if (strncmp ("Update", in_line, 6) == 0) {
		node_name_ptr = part_name = NULL;
		error_code = load_string (&node_name_ptr, "NodeName=", in_line);
		if ((error_code == 0) && (node_name_ptr != NULL))
			error_code = update_node (node_name_ptr, &in_line[6]);	/* skip "Update" */
		else {
			error_code =
				load_string (&part_name, "PartitionName=", in_line);
			if ((error_code == 0) && (part_name != NULL))
				error_code = update_part (part_name, &in_line[6]); /* skip "Update" */
			else
				error_code = EINVAL;
		}		
		if (error_code) {
			if (node_name_ptr)
				error ("slurmctld_req: update error %d on node %s, time=%ld",
					 error_code, node_name_ptr, (long) (clock () - start_time));
			else if (part_name)
				error ("slurmctld_req: update error %d on partition %s, time=%ld",
					 error_code, part_name, (long) (clock () - start_time));
			else
				error ("slurmctld_req: update error %d on request %s, time=%ld",
					 error_code, in_line, (long) (clock () - start_time));

		}
		else {
			if (node_name_ptr)
				info ("slurmctld_req: updated node %s, time=%ld",
					 node_name_ptr, (long) (clock () - start_time));
			else
				info ("slurmctld_req: updated partition %s, time=%ld",
					 part_name, (long) (clock () - start_time));
		}
		sprintf (in_line, "%d", error_code);
		send (sockfd, in_line, strlen (in_line) + 1, 0);

		if (node_name_ptr)
			xfree (node_name_ptr);
		if (part_name)
			xfree (part_name);

	}
	else {
		error ("slurmctld_req: invalid request %s\n", in_line);
		send (sockfd, "EINVAL", 7, 0);
	}			
	return;
}
