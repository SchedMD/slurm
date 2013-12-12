#!/usr/bin/perl -T
use Test::More tests => 17;
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


$params = {
    job_id => $jobid,
    name => "perlapi_test",
    min_nodes => 1,
    task_count => 1,
};

# 4
my $ctx = $slurm->step_ctx_create($params);
ok(defined $ctx, "step ctx create") or diag("step_ctx_create: " . $slurm->strerror());


$params->{node_list} = $resp->{node_list};

# 5
my $ctx2 = $slurm->step_ctx_create_no_alloc($params, 3);
ok(defined $ctx2, "step ctx create no alloc") or diag("step_ctx_create_no_alloc: " . $slurm->strerror());


# 6 - 8
SKIP: {
    skip "no step ctx", 3 unless $ctx;
    foreach my $key (SLURM_STEP_CTX_JOBID, SLURM_STEP_CTX_STEPID, SLURM_STEP_CTX_NUM_HOSTS) {
	undef($data);
	$rc = $ctx->get($key, $data);
	ok($rc == SLURM_SUCCESS && defined $data, "step ctx get $key")
	    or diag("step_ctx_get: $key, $rc, $data, " . $slurm->strerror());
    }
}


# 9
SKIP: {
    skip "no step ctx", 1 unless $ctx;
    foreach my $key (SLURM_STEP_CTX_TASKS) {
	undef $data;
	$rc = $ctx->get($key, $data);
	ok($rc == SLURM_SUCCESS && ref($data) eq "ARRAY", "step ctx get TASKS")
	    or diag("step_ctx_get: TASKS, $rc, $data, " . $slurm->strerror());
    }
}


# 10
SKIP: {
    skip "no step ctx", 1 unless $ctx;
    foreach my $key (SLURM_STEP_CTX_TID) {
	undef $data;
	$rc = $ctx->get($key, 0, $data);
	ok($rc == SLURM_SUCCESS && ref($data) eq "ARRAY", "step ctx get TID")
	    or diag("step_ctx_get: TID, $rc, $data, " . $slurm->strerror());
    }
}

# 11
SKIP: {
    skip "no step ctx", 1 unless $ctx;
    foreach my $key (SLURM_STEP_CTX_CRED) {
	undef $data;
	$rc = $ctx->get($key, $data);
	ok($rc == SLURM_SUCCESS && ref($data) eq "Slurm::slurm_cred_t", "step ctx get CRED")
	    or diag("step_ctx_get: CRED, $rc, $data, " . $slurm->strerror());
    }
}


# 12
SKIP: {
    skip "no step ctx", 1 unless $ctx;
    foreach my $key (SLURM_STEP_CTX_SWITCH_JOB) {
	undef $data;
	$rc = $ctx->get($key, $data);
	ok($rc == SLURM_SUCCESS && (!defined($data) || ref($data) eq "Slurm::switch_jobinfo_t"), "step ctx get SWITCH_JOB")
	    or diag("step_ctx_get: SWITCH_JOB, $rc, $data, " . $slurm->strerror());
    }
}


# 13
SKIP: {
    skip "no step ctx", 1 unless $ctx;
    foreach my $key (SLURM_STEP_CTX_HOST) {
	undef $data;
	$rc = $ctx->get($key, 0, $data);
	ok($rc == SLURM_SUCCESS && defined $data, "step ctx get HOST")
	    or diag("step_ctx_get: HOST, $rc, $data, " . $slurm->strerror());
    }
}


# 14
SKIP: {
    skip "no step ctx", 1 unless $ctx;
    foreach my $key (SLURM_STEP_CTX_USER_MANAGED_SOCKETS) {
	my ($data1, $data2);
	$rc = $ctx->get($key, $data1, $data2);
	ok(($rc == SLURM_SUCCESS && defined $data1 && ref($data2) eq "ARRAY")
	   || ($rc == SLURM_ERROR && $data1 == 0 && !defined($data2)), "step ctx get UMS")
	    or diag("step_ctx_get: UMS, $rc, $data1, $data2" . $slurm->strerror());
    }
}


# 15
SKIP: {
    skip "no step ctx", 1 unless $ctx2;
    my ($data1);
    $data1 = 1;
    $rc = $ctx2->daemon_per_node_hack("test", 1, \$data1);
    ok($rc == SLURM_SUCCESS, "daemon per node hack")
	or diag("step ctx daemon per node hack" . $slurm->strerror());
}


# 16

$params = {
    argv => ["/bin/hostname"],
};

$callbacks = {
    task_start => sub { my $msg = shift; print STDERR "\ntask_start: $msg->{node_name}, $msg->{count_of_pids}\n";},
    task_finish => sub { my $msg = shift; print STDERR "\ntask_finish: " . join(", ", @{$msg->{task_id_list}}) . "\n";},
};

SKIP: {
    skip "no step ctx", 1 unless $ctx;
    $rc = $ctx->launch( $params, $callbacks);
    ok($rc == SLURM_SUCCESS, "step ctx launch")
	or diag("step_ctx_launch" . $slurm->strerror());
}


# 17
SKIP: {
    skip "no step ctx", 1 unless $ctx;
    $rc = $ctx->launch_wait_start();
    ok($rc == SLURM_SUCCESS, "step ctx wait start")
	or diag("step_ctx_wait_start: $rc, " . $slurm->strerror());
}

if ($ctx) {
    $ctx->launch_fwd_signal(SIGINT);
    $ctx->launch_wait_finish();
    $ctx->launch_abort();
}

$slurm->allocation_msg_thr_destroy($thr) if $thr;
$slurm->kill_job($jobid, SIGKILL) if $jobid;
