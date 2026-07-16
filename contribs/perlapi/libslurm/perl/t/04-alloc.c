#!/usr/bin/perl -T
use Test::More tests = > 8;
use Slurm qw(:constant);
use POSIX qw(:signal_h);

my($resp, $job_desc, $jobid, $hostlist, $file);

# 1
my $slurm = Slurm::new();
ok(defined $slurm,  "create slurm object with default configuration");

$job_desc = {
    min_nodes => 1,
    user_id => $>,
    num_tasks => 1,
    name => "perlapi_test"
};


# 2
$resp = $slurm->allocate_resources($job_desc);
ok(defined $resp, "allocate resources") or diag("allocate_resources: " . $slurm->strerror());
$slurm->kill_job($resp->{job_id}, SIGKILL) if $resp;


# 3
$resp = $slurm->allocate_resources_blocking($job_desc, 10, sub {$jobid = shift;});
$jobid = $resp->{job_id} if $resp;
ok($jobid, "allocate resources blocking") or diag("allocate_resources_blocking: " . $slurm->strerror());


# 4
SKIP: {
    skip "resource allocation fail", 1 unless $jobid;
    $resp = $slurm->allocation_lookup($jobid);
    ok(defined $resp, "allocation lookup") or diag("allocation_lookup: " . $slurm->strerror());
}

# 5
SKIP: {
    $stepid = NO_VAL;
    skip "resource allocation fail", 1 unless $jobid;
    $resp = $slurm->sbcast_lookup($jobid, $stepid);
    ok(defined $resp, "sbcast lookup") or diag("sbcast_lookup: " . $slurm->strerror());
}
$slurm->kill_job($jobid, SIGKILL) if $jobid;

# 6
my %env = ('PATH' => $ENV{'PATH'});
$job_desc->{script} = "#!/bin/sh\nsleep 1000\n";
$job_desc->{environment} = \%env;
$resp = $slurm->submit_batch_job($job_desc);
ok($resp, "submit batch job") or diag("submit_batch_job: " . $slurm->strerror());
$slurm->kill_job($resp->{job_id}, SIGKILL) if $resp;

# 7
$rc = $slurm->job_will_run($job_desc);
ok(defined $rc, "job will run") or diag("job_will_run: " . $slurm->strerror());

# 8
SKIP: {
    skip "do not know how to test", 1 if 1;
    $hl = $slurm->read_hostfile($file, 8);
    ok($hl eq "node0,node1,node2,node3,node4,node5,node6,node7", "read hostfile");
}
