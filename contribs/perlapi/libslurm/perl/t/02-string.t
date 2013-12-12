#!/usr/bin/perl -T
use Test::More tests => 16;
use Slurm qw(:constant);


# 1
my $slurm = Slurm::new();
ok(defined $slurm,  "create slurm object with default configuration");

my ($str, $num);

# 2
$str = $slurm->preempt_mode_string(PREEMPT_MODE_REQUEUE);
cmp_ok($str, "eq", "REQUEUE", "preempt mode string");


# 3
$num = $slurm->preempt_mode_num("REQUEUE");
cmp_ok($num, "==", PREEMPT_MODE_REQUEUE, "preempt mode num");


# 4
$str = $slurm->job_reason_string(WAIT_TIME);
cmp_ok($str, "eq", "BeginTime", "job reason string");


# 5
$str = $slurm->job_state_string(JOB_TIMEOUT);
cmp_ok($str, "eq", "TIMEOUT", "job state string");


# 6
$str = $slurm->job_state_string_compact(JOB_TIMEOUT);
cmp_ok($str, "eq", "TO", "job state string compact");


# 7
$num = $slurm->job_state_num("TIMEOUT");
cmp_ok($num, "==", JOB_TIMEOUT, "job state num");


# 8
$num = $slurm->job_state_num("TO");
cmp_ok($num, "==", JOB_TIMEOUT, "job state num compact");


# 9
$str = $slurm->reservation_flags_string(RESERVE_FLAG_DAILY);
cmp_ok($str, "eq", "DAILY", "reservation flags string");


# 10
$str = $slurm->node_state_string(NODE_STATE_UNKNOWN | NODE_STATE_DRAIN);
cmp_ok($str, "eq", "DRAINED", "node state string");


# 11
$str = $slurm->node_state_string_compact(NODE_STATE_UNKNOWN | NODE_STATE_DRAIN);
cmp_ok($str, "eq", "DRAIN", "node state string compact");


# 12
$str = $slurm->private_data_string(PRIVATE_DATA_USAGE);
cmp_ok($str, "eq", "usage", "private data string");


# 13
$str = $slurm->accounting_enforce_string(6);
cmp_ok($str, "eq", "limits,wckeys", "accounting enforce string");


# 14
$str = $slurm->conn_type_string(SELECT_MESH);
cmp_ok($str, "eq", "Mesh", "conn type string");


# 15
$str = $slurm->node_use_string(SELECT_VIRTUAL_NODE_MODE);
cmp_ok($str, "eq", "VIRTUAL", "node use type string");


# 16
$str = $slurm->bg_block_state_string(4);
cmp_ok($str, "eq", "Ready", "bg block state string");



