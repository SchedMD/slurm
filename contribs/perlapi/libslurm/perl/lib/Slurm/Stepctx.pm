package Slurm::Stepctx;

1;

__END__


=head1 NAME

Slurm::Stepctx - Step launching functions in libslurm

=head1 SYNOPSIS

 use Slurm;

 $slurm = Slurm::new();
 $params = {job_id => 1234, ...};
 $ctx = $slurm->step_ctx_create($params);
 $rc = $ctx->launch({...}, {task_start => sub {...},
                         task_finish => sub {...} });

=head1 DESCRIPTION

The Slurm::Stepctx class is a wrapper of the job step context and step launching functions in libslurm. This package is loaded and bootstraped with package Slurm.

=head1 METHODS

=head2 STEP CONTEXT CREATION FUNCTIONS

Please see L<Slurm/"SLURM TASK SPAWNING FUNCTIONS"> for step context creation functions.    


    
=head2 STEP CONTEXT MANIPULATION FUNCTIONS
    
=head3 $rc = $ctx->get($ctx_key, ...);

Get parameters from a job step context.

=over 2

=item * INPUT $ctx_key: type of the parameter to get. Supported key and the corresponding result data are:

=over 2
    
=item * $rc = $ctx->get(SLURM_STEP_CTX_STEPID, $stepid);

Get the created job step id. $stepid will be set to the step id number.

=item * $rc = $ctx->get(SLURM_STEP_CTX_TASKS, $tasks);

Get array of task count on each node. $tasks will be set to an array reference.

=item * $rc = $ctx->get(SLURM_STEP_CTX_TID, $nodeid, $tids);

Get array of task IDs for specified node. $nodeid specifies index of the node. $tids will be set to an array reference.

=item * $rc = $ctx->get(SLURM_STEP_CTX_RESP, $resp);

TODO: this is not exported. Get job step create response message.

=item * $rc = $ctx->get(SLURM_STEP_CTX_CRED, $cred);

Get credential of the created job step. $cred will be an opaque object blessed to "Slurm::slurm_cred_t".

=item * $rc = $ctx->get(SLURM_STEP_CTX_SWITCH_JOB, $switch_info);

Get switch plugin specific info of the step. $switch_info will be an opaque object blessed to "Slurm::dynamic_plugin_data_t".

=item * $rc = $ctx->get(SLURM_STEP_CTX_NUM_HOSTS, $num);

Get number of nodes allocated to the job step.

=item * $rc = $ctx-E<gt>get(SLURM_STEP_CTX_HOST, $nodeid, $nodename);

Get node name allocated to the job step. $nodeid specifies index of the node.

=item * $rc = $ctx->get(SLURM_STEP_CTX_JOBID, $jobid);

Get job ID of the job step. 

=item * $rc = $ctx->get(SLURM_STEP_CTX_USER_MANAGED_SOCKETS, $numtasks, $sockets);

Get user managed I/O sockets. TODO: describe the parameters.

=back

=item * RET: error code.

=back    

=head3 $rc = $ctx->daemon_per_node_hack($node_list, $node_cnt, $curr_task_num);

Hack the step context to run a single process per node, regardless of the settings selected at Slurm::Stepctx::create() time.

=over 2    

=item * RET: error code.

=back    


    
=head2 STEP TASK LAUNCHING FUNCTIONS

=head3 $rc = $ctx->launch($params, $callbacks);

Launch a parallel job step.

=over 2

=item * IN $params: parameters of task launching, with structure of C<slurm_step_launch_params_t>.

=item * IN $callbacks: callback functions, with structure of C<slurm_step_launch_callbacks_t>. NOTE: the callback functions will be called in a thread different from the thread calling the C<launch()> function.

=item * RET: error code.
    
=back    

=head3 $rc = $ctx->launch_wait_start();

Block until all tasks have started.

=over 2    

=item * RET: error code.

=back    
    
=head3 $ctx->launch_wait_finish();

Block until all tasks have finished (or failed to start altogether).

=head3 $ctx->launch_abort();

Abort an in-progress launch, or terminate the fully launched job step. Can be called from a signal handler.

=head3 $ctx->launch_fwd_signal($signo);

Forward a signal to all those nodes with running tasks.

=over 2
    
=item * IN $signo: signal number.    

=back


    
=head1 SEE ALSO

Slurm

=head1 AUTHOR

This library is created by Hongjia Cao, E<lt>hjcao(AT)nudt.edu.cnE<gt> and Danny Auble, E<lt>da(AT)llnl.govE<gt>. It is distributed with Slurm.

=head1 COPYRIGHT AND LICENSE

This library is free software; you can redistribute it and/or modify
it under the same terms as Perl itself, either Perl version 5.8.4 or,
at your option, any later version of Perl 5 you may have available.


=cut

