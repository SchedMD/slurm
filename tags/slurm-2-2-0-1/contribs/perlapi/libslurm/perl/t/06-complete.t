#!/usr/bin/perl -T
use Test::More tests => 5;
use Slurm qw(:constant);
use POSIX qw(:signal_h);

# 1
my $slurm = Slurm::new();
ok(defined $slurm,  "create slurm object with default configuration");

my ($job_desc, $rc, $jobid, $resp);

# 2
$job_desc = {
    min_nodes => 1,
    num_tasks => 1,
    user_id => $>,
    script => "#!/bin/sh\nsrun sleep 1000\nsleep 1000",
    name => "perlapi_test",
    stdout => "/dev/null",
    stderr => "/dev/null",
};
$resp = $slurm->submit_batch_job($job_desc);
ok($resp, "submit batch job") or diag ("submit_batch_job: " . $slurm->strerror());
$jobid = $resp->{job_id} if $resp;


# 3 - 5
SKIP: {
    skip "no job", 4 unless $jobid;

    $rc = $slurm->complete_job($jobid, 123);
    ok($rc == SLURM_SUCCESS, "complete job") or diag("complete_job: " . $slurm->strerror());

    $rc = $slurm->terminate_job_step($jobid, 0);
    ok($rc == SLURM_SUCCESS || $slurm->get_errno() == ESLURM_ALREADY_DONE, "termite job step")
	or diag("terminate_job_step: " . $slurm->strerror());

    $rc = $slurm->terminate_job($jobid);
    ok($rc == SLURM_SUCCESS || $slurm->get_errno() == ESLURM_ALREADY_DONE, "terminate job")
	or diag("terminate_job: " . $slurm->strerror());
}

$slurm->kill_job($jobid, SIGKILL) if $jobid;
