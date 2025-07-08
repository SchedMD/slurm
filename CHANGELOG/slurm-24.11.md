## Changes in 24.11.6

* Fix race condition on x11 shutdown which caused other privileged cleanup operations to fail and leave stray cgroup or spool directories and errors in the logs.
* slurmctld - Prevent sending repeat job start messages to the slurmdbd that can cause the loss of the reservation a job used in the database when upgrading from <24.11.
* Prevent deadlock when loading reservations from state file.
* slurmctld - Avoid race condition during shutdown when rpc_queue is enabled that could result in a SEGFAULT while processing job completions.
* slurmctld - Fix CONMGR_WAIT_WRITE_DELAY, CONMGR_READ_TIMEOUT, CONMGR_WRITE_TIMEOUT, and CONMGR_CONNECT_TIMEOUT being ignored in SlurmctldParameters in slurm.conf.
* slurmctld - Fix race condition in the ping logic that could result in the incorrect DOWNING of healthy/responding nodes.
* Fix a minor and rare memory leak in slurmscriptd.
* Fix a race condition that can cause slurmctld to hang forever when trying to shutdown or reconfigure.
* Prevent slurmctld from crashing due to avoiding a deadlock in the assoc_mgr. The crash was triggered if slurmctld was started before slurmdbd, a partition had either AllowQOS or DenyQOS defined, and something triggered a message to the slurmdbd (like a job running).
* slurmctld - Fix regression where configured timeouts would not be enforced for new incoming RPC connections until after the incoming RPC packet has been read.
* slurmctld - Avoid timeouts not getting enforced due to race condition of when connections are examined for timeouts.
* slurmctld/slurmd/sackd/slurmrestd/slurmdbd/scrun: Modified internal monitoring of I/O activity to always wake up with a maximum sleep of 300s seconds to check for changes and to restart polling of file descriptors. This will avoid daemons from getting effectively stuck forever (or until a POSIX signal) while idle if another bug is triggered which could cause an I/O event to be missed by the internal monitoring.
* Fix sacctmgr ping to be able to connect to newer versioned slurmdbd.
* Prevent slurmctld from allocating to many MPI ports to jobs using the stepmgr.
* sacct - fix assert failure when running dataparser v0.0.42.
* Fixed issue where slurmctld could segfault in specific cases of heavy controller load and job requeues.
* Delay closing sockets in eio code which fixes issues in X11 forwarding when using applications such as Emacs or Matlab.
* slurmctld: Avoid crash causing by race condition when job state cache is enabled with a large number of jobs.
* Fix storing dynanmic future node's instance id and type on registration.
* slurmd - Fixed a few minor memory leaks
* Prevent slurmd -C from potentially crashing.
* Fix incorrect extern step termination when using sbatch or salloc --signal option.
* Fix slurmctld crash when updating a partition's QOS with an invalid QOS and not having AccountingStorageEnforce=QOS.
* slurmrestd - Remove need to set both become_user and disable_user_check in SLURMRESTD_SECURITY when running slurmrestd as root in  become_user mode.
* slurmrestd - Prevent potential crash when using the 'POST /slurmdb/*/accounts_association' endpoints.
* Fix race condition during extern step termination when external pids were being added to or removed from the step. This could cause a segfault in the extern slurmstepd.
* Fix allowing job submission to empty partitions when EnforcePartLimits=NO.
* Fix potential incorrect group listing when using nss_slurm and requesting info for a single group.

## Changes in Slurm 24.11.5

* Return error to scontrol reboot on bad nodelists.
* slurmrestd - Report an error when QOS resolution fails for v0.0.40 endpoints.
* slurmrestd - Report an error when QOS resolution fails for v0.0.41 endpoints.
* slurmrestd - Report an error when QOS resolution fails for v0.0.42 endpoints.
* data_parser/v0.0.42 - Added +inline_enums flag which modifies the output when generating OpenAPI specification. It causes enum arrays to not be defined in their own schema with references ($ref) to them. Instead they will be dumped inline.
* Fix binding error with tres-bind map/mask on partial node allocations.
* Fix stepmgr enabled steps being able to request features.
* Reject step creation if requested feature is not available in job.
* slurmd - Restrict listening for new incoming RPC requests further into startup.
* slurmd - Avoid auth/slurm related hangs of CLI commands during startup and shutdown.
* slurmctld - Restrict processing new incoming RPC requests further into startup. Stop processing requests sooner during shutdown.
* slurmcltd - Avoid auth/slurm related hangs of CLI commands during startup and shutdown.
* slurmctld: Avoid race condition during shutdown or reconfigure that could result in a crash due delayed processing of a connection while plugins are unloaded.
* Fix small memleak when getting the job list from the database.
* Fix incorrect printing of % escape characters when printing stdio fields for jobs.
* Fix padding parsing when printing stdio fields for jobs.
* Fix printing %A array job id when expanding patterns.
* Fix reservations causing jobs to be held for Bad Constraints
* switch/hpe_slingshot - Prevent potential segfault on failed curl request to the fabric manager.
* Fix printing incorrect array job id when expanding stdio file names. The %A will now be substituted by the correct value.
* Fix printing incorrect array job id when expanding stdio file names. The %A will now be substituted by the correct value.
* switch/hpe_slingshot - Fix vni range not updating on slurmctld restart or reconfigre.
* Fix steps not being created when using certain combinations of -c and -n inferior to the jobs requested resources, when using stepmgr and nodes are configured with CPUs == Sockets*CoresPerSocket.
* Permit configuring the number of retry attempts to destroy CXI service via the new destroy_retries SwitchParameter.
* Do not reset memory.high and memory.swap.max in slurmd startup or reconfigure as we are never really touching this in slurmd.
* Fix reconfigure failure of slurmd when it has been started manually and the CoreSpecLimits have been removed from slurm.conf.
* Set or reset CoreSpec limits when slurmd is reconfigured and it was started with systemd.
* switch/hpe-slingshot - Make sure the slurmctld can free step VNIs after the controller restarts or reconfigures while the job is running.
* Fix backup slurmctld failure on 2nd takeover.
* Testsuite - fix python test 130_2.
* Fix security issue where a coordinator could add a user with elevated privileges. CVE-2025-43904.

## Changes in Slurm 24.11.4

* slurmctld,slurmrestd - Avoid possible race condition that could have caused process to crash when listener socket was closed while accepting a new connection.
* slurmrestd - Avoid race condition that could have resulted in address logged for a UNIX socket to be incorrect.
* slurmrestd - Fix parameters in OpenAPI specification for the following endpoints to have "job_id" field: GET /slurm/v0.0.40/jobs/state/ GET /slurm/v0.0.41/jobs/state/ GET /slurm/v0.0.42/jobs/state/ GET /slurm/v0.0.43/jobs/state/
* slurmd - Fix tracking of thread counts that could cause incoming connections to be ignored after burst of simultaneous incoming connections that trigger delayed response logic.
* Stepmgr - Avoid unnecessary SRUN_TIMEOUT forwarding to stepmgr.
* Fix jobs being scheduled on higher weighted powered down nodes.
* Fix how backfill scheduler filters nodes from the available nodes based on exclusive user and mcs_label requirements.
* acct_gather_energy/{gpu,ipmi} - Fix potential energy consumption adjustment calculation underflow.
* acct_gather_energy/ipmi - Fix regression introduced in 24.05.5 (which introduced the new way of preserving energy measurements through slurmd restarts) when EnergyIPMICalcAdjustment=yes.
* Prevent slurmctld deadlock in the assoc mgr.
* Fix memory leak when RestrictedCoresPerGPU is enabled.
* Fix preemptor jobs not entering execution due to wrong calculation of accounting policy limits.
* Fix certain job requests that were incorrectly denied with node configuration unavailable error.
* slurmd - Avoid crash due when slurmd has a communications failure with slurmstepd.
* Fix memory leak when parsing yaml input.
* Prevent slurmctld from showing error message about PreemptMode=GANG being a cluster-wide option for `scontrol update part` calls that don't attempt to modify partition PreemptMode.
* Fix setting GANG preemption on partition when updating PreemptMode with scontrol.
* Fix CoreSpec and MemSpec limits not being removed from previously configured slurmd.
* Avoid race condition that could lead to a deadlock when slurmd, slurmstepd, slurmctld, slurmrestd or sackd have a fatal event.
* Fix jobs using --ntasks-per-node and --mem keep pending forever when the requested mem divided by the number of cpus will surpass the configured MaxMemPerCPU.
* slurmd - Fix address logged upon new incoming RPC connection from "INVALID" to IP address.
* Fix memory leak when retrieving reservations. This affects scontrol, sinfo, sview, and the following slurmrestd endpoints: 'GET /slurm/{any_data_parser}/reservation/{reservation_name}' 'GET /slurm/{any_data_parser}/reservations'
* Log warning instead of debuflags=conmgr gated log when deferring new incoming connections when number of active connections exceed conmgr_max_connections.
* Avoid race condition that could result in worker thread pool not activating all threads at once after a reconfigure resulting in lower utilization of available CPU threads until enough internal activity wakes up all threads in the worker pool.
* Avoid theoretical race condition that could result in new incoming RPC socket connections being ignored after reconfigure.
* slurmd - Avoid race condition that could result in a state where new incoming RPC connections will always be ignored.
* Add ReconfigFlags=KeepNodeStateFuture to restore saved FUTURE node state on restart and reconfig instead of reverting to FUTURE state. This will be made the default in 25.05.
* Fix case where hetjob submit would cause slurmctld to crash.
* Fix jobs using --cpus-per-gpu and --mem keep pending forever when the requested mem divided by the number of cpus will surpass the configured MaxMemPerCPU.
* Enforce that jobs using --mem and several --*-per-* options do not violate the MaxMemPerCPU in place.
* slurmctld - Fix use-cases of jobs incorrectly pending held when --prefer features are not initially satisfied.
* slurmctld - Fix jobs incorrectly held when --prefer not satisfied in some use-cases.
* Ensure RestrictedCoresPerGPU and CoreSpecCount don't overlap.

## Changes in Slurm 24.11.3

* Fix race condition in slurmrestd that resulted in "Requested data_parser plugin does not support OpenAPI plugin" error being returned for valid endpoints.
* If multiple partitions are requested, set the SLURM_JOB_PARTITION output environment variable to the partition in which the job is running for salloc and srun in order to match the documentation and the behavior of sbatch.
* Fix regression where slurmd -G gives no output.
* Don't print misleading errors for stepmgr enabled steps.
* slurmrestd - Avoid connection to slurmdbd for the following endpoints: GET /slurm/v0.0.41/jobs GET /slurm/v0.0.41/job/{job_id}
* slurmrestd - Avoid connection to slurmdbd for the following endpoints: GET /slurm/v0.0.40/jobs GET /slurm/v0.0.40/job/{job_id}
* Significantly increase entropy of clusterid portion of the sluid by seeding the random number generator
* Avoid changing process name to "watch" from original daemon name. This could potentially breaking some monitoring scripts.
* Avoid slurmctld being killed by SIGALRM due to race condition at startup.
* Fix slurmctld crash when after updating a reservation with an empty nodelist. The crash could occur after restarting slurmctld, or if downing/draining a node in the reservation with the REPLACE or REPLACE_DOWN flag.
* Fix race between task/cgroup cpuset and jobacctgather/cgroup. The first was removing the pid from task_X cgroup directory causing memory limits to not be applied.
* srun - Fixed wrongly constructed SLURM_CPU_BIND env variable that could get propagated to downward srun calls in certain mpi environments, causing launch failures.
* slurmrestd - Fix possible memory leak when parsing arrays with data_parser/v0.0.40.
* slurmrestd - Fix possible memory leak when parsing arrays with data_parser/v0.0.41.
* slurmrestd - Fix possible memory leak when parsing arrays with data_parser/v0.0.42.

## Changes in Slurm 24.11.2

* Fix segfault when submitting --test-only jobs that can preempt.
* Fix regression introduced in 23.11 that prevented the following flags from being added to a reservation on an update: DAILY, HOURLY, WEEKLY, WEEKDAY, and WEEKEND.
* Fix crash and issues evaluating job's suitability for running in nodes with already suspended job(s) there.
* Slurmctld will ensure that healthy nodes are not reported as UnavailableNodes in job reason codes.
* Fix handling of jobs submitted to a current reservation with flags OVERLAP,FLEX or OVERLAP,ANY_NODES when it overlaps nodes with a future maintenance reservation. When a job submission had a time limit that overlapped with the future maintenance reservation, it was rejected. Now the job is accepted but stays pending with the reason "ReqNodeNotAvail, Reserved for maintenance".
* pam_slurm_adopt - avoid errors when explicitly setting some arguments to the default value.
* Fix qos preemption with PreemptMode=SUSPEND
* slurmdbd - When changing a user's name update lineage at the same time.
* Fix regression in 24.11 in which burst_buffer.lua does not inherit the SLURM_CONF environment variable from slurmctld and fails to run if slurm.conf is in a non-standard location.
* Fix memory leak in slurmctld if select/linear and the PreemptParameters=reclaim_licenses options are both set in slurm.conf. Regression in 24.11.1.
* Fix running jobs, that requested multiple partitions, from potentially being set to the wrong partition on restart.
* switch/hpe_slingshot - Fix compatibility with newer cxi drivers, specifically when specifying disable_rdzv_get.
* Add ABORT_ON_FATAL environment variable to capture a backtrace from any fatal() message.
* Fix printing invalid address in rate limiting log statement.
* sched/backfill - Fix node state PLANNED not being cleared from fully allocated nodes during a backfill cycle.
* select/cons_tres - Fix future planning of jobs with bf_licenses.
* Prevent redundant "on_data returned rc: Rate limit exceeded, please retry momentarily" error message from being printed in slurmctld logs.
* Fix loading non-default QOS on pending jobs from pre-24.11 state.
* Fix pending jobs displaying QOS=(null) when not explicitly requesting a QOS.
* Fix segfault issue from job record with no job_resrcs
* Fix failing sacctmgr delete/modify/show account operations with where clauses.
* Fix regression in 24.11 in which Slurm daemons started catching several SIGTSTP, SIGTTIN and SIGUSR1 signals and ignored them, while before they were not ignoring them. This also caused slurmctld to not being able to shutdown after a SIGTSTP because slurmscriptd caught the signal and stopped while slurmctld ignored it. Unify and fix these situations and get back to the previous behavior for these signals.
* Document that SIGQUIT is no longer ignored by slurmctld, slurmdbd, and slurmd in 24.11. As of 24.11.0rc1, SIGQUIT is identical to SIGINT and SIGTERM for these daemons, but this change was not documented.
* Fix not considering nodes marked for reboot without ASAP in the scheduler.
* Remove the boot^ state on unexpected node reboot after return to service.
* Do not allow new jobs to start on a node which is being rebooted with the flag nextstate=resume.
* Prevent lower priority job running after cancelling an ASAP reboot.
* Fix srun jobs starting on nextstate=resume rebooting nodes.

## Changes in Slurm 24.11.1

* With client commands MIN_MEMORY will show mem_per_tres if specified.
* Fix errno message about bad constraint
* slurmctld - Fix crash and possible split brain issue if the backup controller handles an scontrol reconfigure while in control before the primary resumes operation.
* Fix stepmgr not getting dynamic node addrs from the controller
* stepmgr - avoid "Unexpected missing socket" errors.
* Fix `scontrol show steps` with dynamic stepmgr
* Deny jobs using the "R:" option of --signal if PreemptMode=OFF globally.
* Force jobs using the "R:" option of --signal to be preemptable by requeue or cancel only. If PreemptMode on the partition or QOS is off or suspend, the job will default to using PreemptMode=cancel.
* If --mem-per-cpu exceeds MaxMemPerCPU, the number of cpus per task will always be increased even if --cpus-per-task was specified. This is needed to ensure each task gets the expected amount of memory.
* Fix compilation issue on OpenSUSE Leap 15
* Fix jobs using more nodes than needed when not using -N
* Fix issue with allocation being allocated less resources than needed when using --gres-flags=enforce-binding.
* select/cons_tres - Fix errors with MaxCpusPerSocket partition limit. Used cpus/cores weren't counted properly, nor limiting free ones to avail, when the socket was partially allocated, or the job request went beyond this limit.
* Fix issue when jobs were preempted for licenses even if there were enough licenses available.
* Fix srun ntasks calculation inside an allocation when nodes are requested using a min-max range.
* Print correct number of digits for TmpDisk in sdiag.
* Fix a regression in 24.11 which caused file transfers to a job with sbcast to not join the job container namespace.
* data_parser/v0.0.40 - Prevent a segfault in the slurmrestd when dumping data with v0.0.40+complex data parser.
* Remove logic to force lowercase GRES names.
* data_parser/v0.0.42 - Prevent the association id from always being dumped as NULL when parsing in complex mode. Instead it will now dump the id. This affects the following endpoints: GET slurmdb/v0.0.42/association GET slurmdb/v0.0.42/associations GET slurmdb/v0.0.42/config
* Fixed a job requeuing issue that merged job entries into the same SLUID when all nodes in a job failed simultaneously.
* When a job completes, try to give idle nodes to reservations with the REPLACE flag before allowing them to be allocated to jobs.
* Avoid expensive lookup of all associations when dumping or parsing for v0.0.42 endpoints.
* Avoid expensive lookup of all associations when dumping or parsing for v0.0.41 endpoints.
* Avoid expensive lookup of all associations when dumping or parsing for v0.0.40 endpoints.
* Fix segfault when testing jobs against nodes with invalid gres.
* Fix performance regression while packing larger RPCs.
* Document the new mcs/label plugin.
* job_container/tmpfs - Fix Xauthoirty file being created outside the container when EntireStepInNS is enabled.
* job_container/tmpfs - Fix spank_task_post_fork not always running in the container when EntireStepInNS is enabled.
* Fix a job potentially getting stuck in CG on permissions errors while setting up X11 forwarding.
* Fix error on X11 shutdown if Xauthority file was not created.
* slurmctld - Fix memory or fd leak if an RPC is received that is not registered for processing.
* Inject OMPI_MCA_orte_precondition_transports when using PMIx. This fixes mpi apps using Intel OPA, PSM2 and OMPI 5.x when ran through srun.
* Don't skip the first partition_job_depth jobs per partition.
* Fix gres allocation issue after controller restart.
* Fix issue where jobs requesting cpus-per-gpu hang in queue.
* switch/hpe_slingshot - Treat HTTP status forbidden the same as unauthorized, allowing for a graceful retry attempt.

## Changes in Slurm 24.11.0

* slurmctld - Reject arbitrary distribution jobs that do not specifying a task count.
* Fix backwards compatibility of the RESPONSE_JOB_INFO RPC (used by squeue, scontrol show job, etc.) with Slurm clients version 24.05 and below. This was a regression in 24.11.0rc1.
* Do not let slurmctld/slurmd to start if there are more nodes defined in slurm.conf than the maximum supported amount (64k nodes).
* slurmctld - Set job's exit code to 1 when a job fails with state JOB_NODE_FAIL. This fixes "sbatch --wait" not being able to exit with error code when a job fails for this reason in some cases.
* Fix certain reservation updates requested from 23.02 clients.
* slurmrestd - Fix populating non-required object fields of objects as '{}' in JSON/YAML instead of 'null' causing compiled OpenAPI clients to reject the response to 'GET /slurm/v0.0.40/jobs' due to validation failure of '.jobs[].job_resources'.
* Fix issue where older versions of Slurm talking to a 24.11 dbd could loose step accounting.
* Fix minor memory leaks.
* Fix bad memory reference when xstrchr fails to find char.
* Remove duplicate checks for a data structure.
* Fix race condition in stepmgr step completion handling.
* slurm.spec - add ability to specify patches to apply on the command line
* slurm.spec - add ability to supply extra version information
* Fix 24.11 HA issues.
* Fix requeued jobs keeping their priority until the decay thread happens.
* Fix potential memory corruption in select/cons_tres plugin.
* Avoid cache coherency issue on non-x86 platforms that could result in a POSIX signal being ignored or an abort().
* slurmctld - Remove assertion in development builds that would trigger if an outdated client attempted to connect.
* slurmd - Wait for PrologEpilogTimeout on reconfigure for prologs to finish. This avoids a situation where the slurmd never detects that the prolog completed.
* job_container/tmpfs - Setup x11 forwarding within the namespace.

## Changes in Slurm 24.11.0rc2

* slurmctld - fix memory leak when sending a DBD_JOB_START message.
* Fix issue with accounting rollup dealing with association tables.
* Fix minor memory leaks.
* Fix potential thread safety issues.
* Init mutex in burst_buffer plugins.
* slurmdbd - don't log errors when no changes occur from db requests.
* slurmcltd,slurmd - Avoid deadlock during reconfigure if too many POSIX signals are received.

## Changes in Slurm 24.11.0rc1

* Improve error type logged from partial or incomplete reading from socket or pipe to avoid potentially logging an error from a previous syscall.
* slurmrestd - Improve the handling of queries when unable to connect to slurmdbd by providing responses when possible.
* slurmrestd,sackd,scrun - Avoid rare hangs related to I/O.
* scrun - Add support '--all' argument for kill subcommand.
* Remove srun --cpu-bind=rank.
* Add resource_spec/cpus and resource_spec/memory entry points in data_parser to print the CpuSpecList and MemSpecLimit in sinfo --json.
* sinfo - Add '.sinfo[].resource_spec.cpus' and '.sinfo[].resource_spec.memory' fields to print the CpuSpecList and MemSpecLimit dumped by 'sinfo --{json|yaml}'.
* Increase efficiency of sending logs to syslog.
* Switch to new official YAML mime type "application/yaml" in compliance with RFC9512 as primary mime type for YAML formatting.
* slurmrestd - Removed deprecated fields from the following endpoints:
	'.result' from 'POST /slurm/v0.0.42/job/submit'.
	'.job_id', '.step_id', '.job_submit_user_msg' from 'POST /slurm/v0.0.42/job/{job_id}'.
	'.job.exclusive', '.jobs[].exclusive' to 'POST /slurm/v0.0.42/job/submit'.
	'.jobs[].exclusive' from 'GET /slurm/v0.0.42/job/{job_id}'.
	'.jobs[].exclusive' from 'GET /slurm/v0.0.42/jobs'.
	'.job.oversubscribe', '.jobs[].oversubscribe' to 'POST /slurm/v0.0.42/job/submit'.
	'.jobs[].oversubscribe' from 'GET /slurm/v0.0.42/job/{job_id}'.
	'.jobs[].oversubscribe' from 'GET /slurm/v0.0.42/jobs'.
* scontrol - Removed deprecated fields '.jobs[].exclusive' and '.jobs[].oversubscribe' from 'scontrol show jobs --{json|yaml}'.
* squeue - Removed deprecated fields '.jobs[].exclusive' and '.jobs[].oversubscribe' from 'squeue --{json|yaml}'.
* Improve the way to run external commands and fork processes to avoid non-async-signal safe calls between a fork and an exec. We fork ourselves now and executes the commands in a safe environment. This includes spank prolog/epilog executions.
* Improve MaxMemPerCPU enforcement when exclusive jobs request per node memory and the partition has heterogeneous nodes.
* Remove a TOCTOU where multiple steps requesting an energy reading at the same time could cause too frequent accesses to the drivers.
* Limit SwitchName to HOST_NAME_MAX chars length.
* For scancel --ctld and the following rest api endpoints:  'DELETE /slurm/v0.0.40/jobs'  'DELETE /slurm/v0.0.41/jobs'  'DELETE /slurm/v0.0.42/jobs' Support array expressions in the responses to the client.
* salloc - Always output node names to the user when an allocation is granted.
* slurmrestd - Removed all v0.0.39 endpoints.
* select/linear - Reject jobs asking for GRES per job|socket|task or cpus|mem per GRES.
* Add /nodes POST endpoint to REST API, supports multiple node update whereas previously only single nodes could be updated through /node/<nodename> endpoint:  'POST /slurm/v0.0.42/nodes'
* Do not allow changing or setting PreemptMode=GANG to a partition as this is a cluster-wide option.
* Add "%b" as a file name pattern for the array task id modulo 10.
* Skip packing empty nodes when they are hidden during REQUEST_NODE_INFO RPC.
* accounting_storage/mysql - Avoid a fatal condition when the db server is not reachable.
* Always lay out steps cyclically on nodes in an allocation.
* squeue - add priority by partition ('.jobs[].priority_by_partition') to JSON and YAML output.
* slurmrestd - Add clarification to "failed to open slurmdbd connection" error if the error was the result of an authentication failure.
* Make it so slurmctld responds to RPCs that have authentication errors with the SLURM_PROTOCOL_AUTHENTICATION_ERROR error code.
* openapi/slurmctld - Display the correct error code instead of "Unspecified error" if querying the following endpoints fails:  'GET /slurm/v0.0.40/diag/'  'GET /slurm/v0.0.41/diag/'  'GET /slurm/v0.0.42/diag/'  'GET /slurm/v0.0.40/licenses/'  'GET /slurm/v0.0.41/licenses/'  'GET /slurm/v0.0.42/licenses/'  'GET /slurm/v0.0.40/reconfigure'  'GET /slurm/v0.0.41/reconfigure'  'GET /slurm/v0.0.42/reconfigure'
* Fix how used cpus are tracked in a job allocation to allow the max number of concurrent steps to run at a time if threads per core is greater than 1.
* In existing allocations SLURM_GPUS_PER_NODE environment variable will be ignored by srun if --gpus is specified.
* When using --get-user-env explicitly or implicitly, check if PID or mnt namespaces are disabled and fall back to old logic that does not rely on them when they are not available.
* Removed non-functional option SLURM_PROLOG_CPU_MASK from TaskProlog which was used to reset the affinity of a task based on the mask given.
* slurmrestd - Support passing of '-d latest' to load latest version of data_parser plugin.
* sacct,sacctmgr,scontrol,sdiag,sinfo,squeue,sshare - Change response to '--json=list' or '--yaml=list' to send list of plugins to stdout and descriptive header to stderr to allow for easier parsing.
* slurmrestd - Change response to '-d list', '-a list' or '-s list' to send list of plugins to stdout and descriptive header to stderr to allow for easier parsing.
* sacct,sacctmgr,scontrol,sdiag,sinfo,squeue,sshare,slurmrestd - Avoid crash when loading data_parser plugins fail due to NULL dereference.
* Add autodetected gpus to the output of slurmd -C
* Remove burst_buffer/lua call slurm.job_info_to_string().
* Add SchedulerParameters=bf_allow_magnetic_slot option. It allows jobs in magnetic reservations to be planned by backfill scheduler.
* slurmrestd - Refuse to run as root, SlurmUser, and nobody(99).
* openapi/slurmctld - Revert regression that caused signaling jobs to cancel entire job arrays instead of job array tasks:  'DELETE /slurm/v0.0.40/{job_id}'  'DELETE /slurm/v0.0.41/{job_id}'  'DELETE /slurm/v0.0.42/{job_id}'
* openapi/slurmctld - Support more formats for {job_id} including job steps:  'DELETE /slurm/v0.0.40/{job_id}'  'DELETE /slurm/v0.0.41/{job_id}'  'DELETE /slurm/v0.0.42/{job_id}'
* Alter scheduling of jobs at submission time to consider job submission time and job id. This makes it so that that interactive jobs aren't allocated resources before batch jobs when they have the same priority at submit time.
* Fix multi-cluster submissions with differing Switch plugins.
* slurmrestd - Change +prefer_refs flag to default in data_parser/v0.0.42 plugin. Add +minimize_refs flag to inline single referenced schemas in the OpenAPI schema. This sets the default OpenAPI schema generation behavior of data_parser/v0.0.42 to match v0.0.41+prefer_refs and v0.0.40 (without flags).
* Fix LaunchParameters=batch_step_set_cpu_freq.
* Clearer seff warning message for running jobs.
* data_parser/v0.0.42 - Rename JOB_INFO field "minimum_switches" to "required_switches" to reflect the actual behavior.
* data_parser/v0.0.42 - Rename ACCOUNT_CONDITION field "association" to "association" to fix typo.
* cgroup/v2 - fix cgroup cleanup when running inside a container without write permissions to /sys/fs/cgroup.
* cgroup/v2 - fix accounting of swap events detection.
* Fix gathering MaxRSS for jobs that run shorter than two jobacctgather intervals. Get the metrics from cgroups memory.peak or memory.max_usage_in_bytes where available.
* openapi/slurmctld - Set complex number support for the following fields:  .shares[][].fairshare.factor  .shares[][].fairshare.level for endpoints:  'GET /slurm/v0.0.42/shares' and for commands:  sshare --json  sshare --yaml
* data_parser/v0.0.42 - Avoid dumping "Infinity" for NO_VAL tagged "number" fields.
* Add TopologyParam=TopoMaxSizeUnroll=# to allow --nodes=<min>-<max> for topology/block.
* sacct - Respect --noheader for --batch-script and --env-vars.
* sacct - Remove extra newline in output from --batch-script and --env-vars.
* Add "sacctmgr ping" command to query status of slurmdbd.
* Generate an error message when a NodeSet name conflicts with a NodeName, and prevent the controller from starting if such a conflict exists.
* slurmd - properly detect slurmd restarts in the energy gathering logic which caused bad numbers in accounting.
* sackd - retry fetching slurm configs indefinitely in configless mode.
* job_submit/lua - Add "assoc_qos" attribute to job_desc to display all potential QOS's for a job's association.
* job_submit/lua - Add slurm.get_qos_priority() function to retrieve the given QOS's priority.
* sbcast - Add --nodelist option to specify where files are transmitted to
* sbcast - Add --no-allocation option to transmit files to nodes outside of a job allocation
* Add DataParserParameters slurm.conf parameter to allow setting default value for CLI --json and --yaml arguments.
* seff - improve step's max memory consumption report by using TresUsageInTot and TresUsageInAve instead of overestimating the values.
* Enable RPC queueing for REQUEST_KILL_JOBS, which is used when scancel is executed with --ctld flag.
* slurmdbd - Add -u option. This is used to determine if restarting the DBD will result in database conversion.
* Fix srun inside an salloc in a federated cluster when using IPv6.
* Calculate the forwarding timeouts according to tree depth rather than node count / tree width for each level. Fixes race conditions with same timeouts between two consecutive node levels.
* Add ability to submit jobs with multiple QOS.
* Fix difference in behavior when swapping partition order in job submission.
* Improve PLANNED state detection for mixed nodes and updating state before yielding backfill locks.
* Always consider partition priority tiers when deciding to try scheduling jobs on submit.
* Prevent starting jobs without reservations on submit when there are pending jobs with reservations that have flags FLEX or ANY_NODES that can be scheduled on overlapping nodes.
* Prevent jobs that request both high and low priority tier partitions from starting on submit in lower priority tier partitions if it could delay pending jobs in higher priority tier partitions.
* scontrol - Wait for slurmctld to start reconfigure in foreground mode before returning.
* Improve reconfigure handling on Linux to only close open file descriptors to avoid long delays on systems with large RLIMIT_NOFILE settings.
* salloc - Removed --get-user-env option.
* Removed the instant on feature from switch/hpe_slingshot.
* Hardware collectives in switch/hpe_slingshot now requires enable_stepmgr.
* Allow backfill to plan jobs on nodes currently being used by exclusive user or mcs jobs.
* Avoid miscaching IPv6 address to hostname lookups that could have caused logs to have the incorrect hostname.
* scontrol - Add --json/--yaml support to listpids
* scontrol - Add liststeps
* scontrol - Add listjobs
* slurmrestd - Avoid connection to slurmdbd for the following endpoints:  GET /slurm/v0.0.42/jobs  GET /slurm/v0.0.42/job/{job_id}
* slurmctld - Changed incoming RPC handling to dedicated thread pool.
* job_container/tmpfs - Add EntireStepInNS option that will place the slurmstepd process within the constructed namespace directly.
* scontrol show topo - Show aggregated block sizes when using topology/block.
* slurmrestd - Add more descriptive HTTP status for authentication failure and connectivity errors with controller.
* slurmrestd - Improve reporting errors from slurmctld for job queries:  'GET /slurm/v0.0.41/{job_id}'  'GET /slurm/v0.0.41/jobs/'
* Avoid rejecting a step request that needs fewer gres than nodes in the job allocation.
* slurmrestd - Tag the never populated '.jobs[].pid' field as deprecated for the following endpoints:  'GET /slurm/v0.0.42/{job_id}'  'GET /slurm/v0.0.42/jobs/'
* scontrol,squeue - Tag the never populated '.jobs[].pid' field as deprecated for the following:  'scontrol show jobs --json'  'scontrol show jobs --yaml'  'scontrol show job ${JOB_ID} --json'  'scontrol show job ${JOB_ID} --yaml'  'squeue --json'  'squeue --yaml'
* data_parser v0.0.42 - fix timestamp parsing regression introduced in in v0.0.40 (eaf3b6631f), parsing of non iso 8601 style timestamps
* cgroup/v2 will detect some special container and namespaced setups and will work with it.
* Support IPv6 in configless mode.
* Add SlurmctldParamters=ignore_constraint_validation to ignore constraint/feature validation at submission.
* slurmrestd - Set '.pings[].mode' field as deprecated in the following endpoints:  'GET /slurm/v0.0.42/ping'
* scontrol - Set '.pings[].mode' field as deprecated in the following commands:  'scontrol ping --json'  'scontrol ping --yaml'
* slurmrestd - Set '.pings[].pinged' field as deprecated in the following endpoints:  'GET /slurm/v0.0.42/ping'
* scontrol - Set '.pings[].pinged' field as deprecated in the following commands:  'scontrol ping --json'  'scontrol ping --yaml'
* slurmrestd - Add '.pings[].primary' field to the following endpoints:  'GET /slurm/v0.0.42/ping'
* scontrol - Add '.pings[].primary' field to the following commands:  'scontrol ping --json'  'scontrol ping --yaml'
* slurmrestd - Add '.pings[].responding' field to the following endpoints:  'GET /slurm/v0.0.42/ping'
* scontrol - Add '.pings[].responding' field to the following commands:  'scontrol ping --json'  'scontrol ping --yaml'
* Prevent jobs without reservations from delaying jobs in reservations with flags FLEX or ANY_NODES in the main scheduler.
* Fix allowing to ask for multiple different types of TRES when one of them has a value of 0.
* slurmctld - Add a grace period to ensure the agent retry queue is properly flushed during shutdown.
* Removed src/slurmrestd/plugins/openapi/slurmdbd/openapi.json from source repository. slurmrest should always be used to generate a new OpenAPI schema (aka openapi.json or openapi.yaml) after compilation.
* mpi/pmix - Fix potential deadlock and races with het jobs, and fix potential memory and FDs leaks.
* Fix jobs with --gpus being rejected in some edge cases for partitions where not all nodes have the same amount of GPUs and CPUs configured.
* In an extra constraints expression in a job request, do not allow an empty string for a key or value.
* In an extra constraints expression in a job request, fix validation that requests are separated by boolean operators.
* Add TaskPluginParam=OOMKillStep to kill the step as a whole when one task OOMs.
* Fix scontrol show conf not showing all TaskPluginParam elements.
* slurmrestd - Add fields '.job.oom_kill_step' '.jobs[].oom_kill_step' to 'POST /slurm/v0.0.42/job/submit' and 'POST /slurm/v0.0.42/job/allocate'.
* Improve performance for _will_run_test().
* Add SchedulerParameters=bf_topopt_enable option to enable experimental hook to control backfill.
* If a step fails to launch under certain conditions, set the step's state to NODE_FAIL.
* sched/backfill - Fix certain situations where a job would not get a planned time, which could lead to it being delayed by lower priority jobs.
* slurmrestd - Dump JSON "null" instead of "{}" (empty object) for non-required fields in objects to avoid client compatibility issues for v0.0.42 version tagged endpoints.
* sacct,sacctmgr,scontrol,sdiag,sinfo,squeue,sshare - Dump "null" instead "{}" (empty object) for non-required fields in objects to avoid client compatibility issues when run with --json or --yaml.
