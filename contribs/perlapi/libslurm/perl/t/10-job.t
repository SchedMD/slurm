#!/usr/bin/perl -T
use Test::More tests => 13;
use Slurm qw(:constant);
use POSIX qw(:signal_h);

# 1
my $slurm = Slurm::new();
ok(defined $slurm,  "create slurm object with default configuration");

my ($jobid, $time, $resp, $rc);


# 2
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


# 3, 4 ,5
SKIP: {
    skip "submit job failure", 3 unless $jobid;
    $time = $slurm->get_end_time($jobid);
    ok(defined $time, "get end time") or diag("get_end_time: " . $slurm->strerror());

    $time = $slurm->get_rem_time($jobid);
    ok($time != -1, "get rem time") or diag("get_rem_time: " . $slurm->strerror());

    $rc = $slurm->job_node_ready($jobid);
    ok(defined $rc, "job node ready") or diag("job_node_ready: " . $slurm->strerror());
}


# 6
$resp = $slurm->load_job($jobid);
ok(ref($resp) eq "HASH", "load job") or diag("load_job: " . $slurm->strerror());


# 7
$resp = $slurm->load_jobs(0, 1);
ok(ref($resp) eq "HASH", "load jobs") or diag("load_job: " . $slurm->strerror());


# 8
undef $rc;
$rc = $slurm->notify_job($jobid, "perl api test");
ok(defined $rc, "notify job") or diag("notify_job: " . $slurm->strerror());


# 9
SKIP: {
    local $TODO = "do not know how to test";
    skip($TODO,1);
    my $jid = $slurm->pid2jobid(1234);
    ok(defined $jid, "pid2jobid");
}


# 10
SKIP: {
    my ($fh, $print_ok);
    skip "failed to open temporary file", 1 unless open($fh, '+>', undef);
    $slurm->print_job_info($fh, $resp->{job_array}->[0]);
    seek($fh, 0, 0);
    while(<$fh>) {
	$print_ok = 1 if /^JobId=\d+/;
    }
    close($fh);
    ok($print_ok, "print job info");
}


# 11
SKIP: {
    my ($fh, $print_ok);
    skip "failed to open temporary file", 1 unless open($fh, '+>', undef);
    $slurm->print_job_info_msg($fh, $resp);
    seek($fh, 0, 0);
    while(<$fh>) {
	$print_ok = 1 if /^Job data as of/;
    }
    close($fh);
    ok($print_ok, "print job info msg");
}


# 12
{
    my ($fh, $print_ok);
    my $str = $slurm->sprint_job_info($resp->{job_array}->[0], 1);
    $print_ok = 1 if $str =~ /^JobId=\d+/;
    ok($print_ok, "print job step info");
}


# 13
undef $rc;
$rc = $slurm->update_job( { job_id => $jobid, timelimit => 100 } );
ok(defined $rc, "update job");

$slurm->kill_job($jobid, SIGKILL) if $jobid;
