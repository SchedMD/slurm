#!/usr/bin/perl -T
use Test::More tests => 10;
use Slurm qw(:constant);
use POSIX qw(:signal_h);

my ($resp, $job_desc, $jobid, $hostlist, $callbacks, $thr, $port, $file);

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
    skip "resource allocation fail", 1 unless $jobid;
    $resp = $slurm->allocation_lookup_lite($jobid);
    ok(defined $resp, "allocation lookup lite") or diag("allocation_lookup_lite: " . $slurm->strerror());
}


# 6
$callbacks = {
    ping => sub { print STDERR "ping from slurmctld, $_->{job_id}.$_->{step_id}\n"; },
    job_complete => sub { print STDERR "job complete, $_->{job_id}.$_->{step_id}\n"; },
    timeout => sub { print STDERR "srun timeout, $_->{job_id}.$_->{step_id}, $_->{timeout}\n"; },
    user_msg => sub { print STDERR "user msg, $_->{job_id}, $_->{msg}\n";},
    node_fail => sub { print STDERR "node fail, $_->{job_id}.$_->{step_id}, $_->{nodelist}\n";},
};
$thr = $slurm->allocation_msg_thr_create($port, $callbacks);
ok(ref($thr) eq "Slurm::allocation_msg_thread_t" && defined $port, "allocation msg thr create")
    or diag("allocation_msg_thr_create: " . $slurm->strerror());
$slurm->allocation_msg_thr_destroy($thr) if $thr;


# 7
SKIP: {
    skip "resource allocation fail", 1 unless $jobid;
    $resp = $slurm->sbcast_lookup($jobid);
    ok(defined $resp, "sbcast lookup") or diag("sbcast_lookup: " . $slurm->strerror());
}
$slurm->kill_job($jobid, SIGKILL) if $jobid;


# 8
$job_desc->{script} = "#!/bin/sh\nsleep 1000\n";
$resp = $slurm->submit_batch_job($job_desc);
ok($resp, "submit batch job") or diag("submit_batch_job: " . $slurm->strerror());
$slurm->kill_job($resp->{job_id}, SIGKILL) if $resp;


# 9 
$rc = $slurm->job_will_run($job_desc);
ok(defined $rc, "job will run") or diag("job_will_run: " . $slurm->strerror());


# 10
SKIP: {
    skip "do not know how to test", 1 if 1;
    $hl = $slurm->read_hostfile($file, 8);
    ok($hl eq "node0,node1,node2,node3,node4,node5,node6,node7", "read hostfile");
}
