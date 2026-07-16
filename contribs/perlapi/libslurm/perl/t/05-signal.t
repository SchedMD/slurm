#!/usr/bin/perl -T
use Test::More tests => 6;
use Slurm qw(:constant);
use POSIX qw(:signal_h);

my ($job_desc, $rc, $jobid, $resp);


# 1
my $slurm = Slurm::new();
ok(defined $slurm,  "create slurm object with default configuration");


# 2
my %env = ('PATH' => $ENV{'PATH'});
$job_desc = {
    min_nodes => 1,
    num_tasks => 1,
    user_id => $>,
    group_id => $>,
    script => "#!/bin/sh\ntrap '/bin/true' SIGUSR1\nsrun sleep 1000\nsrun sleep 1000\nsrun sleep 1000\nsleep 1000",
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


# 3 - 6
SKIP: {
    skip "no job", 4 unless $jobid;

    $rc = $slurm->signal_job($jobid, SIGUSR1);
    ok($rc == SLURM_SUCCESS, "signal job") or diag("signal_job: " . $slurm->strerror());

    $rc = $slurm->signal_job_step($jobid, 0, SIGUSR1);
    ok($rc == SLURM_SUCCESS, "signal job step") or diag("signal_job_step: " . $slurm->strerror());

    $rc = $slurm->kill_job_step($jobid, 1, SIGUSR1);
    ok($rc == SLURM_SUCCESS || $slurm->get_errno() == ESLURM_INVALID_JOB_ID, "kill job step") or diag("kill_job_step: " . $slurm->strerror());

    $rc = $slurm->kill_job($jobid, SIGUSR1, 1);
    ok($rc == SLURM_SUCCESS, "kill job") or diag("kill_job: " . $slurm->strerror());
}

$slurm->kill_job($jobid, SIGKILL) if $jobid;
