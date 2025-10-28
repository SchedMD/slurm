## Changes in 25.11.0rc1

* slurmd - Avoid segfault during startup when /sys/ is not mounted correctly and gpu/nvidia plugin is configured.
* Fix compilation when building with SLURMSTEPD_MEMCHECK == 1.
* slurmresd - Catch use-after-free bugs for closed connections.
* Fix building with libyaml in a non-standard location.
* Suppress false error messages when system.slice cgroup is not present and EnableControllers is set.
* Adjust the OOM score of slurmstepd processes from -1000 to -999 to make them killable. This change addresses cases where slurmstepd consumes more memory than what is allocated to the job's cgroup. Previously, being unkillable could cause the process to get stuck during memory allocation, fully occupy a CPU core, and flood the kernel logs.
* slurmrestd - Prevent crash when an empty request is submitted to 'POST slurm/*/job/submit' endpoints.
* Allow tres-bind on steps with only one task.
* Fix INVALID nodes going DRAIN after a slurmd restart without any gres.conf modification.
* slurmrestd - Prevent potential crash when using the 'POST /slurmdb/*/accounts_association' endpoints.
* data_parser/v0.0.44 - Make field 'association_condition' required to prevent a slurmrestd crash when the field is not provided. This affects the following endpoints: 'POST /slurmdb/v0.0.44/accounts_association/'
* Add no_tag as a possible parameter to environment SLURMRESTD_YAML or environment SLURM_YAML to disable dumping YAML datatype !!tags for CLI commands supporting --yaml output.
* Fix non-fatal "No such file or directory" build errors when building on *EL systems.
* Slurmd will now unload gpu plugin after configuration is over, unless acct_gather_energy/gpu is set.
* Improve stepd termination messages for non-zero return codes.
* Fix double-deducting licenses when recovering COMPLETING jobs from state.
* Remove AccountingStorageUser option from slurm.conf.
* data_parser/v0.0.44 - Add five reservation flags that were missing from the RESERVATION_FLAGS array. This affects scontrol show reservation --{json|yaml} and the following REST API endpoints:
  * 'GET /slurm/v0.0.44/reservation/{reservation_name}'
  * 'GET /slurm/v0.0.44/reservations'
  * 'POST /slurm/v0.0.44/reservation'
  * 'POST /slurm/v0.0.44/reservations'
* Fix Slurm components that depend on libjson-c not setting RUNPATH when requested, which could cause them to fail at runtime.
* Fix Slurm components that depend on libyaml not setting RUNPATH when requested, which could cause them to fail at runtime.
* slurm.spec - Remove duplicated --with-freeipmi
* slurmrestd - Fix crash for /reservations endpoint when a valid reservation_desc_msg body with a specified partition but no node_list string array is submitted.
* Fix memory leak in configless mode when resolving the slurmctld address.
* slurmrestd - Avoid segfault with `-d list` args when no data_parser plugins can be read by process which requires removal of plugins from LD_LIBRARY_PATH or RPATH or some other administrative action as they are always installed with Slurm.
* Change pam_slurm_adopt install location for the Debian package to the multiarch location.
* Support conversion of JSON/YAML dictionary/object to list/array where automatic type inferencing is supported.
* slurmrestd - Avoid giving database connection hex address in warnings when a slurmdbd query "found nothing". All `GET /slurmdb/v0.0.*/*` endpoints are affected by this change.
* slurmrestd - Avoid giving database connection hex address in warnings when a slurmdbd query "reports nothing changed". All `GET /slurmdb/v0.0.*/*` endpoints are affected by this change.
* slurmrestd - Avoid giving database connection hex address in warnings when a slurmdbd query "failed". All `GET /slurmdb/v0.0.*/*` endpoints are affected by this change.
* slurmrestd - Avoid giving database connection hex address in errors when a slurmdbd query "failed". All `GET /slurmdb/v0.0.*/*` endpoints are affected by this change.
* slurmrestd - Avoid giving database connection hex address in errors when a slurmdbd query "failed". All `POST /slurmdb/v0.0.*/*` endpoints are affected by this change.
* slurmrestd - Avoid giving database connection hex address in errors when a slurmdbd query "failed" to commit changes. All `POST /slurmdb/v0.0.*/*` endpoints are affected by this change.
* Add log_user() to be callable by all Lua scripts.
* slurmctld - Relock the controller's pidfile on a reconfigure.
* Add --with cgroupv2 option to slurm.spec to assist building rpms on systems that require cgroupv2 support.
* Pass existing cluster id from slurmctld to slurmdbd when registering the cluster with accounting for the first time
* scontrol - Improve error message when attempting to perform an invalid state update on a node, e.g. RESUME a node that is currently in a state from which resuming is not possible.
* Set exit_code when "scontrol listjobs/liststeps" does not find jobs/steps.
* Fix rejecting some valid jobs that requested --sockets-per-node > 1 and --gres-flags=enforce-binding.
* For some jobs that use --gres-flags=enforce-binding, allocate the lowest numbered socket first. The previous behavior allocated the highest numbered socket first.
* Fix some situations where --gres-flags=enforce-binding was not respected for jobs that request --gpus-per-task and multiple nodes.
* sacctmgr - Catch and reject attempts to pass invalid account flags= arguments.
* Avoid returning an "INVALID" Accounting flag in JSON or YAML output which was never valid flag.
* sacctmgr - Catch and reject attempts to pass invalid Cluster flags= arguments.
* Avoid returning an "INVALID" Cluster flag in JSON or YAML output which was never a valid flag.
* Avoid returning an "INVALID" Association flag in JSON or YAML output which was never a valid flag.
* sacctmgr - Catch and reject attempts to pass invalid QOS flags.
* Avoid returning an "INVALID" QOS flag in JSON or YAML output which was never a valid flag.
* slurmdbd.conf can have permissions of 640 or 600.
* Avoid possible race condition that could cause a hang if process should shutdown before all conmgr worker threads have started.
* Add HttpParserType parameter to slurm.conf
* Add UrlParserType parameter to slurm.conf
* Add http_parser/libhttp_parser plugin.
* slurmrestd - Switch to using http_parser/libhttp_parser plugin for http processing.
* topology/block - Add new TopologyParam=BlockAsNodeRank option to reorder nodes based on block layout. This can be useful if the naming convention for the nodes does not natually map to the network topology.
* Fix memory leak in acct_gather_profile_influxdb.c.
* Allow --segment to be bigger than base block size.
* sacctmgr - Allow an operator to alter the allocated TRES of a job.
* sacctmgr - add new option to set fixed runaway jobs as FAILED or COMPLETED.
* Job option --hint is now mutually exclusive with both --cores-per-socket and --sockets-per-node.
* Add HealthCheckNodeState=START_ONLY option.
* Add a new CliFilterParameters=cli_filter_lua_path=<path> to slurm.conf, enabling the configuration of an absolute path to the cli_filter.lua script when cli_filter/lua is configured.
* Allow removing a user's default association when AllowNoDefAcct=yes.
* scontrol - Fix "KillOnInvalidDependent" typo in show jobs output.
* scontrol - Use whitespace to separate all key-value pairs in show jobs output. Previously, commas were used between job flags.
* scontrol - Fix GresAllowTaskSharing not always appearing in show jobs output.
* slurmrestd - Log URLs that fail to parse under the debugflag=data instead as errors or debug5 as invalid user provided is not directly an error of the slurmrestd daemon itself.
* Fix use-cases incorrectly rejecting job requests when MaxCPUsPer[Socket|Node] applied and CPUSpecList/CoreSpecCount configured.
* slurmrestd - Specify listening on UNIX socket by giving the "unix://" prefix instead of "unix:" prefix for the URL scheme.
* Don't subtract 128 from sacct returned exit codes for the codes above 128.
* Fix issue resolving socket max segment size (MSS) via kernel to more accurately size buffered communications.
* mpi/pmix - Restore the ability to follow symlinks on PMIx cli/lib temporal directory creation when considered trusted (i.e. SlurmdSpoolDir, TmpFS, PMIxCliTmpDirBase). This ability was lost in Slurm 23.02.6.
* Invalid use of -= or += aborts sacctmgr commands with an error.
* Add SLURM_JOB_SEGMENT_SIZE environment variable to salloc and srun job environments when --segment is set.
* Remove the trailing space after AdminComment, SystemComment, Comment, and Extra fields in scontrol show jobs output.
* slurmdbd - Make sure locks are appropriate when modifying or removing associations while filtering by QOS.
* defaultqos is not set when loaded from dump file
* Add display for default qos when listing users and not specifying "withassoc"
* slurmctld - Always close rejected incoming RPC connections to avoid reading another incoming RPC request.
* slurmctld - Log runtime errors from global section of Lua scripts during loading.
* Allow gpu shared gres to use RestrictedCoresPerGPU.
* slurmrestd - Fix memory leak that happened when submitting a request body containing the "warnings", "errors", or "meta" field. This affects the following endpoints: 'POST /slurmdb/v0.0.4*/qos'
* slurmrestd - Prevent triggering a fatal abort when parsing a non-empty group id string. This affects all endpoints with request bodies containing openapi_meta_client group field. It also affects the following endpoints:
  * 'GET /slurmdb/v0.0.4*/jobs'
  * 'POST /slurm/v0.0.4*/job/submit'
  * 'POST /slurm/v0.0.4*/job/{job_id}'
  * 'POST /slurm/v0.0.4*/job/allocate'
* Add the ability to gate access to a reservation by QOS.
* Add the ability to gate access to a reservation by Partition.
* scontrol - Add "SubmitLine" field to show job subcommand's output.
* slurmctld - Prevent an invalid read and a possible crash by rejecting any arbitrary distribution jobs that do not specify a task count equal to the number of node names in their node list. This does not affect srun, salloc, or sbatch if -n is not used since they set the default task count.
* slurmrestd - Load slurmctld and slurmdbd OpenAPI plugins by default when slurmdbd accounting is configured or only the slurmctld OpenAPI plugin otherwise.
* slurmrestd - Improve error response message when URL is not found or not supported.
* Fix race condition that could result in a syslog message being lost right immediately before executing a script.
* Fix race condition that could result in log or scheduling log message being lost or misdirected immediately before executing a script.
* Add support for "+=" and "-=" syntax when modifying TRES options via "sacctmgr modify". Supported for QoS, User, Account and Cluster entities.
* slurmctld - Avoid falsely setting partially closed sockets as stale.
* slurmctld - Check if persistent connections have gone stale before attempting to respond to RPC requests.
* slurmctld - Enforce stale connection check on RPCs with asynchronous I/O.
* TaskProlog and TaskEpilog no longer have to be fully qualified pathnames.
* When configless is enabled, TaskProlog and TaskEpilog scripts can now be pushed to slurmds if the script files are in the same directory as slurm.conf.
* When replacing nodes due to REPLACE flag, do not pull nodes from MAINT reservations.
* Add a new --spread-segments option to salloc/sbatch/srun.
* tls/s2n - Fix PMIx server not working.
* slurmrestd - Add several more verbose errors for rejected requests in slurmrestd's logs instead of "Unspecified error".
* slurmrestd - Add specific error when Content-Length header is too large for any HTTP request.
* slurmrestd - Fix a bug in the "GET /slurm/v0.0.4*/node/{node_name}" endpoint where the node's `partitions` field would be incorrectly populated.
* Log Lua backtrace on Lua script failures while loading scripts when debugflags=script and at at least log level debug when Slurm is linked to all Lua releases after version 5.4.6.
* Add 'MaxPurgeLimit' to 'slurmdbd.conf', replacing constant 50,000 limit.
* slurmrestd - Avoid logging redundant "Unspecified error" after failing to load TLS certificates during startup.
* Fix gres enforce-binding not allowing multi-socket tasks with gres-per-task requests.
* Add limited support for the conjunction of "--segment" and "--nodelist" ("-w") for topology/block.
* squeue - Add new --running-over and --running-under options, used to filter jobs based on their runtime.
* Fix the 'is-busy' check for a node shared between partitions.
* Fix incorrect use of bind() on BSD platforms.
* Fix handling of strigger --fail/-F event.
* slurmctld - Check connections can read and write when testing if connection has gone stale for incoming RPCs.
* Fix power save scripts that close stderr and stdout from being killed at shutdown after 10 second timeout.
* slurmctld - Avoid edge cases causing file descriptors to never close when an incoming RPC connection fails.
* slurmctld - Prevent the gang scheduler from hitting a segfault on restart, reconfigure, or when using 'scontrol delete partition' if all partitions are removed.
* data_parser/v0.0.44 - Added missing node state flag BLOCKED to parsable node states. This affects 'scontrol show nodes --{json|yaml}', 'sinfo --{json|yaml}', and the following REST API endpoints:
  * 'GET /slurm/v0.0.44/nodes'
  * 'GET /slurm/v0.0.44/node/{node_name}'
* scontrol - Fix ping showing the wrong hostname when using the --cluster option. The correct cluster name will now be displayed in the output when using --cluster.
* Job stderr string will be blank in data_parser output, scontrol and sview unless it has been explicitly set in the job. Users must assume that stderr will default to the stdout path in that case.
* Use the node_name instead of hostname in file name creation.
* Set CgroupPlugin=disabled by default when built without cgroup support.
* CgroupPlugin=Autodetect will always return "disabled" when Slurm is built without cgroup support.
* Do not build cgroup plugins on OpenBSD.
* mpi/pmix - The PMIx server will no longer store the node's HWLOC topology in the job-level information under the PMIX_LOCAL_TOPO key by default. The new PMIxShareServerTopology mpi.conf option can be used to share the topology in PMIx v4+.
* slurmctld/slurmd - Fatal instead of crashing if no blocks or switches are defined in topology.conf when using topology/tree or topology/block.
* slurmrestd - Honor HTTP "Connection: Keep-Alive" header to enable persistent connections for HTTP/1.1 connections.
* slurmrestd - Reject HTTP Keep-Alive headers to enable persistent connections for HTTP/0.9 connections.
* slurmrestd - Ignore HTTP "Connection: Keep-Alive" headers for HTTP/1.1 connections as they are persistent connections without "Connection: Close" headers by default.
* slurmrestd - Ignore HTTP "Keep-Alive: *" headers as no valid parameters were ever defined instead of logging a warning.
* slurmrestd - Replies to unparsable or unsupported requests will be HTTP/1.1 formatted.
* Reimplement fs/lustre stats gathering to ensure it only accounts for usage since the beginning of the job, as it was previously incorrectly reporting usage since last stats reset. Also add the ability to gracefully handle reset/overflow of the Lustre counters.
* Remove node_features/knl_generic plugin.
* Limit conmgr_threads to 256 for all configuration parameters.
* slurmctld - Avoid potentially logging invalid address for info level logging of "slurmctld_req: received opcode %s from %pA uid %u".
* slurmrestd - Allow externally signed TLS certificates to be used for https:// sockets. Certificate no longer needs to be signed by the ca_cert_file that is used for internal Slurm TLS communications.
* Add a new 'reconfig_on_restart' to 'SlurmctldParameters' making a restart of slurmctld (a process restart that was not triggered by "scontrol reconfigure") trigger a reconfigure of all slurmd and sackd daemons.
* Skip attempting to resolve reserved user "nobody" (uid:99) using getpwduid_r().
* Skip attempting to resolve reserved group "nobody" (uid:99) using getgrgid_r().
* Always resolve group id 99 as group "nobody".
* Always resolve user id 99 as user "nobody".
* slurmctld,slurmd,slurmstepd,slurmdbd,slurmrestd,scrun: Avoid SEGFAULT during shutdown due to race condition with receiving POSIX signal when debugflags=conmgr.
* Avoid race condition when handling a TLS wrapped connection when the TLS plugin is not loaded.
* switch/nvidia_imex - Fix channel count being one less than the actual imex_channel_count configuration.
* switch/nvidia_imex - Add --network=unique-channel-per-segment option to allocate a unique IMEX channel per segment inside a job when using the topology/block plugin.
* switch/nvidia_imex - When the allocated number of IMEX channels hits imex_channel_count, new jobs will start pending instead of running without creating any IMEX channels.
* slurmrestd - Add environment variables SLURMRESTD_DEBUG_SYSLOG and SLURMRESTD_DEBUG_STDERR to allow control of logging targets.
* slurmrestd - Add support for the SLURM_DEBUG environment variable.
* Add a new --consolidate-segments option to salloc/sbatch/srun.
* openapi/util - Add openapi/util plugin for request not requiring a connection to the slurmctld or slurmdbd.
* slurmrestd - Load the openapi/util plugin by default.
* slurmctld,sackd,slurmdbd,slurmrestd: Limit the automatically detected conmgr thread count to 32. Avoid having too many threads on higher core count hosts.
* slurmd - Change default to SlurmdParameters=conmgr_threads=6.
* scrun - Change default conmgr thread count to 3.
* slurmstepd - Change default conmgr_threads=4 when SlurmdParameters=conmgr_threads=# is not defined.
* slurmctld,slurmd,slurmstepd,sackd,slurmdbd,slurmrestd: Log warnings when configured conmgr thread counts are outside of suggested range based on detected CPU counts.
* slurmd - Add --parameters option to append additional options to the SlurmdParameters option from slurm.conf. Can also be used with 'slurmd -C' to print alternate cpu topologies. (E.g., 'slurmd -C --parameters l3cache_as_socket'.)
* Add per-node Parameters option, allowing node-specific SlurmdParameters.
* Manually updating a node's state to IDLE with 'scontrol update nodename' is no longer allowed if the node is in state REBOOT_ISSUED. This is to prevent the REBBOOT_ISSUED state from not automatically clearing once the reboot finishes.
* openapi/util - Add endpoints to convert hostlist expressions to an array of hostnames and vice versa:
  * 'POST /util/v0.0.44/hostnames'
  * 'POST /util/v0.0.44/hostlist'
* slurmrestd - Return HTTP_STATUS_CODE_ERROR_UNPROCESSABLE_CONTENT if querying an endpoint results in ESLURM_DATA_PATH_NOT_FOUND error.
* data_parser/v0.0.44 - Avoid hitting an xassert by return a DATA_TYPE_LIST on error when dumping a HOSTLIST_STRING without complex mode instead of returning DATA_TYPE_NULL.
* slurmdbd - Allow slurmdbd.conf to be owned by either SlurmUser or root instead of just SlurmUser.
* Add new DisableArchiveCommands parameter to slurmdbd.conf to disable "sacctmgr archive" commands.
* Prevent jobs submitted to multiple partitions from being killed when removing a partition from slurm.conf and restarting or reconfiguring the slurmctld. Pending jobs will remain pending with the remaining partitions. If a job's running partition, or all its partitions, are removed, the job will get killed with JobState=NODE_FAIL and Reason=PartitionDown. Jobs killed due to removing partitions from slurm.conf cannot be requeued.
* slurmctld - Debug Log connection manager diagnostics on receiving POSIX signal SIGPROF.
* slurmscriptd - Debug Log connection manager diagnostics on receiving POSIX signal SIGPROF.
* slurmstepd - Debug Log connection manager diagnostics on receiving POSIX signal SIGPROF.
* slurmrestd - Debug Log connection manager diagnostics on receiving POSIX signal SIGPROF.
* Allow external jobs to be submitted without requiring the current working directory be set.
* Fix srun ntasks calculation when nodes are requested using a min-max range and --ntasks is not used.
* Add CgroupSlice option to cgroup.conf to allow changing the default "system.slice" slice when starting slurmd and slurmstepd. If slurmd is started with a unit file, modifying the unit and adding Slice= into the [Service] section is needed. After making this change in cgroup.conf, the old slurmstepd scopes need to be terminated before starting slurmd again.
* Reject invalid dependencies with more than one dependency separator type.
* Reject dependency specifications with trailing garbage as invalid syntax.
* Mark afterok/afternotok dependencies on a non-existent remote job id as failed. Previously the dependency would remain unfulfilled indefinitely.
* Federated clusters update every registered remote dependency, including duplicates. This prevents jobs from pending forever due to some dependencies never being fulfilled.
* Fix aftercorr dependency on remote jobs in a federation.
* srun - Fix not accepting incoming TTY connection from slurmstepd due to race condition with POSIX signals during step startup.
* Add MetricsType option to slurm.conf.
* Add slurmdb endpoints to update job database:
  * 'POST /slurmdb/v0.0.44/job/{jobid}'
  * 'POST /slurmdb/v0.0.44/jobs'
* Add CommunicationParameters=disable_http to slurm.conf.
* slurmctld - Load http_parser, url_parser and TLS plugins by default when CommunicationParameters=disable_http is not set in slurm.conf.
* slurmctld,slurmd - Add the following HTTP endpoints:
  * GET /
  * GET /livez
  * GET /readyz
  * GET /healthz
* acct_gather_profile/influxdb - Add new acct_gather.conf option of ProfileInfluxDBFrequency to configure the frequency at which data is sent to InfluxDB (or disable buffering entirely if set to 0). Defaults to 30s.
* Fix slurmdbd error triggered by "sreport user topusage" when trying to get data from monthly usage tables.
* Fix systemd potentially losing track of the daemon PIDs.
* Fix fs_factor being 0 in the rest API when using PriorityFalgs=NO_FAIR_TREE.
* slurmctld - Enforce stale connection checks for rpc_queue enabled messages.
* switch/nvidia_imex - IMEX channels are now accessible in the batch step and interactive step.
* Validate QOS configuration even if AccountingStorageEnforce QOS is not set.
* Validate AllowQOS, DenyQOS have valid QOS's listed.
* Change schedule_cycle_sum to uint64_t to prevent overflows which will otherwise occur after 2^32 microseconds (71.6 minutes).
* scontrol - Fix regression where "scontrol update jobid=<id> qos=" was not considered a valid command.
* When using CR_CPU, prevent counting more cpus against a step then was allocated.
* slurmd - Load http_parser, url_parser and TLS plugins by default when CommunicationParameters=disable_http is not set in slurm.conf.
* sackd - Add --jwks-file and --key-file options to specify a non-default absolute path to .jwks and .key authentication key(s) file.
* sinfo --state/-t flag now outputs a more helpful error message when passed an invalid node state.
* When using CR_CPU, prevent counting more cpus against a step then was allocated.
* Fix CPU over-allocation with enforce-binding when ntasks is specified.
* data_parser/v0.0.44 - Add nodes field to slurm_license_info_t parser.
* Add a new mode of operation to HRes (MODE_3). In this mode, resources are allocated on a per-node basis, with the total consumption being calculated from the most granular layer and summed up into the successively higher layers.
* scontrol - return error when attempting to add node to a non-existent topology block.
* Allow cloud nodes to dynamically set topology at registration with slurmd --conf="topology=...".
* Add HTTP endpoints to expose Slurm metrics:
  * GET /metrics/jobs
  * GET /metrics/nodes
  * GET /metrics/partitions
  * GET /metrics/scheduler
  * GET /metrics/user-accts
* switch/hpe_slingshot - Reduce sharing of RGIDs on multi-NIC connected systems. Default to distributing RGIDs evenly across all available NICs.
* switch/hpe_slingshot - Add nic_distribution_count option to SwitchParameters.
* switch/hpe_slingshot - Add nic_distribution_count parameter to the --network job and step option.
* slurmstepd - Prevent the slurmstepd from segfaulting if the switch/hpe_slingshot plugin is enabled and SwitchParameters is not specified.
* openapi/slurmctld - Fix 'DELETE /slurm/v0.0.XX/node/{node_name}' error message so that it says an issue occurred while deleting instead of updating.
* Fix segfault with multi-node gres with PrologFlags=RunInJob.
* accounting_storage/mysql - Improve performance for 'sacct -T/--truncate' by skipping suspend table queries for unsuspended jobs.
* Change job_container/tmpfs plugin to be named namespace/tmpfs.
* Rename DebugFlags=JobContainer to DebugFlags=Namespace
* Add new namespace/linux plugin.
* slurmctld - Close connection earlier when reply on RPC enqueuing fails.
* Add SlurmctldParameters=enable_async_reply to slurm.conf.
* Add new "scancel --admin-comment" to allow admins to set the AdminComment while canceling jobs.
* Add support for database password rotation in slurmdbd through new StoragePassScript option.
* Add support for database password rotation for jobcomp/mysql through new JobCompPassScript option.
* Add interactive_step_set_cpu_freq parameter to the list of LaunchParameters in slurm.conf. This allows for the default cpu freq governor to be set on interactive steps.
* Removed testsuite from the release branch. The testsuite is available on master, and can be used against different releases now.
