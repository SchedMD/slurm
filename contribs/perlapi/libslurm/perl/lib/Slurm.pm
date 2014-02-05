package Slurm;

use 5.008;
use strict;
use warnings;
use Carp;

use Slurm::Hostlist;
use Slurm::Bitstr;
use Slurm::Stepctx;
use Slurm::Constant;

sub import {
    # export constants
    Slurm::Constant->import2() if grep(/^:constant$/, @_) || grep(/^:all$/, @_);
    
    # export job/node state testing macros
    my $callpkg = caller(0);
    {
	no strict "refs";
	my ($macro, $sub);
	while( ($macro, $sub) = each(%{Slurm::}) ) {
	    next unless $macro =~ /^IS_JOB_/ or $macro =~ /^IS_NODE_/;
	    *{$callpkg . "::$macro"} = $sub;
	}
    }
}

our $VERSION = '0.02';

# XSLoader will not work for SLURM because it does not honour dl_load_flags.
require DynaLoader;
our @ISA;
push @ISA, 'DynaLoader';
bootstrap Slurm $VERSION;

sub dl_load_flags { if($^O eq 'aix') { 0x00 } else { 0x01 }}


############################################################
# handy macros defined in slurm_protocol_defs.h
############################################################
# /* Defined job states */
sub IS_JOB_PENDING     { (($_[0]->{job_state} & JOB_STATE_BASE) == JOB_PENDING) }
sub IS_JOB_RUNNING     { (($_[0]->{job_state} & JOB_STATE_BASE) == JOB_RUNNING) }
sub IS_JOB_SUSPENDED   { (($_[0]->{job_state} & JOB_STATE_BASE) == JOB_SUSPENDED) }
sub IS_JOB_COMPLETE    { (($_[0]->{job_state} & JOB_STATE_BASE) == JOB_COMPLETE) }
sub IS_JOB_CANCELLED   { (($_[0]->{job_state} & JOB_STATE_BASE) == JOB_CANCELLED) }
sub IS_JOB_FAILED      { (($_[0]->{job_state} & JOB_STATE_BASE) == JOB_FAILED) }
sub IS_JOB_TIMEOUT     { (($_[0]->{job_state} & JOB_STATE_BASE) == JOB_TIMEOUT) }
sub IS_JOB_NODE_FAILED { (($_[0]->{job_state} & JOB_STATE_BASE) == JOB_NODE_FAIL) }
# /* Derived job states */
sub IS_JOB_COMPLETING  { ($_[0]->{job_state} & JOB_COMPLETING) }
sub IS_JOB_CONFIGURING { ($_[0]->{job_state} & JOB_CONFIGURING) }
sub IS_JOB_STARTED     { (($_[0]->{job_state} & JOB_STATE_BASE) >  JOB_PENDING) }
sub IS_JOB_FINISHED    { (($_[0]->{job_state} & JOB_STATE_BASE) >  JOB_SUSPENDED) }
sub IS_JOB_COMPLETED   { (IS_JOB_FINISHED($_[0]) && (($_[0]->{job_state} & JOB_COMPLETING) == 0)) }
sub IS_JOB_RESIZING    { ($_[0]->{job_state} & JOB_RESIZING) }
# /* Defined node states */
sub IS_NODE_UNKNOWN    { (($_[0]->{node_state} & NODE_STATE_BASE) == NODE_STATE_UNKNOWN) }
sub IS_NODE_DOWN       { (($_[0]->{node_state} & NODE_STATE_BASE) == NODE_STATE_DOWN) }
sub IS_NODE_IDLE       { (($_[0]->{node_state} & NODE_STATE_BASE) == NODE_STATE_IDLE) }
sub IS_NODE_ALLOCATED  { (($_[0]->{node_state} & NODE_STATE_BASE) == NODE_STATE_ALLOCATED) }
sub IS_NODE_ERROR      { (($_[0]->{node_state} & NODE_STATE_BASE) == NODE_STATE_ERROR) }
sub IS_NODE_MIXED      { (($_[0]->{node_state} & NODE_STATE_BASE) == NODE_STATE_MIXED) }
sub IS_NODE_FUTURE     { (($_[0]->{node_state} & NODE_STATE_BASE) == NODE_STATE_FUTURE) }
# /* Derived node states */
sub IS_NODE_DRAIN      { ($_[0]->{node_state} & NODE_STATE_DRAIN) }
sub IS_NODE_DRAINING   { (($_[0]->{node_state} & NODE_STATE_DRAIN) &&
			  (IS_NODE_ALLOCATED($_[0]) || IS_NODE_ERROR($_[0]) || IS_NODE_MIXED($_[0]))) }
sub IS_NODE_DRAINED    { (IS_NODE_DRAIN($_[0]) && !IS_NODE_DRAINING($_[0])) }
sub IS_NODE_COMPLETING { ($_[0]->{node_state} & NODE_STATE_COMPLETING) }
sub IS_NODE_NO_RESPOND { ($_[0]->{node_state} & NODE_STATE_NO_RESPOND) }
sub IS_NODE_POWER_SAVE { ($_[0]->{node_state} & NODE_STATE_POWER_SAVE) }
sub IS_NODE_POWER_UP   { ($_[0]->{node_state} & NODE_STATE_POWER_UP) }
sub IS_NODE_FAIL       { ($_[0]->{node_state} & NODE_STATE_FAIL) }
sub IS_NODE_MAINT      { ($_[0]->{node_state} & NODE_STATE_MAINT) }


1;
__END__

=head1 NAME

Slurm - Perl API for libslurm

=head1 SYNOPSIS

    use Slurm;

    my $slurm = Slurm::new();
    $nodes = $slurm->load_node();
    unless($nodes) {
        die "failed to load node info: " . $slurm->strerror();
    }

=head1 DESCRIPTION

The Slurm class provides Perl interface of the SLURM API functions in C<E<lt>slurm/slurm.hE<gt>>, with some extra frequently used functions exported by libslurm.

=head2 METHODS

To use the API, first create a Slurm object:

    $slurm = Slurm::new($conf);

Then call the desired functions:

    $resp = $slurm->load_jobs();

In the following L</"METHODS"> section, if a parameter is omitted, it will be listed as "param=val" , where "val" is the default value of the parameter.
    
=head2 DATA STRUCTURES
    
Typicaly, C structures are converted to (maybe blessed) Perl hash references, with field names as hash keys. Arrays in C are converted to arrays in Perl. For example, there is a structure C<job_info_msg_t>:

    typedef struct job_info_msg {
        time_t last_update;     /* time of latest info */
        uint32_t record_count;  /* number of records */
        job_info_t *job_array;  /* the job records */
    } job_info_msg_t;

This will be converted to a hash reference with the following structure:

    {
        last_update => 1285847672,
        job_array => [ {account => 'test', alloc_node => 'ln0', alloc_sid => 1234, ...},
                       {account => 'debug', alloc_node => 'ln2', alloc_sid => 5678, ...},
                       ...
                     ]
    }

Note the missing of the C<record_count> field in the hash. It can be derived from the number of elements in array C<job_array>.
    
To pass parameters to the API functions, use the corresponding hash references, for example:

    $rc = $slurm->update_node({node_names => 'node[0-7]', node_state => NODE_STATE_DRAIN});

Please see C<E<lt>slurm/slurm.hE<gt>> for the definition of the structures.

=head2 CONSTANTS

The enumerations and macro definitions are available in the Slurm package. If ':constant' is given when using the Slurm package, the constants will be exported to the calling package.

Please see L<Slurm::Constant> for the available constants.
    
=head1 METHODS

=head2 CONSTRUCTOR/DESTRUCTOR

=head3 $slurm = Slurm::new($conf_file=undef);

Create a Slurm object. For now the object is just a hash reference with no members.

=over 2

=item * IN $conf_file: the SLURM configuration file. If omitted, the default SLURM configuration file will be used (file specified by environment variable SLURM_CONF or the file slurm.conf under directroy specified in compile time).

=item * RET: blessed opaque Slurm object. On error C<undef> is returned.
    
=back


    
    
=head2 ERROR INFORMATION FUNCTIONS

=head3 $errno = $slurm->get_errno();

Get the error number associated with last operation.

=over 2

=item * RET: error number associated with last operation.

=back    

=head3 $str = $slurm->strerror($errno=0)

Get the string describing the specified error number.

=over 2

=item * IN $errno: error number. If omitted or 0, the error number returned by C<$slurm->get_errno()> will be used.
    
=item * RET: error string.

=back    



    
=head2 ENTITY STATE/REASON/FLAG/TYPE STRING FUNCTIONS

=head3 $str = $slurm->preempt_mode_string($mode_num);

Get the string describing the specified preemt mode number.

=over 2

=item * IN $mode_num: preempt mode number.
    
=item * RET: preempt mode string.

=back    

=head3 $num = $slurm->preempt_mode_num($mode_str);

Get the preempt mode number of the specified preempt mode string.

=over 2

=item * IN $mode_str: preempt mode string.
    
=item * RET: preempt mode number.

=back    

=head3 $str = $slurm->job_reason_string($num);

Get the string representation of the specified job state reason number.

=over 2

=item * IN $num: job reason number.
    
=item * RET: job reason string.

=back    

=head3 $str = $slurm->job_state_string($num);

Get the string representation of the specified job state number.

=over 2

=item * IN $num: job state number.
    
=item * RET: job state string.

=back    

=head3 $str = $slurm->job_state_string_compact($num);

Get the compact string representation of the specified job state number.

=over 2

=item * IN $num: job state number.
    
=item * RET: compact job state string.

=back    

=head3 $num = $slurm->job_state_num($str);

Get the job state number of the specified (compact) job state string.

=over 2

=item * IN $str: job state string.
    
=item * RET: job state number.

=back    

=head3 $str = $slurm->reservation_flags_string($flags);

Get the string representation of the specified reservation flags.

=over 2

=item * IN $num: reservation flags number.
    
=item * RET: reservation flags string.

=back    

=head3 $str = $slurm->node_state_string($num);

Get the string representation of the specified node state number.

=over 2

=item * IN $num: node state number.
    
=item * RET: node state string.

=back    

=head3 $str = $slurm->node_state_string_compact($num);

Get the compact string representation of the specified node state number.

=over 2

=item * IN $num: node state number.
    
=item * RET: compact node state string.

=back    

=head3 $str = $slurm->private_data_string($num);

Get the string representation of the specified private data type.

=over 2

=item * IN $num: private data type number.
    
=item * RET: private data type string.

=back    

=head3 $str = $slurm->accounting_enforce_string($num);

Get the string representation of the specified accounting enforce type.

=over 2

=item * IN $num: accounting enforce type number.
    
=item * RET: accounting enforce type string.

=back    

=head3 $str = $slurm->conn_type_string($num);

Get the string representation of the specified connection type.

=over 2

=item * IN $num: connection type number.
    
=item * RET: connection type string.

=back    

=head3 $str = $slurm->node_use_string($num);

Get the string representation of the specified node usage type.

=over 2

=item * IN $num: node usage type number.
    
=item * RET: node usage type string.

=back    

=head3 $str = $slurm->bg_block_state_string($num);

Get the string representation of the specified BlueGene block state.

=over 2

=item * IN $num: BG block state number.
    
=item * RET: BG block state string.

=back    


    

=head2 RESOURCE ALLOCATION FUNCTIONS

=head3 $resp = $slurm->allocate_resources($job_desc);

Allocate resources for a job request. If the requested resources are not immediately available, the slurmctld will send the job_alloc_resp_msg to the sepecified node and port.

=over 2

=item * IN $job_desc: description of resource allocation request, with sturcture of C<job_desc_msg_t>.

=item * RET: response to request, with structure of C<resource_allocation_response_msg_t>.  This only represents a job allocation if resources are immediately available.  Otherwise it just contains the job id of the enqueued job request. On failure C<undef> is returned.
    
=back    

=head3 $resp = $slurm->allocate_resources_blocking($job_desc, $timeout=0, $pending_callbacks=undef);

Allocate resources for a job request.  This call will block until the allocation is granted, or the specified timeout limit is reached.
    
=over 2

=item * IN $job_desc: description of resource allocation request, with sturcture of C<job_desc_msg_t>.

=item * IN $timeout: amount of time, in seconds, to wait for a response before giving up. A timeout of zero will wait indefinitely.    

=item * IN $pending_callbacks: If the allocation cannot be granted immediately, the controller will put the job in the PENDING state.  If
pending callback is given, it will be called with the job id of the pending job as the sole parameter.
    
=item * RET: allcation response, with structure of C<resource_allocation_response_msg_t>. On failure C<undef> is returned, with errno set.
    
=back

=head3 $resp = $slurm->allocation_lookup($job_id);

Retrieve info for an existing resource allocation.

=over 2

=item * IN $job_id: job allocation identifier.

=item * RET: job allocation info, with structure of C<job_alloc_info_response_msg_t>. On failure C<undef> is returned with errno set.    

=back

=head3 $resp = $slurm->allocatiion_lookup_lite($job_id);

Retrieve minor info for an existing resource allocation.

=over 2

=item * IN $job_id: job allocation identifier.

=item * RET: job allocation info, with structure of C<resource_allocation_response_msg_t>. On failure C<undef> is returned with errno set.    

=back

=head3 $str = $slurm->read_hostfile($filename, $n);

Read a specified SLURM hostfile. The file must contain a list of SLURM NodeNames, one per line.

=over 2

=item * IN $filename: name of SLURM hostlist file to be read.

=item * IN $n: number of NodeNames required.

=item * RET: a string representing the hostlist. Returns NULL if there are fewer than $n hostnames in the file, or if an error occurs.
    
=back
    
=head3 $msg_thr = $slurm->allocation_msg_thr_create($port, $callbacks);

Startup a message handler talking with the controller dealing with messages from the controller during an allocation.

=over 2
    
=item * OUT $port: port we are listening for messages on from the controller.
    
=item * IN $callbacks: callbacks for different types of messages, with structure of C<slurm_allocation_callbacks_t>.
    
=item * RET: opaque object of C<allocation_msg_thread_t *>,  or NULL on failure.
    
=back

=head3 $slurm->allocation_msg_thr_destroy($msg_thr);

Shutdown the message handler talking with the controller dealing with messages from the controller during an allocation.

=over 2
    
=item * IN $msg_thr: opaque object of C<allocation_msg_thread_t> pointer.
    
=back

=head3 $resp = $slurm->submit_batch_job($job_desc_msg);

Issue RPC to submit a job for later execution.

=over 2    

=item * IN $job_desc_msg: description of batch job request, with structure of C<job_desc_msg_t>.
    
=item * RET: 0 on success, otherwise return -1 and set errno to indicate the error.
    
=back

=head3 $rc = $slurm->job_will_run($job_desc_msg);

Determine if a job would execute immediately if submitted now.

=over 2
    
=item * IN $job_desc_msg: description of resource allocation request, with structure of C<job_desc_msg_t>.
    
=item * RET: 0 on success, otherwise return -1 and set errno to indicate the error.
    
=back

=head3 $resp = $slurm->sbcast_lookup($job_id);

Retrieve info for an existing resource allocation including a credential needed for sbcast.

=over 2
    
=item * IN $jobid: job allocation identifier.
    
=item * RET: job allocation information includeing a credential for sbcast, with structure of C<job_sbcast_cred_msg_t>. On failure C<undef> is returned with errno set.
    
=back



    
=head2 JOB/STEP SIGNALING FUNCTIONS

=head3 $rc = $slurm->kill_job($job_id, $signal, $batch_flag=0);

Send the specified signal to all steps of an existing job.

=over 2
    
=item * IN $job_id: the job's id.
    
=item * IN $signal: signal number.
    
=item * IN $batch_flag: 1 to signal batch shell only, otherwise 0.
    
=item * RET: 0 on success, otherwise return -1 and set errno to indicate the error.
    
=back
    
=head3 $rc = $slurm->kill_job_step($job_id, $step_id, $signal);

Send the specified signal to an existing job step.

=over 2
    
=item * IN $job_id: the job's id.
    
=item * IN $step_id: the job step's id.
    
=item * IN $signal: signal number.
    
=item * RET: 0 on success, otherwise return -1 and set errno to indicate the error.
    
=back
    
=head3 $rc = $slurm->signal_job($job_id, $signal);

Send the specified signal to all steps of an existing job.

=over 2
    
=item * IN $job_id: the job's id.
    
=item * IN $signal: signal number.
    
=item * RET: 0 on success, otherwise return -1 and set errno to indicate the error.
    
=back
    
=head3 $rc = $slurm->signal_job_step($job_id, $step_id, $signal);

Send the specified signal to an existing job step.

=over 2
    
=item * IN $job_id: the job's id.
    
=item * IN $step_id: the job step's id.
    
=item * IN $signal: signal number.
    
=item * RET: 0 on success, otherwise return -1 and set errno to indicate the error.
    
=back
    


    
=head2 JOB/STEP COMPLETION FUNCTIONS

=head3 $rc = $slurm->complete_job($job_id, $job_rc=0);

Note the completion of a job and all of its steps.

=over 2
    
=item * IN $job_id: the job's id.
    
=item * IN $job_rc: the highest exit code of any task of the job.
    
=item * RET: 0 on success, otherwise return -1 and set errno to indicate the error.
    
=back

=head3 $rc = $slurm->terminate_job_step($job_id, $step_id);

Terminates a job step by sending a REQUEST_TERMINATE_TASKS rpc to all slurmd of a job step, and then calls slurm_complete_job_step() after verifying that all nodes in the job step no longer have running tasks from the job step.  (May take over 35 seconds to return.)

=over 2    

=item * IN $job_id: the job's id.
    
=item * IN $step_id: the job step's id - use SLURM_BATCH_SCRIPT as the step_id to terminate a job's batch script.
    
=item * RET: 0 on success, otherwise return -1 and set errno to indicate the error.
    
=back



    
=head2 SLURM TASK SPAWNING FUNCTIONS

=head3 $ctx = $slurm->step_ctx_create($params);

Create a job step and its context. 

=over 2

=item * IN $params: job step parameters, with structure of C<slurm_step_ctx_params_t>.
    
=item * RET: the step context. On failure C<undef> is returned with errno set.

=back    

=head3 $ctx = $slurm->step_ctx_create_no_alloc($params);

Create a job step and its context without getting an allocation. 

=over 2

=item * IN $params: job step parameters, with structure of C<slurm_step_ctx_params_t>..
    
=item * IN $step_id: fake job step id.
    
=item * RET: the step context. On failure C<undef> is returned with errno set.

=back    




=head2 SLURM CONTROL CONFIGURATION READ/PRINT/UPDATE FUNCTIONS

=head3 ($major, $minor, $micro) = $slurm->api_version();

Get the SLURM API's version number.

=over 2

=item * RET: a three element list of the major, minor, and micro version number.

=back    

=head3 $resp = $slurm->load_ctl_conf($update_time=0);

Issue RPC to get SLURM control configuration information if changed.

=over 2
    
=item * IN $update_time: time of current configuration data.

=item * RET: SLURM configuration data, with structure of C<slurm_ctl_conf_t>. On failure C<undef> is returned with errno set.

=back

=head3 $slurm->print_ctl_conf($out, $conf);

Output the contents of SLURM control configuration message as loaded using C<load_ctl_conf()>.

=over 2
    
=item * IN $out: file to write to.
    
=item * IN $conf: SLURM control configuration, with structure of C<slurm_ctl_conf_t>.

=back

=head3 $list = $slurm->ctl_conf_2_key_pairs($conf);

Put the SLURM configuration data into a List of opaque data type C<config_key_pair_t>.

=over 2
    
=item * IN $conf: SLURM control configuration, with structure of C<slurm_ctl_conf_t>.
    
=item * RET: List of opaque data type C<config_key_pair_t>.

=back    

=head3 $resp = $slurm->load_slurmd_status();

Issue RPC to get the status of slurmd daemon on this machine.

=over 2

=item * RET: slurmd status info, with structure of C<slurmd_status_t>. On failure C<undef> is returned with errno set.    

=back    

=head3 $slurm->print_slurmd_status($out, $slurmd_status);

Output the contents of slurmd status message as loaded using C<load_slurmd_status()>.

=over 2
    
=item * IN $out: file to write to.
    
=item * IN $slurmd_status: slurmd status info, with structure of C<slurmd_status_t>.

=back    

=head3 $slurm->print_key_pairs($out, $key_pairs, $title);

Output the contents of key_pairs which is a list of opaque data type C<config_key_pair_t>.

=over 2
    
=item * IN $out: file to write to.
    
=item * IN $key_pairs: List containing key pairs to be printed.
    
=item * IN $title: title of key pair list.

=back    

=head3 $rc = $slurm->update_step($step_msg);

Update the time limit of a job step.

=over 2
    
=item * IN $step_msg: step update messasge descriptor, with structure of C<step_update_request_msg_t>.
    
=item * RET: 0 or -1 on error.

=back    



    
=head2 SLURM JOB RESOURCES READ/PRINT FUNCTIONS

=head3 $num = $slurm->job_cpus_allocated_on_node_id($job_res, $node_id);

Get the number of cpus allocated to a job on a node by node id.

=over 2
    
=item * IN $job_res: job resources data, with structure of C<job_resources_t>.
    
=item * IN $node_id: zero-origin node id in allocation.
    
=item * RET: number of CPUs allocated to job on this node or -1 on error.

=back    

=head3 $num = $slurm->job_cpus_allocated_on_node($job_res, $node_name);

Get the number of cpus allocated to a job on a node by node name.

=over 2
    
=item * IN $job_res: job resources data, with structure of C<job_resources_t>.
    
=item * IN $node_name: name of node.
    
=item * RET: number of CPUs allocated to job on this node or -1 on error.

=back    




=head2 SLURM JOB CONFIGURATION READ/PRINT/UPDATE FUNCTIONS

=head3 $time = $slurm->get_end_time($job_id);

Get the expected end time for a given slurm job.

=over 2
    
=item * IN $jobid: SLURM job id.
    
=item * RET: scheduled end time for the job. On failure C<undef> is returned with errno set.

=back    

=head3 $secs = $slurm->get_rem_time($job_id);

Get the expected time remaining for a given job.

=over 2
    
=item * IN $jobid: SLURM job id.
    
=item * RET: remaining time in seconds or -1 on error.

=back
    
=head3 $rc = $slurm->job_node_ready($job_id);

Report if nodes are ready for job to execute now.

=over 2
    
=item * IN $job_id: SLURM job id.
    
=item * RET: 

=over 2

=item * READY_JOB_FATAL: fatal error
    
=item * READY_JOB_ERROR: ordinary error
    
=item * READY_NODE_STATE: node is ready
    
=item * READY_JOB_STATE: job is ready to execute

=back    

=back    

=head3 $resp = $slurm->load_job($job_id, $show_flags=0);

Issue RPC to get job information for one job ID.

=over 2    

=item * IN $job_id: ID of job we want information about.
    
=item * IN $show_flags: job filtering options.
    
=item * RET: job information, with structure of C<job_info_msg_t>. On failure C<undef> is returned with errno set.

=back    

=head3 $resp = $slurm->load_jobs($update_time=0, $show_flags=0);

Issue RPC to get all SLURM job information if changed.

=over 2    

=item * IN $update_time: time of current job information data.
    
=item * IN $show_flags: job filtering options.
    
=item * RET: job information, with structure of C<job_info_msg_t>. On failure C<undef> is returned with errno set.

=back    

=head3 $rc = $slurm->notify_job($job_id, $message);

Send message to the job's stdout, usable only by user root.

=over 2
    
=item * IN $job_id: SLURM job id or 0 for all jobs.

=item * IN $message: arbitrary message.
    
=item * RET: 0 or -1 on error.

=back

=head3 $job_id = $slurm->pid2jobid($job_pid);

Issue RPC to get the SLURM job ID of a given process ID on this machine.

=over 2
    
=item * IN $job_pid: process ID of interest on this machine.
    
=item * RET: corresponding job ID. On failure C<undef> is returned.

=back    
    
=head3 $slurm->print_job_info($out, $job_info, $one_liner=0);

Output information about a specific SLURM job based upon message as loaded using C<load_jobs()>.

=over 2
    
=item * IN $out: file to write to.
    
=item * IN $job_info: an individual job information record, with structure of C<job_info_t>.
    
=item * IN $one_liner: print as a single line if true.
    
=back

=head3 $slurm->print_job_info_msg($out, $job_info_msg, $one_liner=0);

Output information about all SLURM jobs based upon message as loaded using C<load_jobs()>.

=over 2    

=item * IN $out: file to write to.
    
=item * IN $job_info_msg: job information message, with structure of C<job_info_msg_t>.
    
=item * IN $one_liner: print as a single line if true.

=back    

=head3 $str = $slurm->sprint_job_info($job_info, $one_liner=0);

Output information about a specific SLURM job based upon message as loaded using C<load_jobs()>.

=over 2
    
=item * IN $job_info: an individual job information record, with structure of C<job_info_t>.
    
=item * IN $one_liner: print as a single line if true.
    
=item * RET: string containing formatted output.

=back    
    
=head3 $rc = $slurm->update_job($job_info);

Issue RPC to a job's configuration per request only usable by user root or (for some parameters) the job's owner.

=over 2
    
=item * IN $job_info: description of job updates, with structure of C<job_desc_msg_t>.
    
=item * RET: 0 on success, otherwise return -1 and set errno to indicate the error.

=back


    
    
=head2 SLURM JOB STEP CONFIGURATION READ/PRINT/UPDATE FUNCTIONS

=head3 $resp = $slurm->get_job_steps($update_time=0, $job_id=NO_VAL, $step_id=NO_VAL, $show_flags=0);

Issue RPC to get specific slurm job step configuration information if changed since update_time.

=over 2
    
=item * IN $update_time: time of current configuration data.
    
=item * IN $job_id: get information for specific job id, NO_VAL for all jobs.
    
=item * IN $step_id: get information for specific job step id, NO_VAL for all job steps.
    
=item * IN $show_flags: job step filtering options.
    
=item * RET: job step information, with structure of C<job_step_info_response_msg_t>. On failure C<undef> is returned with errno set.

=back    

=head3 $slurm->print_job_step_info_msg($out, $step_info_msg, $one_liner);

Output information about all SLURM job steps based upon message as loaded using C<get_job_steps()>.

=over 2
    
=item * IN $out: file to write to.
    
=item * IN $step_info_msg: job step information message, with structure of C<job_step_info_response_msg_t>.
    
=item * IN $one_liner: print as a single line if true.

=back    

=head3 $slurm->print_job_step_info($out, $step_info, $one_liner);

Output information about a specific SLURM job step based upon message as loaded using C<get_job_steps()>.

=over 2
    
=item * IN $out: file to write to.
    
=item * IN $step_info: job step information, with structure of C<job_step_info_t>.
    
=item * IN $one_liner: print as a single line if true.

=back    

=head3 $str = $slurm->sprint_job_step_info($step_info, $one_liner);

Output information about a specific SLURM job step based upon message as loaded using C<get_job_steps()>.

=over 2
    
=item * IN $step_info: job step information, with structure of C<job_step_info_t>.
    
=item * IN $one_liner: print as a single line if true.

=item * RET: string containing formatted output.
    
=back    

=head3 $layout = $slurm->job_step_layout_get($job_id, $step_id);

Get the layout structure for a particular job step.

=over 2

=item * IN $job_id: SLURM job ID.

=item * IN $step_id: SLURM step ID.

=item * RET: layout of the job step, with structure of C<slurm_step_layout_t>. On failure C<undef> is returned with errno set.

=back    

=head3 $resp = $slurm->job_step_stat($job_id, $step_id, $nodelist=undef);

Get status of a current step.

=over 2    

=item * IN $job_id : SLURM job ID.
    
=item * IN $step_id: SLURM step ID.
    
=item * IN $nodelist: nodes to check status of step. If omitted, all nodes in step are used.

=item * RET: response of step status, with structure of C<job_step_stat_response_msg_t>. On failure C<undef> is returned.

=back
    
=head3 $resp = $slurm->job_step_get_pids($job_id, $step_id, $nodelist);
    
Get the complete list of pids for a given job step.

=over 2    

=item * IN $job_id: SLURM job ID.
    
=item * IN $step_id: SLURM step ID.

=item * IN $nodelist: nodes to check pids of step. If omitted, all nodes in step are used.
    
=item * RET: response of pids information, with structure of C<job_step_pids_response_msg_t>. On failure C<undef> is returned.

=back    



    
=head2 SLURM NODE CONFIGURATION READ/PRINT/UPDATE FUNCTIONS

=head3 $resp = $slurm->load_node($update_time=0, $show_flags=0);

Issue RPC to get all node configuration information if changed.

=over 2

=item * IN $update_time: time of current configuration data.

=item * IN $show_flags: node filtering options.

=item * RET: response hash reference with structure of C<node_info_msg_t>. On failure C<undef> is returned with errno set.

=back    

=head3 $slurm->print_node_info_msg($out, $node_info_msg, $one_liner=0);

Output information about all SLURM nodes based upon message as loaded using C<load_node()>.

=over 2

=item * IN $out: FILE handle to write to.

=item * IN $node_info_msg: node information message to print, with structure of C<node_info_msg_t>.

=item * IN $one_liner: if true, each node info will be printed as a single line.

=back    

=head3 $slurm->print_node_table($out, $node_info, $node_scaling=1, $one_liner=0);

Output information about a specific SLURM node based upon message as loaded using C<load_node()>.

=over 2
    
=item * IN $out: FILE handle to write to.

=item * IN $node_info: an individual node information record with structure of C<node_info_t>.

=item * IN $node_scaling: the number of nodes each node information record represents.

=item * IN $one_liner: whether to print as a single line.

=back    

=head3 $str = $slurm->sprint_node_table($node_info, $node_scaling=1, $one_liner=0);

Output information about a specific SLURM node based upon message as loaded using C<load_node>.

=over 2

=item * IN $node_info: an individual node information record with structure of C<node_info_t>.

=item * IN $node_scaling: number of nodes each node information record represents.

=item * IN $one_liner: whether to print as a single line.

=item * RET: string containing formatted output on success, C<undef> on failure.

=back
    
=head3 $rc = $slurm->update_node($node_info);

Issue RPC to modify a node's configuration per request, only usable by user root.

=over 2

=item * IN $node_info: description of node updates, with structure of C<update_node_msg_t>.

=item * RET: 0 on success, -1 on failure with errno set.

=back    


    

=head2 SLURM SWITCH TOPOLOGY CONFIGURATION READ/PRINT FUNCTIONS

=head3 $resp = $slurm->load_topo();

Issue RPC to get all switch topology configuration information. 

=over 2

=item * RET: response hash reference with structure of C<topo_info_response_msg_t>. On failure C<undef> is returned with errno set.

=back
    
=head3 $slurm->print_topo_info_msg($out, $topo_info_msg, $one_liner=0);

Output information about all switch topology configuration information based upon message as loaded using C<load_topo()>. 

=over 2

=item * IN $out: FILE handle to write to.

=item * IN $topo_info_msg: swith topology information message, with structure of C<topo_info_response_msg_t>.

=item * IN $one_liner: print as a single line if not zero.

=back

=head3 $slurm->print_topo_record($out, $topo_info, $one_liner);

Output information about a specific SLURM topology record based upon message as loaded using C<load_topo()>.

=over 2

=item * IN $out: FILE handle to write to.

=item * IN $topo_info: an individual switch information record, with structure of C<topo_info_t>.

=item * IN $one_liner: print as a single line if not zero.

=back




=head2 SLURM SELECT READ/PRINT/UPDATE FUNCTIONS

=head3 $rc = $slurm->get_select_jobinfo($jobinfo, $data_type, $data)

Get data from a select job credential. 

=over 2

=item * IN $jobinfo: select job credential to get data from. Opaque object.

=item * IN $data_type: type of data to get.

=over 2    

=item * TODO: enumerate data type and returned value.

=back

=item * OUT $data: the data got.

=item * RET: error code.    

=back

=head3 $rc = $slurm->get_select_nodeinfo($nodeinfo, $data_type, $state, $data);

Get data from a select node credential. 


=over 2

=item * IN $nodeinfo: select node credential to get data from.

=item * IN $data_type: type of data to get.

=over 2

=item * TODO: enumerate data type and returned value.
    
=back    
    
=item * IN $state: state of node query.

=item * OUT $data: the data got.

=back





=head2 SLURM PARTITION CONFIGURATION READ/PRINT/UPDATE FUNCTIONS

=head3 $resp = $slurm->load_partitions($update_time=0, $show_flags=0);

Issue RPC to get all SLURM partition configuration information if changed.

=over 2

=item * IN $update_time: time of current configuration data.

=item * IN $show_flags: partitions filtering options.

=item * RET: response hash reference with structure of C<partition_info_msg_t>.
    
=back

=head3 $slurm->print_partition_info_msg($out, $part_info_msg, $one_liner=0);

Output information about all SLURM partitions based upon message as loaded using C<load_partitions()>.

=over 2

=item * IN $out: FILE handle to write to.

=item * IN $part_info_msg: partitions information message, with structure of C<partition_info_msg_t>.

=item * IN $one_liner: print as a single line if true.

=back

=head3 $slurm->print_partition_info($out, $part_info, $one_liner=0);

Output information about a specific SLURM partition based upon message as loaded using C<load_partitions()>.

=over 2

=item * IN $out: FILE handle to write to.

=item * IN $part_info: an individual partition information record, with structure of C<partition_info_t>.

=item * IN $one_liner: print as a single ine if true.

=back

=head3 $str = $slurm->sprint_partition_info($part_info, $one_liner=0);

Output information about a specific SLURM partition based upon message as loaded using C<load_reservations()>. 

=over 2

=item * IN $part_info: an individual partition information record, with structure of C<partition_info_t>.

=item * IN $one_liner: print as a single line if true.

=item * RET: string containing formatted output. On failure C<undef> is returned.
    
=back

=head3 $rc = $slurm->create_partition($part_info);

Create a new partition, only usable by user root. 

=over 2

=item * IN $part_info: description of partition configuration with structure of C<update_part_msg_t>.

=item * RET: 0 on success, -1 on failure with errno set.
    
=back

=head3 $rc = $slurm->update_partition($part_info);

Issue RPC to update a partition's configuration per request, only usable by user root.

=over 2

=item * IN $part_info: description of partition updates with structure of C<update_part_msg_t>.

=item * RET: 0 on success, -1 on failure with errno set.
    
=back

=head3 $rc = $slurm->delete_partition($part_info)

Issue RPC to delete a partition, only usable by user root.

=over 2

=item * IN $part_info: description of partition to delete, with structure of C<delete_part_msg_t>.

=item * RET: 0 on success, -1 on failure with errno set.
    
=back





=head2 SLURM RESERVATION CONFIGURATION READ/PRINT/UPDATE FUNCTIONS

=head3 $name = $slurm->create_reservation($resv_info);

Create a new reservation, only usable by user root.

=over 2

=item * IN $resv_info: description of reservation, with structure of C<resv_desc_msg_t>.

=item * RET: name of reservation created. On failure C<undef> is returned with errno set.
    
=back

=head3 $rc = $slurm->update_reservation($resv_info);

Modify an existing reservation, only usable by user root.

=over 2

=item * IN $resv_info: description of reservation, with structure of C<resv_desc_msg_t>.

=item * RET: error code.
    
=back

=head3 $rc = $slurm->delete_reservation($resv_info);

Issue RPC to delete a reservation, only usable by user root. 

=over 2

=item * IN $resv_info: description of reservation to delete, with structure of C<reservation_name_msg_t>.

=item * RET: error code
    
=back

=head3 $resp = $slurm->load_reservations($update_time=0);

Issue RPC to get all SLURM reservation configuration information if changed.

=over 2

=item * IN $update_time: time of current configuration data.

=item * RET: response of reservation information, with structure of C<reserve_info_msg_t>. On failure C<undef> is returned with errno set.
    
=back

=head3 $slurm->print_reservation_info_msg($out, $resv_info_msg, $one_liner=0);

Output information about all SLURM reservations based upon message as loaded using C<load_reservation()>.

=over 2

=item * IN $out: FILE handle to write to.

=item * IN $resv_info_msg: reservation information message, with structure of C<reserve_info_msg_t>.

=item * IN $one_liner: print as a single line if true.

=back

=head3 $slurm->print_reservation_info($out, $resv_info, $one_liner=0);

Output information about a specific SLURM reservation based upon message as loaded using C<load_reservation()>.

=over 2

=item * IN $out: FILE handle to write to.

=item * IN $resv_info: an individual reservation information record, with structure of C<reserve_info_t>.

=item * IN $one_liner: print as a single line if true.

=back

=head3 $str = $slurm->sprint_reservation_info($resv_info, $one_liner=0);

Output information about a specific SLURM reservation based upon message as loaded using C<load_reservations()>.

=over 2

=item * IN $resv_info: an individual reservation information record, with structure of C<reserve_info_t>.

=item * IN $one_liner: print as a single line if true.

=item * RET: string containing formatted output. On failure C<undef> is returned.
    
=back



    
=head2 SLURM PING/RECONFIGURE/SHUTDOWN FUNCTIONS

=head3 $rc = $slurm->ping($primary);

Issue RPC to ping Slurm controller (slurmctld).

=over 2

=item * IN primary: 1 for primary controller, 2 for secondary controller.

=item * RET: error code.
    
=back

=head3 $rc = $slurm->reconfigure()

Issue RPC to have Slurm controller (slurmctld) reload its configuration file.

=over 2

=item * RET: error code.
    
=back

=head3 $rc = $slurm->shutdown($options);

Issue RPC to have Slurm controller (slurmctld) cease operations, both the primary and backup controller are shutdown.

=over 2

=item * IN $options:

=over 4

=item * 0: all slurm daemons are shutdown.

=item * 1: slurmctld generates a core file.

=item * 2: only the slurmctld is shutdown (no core file).

=back

=item * RET: error code.
    
=back

=head3 $rc = $slurm->takeover();

Issue RPC to have Slurm backup controller take over the primary controller. REQUEST_CONTROL is sent by the backup to the primary controller to take control. 

=over 2

=item * RET: error code.
    
=back

=head3 $rc = $slurm->set_debug_level($debug_level)

Issue RPC to set slurm controller debug level.

=over 2

=item * IN $debug_level: requested debug level.

=item * RET: 0 on success, -1 on error with errno set.
    
=back

=head3 $rc = $slurm->set_schedlog_level($schedlog_level);

Issue RPC to set slurm scheduler log level.
    
=over 2

=item * schedlog_level: requested scheduler log level.

=item * RET: 0 on success, -1 on error with errno set.
    
=back



    
=head2 SLURM JOB SUSPEND FUNCTIONS

=head3 $rc = $slurm->suspend($job_id);

Suspend execution of a job.

=over 2

=item * IN $job_id: job on which top perform operation.

=item * RET: error code.    

=back

=head3 $rc = $slurm->resume($job_id);

Resume execution of a previously suspended job.

=over 2

=item * IN $job_id: job on which to perform operation.

=item * RET: error code.
    
=back

=head3 $rc = $slurm->requeue($job_id);

Re-queue a batch job, if already running then terminate it first.

=over 2

=item * IN $job_id: job on which to perform operation.

=item * RET: error code.

=back


    

=head2 SLURM JOB CHECKPOINT FUNCTIONS

=head3 $rc = $slurm->checkpoint_able($job_id, $step_id, $start_time);

Determine if the specified job step can presently be checkpointed.

=over 2

=item * IN $job_id: job on which to perform operation.

=item * IN $step_id: job step on which to perform operation.

=item * OUT $start_time: time at which checkpoint request was issued.

=item * RET: 0 (can be checkpoined) or a slurm error code.
    
=back

=head3 $rc = $slurm->checkpoint_disable($job_id, $step_id);

Disable checkpoint requests for some job step.

=over 2

=item * IN $job_id: job on which to perform operation.

=item * IN $step_id: job step on which to perform operation.

=item * RET: error code.
    
=back

=head3 $rc = $slurm->checkpoint_enable($job_id, $step_id);

Enable checkpoint requests for some job step.

=over 2

=item * IN $job_id: job on which to perform operation.

=item * IN $step_id: job step on which to perform operation.

=item * RET: error code.
    
=back

=head3 $rc = $slurm->checkpoint_create($job_id, $step_id, $max_wait, $image_dir);

Initiate a checkpoint requests for some job step. The job will continue execution after the checkpoint operation completes.

=over 2

=item * IN $job_id: job on which to perform operation.

=item * IN $step_id: job step on which to perform operation.

=item * IN $max_wait: maximum wait for operation to complete, in seconds.

=item * IN $image_dir: directory to store image files.

=item * RET: error code.    
    
=back

=head3 $rc = $slurm->checkpoint_vacate($job_id, $step_id, $max_wait, $image_dir);

Initiate a checkpoint requests for some job step. The job will terminate after the checkpoint operation completes.

=over 2

=item * IN $job_id: job on which to perform operation.

=item * IN $step_id: job step on which to perform operation.

=item * IN $max_wait: maximum wait for operation to complete, in seconds.

=item * IN $image_dir: directory to store image files.

=item * RET: error code.
    
=back

=head3 $rc = $slurm->checkpoint_restart($job_id, $step_id, $stick, $image_dir)

Restart execution of a checkpointed job step.

=over 2

=item * IN $job_id: job on which to perform operation.

=item * IN $step_id: job step on which to perform operation.

=item * IN $stick: if true, stick to nodes previously running on.

=item * IN $image_dir: directory to find checkpoint image files.

=item * RET: error code.
    
=back

=head3 $rc = $slurm->checkpoint_complete($job_id, $step_id, $begin_time, $error_code, $error_msg);

Note the completion of a job step's checkpoint operation. 

=over 2

=item * IN $job_id: job on which to perform operation.

=item * IN $step_id: job step on which to perform operation.

=item * IN $begin_time: time at which checkpoint began.

=item * IN $error_code: error code, highest value for all complete calls is preserved.

=item * IN $error_msg: error message, preserved for highest error_code.

=item * RET: error code.
    
=back

=head3 checkpoint_task_complete($job_id, $step_id, $task_id, $begin_time, $error_code, $error_msg);

Note the completion of a task's checkpoint operation.

=over 2

=item * IN $job_id: job on which to perform operation.

=item * IN $step_id: job step on which to perform operation.

=item * IN $task_id: task which completed the operation.

=item * IN $begin_time: time at which checkpoint began.

=item * IN $error_code: error code, highest value for all complete calls is preserved.

=item * IN $error_msg: error message, preserved for highest error_code.

=item * RET: error code.
    
=back

=head3 $rc = $slurm->checkpoint_error($job_id, $step_id, $error_code, $error_msg);

Gather error information for the last checkpoint operation for some job step.

=over 2

=item * IN $job_id: job on which to perform operation.

=item * IN $step_id: job step on which to perform operation.

=item * OUT $error_code: error number associated with the last checkpoint operation.

=item * OUT $error_msg: error message associated with the last checkpoint operation.

=item * RET: error code.

=back

=head3 $rc = $slurm->checkpoint_tasks($job_id, $step_id, $image_dir, $max_wait, $nodelist);

Send checkoint request to tasks of specified job step.

=over 2

=item * IN $job_id: job on which to perform operation.

=item * IN $step_id: job step on which to perform operation.

=item * IN $image_dir: location to store checkpoint image files.

=item * IN $max_wait: seconds to wait for the operation to complete.

=item * IN $nodelist: nodes to send the request.    

=item * RET: 0 on success, non-zero on failure with errno set.
    
=back


    

=head2 SLURM TRIGGER FUNCTIONS

=head3 $rc = $slurm->set_trigger($trigger_info);

Set an event trigger.

=over 2

=item * IN $trigger_info: hash reference of specification of trigger to create, with structure of C<trigger_info_t>.

=item * RET: error code.

=back

=head3 $rc = $slurm->clear_trigger($trigger_info);

Clear an existing event trigger. 

=over 2

=item * IN $trigger_info: hash reference of specification of trigger to remove, with structure of C<trigger_info_t>.

=item * RET: error code.

=back

=head3 $resp = $slurm->get_triggers();

Get all event trigger information.

=over 2

=item * RET: hash reference with structure of C<trigger_info_msg_t>. On failure C<undef> is returned with errno set.
    
=back


    
    
=head2 JOB/NODE STATE TESTING FUNCTIONS

The following are functions to test job/node state, based on the macros defined in F<src/common/slurm_protocol_defs.h>. The functions take a parameter of a hash reference of a job/node, and return a boolean value. For job, $job->{job_state} is tested. For node, $node->{node_state} is tested.

=head3 $cond = IS_JOB_PENDING($job);

=head3 $cond = IS_JOB_RUNNING($job);

=head3 $cond = IS_JOB_SUSPENDED($job);

=head3 $cond = IS_JOB_COMPLETE($job); 

=head3 $cond = IS_JOB_CANCELLED($job);

=head3 $cond = IS_JOB_FAILED($job);

=head3 $cond = IS_JOB_TIMEOUT($job);

=head3 $cond = IS_JOB_NODE_FAILED($job);

=head3 $cond = IS_JOB_COMPLETING($job);

=head3 $cond = IS_JOB_CONFIGURING($job);

=head3 $cond = IS_JOB_STARTED($job);

=head3 $cond = IS_JOB_FINISHED($job);

=head3 $cond = IS_JOB_COMPLETED($job);

=head3 $cond = IS_JOB_RESIZING($job);

=head3 $cond = IS_NODE_UNKNOWN($node);

=head3 $cond = IS_NODE_DOWN($node);

=head3 $cond = IS_NODE_IDLE($node);

=head3 $cond = IS_NODE_ALLOCATED($node);

=head3 $cond = IS_NODE_ERROR($node);

=head3 $cond = IS_NODE_MIXED($node);

=head3 $cond = IS_NODE_FUTURE($node);

=head3 $cond = IS_NODE_DRAIN($node);

=head3 $cond = IS_NODE_DRAINING($node);

=head3 $cond = IS_NODE_DRAINED($node);

=head3 $cond = IS_NODE_COMPLETING($node);

=head3 $cond = IS_NODE_NO_RESPOND($node);

=head3 $cond = IS_NODE_POWER_SAVE($node);

=head3 $cond = IS_NODE_POWER_UP($node);

=head3 $cond = IS_NODE_FAIL($node);

=head3 $cond = IS_NODE_MAINT($node);



    
=head1 EXPORT

The job/node state testing functions are exported by default.

If ':constant' if specified, all constants are exported.

=head1 SEE ALSO

L<Slurm::Constant>, L<Slurm::Hostlist>, L<Slurm::Stepctx>, L<Slurm::Bitstr>

<slurm/slurm.h> for various hash reference structures.

Home page of SLURM: L<http://www.llnl.gov/linux/slurm/>.

=head1 AUTHOR

This library is created by Hongjia Cao, E<lt>hjcao(AT)nudt.edu.cnE<gt> and Danny Auble, E<lt>da(AT)llnl.govE<gt>. It is distributed with SLURM.

=head1 COPYRIGHT AND LICENSE

This library is free software; you can redistribute it and/or modify
it under the same terms as Perl itself, either Perl version 5.8.4 or,
at your option, any later version of Perl 5 you may have available.


=cut
