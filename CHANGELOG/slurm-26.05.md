## Changes in 26.05.0rc1

* Add SLURM_JOB_QOS to Prolog/Epilog environment.
* data_parser/v0.0.45 - Prevent memory leaks when freeing parsed lists.
* Return an xstring from slurm_create_reservation() instead of one created with strdup().
* scontrol - If a step terminates while its pids are bing queried 'scontrol listpids' will now print all successfully found pids instead of only logging an error.
* Prevent stepd_connect() from overriding the connect calls errno on error.
* slurmctld - Support 'verbose' query parameter in 'GET /readyz' endpoint.
* slurmd - Support 'verbose' query parameter in 'GET /readyz' endpoint.
* sacctmgr - In interactive mode, quiet/verbose will now apply to logging messages that are printed.
* sacctmgr - Quiet (--quiet/-Q) and verbose (--verbose/-v) command line options are now mutually exclusive. sacctmgr will immediately exit if both options are specified.
* sacctmgr - Quiet option (--quiet/-Q) is now applied to all logging messages, ensuring that it is enforced in all cases (e.g. logging from 'dump' previously would not honor --quiet)
* NO_NORMAL_ALL will only be printed if all NO_NORMAL_* flags are set.
* job_submit/lua - Log Lua stacktrace on runtime errors when calling slurm_job_submit() in job_submit.lua when 'debugflags=script' is set in slurm.conf or via environment `SLURM_DEBUG_FLAGS=script`.
* job_submit/lua - Log Lua stacktrace on runtime errors when calling slurm_job_modify() in job_submit.lua when 'debugflags=script' is set in slurm.conf or via environment `SLURM_DEBUG_FLAGS=script`.
* Added error handling and logging when a malformed RESPONSE_CONFIG RPC is received.
* Reject QOS creation requests that use nonuser flags
* Do not print nonuser QOS flags as valid flags
* Add "thread" as possible flag to "debugflags=" in slurm.conf and slurmdbd.conf.
* Do not allow clearing the partition from a reservation  (e.g. scontrol update ReservationName=<res_name> PartitionName=''). Attempts to clear the partition from a reservation will be rejected by slurmctld. This change also fixes several potential slurmctld crashes.
* Add DebugFlag=SelectType log for when a node is skipped during job scheduling attempts because it is in COMPLETING state.
* slurmrestd - Add POWER_DOWN_ASAP and POWER_DOWN_FORCE to as valid node states in REST.
* slurmctld - Remove Slurmctld job state cache including support for SchedulerParameters=enable_job_state_cache in slurm.conf.
* slurmctld - Log error when saving to StateSaveLocation is too slow.
* slurmctld - Include StateSaveLocation statistics with /readyz endpoint.
* Fix error reading /proc/0/* when calling the api outside the step namespace.
* Alter sh5util -j to not allow array or het job ids.
* slurmctld - Improve ability to process RPCs in parallel by removing the need for the node write lock to process REQUEST_NODE_INFO, "metrics/partitions", and "metrics/nodes" requests, as well as when spawning the node health check agent.
* slurmctld - No longer acquire the job write lock when spawning the node health check agent.
* Fix long slurmd stop time when waiting on the slurmd to register.
* Fix slurmstepd memleak when initializing cgroup plugins.
* Fix slurmstepd memleak when initializing cgroup plugins.
* scrun - Update scrun.lua example in `man 1 scrun` removing requirement to compile Lua with JSON support.
* Fix not applying constraints if CpuSpecList string is larger than 1024 chars.
* slurmrestd - Return 200 when querying a non existing partition. This affects the following endpoints: 'GET /slurm/v0.0.45/partition'
* slurmctld - Preserve intermediate job scheduling values to provide consistent scontrol show job output before and after reconfiguring or restarting the controller.
* Increase precision of time reported when timers issue warnings.
* scontrol - Print 'Job 12_23 not found' errors on stderr instead of stdout.
* stepmgr - handle when a steps requested ThreadsPerCore does not equal a nodes configured ThreadsPerCore
* Fix bug where requests from denied uids (i.e. "Users=-<user>") to skip, delete or view (if using PrivateData) reservations were not rejected properly. This bug only existed for clusters not using AccountingStorageEnforce=associations (including other options that imply enforcing associations)
* Fix rare potential race condition in x11 forwarding that could result in a double free.
* salloc/scrun/srun/slurmstepd - Move setting of SLURM_TASKS_PER_NODE to the controller.
* gpu/nvml - The --gpu-freq job submission options will now set the actual Memory/GPU clock frequencies rather than the "Applications clocks" frequencies if the installed version of NVML supports it.  This affects CUDA 11.3+ and prevents build errors in CUDA 13.0+ where the "Applications clocks" interface has been deprecated.
* gpu/nvml - Fix bug that prevented clock frequencies being reset on all GPUs at job completion when cgroups is constraining devices and there are multiple GPUs on the node.
* gpu/nvml - Fix bug that prevented --gpu-freq from being applied to the GPU clock frequency without specifying a memory clock frequency.
* Fixed SLURM_CLUSTER_NAME to be set to correct cluster when multiple clusters are available in a batch job.
* Respect arbitrary task distribution and return ESLURM_NOT_SUPPORTED if it is set together with an incompatible setting, namely topology/block, --spread-job, CR_LLN, pack_serial_at_end or bf_busy_nodes.
* slurmctld,slurmdbd: Avoid segfault when persistent connections fail to establish fully.
* Avoid non-needed numeric UID to user name translation when dumping node information node with unset reason for current node state. The following slurmrestd endpoints have changed: GET /slurm/v0.0.45/nodes GET /slurm/v0.0.45/node/{node_name} The following CLI commands have changed: scontrol show node {node_name} (--json|--yaml) scontrol show nodes (--json|--yaml)
* sinfo - Avoid non-needed numeric UID to user name translation when dumping node information node with unset reason for current node state changing: sinfo (--json|--yaml)
* slurmrestd - Add cores_per_socket to job submission to the following endpoints: GET /slurm/v0.0.45/job/submit GET /slurm/v0.0.45/job/allocate POST /slurm/v0.0.45/job/{job_id}
* slurmctld - Refuse RESPONSE_PING_SLURMD from incorrect nodes
* slurmctld - Skip MODE_3 HRes specific logic in backfill for job the do not request MODE_3 HRes.
* select/cons_tres - fix use-after-free of node_usage[].jobs
* Add status field to `scontrol ping --json` and `scontrol ping --yaml`.
* Add status field to '.components.schemas."v0.0.45_controller_ping"' to following endpoint: GET /slurm/v0.0.45/ping
* Add status field to `sacctmgr ping --json` and `sacctmgr ping --yaml`.
* Add status field to '.components.schemas."v0.0.45_slurmdbd_ping"' to following endpoint: GET /slurmdb/v0.0.45/ping
* slurmctld - Require authentication for the 'GET /readyz?verbose' endpoint, restricting access to only root and SlurmUser.
* slurmctld - Add threadpool to avoid overhead of creating new process threads which kernel freezes entire process to complete. This can be enabled with SlurmctldParameters=threadpool=enabled.
* Fix building with --with-jwt in a non-standard location.
* sacct - Add '.jobs[].sluid' field to the following commands: 'sacct --json', 'sacct --yaml'
* slurmrestd - Add '.jobs[].sluid' field to the following endpoints: 'GET slurmdb/v0.0.45/job', 'GET slurmdb/v0.0.45/jobs'
* slurmrestd - Add 'GET /healthz', 'GET /readyz', and 'GET /livez' endpoints.
* Fix potential glibc deadlock when tearing down the extern step when x11 forwarding is enabled.
* Fix FreeBSD build for --format=binary files, which are currently used for command help and usage text.
* Packaging - MUNGE is now a weak dependency to Slurm RPM and DEB packages, and can now be optionally installed or removed (installed by default).
* Add SuspendTime as a NodeName parameter in slurm.conf, enabling per-node power save configuration.
* slurmrestd - Deprecate ignored reason_uid field from the following endpoints: POST /slurm/v0.0.42/nodes/ POST /slurm/v0.0.42/node/{node_name}
* slurmrestd - Deprecate ignored reason_uid field from the following endpoints: POST /slurm/v0.0.43/nodes/ POST /slurm/v0.0.43/node/{node_name}
* slurmrestd - Deprecate ignored reason_uid field from the following endpoints: POST /slurm/v0.0.44/nodes/ POST /slurm/v0.0.44/node/{node_name}
* slurmrestd - Deprecate ignored reason_uid field from the following endpoints: POST /slurm/v0.0.45/nodes/ POST /slurm/v0.0.45/node/{node_name}
* Adding new archive/purge options to allow for explicit archiving of job_scripts and job_env without jobs.
* When the url_parser plugin does not load, change the log from an error to a warning. This plugin is optional and may not always be built.
* Fix rpmbuild slurm.spec --with selinux.
* Use internal dependency generator in slurm.spec.
* Switch to pkgconfig detection of many packages in slurm.spec.
* Add reqTRES components to the clonensscript and clonensepilog environment variables.
* Name all process POSIX threads consistently with format "worker[{index}]" when threads are not otherwise given a special name.
* slurmctld - Fix unresponsive nodes not being marked DOWN in clusters with frequent reconfigurations, as each reconfigure was updating the SlurmdTimeout countdown.
* slurmctld - If a node is replaced in a reservation mark that the reservation state changed. With bf_continue enabled, this fixes backfill potential incorrect planning if reservation node is replaced mid-cycle.
* Cover rare edge case in job queue sorting.
* Add job priority value to SLURM_RESUME_FILE.
* sbatch/srun/salloc - Make --gres=gpu:N and --gpus-per-node mutually exclusive.
* switch/hpe_slingshot - Add SwitchParameters=fm_authdir_ctld option.
* slurmd - Support POSIX signal SIGPROF to log debug state.
* slurmd - Increase default conmgr_max_connections from 50 to 512 to avoid connections being deferred on nodes with higher CPU counts.
* slurmd - Avoid connection deferring starting after hitting conmgr_max_connections.
* slurmrestd - Add 'partitions[].flags' field to the following REST API endpoints: 'GET /slurm/v0.0.45/partition/{partition_name}' 'GET /slurm/v0.0.45/partitions/'
* scontrol - Add 'partitions[].flags' field to the output of 'scontrol show partitions --{json/yaml}'
* slurmrestd - Changed the 'partitions[].cpus.task_binding' field from an int32 to a flag array. This affects the following REST API endpoints: 'GET /slurm/v0.0.45/partition/{partition_name}' 'GET /slurm/v0.0.45/partitions/'
* scontrol - Changed the 'partitions[].cpus.task_binding' field from an int32 to a flag array in the output of 'scontrol show partitions --{json/yaml}'.
* slurmrestd - Add 'partitions[].preempt_mode' field to the following REST API endpoints: 'GET /slurm/v0.0.45/partition/{partition_name}' 'GET /slurm/v0.0.45/partitions/'
* scontrol - Add 'partitions[].preempt_mode' field to the output of 'scontrol show partitions --{json/yaml}'
* slurmrestd - Deprecate 'partitions[].defaults.memory_per_cpu', 'partitions[].maximums.memory_per_cpu', and 'partitions[].maximums.shares' fields in the following REST API endpoints: 'GET /slurm/v0.0.45/partition/{partition_name}' 'GET /slurm/v0.0.45/partitions/'
* scontrol - Deprecate 'partitions[].defaults.memory_per_cpu', 'partitions[].maximums.memory_per_cpu', and 'partitions[].maximums.shares' fields in the output of 'scontrol show partitions --{json/yaml}'
* Prevent incorrectly logging a false error indicating that reserved MPI ports were being leaked when requeuing jobs.
* slurmd - Log warning when incoming RPCs have deferred processing.
* slurmd - Fix deferred incoming RPCs not sleeping for the expected duration.
* Fix issue where node state CLOUD was being automatically added to slurm.conf nodes in state EXTERNAL on a reconfigure or restart of the slurmctld.
* slurmrestd - Add slurm endpoint to update or create partitions: 'POST /slurm/v0.0.45/partitions'
* slurmrestd - Add slurm endpoint to delete partitions: 'DELETE /slurm/v0.0.45/partition/{partition_name}'
* jobcomp - Send 'admin_comment' and 'comment' fields. 'comment' is only sent if a new JobCompParams of 'send_comment' is also configured. This only applies to elasticsearch and kafka plugins.
* sacct - Print the submit_line for steps when using --json.
* sbatch - `--uid` argument is now deprecated.
* sbatch - `--gid` argument is now deprecated.
* Change API to accept slurm_step_id_t instead of uint32_t job_id. This will break compatibility with any code linked against older libslurm, unless Slurm is compiled with SLURM_BACKWARD_COMPAT.
* The REST API will now respond with an HTTP response code of 422 by default (previously 500) if the HTTP request is properly formatted but the slurmctld or slurmdbd are unable to process it.
* Fix being able to show specific step ids in stepmgr allocations.
* Allow partitions to be dynamically created or updated with PreemptMode=Priority.
* srun - Log diagnostics upon receiving SIGPROF.
* slurmd - Load auth/jwt when available for HTTP authentication.
* slurmd - Load serializer plugins for HTTP endpoints.
* Fix problem when using sacctmgr to remove a default account for a user when more than one is set.
* Add and start using new compress plugin interface.
* scontrol - Display node's suspend_time.
* slurmrestd - Add node suspend_time to following endpoints: GET /slurm/v0.0.45/nodes/ GET /slurm/v0.0.45/node/{node_name}
* Add faster scheduling path for single-node jobs.
* srun/salloc - emit a warning if X11 forwarding is requested together with multi-cluster, as this configuration is unsupported.
* Fix race condition between sequential sruns causing incorrect resource allocation or step creation failure.
* stepmgr - Speedup step creation due to busy MpiParams=ports.
* stepmgr - Avoid logging harmless "Connection reset by peer" when waking up pending steps.
* Authentication is now available for querying any /metrics endpoints. By default, no auth is required. If PrivateData is set, only SlurmUser and root will be allowed. If PrivateData and MetricsParameters=ignore_private_data are set, no auth is required. If MetricsAuthUsers is set, only the users in this list (plus SlurmUser and root) are allowed to query, if correctly authenticated, no matter the values PrivateData and MetricsParameters=ignore_private_data.
* srun - Remove old broken logic that was used to retry step launch three times if exit code 108 was returned by a task that indicated Open MPI failed to open a reserved port.
* scontrol - Allow updating InstanceId for batches of nodes as is possible for updating NodeAddr and NodeHosts.
* scontrol - Allow updating InstanceType for batches of nodes as is possible for updating NodeAddr and NodeHosts.
* Make Resource (remote license) case sensitive by enabling slurmdbd.conf "Parameters=PreserveCaseResource"
* slurmstepd - Prevent crash when UnkillableStepTimeout is reached and Slurm is configured with --enable-memory-leak-debug.
* srun - Added AuditRPCs debug logs for step launch message handler
* sbatch - Now scrubs SLURM_JWT from the environment, if present at submission time, to allow srun to work properly within batch scripts. Previously this had to be done in the job script itself by calling `unset SLURM_JWT` before any calls to srun.
* srun - Now refuses to start and prints an error message if auth/jwt is used.
* Improve archive/purge performance on active clusters.
* avoid calling mysql_db_commit() twice on successful purge cycles.
* PreemptExemptTime now also applies to PreemptMode=suspend,gang when running with preempt/partition_prio.
* Fix sacctmgr silently ignoring trailing characters in numeric options.
* Fix jobs sometimes failing to be held after a requested reservation is purged.
* Fix some reservation updates not getting through into the database.
* Fix reservation node replacement retaining nodes after they are moved out of the reservation's associated partition.
* persist_conn - Avoid races at shutdown with service connections
* persist_conn - Avoid error when thread tries to join itself
* persist_conn - Fix race between detached thread join and its termination
* persist_conn - Fix crash accessing service connection if it was freed
* persist_conn - Avoid thread deadlock at daemon shutdown
* slurmdbd - Avoid deadlock due to race condition during shutdown when federation is active.
* service_connection - Fix crash when cluster list is already empty
* Add option to use UUID strings with CUDA_VISIBLE_DEVICES.
* Add new topology/ring plugin.
* Add topology-aware task distribution for steps. Tasks and step nodes now follow topology order by default when a topology plugin is configured. Use CR_NO_DIST_TOPO_BLOCK to disable.
* Fix sbcast with auth/slurm when user doesn't exist on slurmctld.
* Fix stepmgr crash with using sbcast with auth/slurm.
* Fix memory leak in stepmgr stepd.
* slurmdbd - Add DisableRollups option to slurmdbd.conf.
* slurmctld - Fix possible hang during reconfigure due to slow client I/O due to timeout not being enforced.
* slurmctld - Fix possible hang during shutdown due to slow client I/O due to timeout not being enforced.
* slurmctld - Log warning when quiesce timeout is infinite.
* slurmctld - Avoid race condition during shutdown that could cause a crash while attempting to read from a connection.
* slurmrestd - Propagate connection errors in inetd mode exit code.
* Reject untrusted REQUEST_COMPLETE_PROLOG.
* sacctmgr - Drop "BOOT_TIME" from `sacctmgr show config` output.
* sview - Drop "BOOT_TIME" from "Database Config Info" window.
* slurmrestd - Fixed memory leak resulting from specifying an empty node_list in the request body of the following endpoints: 'POST /slurm/v0.0.4[3-5]/reservation' 'POST /slurm/v0.0.4[3-5]/reservations'
* Fix epilog running multiple times on TERMINATE_JOB retries.
* Fix parsing issue for GRES resources that contain a hyphen ("-") in their name when using sacctmgr.
* Add gpu statistics to metrics endpoints - metrics/nodes endpoint: slurm_nodes_gpus_alloc and slurm_nodes_gpus_total - metrics/partitions: slurm_partition_jobs_gpus_alloc and slurm_partition_gpus - metrics/jobs-users-accts: slurm_account_jobs_gpus_alloc and slurm_user_jobs_gpus_alloc
* Skip segmentation when job fits within a single segment and validate --segment option in client tools.
* Fix jobs getting stuck in COMPLETING state when PrologFlags=RunInJob is configured by passing EpilogMsgTime to slurmstepd.
* Fix external nodes incorrectly marked as not responding after state transitions such as drain/undrain or resume.
* Fix issue where non-stepmgr extern steps where trying to delete jobs from the fabric manager on job completion.
* Only initialize switch/hpe_slignshot collectives on the slurmctld and slurmstepd stepmgr.
* Report specific topology rejection reasons for pending jobs instead of generic "Resources" in scontrol/squeue output.
* Slurm cgroup/v2 paths are now constructed using the SLUID instead of the numeric job id.
* Add support for SLUID in all Slurm CLI tools and jobcomp plugins.
* Fix slurmctld segfault when scheduling on gres reservations after restarting slurmctld.
* srun - Add --ignore-signals option to prevent forwarding specified signals to running tasks.
* Ensure that a request for zero licenses does not prevent a job from running when all licenses are in-use or reserved.
* Show node as REBOOT_ISSUED when rebooting due to licenses or --reboot.
* Add HealthCheckNodeState=REBOOT_ONLY option to run the HealthCheckProgram only when a node reports a reboot.
* slurmctld - Fix crash on startup due to race condition when I/O is processed before the connection (conn) plugin finishes initialization.
* slurmdbd - Fix crash from race condition during shutdown when a persistent connection closes its database connection after the accounting_storage plugin has already unloaded.
* slurmctld - Fix reoccurring reservation overlap check that failed to detect conflicts with future occurrences across Daylight Saving Time transitions.
* slurmctld - Fix incorrect error messages from reoccurring reservation overlap sanity checks.
* Update namespage_g_stepd_delete() API call to accept a stepd_step_rec_t instead of just the job id.
* Prevent deadlock when replacing nodes in reservations.
* Fix slow scheduling for multi-segment jobs with topology/block when blocks have fewer available nodes than the requested segment size.
* Heterogeneous jobs are now validated against all applicable limits even when different for each component.
* srun: add --exclusive=allocation to allow exclusive node allocation for job initiation, but does not enable --exact.  Allows sharing of CPU resources between tasks assigned to same node.
* serializer/url-encoded - Allow non-NULL terminated strings to be passed to serialize_p_string_to_data().
* serializer/yaml - Prevent fataling if the size of a yaml configuration file is a multiple of 4096 bytes.
* slurmrestd - Add mem_update_delay and mem_update_margin to job submission to the following endpoints: GET /slurm/v0.0.45/job/submit GET /slurm/v0.0.45/job/allocate POST /slurm/v0.0.45/job/{job_id}
* slurmrestd - Add mem_update_delay and mem_update_margin to job info to the following endpoints: GET /slurm/v0.0.45/jobs GET /slurm/v0.0.45/job/{job_id}
* Add --mem-update option to sbatch/salloc/srun to automatically reduce a job's memory limit after a configured delay based on actual RSS usage. Memory reduction for running jobs is also available via scontrol update job MinMemoryNode.
* Set the in-memory QOS priority to 0 after INFINITY is handled by slurmdbd.
* acct_gather_profile/influxdb - Fix ProfileInfluxDBDefault to default to None as documented.
* acct_gather_profile/influxdb - Don't send empty HTTP POSTs to InfluxDB at the end of unprofiled steps.
* acct_gather_profile/influxdb - Make ProfileInfluxDBRTPolicy optional. When unset, InfluxDB writes to the default retention policy.
* acct_gather_profile/influxdb - Add ProfileInfluxDBFlags=grouped_fields which emits one series per measurement (task, network, filesystem, energy) with all fields sharing a common tag set, rather than one series per field. This reduces series cardinality and eliminates field name ambiguity between plugins. Special step identifiers (batch, extern, interactive) render as human-readable names in the step tag.
* acct_gather_profile/influxdb - Add ProfileInfluxDBExtraTags to attach cluster, sluid, uid and/or user tags to every measurement. Requires ProfileInfluxDBFlags=grouped_fields.
* job_submit/lua - Expose the job_desc's core_spec and thread_spec.
* lua - Expose the job record's core_spec and thread_spec.
* slurmrestd - Modified select_type field to match slurm.conf naming in the following endpoints: GET /slurm/v0.0.45/partitions GET /slurm/v0.0.45/partition/{partition_name}
* squeue - Modified select_type field to match slurm.conf naming in the '--json' and '--yaml' outputs for partitions.
* scontrol - Modified select_type field to match slurm.conf naming in the '--json' and '--yaml' outputs for partitions.
* slurmrestd - Modified select_type field to match slurm.conf naming in the following endpoints: GET /slurm/v0.0.45/jobs GET /slurm/v0.0.45/job/{job_id}
* squeue - Modified select_type field to match slurm.conf naming in the '--json' and '--yaml' outputs for jobs.
* scontrol - Modified select_type field to match slurm.conf naming in the '--json' and '--yaml' outputs for jobs.
* scontrol - Add support for the following: scontrol show config --json scontrol show config --yaml
* slurmrestd - Add new endpoint to query loaded configuration: GET /slurm/v0.0.45/conf
* Remove need for using cgroup logic to determine newest job for pam_slurm_adopt.
* Fix the formatting of array job ids returned in error messages from the following RPCs: REQUEST_UPDATE_JOB, REQUEST_SUSPEND, and REQUEST_JOB_REQUEUE.
* slurmrestd - Add following endpoint to allow for requeuing a job: GET /slurm/v0.0.45/job/{job_id}/requeue
* slurmrestd - Add following endpoint to allow for requeuing a job: POST /slurm/v0.0.45/jobs/requeue
* Only build and install the html documentation with make html and make install-html respectively.
* ExclusiveTopo now implies Oversubscribe=Exclusive.
* Exclusive=[NO|NODE|USER|TOPO] replaces ExclusiveUser and ExclusiveTopo in slurm.conf when defining partitions.
* squeue, sinfo, and sview show oversubscribe and exclusive consistently; job and ResumeProgram environments export SLURM_JOB_OVERSUBSCRIBE and SLURM_JOB_EXCLUSIVE.
* Add exclusive to the SLURM_RESUME_FILE.
* slurmrestd - Add the following endpoint to query slurmdb config: GET /slurmdb/{data_parser}/conf
* sacctmgr - Add mime-type support for dumping config sacctmgr show config --json sacctmgr show config --yaml
* slurmstepd - Fix segfault when multiple terminate RPCs race with unkillable process monitor.
* slurmctld - Add 'GET /v0.0.45/conf' endpoint to query current configuration.
* slurmd - Add 'GET /v0.0.45/conf' endpoint to query current configuration.
* oci.conf - Add %Z expansion pattern to the step's original working directory.
* Add Exclusive and Oversubscribe fields to slurmdbd
* Add new option (Autodetect=full) where slurm tries each plugin until it finds a gpu.
* Enable automatic GPU detection for dynamic nodes created with slurmd -Z.
* Add `SharedPool` flag for use when creating new slurmdb Resources. Enables multiple clusters to fully share the set of remote licenses.
* Fix gcc-16 build errors.
* Add PowerAction as the way to specify distinct power up/down scripts. Power down scripts can now be run on the controller or compute host.
* Add 'force' option to scontrol power up <node>, allowing admins to ignore typical checks when manually powering up a node.
* Add force option to scontrol reboot which ignores some checks, resets power and reboot states, and then actually reboots the nodes.
* Allow PowerActions for scontrol reboot and RebootProgram.
* Fix slurmstepd crash in jobacctinfo_aggregate() handling when SlurmctldParameters=enable_stepmgr and JobAcctGatherType=jobacct_gather/none are set.
* Add topology/torus3d plugin for 3D torus network topologies. Supports placement-aware scheduling of contiguous sub-cubes, configurable via topology.yaml with torus dimensions, node regions, and placement shapes. Includes --segment support for multi-placement jobs and torus3d{}/torus3dwith{} hostlist functions.
* Send SIGCONT+SIGTERM+SIGKILL instead of SIGKILL when a PMI-1 client calls PMI_Abort(). In PMI1 the client is directly contacting slurmstepd to terminate the step.
* Send a SIGCONT+SIGTERM+SIGKILL instead of just a SIGKILL when a task calls PMI2_Abort(). This also covers PMI1 server side change.
* Send SIGCONT, SIGTERM and SIGKILL to the remaining tasks after a task has returned with a non-zero return code, crashed or called PMIx_Abort() instead of sending just a SIGKILL. This allows tasks to catch TERM signal and do proper cleanup. KillWait will be honored when the task does not end with the TERM signal.
* burst_buffer/lua - Expose oversubscribe and exclusive values.
* burst_buffer/lua - remove the job_info.shared accessor. This has been replaced by job_info.oversubscribe and job_info.exclusive.
* data_parser/v0.0.45 - The 'shared' field on jobs is deprecated; use 'oversubscribe' and 'exclusive' instead.
* PerlAPI - Remove job_info_t.shared.
* Fix slurmd >= 25.05 crash on HetJob step launches from srun <= 24.11.
* Do not allocate maintenance nodes to new reservations.
* Restore the ability for srun --test-only --jobid=# to get the estimated start time of an existing pending job.
* No longer reset the estimated start time to 0 of the job targeted by srun --test-only --jobid=#. Instead keep the estimated start time set by the backfill scheduler.
* Fix bitmap memory leak on node name resolution error when updating AvailableFeatures on a node.
* Fixed bug where an update to AvailableFeatures gives an "Invalid argument" error when trying to update "NodeName=ALL".
* srun - Fixed minor memory leaks from job description message.
* topology/flat - Add alpha_step_rank option to sort step nodes alphabetically by name when picking step nodes and laying out tasks.
* Fixed exit code of sh5util with -h and -V
* sacctmgr - when running an interactive archive dump, use the duration from slurmdbd.conf when none is given.
* Allow tokens used with auth/jwt to provide full identity info. This complements the "use_client_ids" support for auth/slurm to allow the Slurm control plane to be operated without directory services.
* Fix spelling of "TDB" to "TBD" when printing pending step ids.
* Implement async job steps, enabling stepmgr to queue, launch, and manage job steps.
* Use close_range() syscall to close file descriptors when launching new processes.
* namespace/linux - Add support for custom mount options and paths for each target directory.
