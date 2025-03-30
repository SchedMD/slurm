#!/usr/bin/perl -T
use Test::More tests => 26;
use Slurm qw(:constant);
use POSIX qw(:signal_h);
use Devel::Peek;

# 1
my $slurm = Slurm::new();
ok(defined $slurm,  "create slurm object with default configuration");

my ($resp, $rc, $type, $select_type, $jobid, $nodeinfo, $data);

# 2
$resp = $slurm->load_ctl_conf();
ok(ref($resp) eq "HASH", "load ctl conf");
$select_type = substr($resp->{select_type}, 7);


# 3
my %env = ('PATH' => $ENV{'PATH'});
my $job_desc = {
    min_nodes => 1,
    num_tasks => 1,
    user_id => $>,
    script => "#!/bin/sh\nsleep 1000\n",
    name => "perlapi_test",
    stdout => "/dev/null",
    stderr => "/dev/null",
    environment => \%env,
};
$resp = $slurm->submit_batch_job($job_desc);
ok($resp, "submit batch job") or diag ("submit_batch_job: " . $slurm->strerror());
$jobid = $resp->{job_id} if $resp;


# 4
SKIP: {
    skip "job submit failure", 1 unless $jobid;
    $resp = $slurm->load_job($jobid);
    ok($resp, "load job info") or diag("load_job: " . $slurm->strerror());
}

$slurm->kill_job($jobid, SIGKILL) if $jobid;



# 20
$resp = $slurm->load_node();
ok(ref($resp) eq "HASH", "load node");
