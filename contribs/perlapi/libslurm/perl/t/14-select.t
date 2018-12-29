#!/usr/bin/perl -T
use Test::More tests => 26;
use Slurm qw(:constant);
use POSIX qw(:signal_h);
use Devel::Peek;

# 1
my $slurm = Slurm::new();
ok(defined $slurm,  "create slurm object with default configuration");

my ($resp, $rc, $type, $select_type, $jobid, $jobinfo, $nodeinfo, $data);

# 2
$resp = $slurm->load_ctl_conf();
ok(ref($resp) eq "HASH", "load ctl conf");
$select_type = substr($resp->{select_type}, 7);


# 3
my $job_desc = {
    min_nodes => 1,
    num_tasks => 1,
    user_id => $>,
    script => "#!/bin/sh\nsleep 1000\n",
    name => "perlapi_test",
    stdout => "/dev/null",
    stderr => "/dev/null",
};
$resp = $slurm->submit_batch_job($job_desc);
ok($resp, "submit batch job") or diag ("submit_batch_job: " . $slurm->strerror());
$jobid = $resp->{job_id} if $resp;


# 4
SKIP: {
    skip "job submit failure", 1 unless $jobid;
    $resp = $slurm->load_job($jobid);
    ok($resp, "load job info") or diag("load_job: " . $slurm->strerror());
    $jobinfo = $resp->{job_array}->[0]->{select_jobinfo};
}


my $jobinfo_data = {
    SELECT_JOBDATA_RESV_ID() => ["", [qw(cray)]],
    SELECT_JOBDATA_PTR() => ["Slurm::select_jobinfo_t", [qw(cray)]],
};


# 5 - 19
foreach $type (0 .. SELECT_JOBDATA_PTR) {
  SKIP: {
      skip "job submit failure", 1 unless $jobinfo;
      skip "plugin not support", 1 unless grep {$select_type eq $_} @{$jobinfo_data->{$type}->[1]};
      $rc = $slurm->get_select_jobinfo($jobinfo, $type, $data);
      ok($rc == SLURM_SUCCESS && ref($data) eq $jobinfo_data->{$type}->[0], "get select jobinfo $type")
	  or diag("get select jobinfo $type: $rc, " . ref($data));
    }
}
$slurm->kill_job($jobid, SIGKILL) if $jobid;



# 20
$resp = $slurm->load_node();
ok(ref($resp) eq "HASH", "load node");
$nodeinfo = $resp->{node_array}->[0]->{select_nodeinfo};


my $nodeinfo_data = {
    SELECT_NODEDATA_SUBCNT() => ["", [qw(linear cons_res)]],
    SELECT_NODEDATA_PTR() => ["Slurm::select_nodeinfo_t", [qw(linear cray cons_res)]],
};


# 21 - 26
foreach $type (0 .. SELECT_NODEDATA_PTR) {
  SKIP: {
      skip "pluing not support", 1 unless grep {$select_type eq $_} @{$nodeinfo_data->{$type}->[1]};
      $rc = $slurm->get_select_nodeinfo($nodeinfo, $type, NODE_STATE_ALLOCATED, $data);
      ok ($rc == SLURM_SUCCESS && ref($data) eq $nodeinfo_data->{$type}->[0], "get select nodeinfo $type")
	  or diag("get select nodeinfo $type: $rc, " . ref($data));
    }
}

