
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <src/api/slurm.h>
#include <testsuite/dejagnu.h>

/* main is used here for testing purposes only */
int 
allocate_c (int argc, char *argv[])
{
	int error_code;
	char *node_list;
	uint32_t job_id;

	error_code = slurm_allocate_resources
		("User=1500 JobName=job01 TotalNodes=400 TotalProcs=1000 ReqNodes=lx[3000-3003] Partition=batch MinRealMemory=1024 MinTmpDisk=2034 Groups=students,employee MinProcs=4 Contiguous=YES Key=1234 Immediate",
		 &node_list, &job_id);
	if (error_code)
		printf ("allocate error %d\n", error_code);
	else {
		printf ("allocate nodes %s to job %u\n", node_list, job_id);
		free (node_list);
	}

	while (1) {
		error_code = slurm_allocate_resources
			("User=1500 JobName=more TotalProcs=4000 Partition=batch Key=1234 Immediate",
			 &node_list, &job_id);
		if (error_code) {
			printf ("allocate error %d\n", error_code);
			break;
		}
		else {
			printf ("allocate nodes %s to job %u\n", node_list, job_id);
			free (node_list);
		}
	}

	while (1) {
		error_code = slurm_allocate_resources
			("User=1500 JobName=more TotalProcs=40 Partition=batch Key=1234 Immediate",
			 &node_list, &job_id);
		if (error_code) {
			printf ("allocate error %d\n", error_code);
			break;
		}
		else {
			printf ("allocate nodes %s to job %u\n", node_list, job_id);
			free (node_list);
		}
	}

	return (0);
}

/* main is used here for testing purposes only */
int 
cancel_c (int argc, char *argv[]) 
{
	int error_code = 0, i;

	if (argc < 2) {
		printf ("Usage: %s job_id\n", argv[0]);
		return (1);
	}

	for (i=1; i<argc; i++) {
		error_code = slurm_cancel_job ((uint32_t) atoi(argv[i]));
		if (error_code != 0)
			printf ("slurm_cancel error %d for job %s\n", 
				error_code, argv[i]);
	}

	return (error_code);
}

/* main is used here for testing purposes only */
int 
job_info_c (int argc, char *argv[]) 
{
	static time_t last_update_time = (time_t) NULL;
	int error_code;
	job_info_msg_t * job_info_msg_ptr = NULL;

	error_code = slurm_load_jobs (last_update_time, &job_info_msg_ptr);
	if (error_code) {
		printf ("slurm_load_jobs error %d\n", error_code);
		return (error_code);
	}

	slurm_print_job_info_msg ( job_info_msg_ptr ) ;

	slurm_free_job_info ( job_info_msg_ptr ) ;
	return (0);
}

/* main is used here for testing purposes only */
int
node_info_c (int argc, char *argv[]) 
{
	static time_t last_update_time = (time_t) NULL;
	int error_code, i;
	node_info_msg_t * node_info_msg_ptr = NULL;
	node_table_t * node_ptr = node_info_msg_ptr -> node_array ;

	error_code = slurm_load_node (last_update_time, &node_info_msg_ptr);
	if (error_code) {
		printf ("slurm_load_node error %d\n", error_code);
		return (error_code);
	}

	printf("Nodes updated at %d, record count %d\n",
		node_info_msg_ptr ->last_update, node_info_msg_ptr->record_count);

	for (i = 0; i < node_info_msg_ptr-> record_count; i++) 
	{
		/* to limit output we print only the first 10 entries, 
		 * last 1 entry, and every 200th entry */
		if ((i < 10) || (i % 200 == 0) || 
		    ((i + 1)  == node_info_msg_ptr-> record_count)) {
			slurm_print_node_table ( & node_ptr[i] ) ;
		}
		else if ((i==10) || (i % 200 == 1))
			printf ("skipping...\n");
	}

	slurm_free_node_info ( node_info_msg_ptr ) ;
	return (0);
}

/* main is used here for module testing purposes only */
int
partition_info_c (int argc, char *argv[]) 
{
	static time_t last_update_time = (time_t) NULL;
	int error_code ;
	partition_info_msg_t * part_info_ptr = NULL;

	error_code = slurm_load_partitions (last_update_time, &part_info_ptr);
	if (error_code) {
		printf ("slurm_load_part error %d\n", error_code);
		return (error_code);
	}

	note("Updated at %lx, record count %d\n",
		(time_t) part_info_ptr->last_update, part_info_ptr->record_count);

	slurm_print_partition_info ( part_info_ptr );
	slurm_free_partition_info (part_info_ptr);
	return (0);
}

/* main is used here for module testing purposes only */
int 
reconfigure_c (int argc, char *argv[]) {
	int i, count, error_code;

	if (argc < 2)
		count = 1;
	else
		count = atoi (argv[1]);

	for (i = 0; i < count; i++) {
		error_code = slurm_reconfigure ();
		if (error_code != 0) {
			printf ("reconfigure error %d\n", error_code);
			return (1);
		}
	}
	return (0);
}

/* main is used here for testing purposes only */
int 
submit_c (int argc, char *argv[]) 
{
	int error_code, i, count;
	uint32_t job_id;

	error_code = slurm_submit_batch_job
		("User=1500 Script=/bin/hostname JobName=job01 TotalNodes=400 TotalProcs=1000 ReqNodes=lx[3000-3003] Partition=batch MinRealMemory=1024 MinTmpDisk=2034 Groups=students,employee MinProcs=4 Contiguous=YES Key=1234",
		 &job_id);
	if (error_code) {
		printf ("submit error %d\n", error_code);
		return (error_code);
	}
	else
		printf ("job %u submitted\n", job_id);

	if (argc > 1) 
		count = atoi (argv[1]);
	else
		count = 5;

	for (i=0; i<count; i++) {
		error_code = slurm_submit_batch_job
			("User=1500 Script=/bin/hostname JobName=more TotalProcs=4000 Partition=batch Key=1234 ",
			 &job_id);
		if (error_code) {
			printf ("submit error %d\n", error_code);
			break;
		}
		else {
			printf ("job %u submitted\n", job_id);
		}
	}

	return (error_code);
}

/* main is used here for module testing purposes only */
int
update_config_c (int argc, char *argv[]) {
	int error_code;
	char part_update1[] = "PartitionName=batch State=DOWN";
	char part_update2[] = "PartitionName=batch State=UP";
	char node_update1[] = "NodeName=lx1234 State=DOWN";
	char node_update2[] = "NodeName=lx1234 State=IDLE";

	error_code = slurm_update_config (part_update1);
	if (error_code)
		printf ("error %d for part_update1\n", error_code);
	error_code = slurm_update_config (part_update2);
	if (error_code)
		printf ("error %d for part_update2\n", error_code);
	error_code = slurm_update_config (node_update1);
	if (error_code)
		printf ("error %d for node_update1\n", error_code);
	error_code = slurm_update_config (node_update2);
	if (error_code)
		printf ("error %d for node_update2\n", error_code);

	return (error_code);
}


int
main ( int argc, char* argv[] )
{
	exit (0);
}
