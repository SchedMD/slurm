#!/usr/bin/perl -T
use Test::More tests => 3;
use Slurm qw(:constant);
use POSIX qw(:signal_h);

use Devel::Peek;

my ($resp, $job_desc, $jobid, $hostlist, $callbacks, $thr, $port, $file, $params, $rc, $data);


# 1
my $slurm = Slurm::new();
ok(defined $slurm,  "create slurm object with default configuration");

$job_desc = {
    min_nodes => 1,
    user_id => $>,
    num_tasks => 1,
    name => "perlapi_test"
};

$callbacks = {
    ping => sub { print STDERR "ping from slurmctld, $_->{job_id}.$_->{step_id}\n"; },
    job_complete => sub { print STDERR "job complete, $_->{job_id}.$_->{step_id}\n"; },
    timeout => sub { print STDERR "srun timeout, $_->{job_id}.$_->{step_id}, $_->{timeout}\n"; },
    user_msg => sub { print STDERR "user msg, $_->{job_id}, $_->{msg}\n";},
    node_fail => sub { print STDERR "node fail, $_->{job_id}.$_->{step_id}, $_->{nodelist}\n";},
};


# 2
$thr = $slurm->allocation_msg_thr_create($port, $callbacks);
ok(ref($thr) eq "Slurm::allocation_msg_thread_t" && defined $port, "allocation msg thr create")
    or diag("allocation_msg_thr_create: " . $slurm->strerror());


# 3
$resp = $slurm->allocate_resources_blocking($job_desc, 10, sub {$jobid = shift;});
$jobid = $resp->{job_id} if $resp;
ok($jobid, "allocate resources blocking") or diag("allocate_resources_blocking: " . $slurm->strerror());


$slurm->allocation_msg_thr_destroy($thr) if $thr;
$slurm->kill_job($jobid, SIGKILL) if $jobid;
