## Changes in Slurm 23.11.11

* Fixed a job requeuing issue that merged job entries into the same SLUID when all nodes in a job failed simultaneously.
* Add ABORT_ON_FATAL environment variable to capture a backtrace from any fatal() message.
* Testsuite - fix python test 130_2.
* Fix security issue where a coordinator could add a user with elevated privileges. CVE-2025-43904.

## Changes in Slurm 23.11.10

* switch/hpe_slingshot - Fix issue that could result in a 0 length state file.
* Fix unnecessary message protocol downgrade for unregistered nodes.
* Fix unnecessarily packing alias addrs when terminating jobs with a mix of non-cloud/dynamic nodes and powered down cloud/dynamic nodes.
* Fix allowing access to reservations without MaxStartDelay set.
* Fix scontrol allowing updating job with bad cpus-per-task value.
* sattach - Fix regression from 23.11.9 security fix leading to crash.

## Changes in Slurm 23.11.9

* Fix many commands possibly reporting an "Unexpected Message Received" when in reality the connection timed out.
* Fix heterogeneous job components not being signaled with scancel --ctld and 'DELETE slurm/v0.0.40/jobs' if the job ids are not explicitly given, the heterogeneous job components match the given filters, and the heterogeneous job leader does not match the given filters.
* Fix regression from 23.02 impeding job licenses from being cleared.
* Move error to log_flag which made _get_joules_task error to be logged to the user when too many rpcs were queued in slurmd for gathering energy.
* slurmrestd - Prevent a slurmrestd segfault when modifying an association without specifying max TRES limits in the request if those TRES limits are currently defined in the association. This affects the following fields of endpoint 'POST /slurmdb/v0.0.38/associations/':  'associations/max/tres/per/job'  'associations/max/tres/per/node'  'associations/max/tres/total'  'associations/max/tres/minutes/per/job'  'associations/max/tres/minutes/total'
* Fix power_save operation after recovering from a failed reconfigure.
* scrun - Delay shutdown until after start requested. This caused scrun to never start or shutdown and hung forever when using --tty.
* Fix backup slurmctld potentially not running the agent when taking over as the primary controller.
* Fix primary controller not running the agent when a reconfigure of the slurmctld fails.
* jobcomp/{elasticsearch,kafka} - Avoid sending fields with invalid date/time.
* Fix energy gathering rpc counter underflow in _rpc_acct_gather_energy when more than 10 threads try to get energy at the same time. This prevented the possibility to get energy from slurmd by any step until slurmd was restarted, so losing energy accounting metrics in the node.
* slurmrestd - Fix memory leak for dbv0.0.39 jobs query which occurred if the query parameters specified account, association, cluster, constraints, format, groups, job_name, partition, qos, reason, reservation, state, users, or wckey. This affects the following endpoints:  'GET /slurmdb/v0.0.39/jobs'
* switch/hpe_slingshot - Fix security issue around managing VNI access. CVE-2024-42511.

## Changes in Slurm 23.11.8

* Fix slurmctld crash when reconfiguring with a PrologSlurmctld is running.
* Fix slurmctld crash after a job has been resized.
* Fix slurmctld and slurmdbd potentially stopping instead of performing a logrotate when receiving SIGUSR2 when using auth/slurm.
* Fix not having a disabled value for keepalive CommunicationParameters in slurm.conf when these parameters are not set. This can log an error when setting a socket, for example during slurmdbd registration with ctld.
* switch/hpe_slingshot - Fix slurmctld crash when upgrading from 23.02.
* Fix "Could not find group" errors from validate_group() when using AllowGroups with large /etc/group files.
* slurmrestd - Prevent a slurmrestd segfault when parsing the crontab field, which was never usable. Now it explicitly ignores the value and emits a warning if it is used for the following endpoints:  'POST /slurm/v0.0.39/job/{job_id}'  'POST /slurm/v0.0.39/job/submit'  'POST /slurm/v0.0.40/job/{job_id}'  'POST /slurm/v0.0.40/job/submit'
* Fix getting user environment when using sbatch with "--get-user-env" or "--export=" when there is a user profile script that reads /proc.
* Prevent slurmd from crashing if acct_gather_energy/gpu is configured but GresTypes is not configured.
* Do not log the following errors when AcctGatherEnergyType plugins are used but a node does not have or cannot find sensors: "error: _get_joules_task: can't get info from slurmd" "error: slurm_get_node_energy: Zero Bytes were transmitted or received" However, the following error will continue to be logged: "error: Can't get energy data. No power sensors are available. Try later"
* Fix cloud nodes not being able to forward to nodes that restarted with new IP addresses.
* sacct - Fix printing of job group for job steps.
* Fix error in scrontab jobs when using slurm.conf:PropagatePrioProcess=1.
* Fix slurmctld crash on a batch job submission with "--nodes 0,...".
* Fix dynamic IP address fanout forwarding when using auth/slurm.

## Changes in Slurm 23.11.7

* slurmrestd - Correct OpenAPI specification for 'GET /slurm/v0.0.40/jobs/state' having response as null.
* Allow running jobs on overlapping partitions if jobs don't specify -s.
* Fix segfault when requesting a shared gres along with an exclusive allocation.
* Fix regression in 23.02 where afternotok and afterok dependencies were rejected for federated jobs not running on the origin cluster of the submitting job.
* slurmctld - Disable job table locking while job state cache is active when replying to `squeue --only-job-state` or `GET /slurm/v0.0.40/jobs/state`.
* Fix sanity check when setting tres-per-task on the job allocation as well as the step.
* slurmrestd - Fix compatibility with auth/slurm.
* Fix issue where TRESRunMins gets off correct value if using QOS UsageFactor != 1.
* slurmrestd - Require `user` and `association_condition` fields to be populated for requests to 'POST /slurmdb/v0.0.40/users_association'.
* Avoid a slurmctld crash with extra_constraints enabled when a job requests certain invalid --extra values.
* `scancel --ctld` and `DELETE /slurm/v0.0/40/jobs` - Fix support for job array expressions (e.g. 1_[3-5]). Also fix signaling a single pending array task (e.g. 1_10), which previously signaled the whole array job instead.
* Fix a possible slurmctld segfault when at some point we failed to create an external launcher step.
* Allow the slurmctld to open a connection to the slurmdbd if the first attempt fails due to a protocol error.
* mpi/cray_shasta - Fix launch for non-het-steps within a hetjob.
* sacct - Fix "gpuutil" TRES usage output being incorrect when using --units.
* Fix a rare deadlock on slurmctld shutdown or reconfigure.
* Fix issue that only left one thread on each core available when "CPUs=" is configured to total thread count on multi-threaded hardware and no other topology info ("Sockets=", "CoresPerSocket", etc.) is configured.
* Fix the external launcher step not being allocated a VNI when requested.
* jobcomp/kafka - Fix payload length when producing and sending a message.
* scrun - Avoid a crash if RunTimeDelete is called before the container finishes.
* Save the slurmd's cred_state while reconfiguring to prevent the loss job credentials.

## Changes in Slurm 23.11.6

* Avoid limiting sockets per node to one when using gres enforce-binding.
* slurmrestd - Avoid permission denied errors when attempting to listen on the same port multiple times.
* Fix GRES reservations where the GRES has no topology (no cores= in gres.conf).
* Ensure that thread_id_rpc is gone before priority_g_fini().
* Fix scontrol reboot timeout removing drain state from nodes.
* squeue - Print header on empty response to `--only-job-state`.
* Fix slurmrestd not ending job properly when xauth is not present and a x11 job is sent.
* Add experimental job state caching with SchedulerParameters=enable_job_state_cache to speed up querying job states with squeue --only-job-state.
* slurmrestd - Correct dumping of invalid ArrayJobIds returned from 'GET /slurm/v0.0.40/jobs/state'.
* squeue - Correct dumping of invalid ArrayJobIds returned from `squeue --only-job-state --{json|yaml}`.
* If scancel --ctld is not used with --interactive, --sibling, or specific step ids, then this option issues a single request to the slurmctld to signal all jobs matching the specified filters. This greatly improves the performance of slurmctld and scancel. The updated --ctld option also fixes issues with the --partition or --reservation scancel options for jobs that requested multiple partitions or reservations.
* slurmrestd - Give EINVAL error when failing to parse signal name to numeric signal.
* slurmrestd - Allow ContentBody for all methods per RFC7230 even if ignored.
* slurmrestd - Add 'DELETE /slurm/v0.0.40/jobs' endpoint to allow bulk job signaling via slurmctld.
* Fix combination of --nodelist and --exclude not always respecting the excluded node list.
* Fix jobs incorrectly allocating nodes exclusively when started on a partition that doesn't enforce it. This could happen if a multi-partition job doesn't specify --exclusive and is evaluated first on a partition configured with OverSubscribe=EXCLUSIVE but ends up starting in a partition configured with OverSubscribe!=EXCLUSIVE evaluated afterwards.
* Setting GLOB_SILENCE flag no longer exposes old bugged behavior.
* Fix associations AssocGrpCPURunMinutes being incorrectly computed for running jobs after a controller reconfiguration/restart.
* Fix scheduling jobs that request --gpus and nodes have different node weights and different numbers of gpus.
* slurmrestd - Add "NO_CRON_JOBS" as possible flag value to the following:  'DELETE /slurm/v0.0.40/jobs' flags field.  'DELETE /slurm/v0.0.40/job/{job_id}?flags=' flags query parameter.
* Fix scontrol segfault/assert failure if the TRESPerNode parameter is used when creating reservations.
* Avoid checking for wsrep_on when restoring streaming replication settings.
* Clarify in the logs that error "1193 Unknown system variable 'wsrep_on'" is innocuous.
* accounting_storage/mysql - Fix problem when loading reservations from an archive dump.
* slurmdbd - Fix minor race condition when sending updates to a shutdown slurmctld.
* slurmctld - Fix invalid refusal of a reservation update.
* openapi - Fix memory leak of /meta/slurm/cluster response field.
* Fix memory leak when using auth/slurm and AuthInfo=use_client_ids.

## Changes in Slurm 23.11.5

* Fix Debian package build on systems that are not able to query the systemd package.
* data_parser/v0.0.40 - Emit a warning instead of an error if a disabled parser is invoked.
* slurmrestd - Improve handling when content plugins rely on parsers that haven't been loaded.
* Fix old pending jobs dying (Slurm version 21.08.x and older) when upgrading Slurm due to "Invalid message version" errors.
* Have client commands sleep for progressively longer periods when backed off by the RPC rate limiting system.
* slurmctld - Ensure agent queue is flushed correctly at shutdown time.
* slurmdbd - correct lineage construction during assoc table conversion for partition based associations.
* Add new RPCs and API call for faster querying of job states from slurmctld.
* slurmrestd - Add endpoint '/slurm/{data_parser}/jobs/state'.
* squeue - Add `--only-job-state` argument to use faster query of job states.
* Make a job requesting --no-requeue, or JobRequeue=0 in the slurm.conf, supersede RequeueExit[Hold].
* Add sackd man page to the Debian package.
* Fix issues with tasks when a job was shrunk more than once.
* Fix reservation update validation that resulted in reject of correct updates of reservation when the reservation was running jobs.
* Fix possible segfault when the backup slurmctld is asserting control.
* Fix regression introduced in 23.02.4 where slurmctld was not properly tracking the total GRES selected for exclusive multi-node jobs, potentially and incorrectly bypassing limits.
* Fix tracking of jobs typeless GRES count when multiple typed GRES with the same name are also present in the job allocation. Otherwise, the job could bypass limits configured for the typeless GRES.
* Fix tracking of jobs typeless GRES count when request specification has a typeless GRES name first and then typed GRES of different names (i.e. --gres=gpu:1,tmpfs:foo:2,tmpfs:bar:7). Otherwise, the job could bypass limits configured for the generic of the typed one (tmpfs in the example).
* Fix batch step not having SLURM_CLUSTER_NAME filled in.
* slurmstepd - Avoid error during `--container` job cleanup about RunTimeQuery never being configured. Results in cleanup where job steps not fully started.
* Fix nodes not being rebooted when using salloc/sbatch/srun "--reboot" flag.
* Send scrun.lua in configless mode.
* Fix rejecting an interactive job whose extra constraint request cannot immediately be satisfied.
* Fix regression in 23.11.0 when parsing LogTimeFormat=iso8601_ms that prevented milliseconds from being printed.
* Fix issue where you could have a gpu allocated as well as a shard on that gpu allocated at the same time.
* Fix slurmctld crashes when using extra constraints with job arrays.
* sackd/slurmrestd/scrun - Avoid memory leak on new unix socket connection.
* The failed node field is filled when a node fails but does not time out.
* slurmrestd - Remove requiring job script field and job component script fields to both be populated in the `POST /slurm/v0.0.40/job/submit` endpoint as there can only be one batch step script for a job.
* slurmrestd - When job script is provided in '.jobs[].script' and '.script' fields, the '.script' field's value will be used in the `POST /slurm/v0.0.40/job/submit` endpoint.
* slurmrestd - Reject HetJob submission missing or empty batch script for first Het component in the `POST /slurm/v0.0.40/job/submit` endpoint.
* slurmrestd - Reject job when empty batch script submitted to the POST /slurm/v0.0.40/job/submit` endpoint.
* Fix pam_slurm and pam_slurm_adopt when using auth/slurm.
* slurmrestd - Add 'cores_per_socket' field to `POST /slurm/v0.0.40/job/submit` endpoint.
* Fix srun and other Slurm commands running within a "configless" salloc when salloc itself fetched the config.
* Enforce binding with shared gres selection if requested.
* Fix job allocation failures when the requested tres type or name ends in "gres" or "license".
* accounting_storage/mysql - Fix lineage string construction when adding a user association with a partition.
* Fix sattach command.
* Fix ReconfigFlags. Due how reconfig was changed in 23.11, they will also be used to influence the slurmctld startup as well.
* Fix starting slurmd in configless mode if MUNGE support was disabled.

## Changes in Slurm 23.11.4

* Fix a memory leak when updating partition nodes.
* Don't leave a partition around if it fails to create with scontrol.
* Fix segfault when creating partition with bad node list from scontrol.
* Fix preserving partition nodes on bad node list update from scontrol.
* Fix assertion in developer mode on a failed message unpack.
* Fix repeat POWER_DOWN requests making the nodes available for ping.
* Fix rebuilding job alias_list on restart when nodes are still powering up.
* Fix INVALID nodes running health check.
* Fix cloud/future nodes not setting addresses on invalid registration.
* scrun - Remove the requirement to set the SCRUN_WORKING_DIR environment variable. This was a regression in 23.11.
* Add warning for using select/linear with topology/tree. This combination will not be supported in the next major version.
* Fix health check program not being run after first pass of all nodes when using MaxNodeCount.
* sacct - Set process exit code to one for all errors.
* Add SlurmctldParameters=disable_triggers option.
* Fix issue running steps when the allocation requested an exclusive allocation shards along with shards.
* Fix cleaning up the sleep process and the cgroup of the extern step if slurm_spank_task_post_fork returns an error.
* slurm_completion - Add missing --gres-flags= options multiple-tasks-per-sharing and one-task-per-sharing.
* scrun - Avoid race condition that could cause outbound network communications to incorrectly rejected with an incomplete packet error.
* scrun - Gracefully handle kernel giving invalid expected number of incoming bytes for a connection causing incoming packet corruption resulting in connection getting closed.
* srun - return 1 when a step launch fails
* scrun - Avoid race condition that could cause deadlock during shutdown.
* Fix scontrol listpids to work under dynamic node scenarios.
* Add --tres-bind to --help and --usage output.
* Add --gres-flags=allow-task-sharing to allow GPUs to still be accessible among all tasks when binding GPUs to specific tasks.
* Fix issue with CUDA_VISIBLE_DEVICES showing the same MIG device for all tasks when using MIGs with --tres-per-task or --gpus-per-task.
* slurmctld - Prevent a potential hang during shutdown/reconfigure if the association cache thread was previously shut down.
* scrun - Avoid race condition that could cause scrun to hang during shutdown when connections have pending events.
* scrun - Avoid excessive polling of connections during shutdown that could needlessly cause 100% CPU usage on a thread.
* sbcast - Use user identity from broadcast credential instead of looking it up locally on the node.
* scontrol - Remove "abort" option handling.
* Fix an error message referring to the wrong RPC.
* Fix memory leak on error when creating dynamic nodes.
* Fix a slurmctld segfault when a cloud/dynamic node changes hostname on registration.
* Prevent a slurmctld deadlock if the gpu plugin fails to load when creating a node.
* Change a slurmctld fatal() to an error() when attempting to create a dynamic node with a global autodetect set in gres.conf.
* Fix leaving node records on error when creating nodes with scontrol.
* scrun/sackd - Avoid race condition where shutdown could deadlock.
* Fix a regression in 23.02.5 that caused pam_slurm_adopt to fail when the user has multiple jobs on a node.
* Add GLOB_SILENCE flag that silences the error message which will display if an include directive attempts to use the "*" wildcard.
* Fix jobs getting rejected when submitting with --gpus option from older versions of job submission commands (23.02 and older).
* cgroup/v2 - Return 0 for VSZ. Kernel cgroups do not provide this metric.
* scrun - Avoid race condition where outbound RPCs could be corrupted.
* scrun - Avoid race condition that could cause a crash while compiled in debug mode.
* gpu/rsmi - Disable gpu usage statistics when not using ROCM 6.0.0+
* Fix stuck processes and incorrect environment when using --get-user-env.
* Avoid segfault in the slurmdbd when TrackWCKey=no but you are still using use WCKeys.
* Fix ctld segfault with TopologyParam=RoutePart and no partition defined.
* slurmctld - Fix missing --deadline handling for jobs not evaluated by the schedulers (i.e. non-runnable, skipped for other reasons, etc.).
* Demote some eio related logs from error to verbose in user commands. These are not generally actionable by the user and are easily generated by port scanning a machine running srun.
* Make sprio correctly print array tasks that have not yet been split out.
* topology/block - Restrict the number of last-level blocks in any allocation.
* slurmrestd - Treat multiple repeat URL query values as list.
* slurmrestd - Treat all URL query values as string by default to avoid parser warnings.

## Changes in Slurm 23.11.3

* Fix debian/changelog file to reflect the correct version so the packages are generated correctly.

## Changes in Slurm 23.11.2

* slurmrestd - Reject single http query with multiple path requests.
* Fix launching Singularity v4.x containers with srun --container by setting .process.terminal to true in generated config.json when step has pseudoterminal (--pty) requested.
* Fix loading in dynamic/cloud node jobs after net_cred expired.
* Fix cgroup null path error on slurmd/slurmstepd tear down.
* data_parser/v0.0.40 - Prevent failure if accounting is disabled, instead issue a warning if needed data from the database can not be retrieved.
* openapi/slurmctld - Prevent failure if accounting is disabled.
* Prevent slurmscriptd processing delays from blocking other threads in slurmctld while trying to launch various scripts. This is additional work for a fix in 23.02.6.
* Fix memory leak when receiving alias addrs from controller.
* scontrol - Accept `scontrol token lifespan=infinite` to create tokens that effectively do not expire.
* Avoid errors when Slurmdb accounting disabled when '--json' or '--yaml' is invoked with CLI commands and slurmrestd. Add warnings when query would have populated data from Slurmdb instead of errors.
* Fix slurmctld memory leak when running job with --tres-per-task=gres:shard:#
* Fix backfill trying to start jobs outside of backfill window.
* Fix oversubscription on partitions with PreemptMode=OFF.
* Preserve node reason on power up if the node is downed or drained.
* data_parser/v0.0.40 - Avoid aborting when invoking a not implemented parser.
* data_parser/v0.0.40 - Fix how nice values are parsed for job submissions.
* data_parser/v0.0.40 - Fix regression where parsing error did not result in invalid request being rejected.
* Fix segfault in front-end node registration.
* Prevent jobs using none typed gpus from being killed by the controller after a reconfig or restart.
* Fix deadlock situation in the dbd when adding associations.
* Update default values of text/blob columns when updating from old mysql versions in more situations. This improves a previous fix to handle an uncommon case when upgrading mysql/mariadb.
* Fix rpmbuild in openSUSE/SLES due to incorrect mariadb dependency.
* Fix compilation on RHEL 7.
* When upgrading the slurmdbd to 23.11, avoid generating a query to update the association table that is larger than max_allowed_packet which would result in an upgrade failure.
* Fix rare deadlock when a dynamic node registers at the same time that a once per minute background task occurs.
* Fix build issue on 32-bit systems.
* data_parser/v0.0.40 - Fix enumerated strings in OpenAPI specification not have type field specified.
* Improve scontrol show job -d information of used shared gres (shard/mps) topology.
* Allow Slurm to compile without MUNGE if --without-munge is used as an argument to configure.
* accounting_storage/mysql - Fix usage query to use new lineage column instead of lft/rgt.
* slurmrestd - Improve handling of missing parsers when content plugins expect parsers not loaded.
* slurmrestd - Correct parsing of StepIds when querying jobs.
* slurmrestd - Improve error from parsing failures of lists.
* slurmrestd - Improve parsing of singular values for lists.
* accounting_storage/mysql - Fix PrivateData=User when listing associations.
* Disable sorting of dynamic nodes to avoid issues when restarting with heterogeneous jobs that cause jobs to abort on restart.
* Don't allow deletion of non-dynamic nodes.
* accounting_storage/mysql - Fix issue adding partition based associations.
* Respect non-"slurm" settings for I_MPI_HYDRA_BOOTSTRAP and HYDRA_BOOTSTRAP and avoid injecting the --external-launcher option which will cause mpirun/mpiexec to fail with an unexpected argument error.
* Fix bug where scontrol hold would change node count for jobs with implicitly defined node counts.
* data_parser/v0.0.40 - Fix regression of support for "hold" in job description.
* Avoid sending KILL RPCs to unresolvable POWERING_UP and POWERED_DOWN nodes.
* data_parser/v0.0.38 - Fix several potential NULL dereferences that could cause slurmrestd to crash.
* Add --gres-flags=one-task-per-sharing. Do not allow different tasks in to be allocated shared gres from the same sharing gres.
* Add SelectTypeParameters=ENFORCE_BINDING_GRES and ONE_TASK_PER_SHARING_GRES. This gives default behavior for a job's --gres-flags.
* Alter the networking code to try connecting to the backup controllers if the DNS lookup for the primary SlurmctldHost fails.
* Alter the name resolution to only log at verbose() in client commands on failures. This allows for HA setups where the DNS entries are withdrawn for some SlurmctldHost entries without flooding the user with errors.
* Prevent slurmscriptd PID leaks when running slurmctld in foreground mode.
* Open all slurmctld listening ports at startup, and persist throughout. This also changes the backup slurmctld process to open the SlurmctldPort range, instead of only the first.
* Fix backup slurmctld shutting down instead of resuming standby duty if it took control.
* Fix race condition that delayed the primary slurmctld resuming when taking control from a backup controller.
* srun - Ensure processed messages are meant for this job in case of a rapidly-reused TCP port.
* srun - Prevent step launch failure while waiting for step allocation if a stray message is received.
* Fix backup slurmctld to be able to respond to configless config file requests correctly.
* Fix slurmctld crashing when recovering from a failed reconfigure.
* Fix slurmscriptd operation after recovering from a failed reconfigure.

## Changes in Slurm 23.11.1

* Fix scontrol update job=... TimeLimit+=/-= when used with a raw JobId of job array element.
* Reject TimeLimit increment/decrement when called on job with TimeLimit=UNLIMITED.
* Fix slurmctld segfault when reconfiguring after a job resize.
* Fix compilation on FreeBSD.
* Fix issue with requesting a job with --licenses as well as --tres-per-task=license.
* slurmctld - Prevent segfault in getopt_long() with an invalid long option.
* Switch to man2html-base in Build-Depends for Debian package.
* slurmrestd - Added /meta/slurm/cluster field to responses.
* Adjust systemd service files to start daemons after remote-fs.target.
* Add "--with selinux" option to slurm.spec.
* Fix task/cgroup indexing tasks in cgroup plugins, which caused jobacct/gather to match the gathered stats with the wrong task id.
* select/linear - Fix regression in 23.11 in which jobs that requested --cpus-per-task were rejected.
* Fix crash in slurmstepd that can occur when launching tasks via mpi using the pmi2 plugin and using the route/topology plugin.
* Fix sgather not gathering from all nodes when using CR_PACK_NODES/--m pack.
* Fix mysql query syntax error when getting jobs with private data.
* Fix sanity check to prevent deleting default account of users.
* data_parser/v0.0.40 - Fix the parsing for /slurmdb/v0.0.40/jobs exit_code query parameter.
* Fix issue where TRES for energy wasn't always set before sending it to the jobcomp plugin.
* jobcomp/[kafka|elastisearch] Print raw TRES values along with the formatted versions as tres_[req|alloc]_raw.
* Fix inconsistencies with --cpu-bind/SLURM_CPU_BIND and --hint/SLURM_HINT.
* Fix ignoring invalid json in various subsystems.
* Remove shebang from bash completion script.
* Fix elapsed time in JobComp being set from invalid start and end times.
* Update service files to start slurmd, slurmctld, and slurmdbd after sssd.
* data_parser/v0.0.40 - Fix output of DefMemPerCpu, MaxMemPerCpu, and max_shares.
* When determining a jobs index in the database don't wait if there are more jobs waiting.
* If a job requests more shards which would allocate more than one sharing GRES (gpu) per node refuse it unless SelectTypeparameters has MULTIPLE_SHARING_GRES_PJ.
* Avoid refreshing the hwloc xml file when slurmd is reconfigured. This fixes an issue seen with CoreSpecCount used on nodes with Intel E-cores.
* Trigger fatal exit when Slurm API function is called before slurm_init() is called.
* slurmd - Fix issue with 'scontrol reconfigure' when started with '-c'.
* data_parser/v0.0.40 - Fix handling of negative job nice values.
* data_parser/v0.0.40 - Fill the "id" object for associations with the cluster, account, partition, and user in addition to the assoc id.
* data_parser/v0.0.40 - Remove unusable cpu_binding_flags enums from v00.0.40_job_desc_msg.
* Improve performance and resiliency of slurmscriptd shutdown on 'scontrol reconfigure'.
* slurmrestd - Job submissions that result in the following error codes will be considered as successfully submitted (with a warning), instead of returning an HTTP 500 error back: ESLURM_NODES_BUSY, ESLURM_RESERVATION_BUSY, ESLURM_JOB_HELD, ESLURM_NODE_NOT_AVAIL, ESLURM_QOS_THRES, ESLURM_ACCOUNTING_POLICY, ESLURM_RESERVATION_NOT_USABLE, ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE, ESLURM_BURST_BUFFER_WAIT, ESLURM_PARTITION_DOWN, ESLURM_LICENSES_UNAVAILABLE.
* Fix issue with node appearing to reboot on every "scontrol reconfigure" when slurmd was started with the '-b' flag.
* Fix a slurmctld fatal error when upgrading to 23.11 and changing from select/cons_res to select/cons_tres at the same time.
* slurmctld - Fix subsequent reconfigure hanging after a failed reconfigure.
* slurmctld - Reject arbitrary distribution jobs that have a minimum node count that differs from the number of unique nodes in the hostlist.
* Prevent slurmdbd errors when updating reservations with names containing apostrophes.
* Prevent message extension attacks that could bypass the message hash. CVE-2023-49933.
* Prevent SQL injection attacks in slurmdbd. CVE-2023-49934.
* Prevent message hash bypass in slurmd which can allow an attacker to reuse root-level MUNGE tokens and escalate permissions. CVE-2023-49935.
* Prevent NULL pointer dereference on size_valp overflow. CVE-2023-49936.
* Prevent double-xfree() on error in _unpack_node_reg_resp(). CVE-2023-49937.

## Changes in Slurm 23.11.0

* For jobs that request --cpus-per-gpu, ensure that the --cpus-per-gpu request is honored on every node in the and not just for the job as a whole.
* Fix "srun -Z" for cred/munge and cred/none.
* Fix listing available data_parser plugins for json and yaml when giving no commands to scontrol or sacctmgr.
* data_parser/v0.0.40 - Fixed how deleted QOS and associations for jobs are dumped.
* data_parser/v0.0.40 - Fix how errors and warnings are dumped.
* Print consistent errors when serializer plugin fails to load.
* data_parser/v0.0.40 - Fix parsing of flag arrays to allow multiple flags to be set.
* slurmctld - Rework 'scontrol reconfigure' to avoid race conditions that can result in stray jobs.
* slurmctld - Shave ~1 second off average reconfigure time by terminating internal processing threads faster.
* Skip running slurmdbd -R if the connected cluster is 23.11 or newer. This operation is nolonger relevant for 23.11.
* Fix segfault when updating node instance id/type without accounting enabled.
* Ensure slurmscriptd shuts down before slurmctld is stopped / reconfigured.
* Improve error handling and error messages in slurmctld to slurmscriptd communications. This includes avoiding potential deadlock in slurmctld if slurmscript dies unexpectedly.
* Do not hold batch jobs whose extra constraints cannot be immediately satisfied, and set the state reason to "Constraints" instead of "BadConstraints".
* Fix verbose log message printing a hex number instead of a job id.
* Upgrade rate limit parameters message from debug to info.
* Fix missing symbols for those linking to libslurm.
* Fix memory leak when getting and forwarding addrs from client.
* Fix xassert when forwarding to non-addressable nodes.
* Fix regression in 23.11.0rc1 where assocs were created with an incorrect hierarchy for non-23.11 clusters.
* For SchedulerParameters=extra_constraints, prevent slurmctld segfault when starting a slurmd with --extra for a node that did not previously set this. This also ensures the extra constraints model works off the current node state, not the prior state.
* Fix regression in 23.11.0rc1 where data_t would not decode negative float values correctly, instead the absolute value was always returned.
* Fix issue where 'scontrol reconfigure' right as the controller was started or reconfigured could lead to it shutting down completely.
* Fix --tres-per-task assertion.
* Fix a few issues when creating reservations.
* Fix slurmctld segfault when packing a job step loaded from a < 23.11 state.
* Fix a 32-bit compile issue.
* Add SchedulerParameters=time_min_as_soft_limit option.

## Changes in Slurm 23.11.0rc1

* task/affinity - remove Power7 cpu-specific workarounds.
* Remove SLURM_WORKING_CLUSTER env from batch and srun environments.
* cli_filter/lua - return nil for unset time options rather than the string "2982616-04:14:00" (which is the internal macro "NO_VAL" represented as time string).
* Remove 'none' plugins for all but auth and cred. scontrol show config will report (null) now.
* Removed select/cons_res. Please update your configuration to select/cons_tres.
* mpi/pmix - When aborted with status 0, avoid marking job/step as failed.
* Fixed typo on "initialized" for the description of ESLURM_PLUGIN_NOT_LOADED.
* Added max_submit_line_size to SchedulerParameters.
* Change TreeWidth default from 50 to 16.
* cgroup.conf - Removed deprecated parameters AllowedKmemSpace, ConstrainKmemSpace, MaxKmemPercent, and MinKmemSpace.
* proctrack/cgroup - Add "SignalChildrenProcesses=<yes|no>" option to cgroup.conf. This allows signals for cancelling, suspending, resuming, etc. to be sent to children processes in a step/job rather than just the parent.
* Add PreemptParameters=suspend_grace_time parameter to control amount of time between SIGTSTP and SIGSTOP signals when suspending jobs.
* job_submit/throttle - improve reset of submitted job counts per user in order to better honor SchedulerParameters=jobs_per_user_per_hour=#.
* Load the user environment into a private pid namespace to avoid user scripts leaving background processes on a node.
* scontrol show assoc_mgr will display Lineage instead of Lft for associations.
* Add SlurmctldParameters=no_quick_restart to avoid a new slurmctld taking over the old slurmctld on accident.
* Fix --cpus-per-gpu for step allocations, which was previously ignored for job steps. --cpus-per-gpu implies --exact.
* Fix mutual exclusivity of --cpus-per-gpu and --cpus-per-task: fatal if both options are requested in the commandline or both are requested in the environment. If one option is requested in the command line, it will override the other option in the environment.
* slurmrestd - openapi/dbv0.0.37 and openapi/v0.0.37 plugins have been removed.
* slurmrestd - openapi/dbv0.0.38 and openapi/v0.0.38 plugins have been tagged as deprecated.
* openapi/slurmctld - forked from openapi/v0.0.38.
* openapi/slurmdbd - forked from openapi/dbv0.0.38.
* data_parser/v0.0.40 - forked from data_parser/v0.0.39 plugin.
* data_parser/v0.0.40 - added OpenAPI schema generation of path parameters and OperationIds.
* slurmrestd - added auto population of info/version field.
* openapi/slurmctld - convert to using data_parser plugins for all input and output formatting.
* openapi/slurmdbd - convert to using data_parser plugins for all input and output formatting.
* data_parser/v0.0.39 - skip empty string when parsing QOS ids.
* data_parser/v0.0.40 - skip empty string when parsing QOS ids.
* data_parser/v0.0.40 - log errors on every level of parsing on failure.
* sdiag - add --yaml and --json arg support to specify data_parser plugin.
* sacct - add --yaml and --json arg support to specify data_parser plugin.
* scontrol - add --yaml and --json arg support to specify data_parser plugin.
* sinfo - add --yaml and --json arg support to specify data_parser plugin.
* squeue - add --yaml and --json arg support to specify data_parser plugin.
* data_parser/v0.0.40 - add warnings for unknown fields during parsing.
* data_parser/v0.0.40 - add FAST parameter to allow requester to skip more time intensive warning checks.
* Changed the default SelectType to select/cons_tres (from select/linear).
* Allow SlurmUser/root to use reservations without specific permissions.
* Fix sending step signals to nodes not allocated by the step.
* Remove CgroupAutomount= option from cgroup.conf.
* Add TopologyRoute=RoutePart to route communications based on partition node lists.
* SPANK - added new spank_prepend_task_argv() function.
* slurmd - improve error logging at job startup during transition from slurmd to slurmstepd.
* Added ability for configless to push Prolog and Epilog scripts to slurmds.
* Prolog and Epilog do not have to be fully qualified pathnames.
* Changed default value of PriorityType from priority/basic to priority/multifactor.
* torque/mpiexec - Propagate exit code from launched process.
* slurmrestd - Add new rlimits fields for job submission.
* data_parser/v0.0.40 - convert job state field to flag array to provide enumeration of values.
* sbatch - removed --export-file option (used with defunct Moab integration).
* Define SPANK options environment variables when --export=[NIL|NONE] is specified.
* slurmrestd - Numeric input fields provided with a null formatted value will now convert to zero (0) where it can be a valid value. This is expected to be only be notable with job submission against v0.0.38 versioned endpoints with job requests with fields provided with null values. These fields were already rejected by v0.0.39+ endpoints, unless +complex parser value is provided to v0.0.40+ endpoints.
* slurmrestd - Improve parsing of integers and floating point numbers when handling incoming user provided numeric fields. Fields that would have not rejected a number for a numeric field followed by other non-numeric characters will now get rejected. This is expected to be only be notable with job submission against v0.0.38 versioned endpoints with malformed job requests.
* Reject reservation update if it will result in previously submitted jobs losing access to the reservation.
* data_parser/v0.0.40 - output partition state when dumping partitions.
* Allow for a shared suffix to be used with the hostlist format. E.g., "node[0001-0010]-int".
* Fix perlapi build when using non-default libdir.
* Replace SRUN_CPUS_PER_TASK with SLURM_CPUS_PER_TASK and get back the previous behavior before Slurm 22.05 since now we have the new external launcher step.
* data_parser/v0.0.40 - Change v0.0.40_job_info response to tag exit_code field as verbose job exit code object.
* data_parser/v0.0.40 - Change v0.0.40_job_info response to tag derived_exit_code field as verbose job exit code object.
* Avoid database upgrade failures with galera by enabling streaming replication for Galera 4 clusters during the upgrade process.
* job_container/tmpfs - disable plugin for nodes that are not listed in job_container.conf when there is no global BasePath set.
* job_container/tmpfs - Add "BasePath=none" option to disable plugin on node subsets when there is a global setting.
* Remove cloud_reg_addrs and make it default behavior.
* Remove NoAddrCache CommunicationParameter.
* Add QOS flag 'Relative'. If set the QOS limits will be treated as percentages of a cluster/partition instead of absolutes.
* Remove FIRST_CORES flag from reservations.
* scontrol/sview - Remove comma separated CoreCnt option from reservations.
* scontrol/sview - Remove comma separated NodeCnt option from reservations.
* Add cloud instance id and instance type to node records. Can be viewed/ updated with scontrol.
* slurmd - add "instance-id", "instance-type", and "extra" options to allow them to be set on startup.
* Add cloud instance accounting to database that can be viewed with 'sacctmgr show instance'.
* openapi/v0.0.40 - add /instance and /instances endpoints.
* SelectTypeParameters=cr_cpu - Fix a log error "CPU underflow" at step completion for steps that request --threads-per-core or --hint=nomultithread.
* select/linear - fix task launch failure that sometimes occurred when requesting --threads-per-core or --hint=nomultithread. This also fixes memory calculation with one of these options and --mem-per-cpu: Previously, memory = mem-per-cpu * all cpus including unusable threads. Now, memory = mem-per-cpu * only usable threads. This behavior matches the documentation and select/cons_tres.
* gpu/nvml - Reduce chances of NVML_ERROR_INSUFFICIENT_SIZE error when getting gpu memory information.
* slurmrestd - Convert to generating OperationIDs based on path for all v0.0.40 tagged paths.
* slurmrestd - Reduce memory used while dumping a job's stdio paths.
* slurmrestd - Jobs queried from data_parser/v0.0.40 from slurmdb will have 'step/id' field given as a string to match CLI formatting instead of an object.
* sacct - Output in JSON or YAML output will will have the 'step/id' field given as a string instead of an object.
* scontrol/squeue - Step output in JSON or YAML output will will have the 'id' field given as a string instead of an object.
* slurmrestd - For 'GET /slurmdb/v0.0.40/jobs' mimic default behavior for handling of job start and end times as sacct when one or both fields are not provided as a query parameter.
* openapi/slurmctld - Add 'GET /slurm/v0.0.40/shares' endpoint to dump same output as sshare.
* sshare - add JSON/YAML support.
* data_parser/v0.0.40 - Remove "required/memory" output in json. It is replaced by "required/memory_per_cpu" and "required/memory_per_node".
* slurmrestd - Add numeric id to all association identifiers to allow unique identification where association has been deleted but is still referenced by accounting record.
* slurmrestd - Add accounting, id, and comment fields to association dumps.
* slurmrestd - Removed usage field from association dumps which was never populated. See accounting field for accounting usage records.
* slurmrestd - Default to not query associations or coordinators with 'GET /slurmdb/v0.0.40/accounts'.
* slurmrestd - Default to not query associations, wckeys or coordinators with 'GET /slurmdb/v0.0.40/user'.
* slurmrestd - Enforce user's default wckey on supplied list of user wckeys in 'POST /slurmdb/v0.0.40/user' queries to avoid conflicting or changed default wckey from being ignored.
* slurmrestd - Enforce user's default wckey on supplied list of user wckeys in 'POST /slurmdb/v0.0.40/user' queries to avoid conflicting or changed default wckey from being ignored.
* slurmrestd - 'POST /slurm/v0.0.40/job/submit' will return "step_id" as string to provide descriptive step names (batch, extern, interactive, TBD) for non-numeric steps.
* slurmrestd - Tagged "result" field from 'POST /slurm/v0.0.40/job/submit' as deprecated.
* slurmrestd - Warning will be added for rejected job submissions with submissions to 'POST /slurm/v0.0.40/job/submit'.
* slurmrestd - Tagged "job_id", "step_id", and "job_submit_user_msg" fields from 'POST /slurm/v0.0.40/job/{job_id}' response as deprecated due their only being valid for the first entry in the "result" field array.
* slurmrestd - Warning will be added for rejected job updates with queries to 'POST /slurm/v0.0.40/job/{job_id}'.
* slurmrestd - Add SLURMRESTD_JSON and SLURMRESTD_YAML input environment variables.
* slurmrestd - Correct issue where field and $ref description fields were not getting populated for OpenAPI specification generation for queries to 'GET /openapi/v3' for v0.0.40 endpoints.
* slurmdbd - Check for innodb_redo_log_capacity instead of innodb_log_file_size in MySQL 8.0.30+.
* slurmrestd - Fix log level requests SLURMRESTD_DEBUG and -v applying on top of each other and the default logging level (info). -v now applies on top of the default log level, and SLURMRESTD_DEBUG sets the log level if -v is not given.
* slurmrestd - Allow SLURMRESTD_DEBUG=quiet or 0, which was previously denied. Also deny negative values for SLURMRESTD_DEBUG, which previously set the debug level to debug5.
* The backup slurmctld now checks that the heartbeat file exists in StateSaveLocation before starting and attempting to assert control. This avoids issues with misconfiguration, or the shared filesystem being unavailable, that could previously have lead to all jobs being cancelled.
* The warning printed when using configure --without-PACKAGE has been changed to a notice.
* Fix --cpu-freq with userspace governor and frequency ranges behavior.
* Fix --cpu-freq parsing with incorrect frequencies.
* PMIx support is nolonger built by default. Passing --with-pmix option is now required to build with PMIx.
* Use memory.current in cgroup/v2 instead of manually calculating RSS. This makes accounting consistent with OOM Killer.
* Update slurmstepd processes with current SlurmctldHost settings, allowing for controller changes without draining all compute jobs.
* Add format_stderr to LogTimeFormat of slurm.conf and slurmdbd.conf.
* slurmrestd - add `GET /slurm/v0.0.40/reconfigure` endpoint to allow equivalent requests of `scontrol reconfigure`.
* sreport - cluster Utilization PlannedDown field now includes the time that all nodes were in the POWERED_DOWN state instead of just cloud nodes.
* scontrol update partition now allows Nodes+=<node-list> and Nodes-=<node-list> to add/delete nodes from the existing partition node list. Nodes=+host1,-host2 is also allowed.
* sacctmgr - add --yaml and --json arg support to specify data_parser plugin.
* slurmrestd - Add last_update and last_backfill fields to response to `GET /slurm/v0.0.40/job` and `GET /slurm/v0.0.40/jobs` queries.
* slurmrestd - Add last_update fields to response to `GET /slurm/v0.0.40/node` and `GET /slurm/v0.0.40/nodes` queries.
* slurmrestd - Add last_update fields to response to `GET /slurm/v0.0.40/partition` and `GET /slurm/v0.0.40/partitions` queries.
* slurmrestd - Add last_update fields to response to `GET /slurm/v0.0.40/licenses` query.
* auth/jwt - fatal when jwt or jwks key files are writable by other.
* sacctmgr can now modify QOS's RawUsage to zero or a positive value.
* sdiag - Added statistics on why the main and backfill schedulers have stopped evaluation on each scheduling cycle.
* openapi/v0.0.40 - add /{accounts,users}_association endpoints.
* slurm.spec - Add `--with yaml` argument to require YAML support.
* Add new rl_log_freq option to SlurmctldParameters to allow sites to limit the number of 'RPC limit exceeded...' messages that are logged.
* Rename sbcast --fanout to --treewidth.
* Remove SLURM_NODE_ALIASES env variable.
* Enable fanout for dynamic and unaddresable cloud nodes.
* Fix how steps are dealloced in an allocation if the last step of an srun never completes due to a node failure.
* Remove redundant database indexes.
* Add database index to suspend table to speed up archive/purges.
* When requesting --tres-per-task alter incorrect request for TRES, it should be TRESType/TRESName not TRESType:TRESName.
* Make it so reservations can reserve GRES.
* switch/hpe_slingshot - Add disable_rdzv_get flag to disable rendezvous gets.
* slurmrestd - Avoid matching query URLs based on partial matches to an existing endpoint, e.g. POST request to /slurm/v0.0.40/job was routed to the handler of POST /slurm/v0.0.40/job/submit.
* Don't display old job_arrays/het_jobs in sacct if Job ID was reused.
* sbcast - use the specified --fanout value on all hops in message forwarding; previously the specified fanout was only used on the first hop, and additional hops used TreeWidth in slurm.conf.
* slurmrestd - remove logger prefix from '-s/-a list' options outputs.
* Fix fd socket name resolution which could flood log files at debug level.
* The rpmbuild "--with mysql" option has been removed. The rpm has long required sql development libraries to build and the existence of this option was confusing. The default behavior now is to always require one of the sql development libraries.
* Add support for Debian packaging.
* switch/hpe_slingshot - Add support for collectives.
* Nodes with suspended jobs can now be displayed as MIXED.
* sview - Fix search by node state returning incorrect node list.
* Fix inconsistent handling of using cli and/or environment options for tres_per_task=cpu:# and cpus_per_gpu.
* Requesting --cpus-per-task will now set SLURM_TRES_PER_TASK=cpu:# in the environment.
* For some tres related environment variables such as SLURM_TRES_PER_TASK, when srun requests a different value for that option, set these environment variables to the value requested by srun. Previously these environment variables were unchanged from the job allocation. This bug only affected the output environment variables, not the actual step resource allocation.
* RoutePlugin=route/topology has been replaced with TopologyParam=RouteTree.
* If ThreadsPerCore in slurm.conf is configured with less than the number of hardware threads, fix a bug where the task plugins used fewer cores instead of using fewer threads per core.
* Fix arbitrary distribution allowing it to be used with salloc and sbatch and fix how cpus are allocated to nodes.
* Allow nodes to reboot while node is drained or in a maintenance state.
* Allow scontrol reboot to use nodesets to filter nodes to reboot.
* Fix how the topology of typed gres gets updated.
* Changes to the Type option in gres.conf now can be applied with scontrol reconfig.
* Allow for jobs that request a newly configured gres type to be queued even when the needed slurmds have not yet registered.
* Kill recovered jobs that require unconfigured gres types.
* If keepalives are configured, enable them on all persistent connections.
* data_parser/v0.0.40 - add parsers for main/backfill cycle exit reasons.
* Configless - Also send Includes from configuration files not parsed by the controller (i.e. from plugstack.conf).
* Add gpu/nrt plugin for nodes using Trainium/Inferentia devices.
* data_parser/v0.0.40 - Add START_RECEIVED to job flags in dumped output.
* SPANK - Failures from most spank functions (not epilog or exit) will now cause the step to be marked as failed and the command (srun, salloc, sbatch --wait) to return 1.
* data_parser/v0.0.40 - Fix how the "INVALID" nodes state is dumped.
* Add SchedulerParameters=extra_constraints. This enables various node filtering options in the --extra flag of salloc, sbatch, and srun.
* Improve scontrol show node -d information of used shared gres (shard/mps) topology.
