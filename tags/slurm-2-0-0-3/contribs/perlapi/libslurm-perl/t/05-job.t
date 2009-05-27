#!/usr/bin/perl -T
use Test::More tests => 7;
use Slurm ':all';

my $resp;

$resp = Slurm->load_jobs(1);
ok(ref($resp) eq "HASH", "loading jobs");

SKIP: {
	skip "no jobs in system", 2 unless @{$resp->{job_array}};
	my $job = $resp->{job_array}->[0];
	my $end_time = Slurm->get_end_time($job->{job_id});
	ok(defined $end_time, "get job end time");
	my $rem_time = Slurm->get_rem_time($job->{job_id});
	ok($rem_time != -1, "get job remainning time");
}
SKIP: {
	skip "you have to be superuser to update job", 1 if $>;
	my $rc = Slurm->update_job({job_id => $job->{job_id}, time_limit => $job->{time_limit} + 1});
	ok($rc == SLURM_SUCCESS, "update job");
}

TODO: {
	local $TODO = "don't know how to test it";
	my $jobid = Slurm->pid2jobid(1234);
	ok(defined $jobid, "pid2jobid");
}

$resp = Slurm->get_job_steps(1);
ok(ref($resp) eq "HASH", "getting steps");

SKIP: {
	skip "no job steps in system", 1 unless @{$resp->{job_steps}};
	my $step = $resp->{job_steps}->[0];
	$resp = Slurm->job_step_layout_get($step->{job_id}, $step->{step_id});
	ok(ref($resp) eq "HASH", "job step layout get");
}
