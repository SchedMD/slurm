#!/usr/bin/perl -T
use Test::More tests => 4;
use Slurm qw(:constant);
use POSIX qw(:signal_h);

# 1
my $slurm = Slurm::new();
ok(defined $slurm,  "create slurm object with default configuration");

my ($job_desc, $rc, $jobid, $resp);

# 2
my %env = ('PATH' => $ENV{'PATH'});
$job_desc = {
    min_nodes => 1,
    num_tasks => 1,
    user_id => $>,
    group_id => $>,
    script => "#!/bin/sh\nsrun sleep 1000\nsleep 1000",
    name => "perlapi_test",
    std_out => "/dev/null",
    std_err => "/dev/null",
    work_dir => "/tmp",
    environment => \%env,
};
$resp = $slurm->submit_batch_job($job_desc);
ok($resp, "submit batch job") or diag ("submit_batch_job: " . $slurm->strerror());
$jobid = $resp->{job_id} if $resp;
sleep 2;


# 3 - 4
SKIP: {
    skip "no job", 4 unless $jobid;

    $rc = $slurm->complete_job($jobid, 123);
    ok($rc == SLURM_SUCCESS, "complete job") or diag("complete_job: " . $slurm->strerror());

    $rc = $slurm->terminate_job_step($jobid, 0);
    ok($rc == SLURM_SUCCESS || $slurm->get_errno() == ESLURM_ALREADY_DONE, "terminate job step")
	or diag("terminate_job_step: " . $slurm->strerror());
}

$slurm->kill_job($jobid, SIGKILL) if $jobid;
