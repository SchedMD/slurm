## Changes in Slurm 24.05.8

* Testsuite - fix python test 130_2.
* Fix security issue where a coordinator could add a user with elevated privileges. CVE-2025-43904.

## Changes in Slurm 24.05.7

* Fix slurmctld crash when after updating a reservation with an empty nodelist. The crash could occur after restarting slurmctld, or if downing/draining a node in the reservation with the REPLACE or REPLACE_DOWN flag.
* Fix jobs being scheduled on higher weighted powered down nodes.
* Fix memory leak when RestrictedCoresPerGPU is enabled.
* Prevent slurmctld deadlock in the assoc mgr.

## Changes in Slurm 24.05.6

* data_parser/v0.0.40 - Prevent a segfault in the slurmrestd when dumping data with v0.0.40+complex data parser.
* Fix sattach when using auth/slurm.
* scrun - Add support '--all' argument for kill subcommand.
* Fix performance regression while packing larger RPCs.
* Fix crash and issues evaluating job's suitability for running in nodes with already suspended job(s) there.
* Fixed a job requeuing issue that merged job entries into the same SLUID when all nodes in a job failed simultaneously.
* switch/hpe_slingshot - Fix compatibility with newer cxi drivers, specifically when specifying disable_rdzv_get.
* Add ABORT_ON_FATAL environment variable to capture a backtrace from any fatal() message.

## Changes in Slurm 24.05.5

* Fix issue signaling cron jobs resulting in unintended requeues.
* Fix slurmctld memory leak in implementation of HealthCheckNodeState=CYCLE.
* job_container/tmpfs - Fix SLURM_CONF env variable not being properly set.
* sched/backfill - Fix job's time_limit being overwritten by time_min for job arrays in some situations.
* RoutePart - fix segfault from incorrect memory allocation when node doesn't exist in any partition.
* slurmctld - Fix crash when a job is evaluated for a reservation after removal of a dynamic node.
* gpu/nvml - Attempt loading libnvidia-ml.so.1 as a fallback for failure in loading libnvidia-ml.so.
* slurmrestd - Fix populating non-required object fields of objects as '{}' in JSON/YAML instead of 'null' causing compiled OpenAPI clients to reject the response to 'GET /slurm/v0.0.40/jobs' due to validation failure of '.jobs[].job_resources'.
* Fix sstat/sattach protocol errors for steps on higher version slurmd's (regressions since 20.11.0rc1 and 16.05.1rc1 respectively).
* slurmd - Avoid a crash when starting slurmd version 24.05 with SlurmdSpoolDir files that have been upgraded to a newer major version of Slurm. Log warnings instead.
* Fix race condition in stepmgr step completion handling.
* Fix slurmctld segfault with stepmgr and MpiParams when running a job array.
* Fix requeued jobs keeping their priority until the decay thread happens.
* slurmctld - Fix crash and possible split brain issue if the backup controller handles an scontrol reconfigure while in control before the primary resumes operation.
* Fix stepmgr not getting dynamic node addrs from the controller
* stepmgr - avoid "Unexpected missing socket" errors.
* Fix `scontrol show steps` with dynamic stepmgr
* Support IPv6 in configless mode.

## Changes in Slurm 24.05.4

* Fix generic int sort functions.
* Fix user look up using possible unrealized uid in the dbd.
* Fix FreeBSD compile issue with tls/none plugin.
* slurmrestd - Fix regressions that allowed slurmrestd to be run as SlurmUser when SlurmUser was not root.
* mpi/pmix fix race conditions with het jobs at step start/end which could make srun to hang.
* Fix not showing some SelectTypeParameters in scontrol show config.
* Avoid assert when dumping removed certain fields in JSON/YAML.
* Improve how shards are scheduled with affinity in mind.
* Fix MaxJobsAccruePU not being respected when MaxJobsAccruePA is set in the same QOS.
* Prevent backfill from planning jobs that use overlapping resources for the same time slot if the job's time limit is less than bf_resolution.
* Fix memory leak when requesting typed gres and --[cpus|mem]-per-gpu.
* Prevent backfill from breaking out due to "system state changed" every 30 seconds if reservations use REPLACE or REPLACE_DOWN flags.
* slurmrestd - Make sure that scheduler_unset parameter defaults to true even when the following flags are also set: show_duplicates, skip_steps, disable_truncate_usage_time, run_away_jobs, whole_hetjob, disable_whole_hetjob, disable_wait_for_result, usage_time_as_submit_time, show_batch_script, and or show_job_environment. Additionally, always make sure show_duplicates and disable_truncate_usage_time default to true when the following flags are also set: scheduler_unset, scheduled_on_submit, scheduled_by_main, scheduled_by_backfill, and or job_started. This effects the following endpoints:  'GET /slurmdb/v0.0.40/jobs'  'GET /slurmdb/v0.0.41/jobs'
* Ignore --json and --yaml options for scontrol show config to prevent mixing output types.
* Fix not considering nodes in reservations with Maintenance or Overlap flags when creating new reservations with nodecnt or when they replace down nodes.
* Fix suspending/resuming steps running under a 23.02 slurmstepd process.
* Fix options like sprio --me and squeue --me for users with a uid greater than 2147483647.
* fatal() if BlockSizes=0. This value is invalid and would otherwise cause the slurmctld to crash.
* sacctmgr - Fix issue where clearing out a preemption list using preempt='' would cause the given qos to no longer be preempt-able until set again.
* Fix stepmgr creating job steps concurrently.
* data_parser/v0.0.40 - Avoid dumping "Infinity" for NO_VAL tagged "number" fields.
* data_parser/v0.0.41 - Avoid dumping "Infinity" for NO_VAL tagged "number" fields.
* slurmctld - Fix a potential leak while updating a reservation.
* slurmctld - Fix state save with reservation flags when a update fails.
* Fix reservation update issues with parameters Accounts and Users, when using +/- signs.
* slurmrestd - Don't dump warning on empty wckeys in:  'GET /slurmdb/v0.0.40/config'  'GET /slurmdb/v0.0.41/config'
* Fix slurmd possibly leaving zombie processes on start up in configless when the initial attempt to fetch the config fails.
* Fix crash when trying to drain a non-existing node (possibly deleted before).
* slurmctld - fix segfault when calculating limit decay for jobs with an invalid association.
* Fix IPMI energy gathering with multiple sensors.
* data_parser/v0.0.39 - Remove xassert requiring errors and warnings to have a source string.
* slurmrestd - Prevent potential segfault when there is an error parsing an array field which could lead to a double xfree. This applies to several endpoints in data_parser v0.0.39, v0.0.40 and v0.0.41.
* scancel - Fix a regression from 23.11.6 where using both the --ctld and --sibling options would cancel the federated job on all clusters instead of only the cluster(s) specified by --sibling.
* accounting_storage/mysql - Fix bug when removing an association specified with an empty partition.
* Fix setting multiple partition state restore on a job correctly.
* Fix difference in behavior when swapping partition order in job submission.
* Fix security issue in stepmgr that could permit an attacker to execute processes under other users' jobs. CVE-2024-48936.

## Changes in Slurm 24.05.3

* data_parser/v0.0.40 - Added field descriptions
* slurmrestd - Avoid creating new slurmdbd connection per request to '* /slurm/slurmctld/*/*' endpoints.
* Fix compilation issue with switch/hpe_slingshot plugin.
* Fix gres per task allocation with threads-per-core.
* data_parser/v0.0.41 - Added field descriptions
* slurmrestd - Change back generated OpenAPI schema for `DELETE /slurm/v0.0.40/jobs/` to RequestBody instead of using parameters for request. slurmrestd will continue accept endpoint requests via RequestBody or HTTP query.
* topology/tree - Fix issues with switch distance optimization.
* Fix potential segfault of secondary slurmctld when falling back to the primary when running with a JobComp plugin.
* Enable --json/--yaml=v0.0.39 options on client commands to dump data using data_parser/v0.0.39 instead or outputting nothing.
* switch/hpe_slingshot - Fix issue that could result in a 0 length state file.
* Fix unnecessary message protocol downgrade for unregistered nodes.
* Fix unnecessarily packing alias addrs when terminating jobs with a mix of non-cloud/dynamic nodes and powered down cloud/dynamic nodes.
* accounting_storage/mysql - Fix issue when deleting a qos that could remove too many commas from the qos and/or delta_qos fields of the assoc table.
* slurmctld - Fix memory leak when using RestrictedCoresPerGPU.
* Fix allowing access to reservations without MaxStartDelay set.
* Fix regression introduced in 24.05.0rc1 breaking srun --send-libs parsing.
* Fix slurmd vsize memory leak when using job submission/allocation commands that implicitly or explicitly use --get-user-env.
* slurmd - Fix node going into invalid state when using CPUSpecList and setting CPUs to the # of cores on a multithreaded node
* Fix reboot asap nodes being considered in backfill after a restart.
* Fix --clusters/-M queries for clusters outside of a federation when fed_display is configured.
* Fix scontrol allowing updating job with bad cpus-per-task value.
* sattach - Fix regression from 24.05.2 security fix leading to crash.
* mpi/pmix - Fix assertion when built under --enable-debug.

## Changes in Slurm 24.05.2

* Fix energy gathering rpc counter underflow in _rpc_acct_gather_energy when more than 10 threads try to get energy at the same time. This prevented the possibility to get energy from slurmd by any step until slurmd was restarted, so losing energy accounting metrics in the node.
* accounting_storage/mysql - Fix issue where new user with wckey did not have a default wckey sent to the slurmctld.
* slurmrestd - Prevent slurmrestd segfault when handling the following endpoints when none of the optional parameters are specified:  'DELETE /slurm/v0.0.40/jobs'  'DELETE /slurm/v0.0.41/jobs'  'GET /slurm/v0.0.40/shares'  'GET /slurm/v0.0.41/shares'  'GET /slurmdb/v0.0.40/instance'  'GET /slurmdb/v0.0.41/instance'  'GET /slurmdb/v0.0.40/instances'  'GET /slurmdb/v0.0.41/instances'  'POST /slurm/v0.0.40/job/{job_id}'  'POST /slurm/v0.0.41/job/{job_id}'
* Fix IPMI energy gathering when no IPMIPowerSensors are specified in acct_gather.conf. This situation resulted in an accounted energy of 0 for job steps.
* Fix a minor memory leak in slurmctld when updating a job dependency.
* scontrol,squeue - Fix regression that caused incorrect values for multisocket nodes at '.jobs[].job_resources.nodes.allocation' for 'scontrol show jobs --(json|yaml)' and 'squeue --(json|yaml)'.
* slurmrestd - Fix regression that caused incorrect values for multisocket nodes at '.jobs[].job_resources.nodes.allocation' to be dumped with endpoints:  'GET /slurm/v0.0.41/job/{job_id}'  'GET /slurm/v0.0.41/jobs'
* jobcomp/filetxt - Fix truncation of job record lines > 1024 characters.
* Fixed regression that prevented compilation on FreeBSD hosts.
* switch/hpe_slingshot - Drain node on failure to delete CXI services.
* Fix a performance regression from 23.11.0 in cpu frequency handling when no CpuFreqDef is defined.
* Fix one-task-per-sharing not working across multiple nodes.
* Fix inconsistent number of cpus when creating a reservation using the TRESPerNode option.
* data_parser/v0.0.40+ - Fix job state parsing which could break filtering.
* Prevent cpus-per-task to be modified in jobs where a -c value has been explicitly specified and the requested memory constraints implicitly increase the number of CPUs to allocate.
* slurmrestd - Fix regression where args '-s v0.0.39,dbv0.0.39' and '-d v0.0.39' would result in 'GET /openapi/v3' not registering as a valid possible query resulting in 404 errors.
* slurmrestd - Fix memory leak for dbv0.0.39 jobs query which occurred if the query parameters specified account, association, cluster, constraints, format, groups, job_name, partition, qos, reason, reservation, state, users, or wckey. This affects the following endpoints:  'GET /slurmdb/v0.0.39/jobs'
* slurmrestd - In the case the slurmdbd does not respond to a persistent connection init message, prevent the closed fd from being used, and instead emit an error or warning depending on if the connection was required.
* Fix 24.05.0 regression that caused the slurmdbd not to send back an error message if there is an error initializing a persistent connection.
* Reduce latency of forwarded x11 packets.
* Add "curr_dependency" (representing the current dependency of the job) and "orig_dependency" (representing the original requested dependency of the job) fields to the job record in job_submit.lua (for job update) and jobcomp.lua.
* Fix potential segfault of slurmctld configured with SlurmctldParameters=enable_rpc_queue from happening on reconfigure.
* Fix potential segfault of slurmctld on its shutdown when rate limiting is enabled.
* slurmrestd - Fix missing job environment for SLURM_JOB_NAME, SLURM_OPEN_MODE, SLURM_JOB_DEPENDENCY, SLURM_PROFILE, SLURM_ACCTG_FREQ, SLURM_NETWORK and SLURM_CPU_FREQ_REQ to match sbatch.
* Add missing bash-completions dependency to slurm-smd-client debian package.
* Fix bash-completions installation in debian packages.
* Fix GRES environment variable indices being incorrect when only using a subset of all GPUs on a node and the --gres-flags=allow-task-sharing option
* Add missing mariadb/mysql client package dependency to debian package.
* Fail the debian package build early if mysql cannot be found.
* Prevent scontrol from segfaulting when requesting scontrol show reservation --json or --yaml if there is an error retrieving reservations from the slurmctld.
* switch/hpe_slingshot - Fix security issue around managing VNI access. CVE-2024-42511.
* switch/nvidia_imex - Fix security issue managing IMEX channel access. CVE-2024-42511.
* switch/nvidia_imex - Allow for compatibility with job_container/tmpfs.

## Changes in Slurm 24.05.1

* Fix slurmctld and slurmdbd potentially stopping instead of performing a logrotate when receiving SIGUSR2 when using auth/slurm.
* switch/hpe_slingshot - Fix slurmctld crash when upgrading from 23.02.
* Fix "Could not find group" errors from validate_group() when using AllowGroups with large /etc/group files.
* Prevent an assertion in debugging builds when triggering log rotation in a backup slurmctld.
* Add AccountingStoreFlags=no_stdio which allows to not record the stdio paths of the job when set.
* slurmrestd - Prevent a slurmrestd segfault when parsing the crontab field, which was never usable. Now it explicitly ignores the value and emits a warning if it is used for the following endpoints:  'POST /slurm/v0.0.39/job/{job_id}'  'POST /slurm/v0.0.39/job/submit'  'POST /slurm/v0.0.40/job/{job_id}'  'POST /slurm/v0.0.40/job/submit'  'POST /slurm/v0.0.41/job/{job_id}'  'POST /slurm/v0.0.41/job/submit'  'POST /slurm/v0.0.41/job/allocate'
* mpi/pmi2 - Fix communication issue leading to task launch failure with "invalid kvs seq from node".
* Fix getting user environment when using sbatch with "--get-user-env" or "--export=" when there is a user profile script that reads /proc.
* Prevent slurmd from crashing if acct_gather_energy/gpu is configured but GresTypes is not configured.
* Do not log the following errors when AcctGatherEnergyType plugins are used but a node does not have or cannot find sensors: "error: _get_joules_task: can't get info from slurmd" "error: slurm_get_node_energy: Zero Bytes were transmitted or received" However, the following error will continue to be logged: "error: Can't get energy data. No power sensors are available. Try later"
* sbatch, srun - Set SLURM_NETWORK environment variable if --network is set.
* Fix cloud nodes not being able to forward to nodes that restarted with new IP addresses.
* Fix cwd not being set correctly when running a SPANK plugin with a spank_user_init() hook and the new "contain_spank" option set.
* slurmctld - Avoid deadlock during shutdown when auth/slurm is active.
* Fix segfault in slurmctld with topology/block.
* sacct - Fix printing of job group for job steps.
* scrun - Log when an invalid environment variable causes the job submission to be rejected.
* accounting_storage/mysql - Fix problem where listing or modifying an association when specifying a qos list could hang or take a very long time.
* gpu/nvml - Fix gpuutil/gpumem only tracking last GPU in step. Now, gpuutil/gpumem will record sums of all GPUS in the step.
* Fix error in scrontab jobs when using slurm.conf:PropagatePrioProcess=1.
* Fix slurmctld crash on a batch job submission with "--nodes 0,...".
* Fix dynamic IP address fanout forwarding when using auth/slurm.
* Restrict listening sockets in the mpi/pmix plugin and sattach to the SrunPortRange.
* slurmrestd - Limit mime types returned from query to 'GET /openapi/v3' to only return one mime type per serializer plugin to fix issues with OpenAPI client generators that are unable to handle multiple mime type aliases.
* Fix many commands possibly reporting an "Unexpected Message Received" when in reality the connection timed out.
* Prevent slurmctld from starting if there is not a json serializer present and the extra_constraints feature is enabled.
* Fix heterogeneous job components not being signaled with scancel --ctld and 'DELETE slurm/v0.0.40/jobs' if the job ids are not explicitly given, the heterogeneous job components match the given filters, and the heterogeneous job leader does not match the given filters.
* Fix regression from 23.02 impeding job licenses from being cleared.
* Move error to log_flag which made _get_joules_task error to be logged to the user when too many rpcs were queued in slurmd for gathering energy.
* For scancel --ctld and the associated rest api endpoints:  'DELETE /slurm/v0.0.40/jobs'  'DELETE /slurm/v0.0.41/jobs' Fix canceling the final array task in a job array when the task is pending and all array tasks have been split into separate job records. Previously this task was not canceled.
* Fix power_save operation after recovering from a failed reconfigure.
* slurmctld - Skip removing the pidfile when running under systemd. In that situation it is never created in the first place.
* Fix issue where altering the flags on a Slurm account (UsersAreCoords) several limits on the account's association would be set to 0 in Slurm's internal cache.
* Fix memory leak in the controller when relaying stepmgr step accounting to the dbd.
* Fix segfault when submitting stepmgr jobs within an existing allocation.
* Added "disable_slurm_hydra_bootstrap" as a possible MpiParams parameter in slurm.conf. Using this will disable env variable injection to allocations for the following variables: I_MPI_HYDRA_BOOTSTRAP, I_MPI_HYDRA_BOOTSTRAP_EXEC_EXTRA_ARGS, HYDRA_BOOTSTRAP, HYDRA_LAUNCHER_EXTRA_ARGS.
* scrun - Delay shutdown until after start requested. This caused scrun to never start or shutdown and hung forever when using --tty.
* Fix backup slurmctld potentially not running the agent when taking over as the primary controller.
* Fix primary controller not running the agent when a reconfigure of the slurmctld fails.
* slurmd - fix premature timeout waiting for REQUEST_LAUNCH_PROLOG with large array jobs causing node to drain.
* jobcomp/{elasticsearch,kafka} - Avoid sending fields with invalid date/time.
* jobcomp/elasticsearch - Fix slurmctld memory leak from curl usage
* acct_gather_profile/influxdb - Fix slurmstepd memory leak from curl usage
* Fix 24.05.0 regression not deleting job hash dirs after MinJobAge.
* Fix filtering arguments being ignored when using squeue --json.
* switch/nvidia_imex - Move setup call after spank_init() to allow namespace manipulation within the SPANK plugin.
* switch/nvidia_imex - Skip plugin operation if nvidia-caps-imex-channels device is not present rather than preventing slurmd from starting.
* switch/nvidia_imex - Skip plugin operation if job_container/tmpfs is configured due to incompatibility.
* switch/nvidia_imex - Remove any pre-existing channels when slurmd starts.
* rpc_queue - Add support for an optional rpc_queue.yaml configuration file.
* slurmrestd - Add new +prefer_refs flag to data_parser/v0.0.41 plugin. This flag will avoid inlining single referenced schemas in the OpenAPI schema.

## Changes in Slurm 24.05.0

* Fix regression in rc1 causing power_save thread to spin continuously.
* Improve ctld_relay shutdown sequence.
* Fixed 'make distclean' behavior for contribs/perlapi.
* slurmrestd - Avoid ignoring numerical only endpoints during startup with older libjson-c due to type parsing mismatching.
* Reject non-stepmgr job allocations requesting --resv-ports from the ctld.
* slurmrestd - Add fields '.job.resv_ports' '.jobs[].resv_ports' to 'POST /slurm/v0.0.41/job/submit' and 'POST /slurm/v0.0.41/job/allocate'.
* slurmstepd - Fix crash when cleaning up on shutdown with --enable-memory-leak-debug.
* Fix segfault in switch/hpe_slingshot plugin due to initialization sequence.
* scrun - Fix regression in rc1 that caused scrun to crash.
* Prevent unnecessary log statement when free'ing ports.
* Fix regression in rc1 causing communication problems when sending large responses from slurmctld.
* sreport - fix parsing of 'format=Planned' to prevent it from being misinterpreted as 'PlannedDown'. 'PlannedDown' is now also known as 'PLNDDown' to match what is printed as the column title.
* topology/block - Always return an error when the segment size does not match the system or job specification.
* Add previously missing timers for Prolog and Epilog scripts when RunInJob is set.
* Show an error when PrologFlags RunInJob and Serial are used together. PrologFlags=Serial is not compatible with how RunInJob operates.
* Fix memory leak on shutdown when using --enable-memory-leak-debug and freeing cons_tres node usage.
* Rename src/stepmgr/gres_ctld.[ch] to src/stepmgr/gres_stepmgr.[ch].
* Fix various cosmetic issues with states in sinfo.
* slurmrestd - Avoid crash due to associations query.
* Calculate a job's min_cpus with consideration to --cpus-per-gpu.
* Fix scancel request when specifying individual array tasks in combination with filtering options (in both regular and --interactive mode).
* Enable MaxStepCount in stepmgr.
* Enable AccountingStorageEnforce=nojobs,nosteps in stepmgr.
* Add AccountingStorageParameters=max_step_records to limit how many steps are recorded in the database for each job -- excluding batch, extern, and interactive steps.
* switch/hpe_slingshot - allocate VNIs on the controller for stepmgr jobs and pass to the stepmgr for steps to use.
* switch/hpe_slingshot - fix assertion when restarting the controller.
* switch/hpe_slingshot - fix calculation of free vnis when restarting the controller with running jobs.
* Improve default job reserve MPI port allocations that use overcommit or do not specify a task count for stepmgr enabled jobs.
* Fix a regression in rc1 resulting in scrun occasionally deadlocking when the --enable-memory-leak-debug configure option was used.
* topology/default - Prevent segfault in slurmctld on 'scontrol show topo'.
* slurmrestd - Avoid creating or requiring a connection to slurmdbd for the 'GET /openapi/v3' endpoint, fixing a regression in rc1.
* scrun - Fix setting and getting environment via SPANK plugins.
* sview - Fix nodes tab if a node has RestrictedCoresPerGPU configured.
* slurmrestd - Add --generate-openapi-spec argument.
* sview - Prevent segfault when retrieving slurmdbd configuration.
* Avoid canceling rejected heterogeneous jobs without job write lock.
* Fix slurmctld crash when reconfiguring with a PrologSlurmctld is running.
* Fix slurmctld crash after a job has been resized.

## Changes in Slurm 24.05.0rc1

* Make slurmstepd retry REQUEST_COMPLETE_BATCH_SCRIPT indefinitely.
* Always load serializer/json when using any data_parser plugins.
* slurmrestd - Reject single http query with multiple path requests.
* slurmrestd - Add time/planned field to slurmdb/v0.0.41/job/{job_id}.
* Improve Power Save's Resume/Suspend rate limiting.
* slurmrestd - Improve reliability under high memory pressure by closing connections instead of forcing a fatal exit due to lack of memory.
* data_parser/v0.0.41 - Avoid aborting when invoking a not implemented parser.
* data_parser/v0.0.41 - Fix how nice values are parsed for job submissions.
* data_parser/v0.0.41 - Fix regression where parsing error did not result in invalid request being rejected.
* Print an error message in 'scontrol reboot' when a node reboot request is ignored due to the current node state.
* squeue - Add "--notme" option.
* data_parser/v0.0.41 - change "association.id" to just include the int "id" rather than include redundant assoc info (cluster, user, partition, account) that's already included in the "association" object.
* data_parser/v0.0.41 - Improve parsing of numeric user id.
* data_parser/v0.0.41 - Improve parsing of numeric group id.
* slurmrestd - Generated openapi.json will only populate "deprecated" fields if true. False is the default value and does not require being present.
* slurmrestd - Populate missing "deprecated" fields in openapi.json.
* slurmrestd - Corrected deprecated fields in generated openapi.json not getting populated.
* slurmrestd - Generated openapi.json will have reduced number of "$ref" fields. Where there was only 1 reference for the schema, the "$ref" schema will be directly populated in place.
* slurmrestd - Rename *_NO_VAL schemas in generated openapi.json to have _struct and to pass along correct integer sizing when possible.
* slurmrestd - Correct description fields in generated openapi.json where descriptions were not present or too generic.
* Remove support for Cray XC ("cray_aries") systems.
* Prevent backup slurmctld from taking over if the heartbeat file is still being updated. Failure to ping may have been due to clock skew.
* serializer/yaml - Converted to new parsing interface in libyaml to improve parsing compatibility.
* Removed TopologyPlugin tree and dragonfly support from select/linear. If those topology plugins are desired please switch to select/cons_tres.
* Changed slurmrestd.service to only listen on TCP socket by default. Environments with existing drop-in units for the service may need further adjustments to work after upgrading.
* Fix how gres are allocated per job when using multiple gres types.
* Log an error when UnkillableStepTimeout is less than five times MessageTimeout.
* Avoid step gres dealloc count underflow errors after reconfiguring or restarting slurmctld.
* Fix controller not validating periodic dynamic future registrations.
* Fix dynamic future nodes registering as new node when specifying -N<name>.
* Fix sbcast (or srun --bcast) --send-libs when it is used multiple times in the same job. Previously, subsequent calls to sbcast --send-libs would overwrite the libraries for the first executable.
* Add support for sbcast --preserve when job_container/tmpfs configured (previously documented as unsupported).
* Changed the default value for UnkillableStepTimeout to 60 seconds or five times the value of MessageTimeout, whichever is greater.
* slurmctld - Check if --deadline has been reached and not satisfied on held jobs, otherwise they could remain without automatic cancellation until after the job is released.
* scrun/slurmrestd/sackd - Avoid closing all listening sockets when interrupted from signal such as SIGALRM. Normal shutdown remains unaffected.
* Remove systemd AbandonScope() logic for scope units as it is not needed.
* Fix GresUsed output from `scontrol show nodes --details` showing GRES types that are not configured on a node.
* slurmrestd - Fatal during start up when loading content plugin fails.
* slurmrestd - Reduce complexity in URL path matching.
* data_parser/v0.0.41 - Emit a warning instead of an error if a disabled parser is invoked.
* Federation - allow client command operation when slurmdbd is unavailable.
* Enforce mutual exclusivity of --systemd and -D when launching daemons
* slurmctld - remove -d option
* burst_buffer/lua - Trigger a burst_buffer event for strigger when the real_size function fails.
* burst_buffer/lua - Added two new hooks: slurm_bb_test_data_in and slurm_bb_test_data_out. The syntax and use of the new hooks are documented in etc/burst_buffer.lua.example. These are required to exist. slurmctld now checks on startup if the burst_buffer.lua script loads and contains all required hooks; slurmctld will exit with a fatal error if this is not successful. Added PollInterval to burst_buffer.conf. Removed the arbitrary limit of 512 copies of the script running simultaneously.
* sackd/slurmrestd/scrun - Avoid using empty string while logging unix socket connections from a listening connection.
* Fix 20 character username limit from 'sacctmgr show events'
* Log an error if UsePss or NoShare are configured with a plugin other than jobacct_gather/linux. In such case these parameters are ignored.
* helpers.conf - Added Flags=rebootless parameter allowing feature changes without rebooting compute nodes.
* scontrol - Add new subcommand 'power' for node power control.
* data_parser/v0.0.41 - Implement parser of distribution for /slurm/v0.0.41/job/submit.
* data_parser/v0.0.41 - Change distribution_plane_size field type from UINT16 to UINT16_NO_VAL for /slurm/v0.0.41/job/submit.
* topology/block - Replaced the BlockLevels with BlockSizes in topology.conf.
* Fix slurmd cgroup/v2 startup race with systemd and cgroupfs.
* Add SystemdTimeout= parameter in cgroup.conf.
* Add QOS limit MaxTRESRunMinsPerAccount.
* Add QOS limit MaxTRESRunMinsPerUser.
* jobcomp/{elasticsearch,kafka} - Send priority alongside the rest of fields.
* Add contain_spank option to SlurmdParameters. When set, spank_user_init(), spank_task_post_fork(), and spank_task_exit() will execute within the job_container/tmpfs plugin namespace.
* Update job reason appropriately when bf_licenses is used.
* slurmrestd - Tagged `script` field as deprecated in 'POST /slurm/v0.0.41/job/submit' in anticipation of removal in future OpenAPI plugin versions.
* Fix salloc/sbatch/srun crashing with certain invalid nodelist requests.
* Optimize jobacctgather by not iterating every time over pids that have already finished.
* Remote SPANK callbacks invoked by srun get called once instead of twice.
* auth/slurm - Support multiple keys through slurm.jwks.
* sched/backfill - Fix issue with bf_continue where a job partition request could be incorrectly reset back to a partition that is no longer specified after a job partition update processed during a lock yield time window.
* slurmrestd - Explicitly set process as dumpable (and ptrace-able) at startup for systems where suid_dumpable is not 2.
* slurmrestd - Tag all /slurm/v0.0.39/ and /slurmdb/v0.0.39/ endpoints as deprecated in anticipation of removal in Slurm 24.11.
* Add ELIGIBLE environment variable to jobcomp/script plugin.
* slurmrestd,sackd,scrun - Improve outgoing data efficiency using non-contiguous write support in kernel.
* sackd - Add support for SACKD_DEBUG, SACKD_STDERR_DEBUG, and SACKD_SYSLOG_DEBUG environment variables to control logging.
* mpi/pmi2 - PMI_process_mapping values have been adapted for executions where arbitrary distribution/SLURM_HOSTFILE is used. Now it can take into account multiple instances of the same node inside SLURM_HOSTFILE.
* Avoid wrong limit oriented (i.e. QosMaxGresPer*) job pending reason for jobs actually pending on Resources when GPUs are requested per job.
* Fix --ntasks-per-node not being treated as a max count of tasks per node when used in combination with --ntasks. --ntasks option will now take precedence as it is documented.
* Accept X11 cookies that do not have a display number associated with it.
* Always use the QOS name for SLURM_JOB_QOS environment variables. Previously the batch environment would use the description field, which was usually equivalent to the name.
* slurmrestd - Add "CRON_JOBS" as possible flag value to the following:  'DELETE /slurm/v0.0.40/jobs' flags field.  'DELETE /slurm/v0.0.41/jobs' flags field.  'DELETE /slurm/v0.0.40/job/{job_id}?flags=' flags query parameter.  'DELETE /slurm/v0.0.41/job/{job_id}?flags=' flags query parameter.
* Fix ScronParameters=explicit_scancel when using the rest api DELETE jobs query: if the CRON_JOBS flag is not used then cron jobs will not be cancelled. The NO_CRON_JOBS flag is ignored in v0.0.40 and removed in v0.0.41.
* Pass multi-partition job priorities to job for squeue to display.
* cgroup/v2 - Require dbus-1 version >= 1.11.16.
* Add RestrictedCoresPerGPU configuration option.
* Fix how ntasks is inferred from --cpus-per-task when using --nodes, --threads-per-core, or --hint=nomultithread.
* For PreemptMode=CANCEL and PreemptMode=REQUEUE assume that job signalled for GraceTime was preempted.
* slurmd - Retry fetching configs indefinitely during startup.
* Fix SPANK options not bing sent to remote context when --export was used.
* slurmrestd - Attempt to automatically convert enumerated string arrays with incoming non-string values into strings. Add warning when incoming value for enumerated string arrays can not be converted to string and silently ignore instead of rejecting entire request.
* slurmrestd - Require `user` and `association_condition` fields to be populated for requests to 'POST /slurmdb/v0.0.41/users_association'.
* Allow NodeSet names to be used in SuspendExcNodes.
* SuspendExcNodes=<nodes>:N now counts allocated nodes in N. The first N powered up nodes in <nodes> are protected from being suspended.
* Add SlurmctldParameters=max_powered_nodes=N, which prevents powering up nodes after the max is reached.
* Store output, error and input paths in the database and make them available in accounting tools.
* slurmrestd - Add 'POST /slurm/v0.0.41/job/allocate' endpoint.
* Fix issues related to the extern step getting killed before other steps. This includes the job_containter/tmpfs plugin not cleaning up.
* Add USER_DELETE reservation flag to allow users with access to a reservation to delete it.
* Add CgroupPlugin=disabled to disable any interaction with Cgroups.
* slurmrestd - Add "STEPMGR_ENABLED" as possible flag value to the following:  'GET /slurm/v0.0.41/jobs' flags field.  'GET /slurm/v0.0.41/job/{job_id}' flags query parameter.
* scontrol,squeue - Added possible flags "STEPMGR_ENABLED" to '.jobs[].flags' for 'scontrol show jobs --{json|yaml}' and 'squeue --{json|yaml}' responses.
* Add SlurmctldParameters=enable_stepmgr to enable step management through the slurmstepd instead of the controller.
* Avoid slurmstepd infinite loop waiting for tasks termination.
* Fix logging of JSON/YAML values in some messages where nothing would be printed as the value instead of the actual JSONified version of the parsed string.
* slurmrestd,sackd,scrun - Improve logic around handling kernel provided buffer size of incoming data in files/sockets/pipes to avoid crashes.
* Add --segment to job allocation to be used in topology/block.
* Add --exclusive=topo for use with topology/block.
* Add ExclusiveTopo to a partition definition in slurm.conf.
* Add new 'BLOCKED' state to a node.
* Account coordinators may not increase association job limits above parent ones
* Account coordinators can now suspend/resume jobs owned by member users.
* Add DisableCoordDBD slurmdbd configuration parameter to disable the coordinator status in all slurmdbd interactions.
* slurmrestd - Added possible flags "WithAssociations" and "WithCoordinators" to `.accounts[].flags` for "GET /slurmdb/v0.0.41/accounts/" and "POST /slurmdb/v0.0.41/accounts/" endpoints.
* sacctmgr - Added possible flags "WithAssociations" and "WithCoordinators" to `.accounts[].flags` for `sacctmgr show accounts --{json|yaml}` response.
* slurmrestd - Rename URL query parameter "with_assocs" to "WithAssociations" for "GET /slurmdb/v0.0.41/accounts?WithAssociations".
* slurmrestd - Rename URL query parameter "with_coords" to "WithCoordinators" for "GET /slurmdb/v0.0.41/accounts?WithCoordinators".
* slurmrestd - Rename URL query parameter "with_deleted" to "deleted" for "GET /slurmdb/v0.0.41/accounts?deleted".
* slurmrestd - Added possible flags "RemoveUsersAreCoords" and "UsersAreCoords" to `.accounts[].flags` for "GET /slurmdb/v0.0.41/accounts/" and "POST /slurmdb/v0.0.41/accounts/" endpoints.
* sacctmgr - Added possible flags "RemoveUsersAreCoords" and "UsersAreCoords" to `.accounts[].flags` for `sacctmgr show accounts --{json|yaml}` response.
* slurmrestd - Add URL query parameter "UsersAreCoords" and "RemoveUsersAreCoords" for "GET /slurmdb/v0.0.41/accounts?UsersAreCoords&RemoveUsersAreCoords".
* sacctmgr - Add new possible new flags "NoUpdate" and "Exact" to '.associations[].flags' response from 'sacctmgr show assocs --{json|yaml}'.
* slurmrestd - Added possible flags "NoUpdate" and "Exact" to `.associations[].flags` for "GET /slurmdb/v0.0.41/associations/" and "POST /slurmdb/v0.0.41/associations/" endpoints.
* Fix false success of REQUEST_FORWARD_DATA RPC that made pmix to get out of sync during initialization.
* slurmrestd - Allow startup when slurmdbd is not configured and avoid loading slurmdbd specific plugins.
* Added PrologFlags=RunInJob to make prolog and epilog run inside the job extern step to include it in the job's cgroup.
* Return '*' for the password field for nss_slurm instead of "x".
* slurmrestd - Add "topo" as possible value to the following:  'GET /slurm/v0.0.41/jobs' in '.jobs[].shared' field  'GET /slurm/v0.0.41/job/{job_id}' in '.jobs[].shared' field  'POST /slurm/v0.0.41/job/submit' in '.job.shared' and '.jobs[].shared'  'POST /slurm/v0.0.41/job/allocate' in '.job.shared' and '.jobs[].shared'
* sacctmgr - Added possible flags "NoUsersAreCoords" and "UsersAreCoords" to `.accounts[].flags` for `sacctmgr show accounts --{json|yaml}` response.
* sacct - Add "topo" as possible value to output of 'sacct --{json|yaml}' to '.jobs[].shared' field.
* squeue - Add "topo" as possible value to output of 'squeue --{json|yaml}' to '.jobs[].shared' field.
* scontrol - Add "topo" as possible value to output of 'scontrol show jobs --{json|yaml}' to '.jobs[].shared' field.
* slurmrestd - Add "topo" as possible value to the following:  'GET /slurm/v0.0.41/jobs' in '.jobs[].exclusive' field  'GET /slurm/v0.0.41/job/{job_id}' in '.jobs[].exclusive' field  'POST /slurm/v0.0.41/job/submit' in '.job.exclusive' and   '.jobs[].exclusive'  'POST /slurm/v0.0.41/job/allocate' in '.job.exclusive' and   '.jobs[].exclusive'
* sacctmgr - Added possible flags "RemoveUsersAreCoords" and "UsersAreCoords" to `.accounts[].flags` for `sacctmgr show accounts --{json|yaml}` response.
* sacct - Add "topo" as possible value to output of 'sacct --{json|yaml}' to '.jobs[].exclusive' field.
* squeue - Add "topo" as possible value to output of 'squeue --{json|yaml}' to '.jobs[].exclusive' field.
* scontrol - Add "topo" as possible value to output of 'scontrol show jobs --{json|yaml}' to '.jobs[].exclusive' field.
* slurmrestd - Add fields '.job.segment_size' and '.jobs[].segment_size' to 'POST /slurm/v0.0.41/job/submit' and 'POST /slurm/v0.0.41/job/allocate'.
* sacctmgr - Added possible flags "NoUsersAreCoords" and "UsersAreCoords" to `.associations[].flags` for `sacctmgr show assocs --{json|yaml}` response.
* slurmrestd - Added possible flags "NoUsersAreCoords" and "UsersAreCoords" to `.associations[].flags` for "GET /slurmdb/v0.0.41/associations/" and "POST /slurmdb/v0.0.41/associations/" endpoints.
* Add ability to reserve MPI ports at the job level for stepmgr jobs and subdivide them at the step level.
* slurmrestd - Fix possible memory leak from failed job submissions to 'POST /slurm/v0.0.{39,40,41}/job/submit'.
* slurmrestd - Fix possible memory leak from failed job allocation requests to 'POST /slurm/v0.0.{39,40,41}/job/allocate'.
