
/*#define _DEJAGNU_WAIT_
*/
#include <src/slurmctld/slurmctld.h>
#include <testsuite/dejagnu.h>

slurm_ctl_conf_t slurmctld_conf;
int
main (int argc, char *argv[]) 
{
	int dump_size, error_code, error_count = 0, i;
	time_t update_time = (time_t) NULL;
	struct job_record * job_rec;
	log_options_t opts = LOG_OPTS_STDERR_ONLY;
	char *dump;
	uint16_t tmp_id;
	//char update_spec[] = "TimeLimit=1234 Priority=123";

	log_init(argv[0], opts, SYSLOG_FACILITY_DAEMON, NULL);
	error_code = init_job_conf ();
	if (error_code) 
		fail("init_job_conf error %d", error_code);
	else pass("init_job_conf" );

	job_rec = create_job_record(&error_code);
	if ((job_rec == NULL) || error_code) {
		fail ("create_job_record failure");
		exit(error_count);
	}
	else pass( "create_job_record" );

	strcpy (job_rec->name, "Name1");
	strcpy (job_rec->partition, "batch");
	job_rec->details->num_nodes = 1;
	job_rec->details->num_procs = 1;
	tmp_id = job_rec->job_id;

	for (i=1; i<=4; i++) {
		job_rec = create_job_record (&error_code);
		if ((job_rec == NULL) || error_code) {
			fail ("create_job_record failure %d",error_code);
			exit (error_count);
		}
		else pass( "create_job_record" );

		strcpy (job_rec->name, "Name2");
		strcpy (job_rec->partition, "debug");
		job_rec->details->num_nodes = i;
		job_rec->details->num_procs = i;
	}

	error_code = 0;
       	pack_all_jobs (&dump, &dump_size, &update_time);
	if (error_code)
		fail ("dump_all_job error %d", error_code);
	else pass( "dump_all_job");

	if (dump)
		xfree(dump);

	job_rec = find_job_record (tmp_id);
	if (job_rec == NULL)
		fail("find_job_record error 1");
	else
		pass ("found job %u, ", 
			job_rec->job_id );

	job_rec = find_job_record (tmp_id);
	if (job_rec != NULL) 
		fail ("find_job_record error 2");
	else pass( "find_job_record");

	totals();
	exit (error_count);
}

