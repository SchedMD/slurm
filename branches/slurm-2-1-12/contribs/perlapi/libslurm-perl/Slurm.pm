package Slurm;

use 5.008;
use strict qw(refs vars);
use warnings;
use Carp;

require Exporter;
use AutoLoader;

our @ISA = qw(Exporter);

our %constants;
# make this in BEGIN block to use subs
BEGIN {

$constants{macros} = [qw /
		INFINITE
		NO_VAL
		MAX_TASKS_PER_NODE
		SLURM_BATCH_SCRIPT
		SHOW_ALL
                SHOW_DETAIL
		JOB_COMPLETING
                JOB_CONFIGURING
		READY_JOB_FATAL
		READY_JOB_ERROR
		READY_NODE_STATE
		READY_JOB_STATE
		NICE_OFFSET

		MAIL_JOB_BEGIN
		MAIL_JOB_END
		MAIL_JOB_FAIL

		NODE_STATE_BASE
		NODE_STATE_FLAGS
		NODE_RESUME
		NODE_STATE_DRAIN
		NODE_STATE_COMPLETING
		NODE_STATE_NO_RESPOND
		NODE_STATE_POWER_SAVE
		NODE_STATE_FAIL
		NODE_STATE_POWER_UP
		NODE_STATE_MAINT

		SLURM_SSL_SIGNATURE_LENGTH

		OPEN_MODE_APPEND
		OPEN_MODE_TRUNCATE

		MEM_PER_CPU
		SHARED_FORCE

		PRIVATE_DATA_JOBS
		PRIVATE_DATA_NODES
		PRIVATE_DATA_PARTITIONS
		PRIVATE_DATA_USAGE
		PRIVATE_DATA_USERS
		PRIVATE_DATA_ACCOUNTS
		PRIVATE_DATA_RESERVATIONS

		PRIORITY_RESET_NONE
		PRIORITY_RESET_NOW
		PRIORITY_RESET_DAILY
		PRIORITY_RESET_WEEKLY
		PRIORITY_RESET_MONTHLY
		PRIORITY_RESET_QUARTERLY
		PRIORITY_RESET_YEARLY

		RESERVE_FLAG_MAINT
		RESERVE_FLAG_NO_MAINT
		RESERVE_FLAG_DAILY
		RESERVE_FLAG_NO_DAILY
		RESERVE_FLAG_WEEKLY
		RESERVE_FLAG_NO_WEEKLY
		RESERVE_FLAG_IGN_JOBS
		RESERVE_FLAG_NO_IGN_JOB
		RESERVE_FLAG_OVERLAP
		RESERVE_FLAG_SPEC_NODES

		DEBUG_FLAG_SELECT_TYPE
		DEBUG_FLAG_STEPS
		DEBUG_FLAG_TRIGGERS
		DEBUG_FLAG_CPU_BIND
		DEBUG_FLAG_WIKI

		PREEMPT_MODE_OFF
		PREEMPT_MODE_SUSPEND
		PREEMPT_MODE_REQUEUE
		PREEMPT_MODE_CHECKPOINT
		PREEMPT_MODE_CANCEL
		PREEMPT_MODE_GANG

		TRIGGER_RES_TYPE_JOB
		TRIGGER_RES_TYPE_NODE
		TRIGGER_TYPE_UP
		TRIGGER_TYPE_DOWN
		TRIGGER_TYPE_FAIL
		TRIGGER_TYPE_TIME
		TRIGGER_TYPE_FINI
		TRIGGER_TYPE_RECONFIG
		TRIGGER_TYPE_BLOCK_ERR
		TRIGGER_TYPE_IDLE
		TRIGGER_TYPE_DRAINED

		SLURM_SUCCESS
		SLURM_ERROR
		/];

$constants{job_states} = [qw /
		JOB_PENDING
		JOB_RUNNING
		JOB_SUSPENDED
		JOB_COMPLETE
		JOB_CANCELLED
		JOB_TIMEOUT
		JOB_FAILED
		JOB_NODE_FAIL
                JOB_END
		/];


$constants{job_state_reason} = [qw /
		WAIT_NO_REASON
		WAIT_PRIORITY
		WAIT_DEPENDENCY
		WAIT_RESOURCES
		WAIT_PART_NODE_LIMIT
		WAIT_PART_TIME_LIMIT
		WAIT_PART_STATE
		WAIT_HELD
		WAIT_TIME
		WAIT_TBD1
		WAIT_TBD2
		FAIL_DOWN_PARTITION
		FAIL_DOWN_NODE
		FAIL_BAD_CONSTRAINTS
		FAIL_SYSTEM
		FAIL_LAUNCH
		FAIL_EXIT_CODE
		FAIL_TIMEOUT
		FAIL_INACTIVE_LIMIT
		/];

$constants{job_acct_type} = [qw /
		JOB_START
		JOB_STEP
		JOB_SUSPEND
		JOB_TERMINATED
		/];

$constants{connection_type} = [qw /
		SELECT_MESH
		SELECT_TORUS
		SELECT_NAV
		SELECT_SMALL
		/];

# some bluegene specific codes needed here

$constants{jobacct_data_type} = [qw /
		JOBACCT_DATA_TOTAL
		JOBACCT_DATA_PIPE
		JOBACCT_DATA_RUSAGE
		JOBACCT_DATA_MAX_VSIZE
		JOBACCT_DATA_MAX_VSIZE_ID
		JOBACCT_DATA_TOT_VSIZE
		JOBACCT_DATA_MAX_RSS
		JOBACCT_DATA_MAX_RSS_ID
		JOBACCT_DATA_TOT_RSS
		JOBACCT_DATA_MAX_PAGES
		JOBACCT_DATA_MAX_PAGES_ID
		JOBACCT_DATA_TOT_PAGES
		JOBACCT_DATA_MIN_CPU
		JOBACCT_DATA_MIN_CPU_ID
		JOBACCT_DATA_TOT_CPU
		/];

$constants{task_dist_state} = [qw /
		SLURM_DIST_CYCLIC
		SLURM_DIST_BLOCK
		SLURM_DIST_ARBITRARY
		SLURM_DIST_PLANE
		SLURM_DIST_CYCLIC_CYCLIC
		SLURM_DIST_CYCLIC_BLOCK
		SLURM_DIST_BLOCK_CYCLIC
		SLURM_DIST_BLOCK_BLOCK
		SLURM_NO_LLLP_DIST
		SLURM_DIST_UNKNOWN
		/];

$constants{cpu_bind_type} = [qw /
		CPU_BIND_TO_THREADS
		CPU_BIND_TO_CORES
		CPU_BIND_TO_SOCKETS
		CPU_BIND_VERBOSE
		CPU_BIND_NONE
		CPU_BIND_RANK
		CPU_BIND_MAP
		CPU_BIND_MASK
		/];

$constants{mem_bind_type} = [qw /
		MEM_BIND_VERBOSE
		MEM_BIND_NONE
		MEM_BIND_RANK
		MEM_BIND_MAP
		MEM_BIND_MASK
		MEM_BIND_LOCAL
		/];

$constants{node_states} = [qw /
		NODE_STATE_UNKNOWN
		NODE_STATE_DOWN
		NODE_STATE_IDLE
		NODE_STATE_ALLOCATED
                NODE_STATE_ERROR
	        NODE_STATE_MIXED
	        NODE_STATE_FUTURE
	        NODE_STATE_END
		/];

$constants{ctx_keys} = [qw /
		SLURM_STEP_CTX_STEPID
		SLURM_STEP_CTX_TASKS
		SLURM_STEP_CTX_TID
		SLURM_STEP_CTX_RESP
		SLURM_STEP_CTX_CRED
		SLURM_STEP_CTX_SWITCH_JOB
		SLURM_STEP_CTX_NUM_HOSTS
		SLURM_STEP_CTX_HOST
		SLURM_STEP_CTX_JOBID
		SLURM_STEP_CTX_USER_MANAGED_SOCKETS
		/];

$constants{select_type_plugin_info} = [qw /
		SELECT_TYPE_INFO_NONE
		CR_CPU
		CR_SOCKET
		CR_CORE
		CR_MEMORY
		CR_SOCKET_MEMORY
		CR_CORE_MEMORY
		CR_CPU_MEMORY
		/];

$constants{select_jobdata_type} = [qw /
                SELECT_JOBDATA_GEOMETRY
	        SELECT_JOBDATA_ROTATE
	        SELECT_JOBDATA_CONN_TYPE
	        SELECT_JOBDATA_BLOCK_ID
	        SELECT_JOBDATA_NODES
	        SELECT_JOBDATA_IONODES
	        SELECT_JOBDATA_NODE_CNT
	        SELECT_JOBDATA_ALTERED
	        SELECT_JOBDATA_MAX_CPUS
	        SELECT_JOBDATA_BLRTS_IMAGE
	        SELECT_JOBDATA_LINUX_IMAGE
	        SELECT_JOBDATA_MLOADER_IMAGE
	        SELECT_JOBDATA_RAMDISK_IMAGE
	        SELECT_JOBDATA_REBOOT
	        SELECT_JOBDATA_RESV_ID
                /];

$constants{select_nodedata_type} = [qw /
                SELECT_NODEDATA_BITMAP_SIZE
	        SELECT_NODEDATA_SUBGRP_SIZE
	        SELECT_NODEDATA_SUBCNT
	        SELECT_NODEDATA_BITMAP
	        SELECT_NODEDATA_STR
                /];

foreach my $const(values(%constants)) {
    push @{$constants{all}}, @$const;
}

} # BEGIN

use subs @{$constants{all}};

# Items to export into callers namespace by default. Note: do not export
# names by default without a very good reason. Use EXPORT_OK instead.
# Do not simply export all your public functions/methods/constants.

# This allows declaration	use Slurm ':all';
# If you do not need this, moving things directly into @EXPORT or @EXPORT_OK
# will save memory.
our %EXPORT_TAGS = %constants;

our @EXPORT_OK = ( @{ $EXPORT_TAGS{'all'} } );

our @EXPORT = qw();

our $VERSION = '0.01';

sub AUTOLOAD {
    # This AUTOLOAD is used to 'autoload' constants from the constant()
    # XS function.

    my $constname;
    our $AUTOLOAD;
    ($constname = $AUTOLOAD) =~ s/.*:://;
    croak "&Slurm::constant not defined" if $constname eq 'constant';
    my ($error, $val) = constant($constname);
    if ($error) { croak $error; }
    {
	no strict 'refs';
	# Fixed between 5.005_53 and 5.005_61
#XXX	if ($] >= 5.00561) {
#XXX	    *$AUTOLOAD = sub () { $val };
#XXX	}
#XXX	else {
	    *$AUTOLOAD = sub { $val };
#XXX	}
    }
    goto &$AUTOLOAD;
}

#require XSLoader;
#XSLoader::load('Slurm', $VERSION);

# XSLoader will not work for SLURM because it does not honour dl_load_flags.
require DynaLoader;
push @ISA, 'DynaLoader';
bootstrap Slurm $VERSION;

sub dl_load_flags { if($^O eq 'aix') { 0x00 } else { 0x01 }}

############################################################
# Preloaded methods go here.

# Autoload methods go after =cut, and are processed by the autosplit program.

sub break_node_state {
	my $st = shift;
	return ($st & NODE_STATE_BASE, $st & NODE_STATE_DRAIN, $st & NODE_STATE_COMPLETING, $st & NODE_STATE_NO_RESPOND);
}

sub node_state_string {
	my $st = shift;
	my ($base, $drain, $comp, $noresp) = break_node_state($st);

	if($drain) {
		my $str = ($comp || $base == NODE_STATE_ALLOCATED) ? "DRAINING" : "DRAINED";
		return $noresp ? $str . "*" : $str;
	}
	return $noresp ? "DOWN*" : "DOWN" if $base == NODE_STATE_DOWN;
	return $noresp ? "ALLOCATED*" : ($comp ? "ALLOCATED+" : "ALLOCATED") if $base == NODE_STATE_ALLOCATED;
	return $noresp ? "COMPLETING*" : "COMPLETING" if $comp;
	return $noresp ? "IDLE*" : "IDLE" if $base == NODE_STATE_IDLE;
	return $noresp ? "UNKNOWN*" : "UNKNOWN" if $base == NODE_STATE_UNKNOWN;
}

sub node_state_string_compact {
	my $st = shift;
	my ($base, $drain, $comp, $noresp) = break_node_state($st);

	if($drain) {
		my $str = ($comp || $base == NODE_STATE_ALLOCATED) ? "DRNG" : "DRAIN";
		return $noresp ? $str . "*" : $str;
	}
	return $noresp ? "DOWN*" : "DOWN" if $base == NODE_STATE_DOWN;
	return $noresp ? "ALLOC*" : ($comp ? "ALLOC+" : "ALLOC") if $base == NODE_STATE_ALLOCATED;
	return $noresp ? "COMP*" : "COMP" if $comp;
	return $noresp ? "IDLE*" : "IDLE" if $base == NODE_STATE_IDLE;
	return $noresp ? "UNK*" : "UNK" if $base == NODE_STATE_UNKNOWN;
}

sub job_state_string {
	my $st = shift;
	return "COMPLETING" if $st & JOB_COMPLETING;
	return "PENDING" if $st == JOB_PENDING;
	return "RUNNING"  if $st == JOB_RUNNING;
	return "SUSPENDED"  if $st == JOB_SUSPENDED;
	return "COMPLETED" if $st == JOB_COMPLETE;
	return "CANCELLED" if $st == JOB_CANCELLED;
	return "FAILED"  if $st == JOB_FAILED;
	return "TIMEOUT" if $st == JOB_TIMEOUT;
	return "NODE_FAIL" if$st == JOB_NODE_FAIL;
	return "?";
}

sub job_state_string_compact {
	my $st = shift;
	return "CG" if $st & JOB_COMPLETING;
	return "PD" if $st == JOB_PENDING;
	return "R"  if $st == JOB_RUNNING;
	return "S"  if $st == JOB_SUSPENDED;
	return "CD" if $st == JOB_COMPLETE;
	return "CA" if $st == JOB_CANCELLED;
	return "F"  if $st == JOB_FAILED;
	return "TO" if $st == JOB_TIMEOUT;
	return "NF" if$st == JOB_NODE_FAIL;
	return "?";
}

sub job_reason_string {
	my $r = shift;
	return "None" if $r == WAIT_NO_REASON;
	return "Priority" if $r == WAIT_PRIORITY;
	return "Dependency" if $r == WAIT_DEPENDENCY;
	return "Resource" if $r == WAIT_RESOURCES;
	return "PartitionNodeLimit" if $r == WAIT_PART_NODE_LIMIT;
	return "PartitionTimeLimit" if $r == WAIT_PART_TIME_LIMIT;
	return "PartitionDown" if $r == WAIT_PART_STATE;
	return "JobHeld" if $r == WAIT_HELD;
	return "BeginTime" if $r == WAIT_TIME;
	return "PartitionDown" if $r == FAIL_DOWN_PARTITION;
	return "NodeDown" if $r == FAIL_DOWN_NODE;
	return "BadConstraints" if $r == FAIL_BAD_CONSTRAINTS;
	return "SystemFailure" if $r == FAIL_SYSTEM;
	return "JobLaunchFailure" if $r == FAIL_LAUNCH;
	return "NonZeroExitCode" if $r == FAIL_EXIT_CODE;
	return "TimeLimit" if $r == FAIL_TIMEOUT;
	return "InactiveLimit" if $r == FAIL_INACTIVE_LIMIT;
	return "?";
}


1;
__END__
# Below is stub documentation for your module. You'd better edit it!

=head1 NAME

Slurm - Perl API for slurm

=head1 SYNOPSIS

  use Slurm;

  $nodes = Slurm->load_node();
  unless($nodes) {
    $errmsg = Slurm->strerror();
    print "Error loading nodes: $errmsg";
  }


=head1 DESCRIPTION

The Slurm class is a wrapper of the slurm API.

TODO

=head2 FUNCTIONS


=head2 EXPORT

None by default.

=head2 Exportable constants



=head1 SEE ALSO

Mention other useful documentation such as the documentation of
related modules or operating system documentation (such as man pages
in UNIX), or any relevant external documentation such as RFCs or
standards.

If you have a mailing list set up for your module, mention it here.

If you have a web site set up for your module, mention it here.

=head1 AUTHOR

Hongjia Cao, E<lt>hjcao@nudt.edu.cnE<gt>

=head1 COPYRIGHT AND LICENSE

Copyright (C) 2007 by Hongjia Cao

This library is free software; you can redistribute it and/or modify
it under the same terms as Perl itself, either Perl version 5.8.4 or,
at your option, any later version of Perl 5 you may have available.


=cut
