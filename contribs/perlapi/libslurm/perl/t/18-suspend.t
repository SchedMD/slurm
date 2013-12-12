#!/usr/bin/perl -T
use Test::More tests => 5;
use Slurm qw(:constant);
use POSIX qw(:signal_h);

my ($resp, $jobid, $rc, $susp, $job_desc);

# 1
my $slurm = Slurm::new();
ok(defined $slurm,  "create slurm object with default configuration");


$job_desc = {
    min_nodes => 1,
    num_tasks => 1,
    user_id => $>,
    script => "#!/bin/sh\nsleep 1000\n",
    name => "perlapi_test",
    stdout => "/dev/null",
    stderr => "/dev/null",
};


# 2
$resp = $slurm->submit_batch_job($job_desc);
ok($resp, "submit batch job") or diag ("submit_batch_job: " . $slurm->strerror());
$jobid = $resp->{job_id} if $resp;

# 3 
SKIP: {
    skip "not super user", 1 if $>;
    skip "no job", 1 unless $jobid;
    $rc = $slurm->suspend($jobid);
    ok($rc == SLURM_SUCCESS || $slurm->get_errno() == ESLURM_JOB_PENDING, "suspend")
	and $susp = 1 or diag("suspend: " . $slurm->strerror());
}


# 4
SKIP: {
    skip "not super user", 1 if $>;
    skip "not suspended", 1 unless $susp;
    $rc = $slurm->resume($jobid);
    ok($rc == SLURM_SUCCESS || $slurm->get_errno() == ESLURM_JOB_PENDING, "resume")
	or diag("resume: " . $slurm->strerror());
}


# 5
SKIP: {
    skip "not super user", 1 if $>;
    skip "no job", 1 unless $jobid;
    $rc = $slurm->requeue($jobid);
    ok($rc == SLURM_SUCCESS, "requeue") or diag("requeue: " . $slurm->strerror());
}

$slurm->kill_job($jobid, SIGKILL) if $jobid;
