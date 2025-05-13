## Changes in 25.05.0rc1

* slurmstepd - Harden the initialization of I/O file descriptors.
* slurmctld - Validate reservation data before scheduling in backfill to avoid doing an evaluation of nodes with outdated core/node bitmaps.
* Now sreport has its start times rounded down and the end times rounded up, rather than the start and end times being rounded to the nearest minute or hour, which was not intuitive.
* switch/hpe_slingshot - Reduce default acs to 2
* slurmctld - fatal() if the slurmctld is not running as SlurmUser when started with systemd.
* Remove implicit format_stderr in thread_id, so LogFormat=thread_id does not print to stderr anymore.
* Fatal squeue --only-job-state if used with an unsupported combination of parameters.
* Remove races during slurmdbd shutdown
* Sigkill user processes atomically by using cgroup.kill interface
* Update jobs' SLURM_CPUS_PER_TASK and SLURM_TRES_PER_TASK environment variables if they are modified before the job is allocated.
* Deprecate v0.0.43 *_CONDITION field "format" as it is not interpreted; users should instead process the resulting JSON or YAML object manually.
* Remove LogTimeFormat=format_stderr, logs and stderr will always print the same.
* slurmdbd - Fix race condition that could cause a crash during shutdown.
* Fix libslurm(db) install paths when PREFIX contains /usr(/local)/
* Add CommunicationParameters=host_unreach_retry_count=# to retry connecting when a host might be temporarily unreachable.
* Make "--spread-job" option works as documented
* data_parser/v0.0.43 - Deprecating the 'instances' field of the GET /slurmdb/v0.0.43/config endpoint. This field was never populated.
* Fix the --gpus-per-task option to allow a list of gpu types to be specified.
* Fix how the --tres-bind option was being implicitly set for the --[tres|gpu]_per_task options when multiple gpu types are specified. It now sets the per_task binding count to the sum of the types.
* data_parser/v0.0.43 - Avoid setting dumped value of JSON or YAML for a 64 bit float as "Infinity" or "NaN".
* data_parser/v0.0.43 - Avoid setting dumped value of JSON or YAML for a 128 bit float as "Infinity" or "NaN". Add warning when values are truncated on conversion of 128 bit float to 64 bit float.
* data_parser/v0.0.43 - Avoid setting dumped value of JSON or YAML as null when 64 bit integer value is Infinity or NaN.
* data_parser/v0.0.43 - Avoid setting dumped value of JSON or YAML as null when 32 bit integer value is Infinity or NaN.
* data_parser/v0.0.43 - Avoid misparsing integer of INFINITE64 as INFINITE
* Avoid dumping Infinity and NaN as values in JSON output.
* Remove an unnecessary thread creation in acct_gather_energy/gpu
* Correct DRAM units for some cpus in rapl energy gathering.
* srun - Disallow issuing incorrect [Het]Job resource allocation for some use-cases when specifying --het-group outside an allocation.
* Fix performance regression when dumping associations list to JSON or YAML.
* Stop expensive association resolving during job dumping in JSON and YAML while dumping job(s) for v0.0.43 endpoints.
* Stop expensive association resolving during association dumping in JSON and YAML while dumping job(s) for v0.0.43 endpoints.
* Avoid expensive lookup of all associations when dumping or parsing for v0.0.43 endpoints.
* sacctmgr - avoid freeing username in the username cache.
* sacctmgr - avoid leaking user_list in sacctmgr dump cluster
* Allow topology/block Block definitions to have fewer nodes than the minimum BlockSize.
* slurmrestd - Allow use of "-d list" option without loading slurm.conf.
* Allow topology/tree leaf switch definitions without child nodes.
* data_parser/v0.0.43 - Added field '.jobs[].segment_size' to the following endpoints: 'GET /slurm/v0.0.43/jobs' 'GET /slurm/v0.0.43/job/{job_id}'
* squeue - Add "SegmentSize" as a format option
* scontrol - Add "SegmentSize" to 'show jobs' (if set)
* sacct - Add "SegmentSize" as a format option
* data_parser/v0.0.43 - Added field '.jobs[].segment_size' to the following endpoints: 'GET /slurmdb/v0.0.43/jobs' 'GET /slurmdb/v0.0.43/job/{job_id}'
* Revert some commits that were preventing from some variables to be freed when reconfiguring the daemon. This was intentional because at that point in time reconfiguring daemons didn't involve spawning a new process. Now we want to free these variables as reconfigure spawns a new process, and the proper way to conserve the energy values must be the same logic for daemon reload.
* Fix issue where gres with upper case names are improperly registered
* sacct - Add "TimeLimit" and "TimelimitRaw" as a possible output value for steps.
* sacct - Add step time limit to output of 'sacct --{json|yaml}' to '.jobs[].steps[].time.limit'
* data_parser/v0.0.43 - Add field '.jobs[].steps[].time.limit' to the following endpoints: 'GET /slurm/v0.0.43/jobs' 'GET /slurm/v0.0.43/job/{job_id}' 'GET /slurmdb/v0.0.43/jobs' 'GET /slurm/v0.0.43/job/{job_id}'
* Show AllocTRES in a running step using sstat. The format will be generated for regular compute steps.
* Remove unneeded user lookup in slurmrestd when using MUNGE authentication (via rest_auth/local plugin).
* sched/backfill - make yield_rpc_cnt configurable
* Fix excessive reservation update rpcs when a node is down.
* slurmctld - fatal() on startup if certain core state files do not exist and it is not the first start of the cluster.
* sacctmgr - Automatically trim user strings to prevent having user strings containing whitespaces.
* Fix sbcast issue failing to broadcast file(s) to non-leader HetJob components when JobContainer plugin enabled.
* Call spank_fini() before pam_finish().
* fatal() when failing to load a serializer plugin
* Improve burst_buffer.lua error messages and job state reason descriptions. This clarifies when a job is held and gives specific reasons if loading the script failed.
* Add new FORCE_START flag reservation parameter to allow reoccurring reservations to start in the past.
* Avoid repeated "failed to generate json for resume job/node list" error with ResumeProgram when slurmctld was built without JSON-c.
* Do not ignore SIGTSTP in slurmctld, slurmscriptd, and slurmdbd which makes it possible to stop (ctrl-z) the processes.
* If a user is trying to modify an account along with it's associations break that into 2 commits instead of 1 just in case one doesn't have any change to it.
* Add optional Reason= argument to `scontrol power` command.
* Allow delimiting sinfo states with '+' to avoid shell misinterpretation. '&' is still a valid delimiter.
* Add support for sinfo negated node state filtering.
* topology/block - Rank nodes by name rather than configuration order.
* Remove the "sleep 100000000" process running within the extern step
* Fix slurmctld takeover leading to nodes drained when GRES configured without type in slurm.conf, but with type in gres.conf. This also fixes the ability to restore GRES configured by scontrol for slurmctld restart with -R.
* Remove truncation of syslog messages at 500 characters.
* slurmrestd - Fix possible memory leak when parsing arrays with data_parser/v0.0.43.
* Add partition field to sreport topuser if partition based associations are found.
* slurmrestd - The following endpoint was added: 'POST /slurm/v0.0.43/reservations/create/'
* slurmrestd - The following endpoint was added: 'POST /slurm/v0.0.43/reservation/update/{reservation_name}'
* slurmrestd - The following endpoint was added: 'POST /slurm/v0.0.43/reservations/update/'
* slurmrestd - Avoid a fatal when querying GET /openapi/v3
* interfaces/jobcomp - Add event argument to jobcomp_p_record_job_end().
* interfaces/jobcomp - Add new jobcomp_[g|p]_record_job_start() hook.
* jobcomp/kafka - Add support for sending job information when jobs start running. This can be optionally configured via two new JobCompParams of 'enable_job_start' and 'topic_job_start=<topic>'.
* MailType arguments set in the command line will override MailType arguments set in the batch script, and multiple --mail-type arguments will not be merged, honoring the documentation which states that multiple kinds of --mail-type can be set by providing a comma-separated list and making this behave like any other command line arguments.
* slurmctld,slurmrestd - Avoid race condition that could result in more connections accepted than configured via conmgr_max_connections.
* slurmctld - Improve incoming connection throughput by accepting new connections faster.
* slurmctld - Reduce latency in some responses times for incoming RPCs.
* Add new SchedulerParameter option to force job requeue in the situation that a cloud node failed to be resumed.
* Improve debug log message in slurm_recv_timeout() when EOF encountered from remote socket.
* Improve error logged when slurm_recv_timeout() encounters EOF when part of RPC has already been read from socket.
* Removed support for FrontEnd systems (--enable-front-end).
* Improve backfill performance when testing licenses.
* Add std[in,out,err]_expanded fields to json output when querying in-memory job information. The non-expanded fields will be shown exactly as the user specified them. This will match sacct behavior.
* slurmctld - Fix bad memory reference to default part record.
* Fix dynamic future nodes not suspending after registration.
* Add "(JobId=%u StepId=%u) suffix to node drain reason when setting it to 'Kill task failed'".
* For configured FUTURE nodes, restore saved node state on restart and reconfig instead of reverting to FUTURE state.
* slurmctld,slurmd,sackd - Add global timeout for reconfigure that will close connections to complete reconfigure instead of waiting for all connections to timeout individually. The default timeout is 2x MessageTimeout.
* Prevent deletions of a WCKey if a job is running with it.
* Prevent deletions of a QOS if a job is running with it.
* Allow ResumeTimeout=INFINITE.
* Add new MAX_TRES_GRES PriorityFlags option to calculate billable TRES as "MAX(node TRES) + node GRES + SUM(Global TRES)".
* Fix archive/purge performance regression.
* data_parser/v0.0.43 - Switched to +inline_enums flag as default behavior when generating OpenAPI specification. Enum arrays will not be defined in their own schema with references ($ref) to them. Instead they will be dumped inline.
* slurmrestd - Modified output of 'GET /openapi/' to include descriptions from the field's description and the type's description when generating the "description" fields.
* slurmrestd - Modified '.info.version' string of the generated OpenAPI specification. It will no longer specify the openapi plugins being used. Now it only specifies the slurm version. This affects slurmrestd's --generate-openapi-spec option and the following endpoints: 'GET /openapi.json' 'GET /openapi.yaml' 'GET /openapi' 'GET /openapi/v3'
* slurmrestd - Added '.info.x-slurm' field to the generated OpenAPI specification. This object specifies the slurm version, lists the used openapi plugins, and lists the used data_parsers and flags. This affects slurmrestd's --generate-openapi-spec option and the following endpoints: 'GET /openapi.json' 'GET /openapi.yaml' 'GET /openapi' 'GET /openapi/v3'
* Add sacct format option ReqReservation to get a comma separated list of reservation names the job requested.
* data_parser/v0.0.43 - Add field 'jobs[].reservation.requested' to the following endpoints: 'GET /slurmdb/v0.0.43/jobs' 'GET /slurmdb/v0.0.43/job/{job_id}'
* slurmrestd - Report an error when QOS resolution fails for v0.0.43 endpoints.
* slurmrestd - The following endpoints was added: 'DELETE /slurm/v0.0.40/reservation/{reservation_name}' 'DELETE /slurm/v0.0.41/reservation/{reservation_name}' 'DELETE /slurm/v0.0.42/reservation/{reservation_name}' 'DELETE /slurm/v0.0.43/reservation/{reservation_name}'
* slurmrestd - The following endpoints were removed: 'POST /slurm/v0.0.43/reservation/{reservation_name}' 'POST /slurm/v0.0.43/reservations/create' 'POST /slurm/v0.0.43/reservations/update'
* slurmrestd - The following endpoints were added: 'POST /slurm/v0.0.43/reservation' 'POST /slurm/v0.0.43/reservations'
* Fix the controller being able to talk to dynamically created cloud nodes.
* Restore the energy consumption tracking for the extern step that was removed in commit 0f533f44. This re-enables the consumed energy metric for both the extern step and the job.
* slurmstepd - log at debug instead of error level if SLURM_CPU_BIND or SLURM_CPU_BIND_LIST are too long to set in the environment.
* Deprecate "GetEnvTimeout" from slurm.conf and "timeout" and "mode" optional parameters from sbatch's --get-user-env option. The default timeout to get the user environment will be 120 seconds.
* Deprecate SchedulerParameters=no_env_cache and requeue_setup_env. no_env_cache was effectively not doing anything, and requeue_setup_env do not serve a purpose after the env cache code cleanup and new behavior of always requeuing and hold a job which failed to load the environment.
* When a job fails to retrieve the user environment and there is a failure or timeout during, then the job will be requeued + held, while previously it just continued. It will be requeued with distinctive reason.
* Fix and improve logging for IPv4 and IPv6 network strings.
* jobcomp - Change partition field to contain the partition name the job ran on. It previously contained the comma-separated list of all specified partition names.
* Fix false fatal with squeue --only-job-state -j<job id>.
* sreport - Fixed issue where PrivateData=usage was not honored in sreport cluster queries.
* sreport - Fixed issue where PrivateData=usage was not honored in sreport user queries.
* slurmrestd - Disable setting SLURMRESTD_SECURITY=disable_user_check outside of '--enable-developer' builds.
* Prefer allocating already available powered up nodes over POWERING_UP nodes.
* Added slurm.conf parameters PrologTimeout and EpilogTimout to configure separately how long prolog and epilog can run.
* Add support for printing and recording the stdout, stdin and stderr fields for job steps. scontrol, squeue, sacct, and the rest API will now show these fields if requested. For batch steps this will be recorded globally at the job level. For the job steps (excluding interactive and extern) they will be recorded per step.
* Always print the absolute path when printing stdio fields for steps. The cwd of the step is now stored in the database and used to form the final std_err, std_in or std_out string.
* switch/hpe_slingshot - The following SwitchParameters options have been added to allow mTLS authentication to be enabled between Slurm daemons and the fabric manager: fm_mtls_ca, fm_mtls_cert, fm_mtls_key, and fm_mtls_url.
* SlurmctldPrimaryOnProg and SlurmctldPrimaryOffProg will now block until they complete. This is to prevent issues like a VIP still being configured when the controller is shutdown.
* switch/hpe_slingshot - Support more than 252 tasks per node by allowing RGIDs to be shared.
* Add new PRIORITY preemption option to only permit preemptor jobs to preempt when their priority is higher than the preemptee.
* Avoid crashes if the slurmctld binary is renamed to something other than "slurmctld".
* Fix backfill scheduling issue with shared gres.
* Allow scontrol to expand hostlist functions.
* Fix steps not being created when using certain combinations of -c and -n inferior to the jobs requested resources, when using stepmgr and nodes are configured with CPUs == Sockets*CoresPerSocket.
* jobcomp/elasticsearch - Don't send the batch script by default. A new option - JobCompParams=send_script - has been added to explicitly permit sites to send it.
* jobcomp/kafka - Don't send the batch script by default. A new option - JobCompParams=send_script - has been added to explicitly permit sites to send it.
* scontrol - The `.nodes[].tres_weighted` field has been marked deprecated and is now ignored.
* slurmrestd - The `.nodes[].tres_weighted` field for following endpoints has been marked deprecated and is now ignored: GET /slurm/v0.0.40/nodes GET /slurm/v0.0.41/nodes GET /slurm/v0.0.42/nodes GET /slurm/v0.0.43/nodes
* Improve support for newer bash-completion packages.
* switch/hpe_slingshot - Always try to renew the fabric manager token on a HTTP 401 or 403 error status regardless of the contents of the response body.
* switch/hpe_slingshot - Fix slurmctld memory leak when cleaning up the switch/hpe_slingshot plugin.
* switch/hpe_slingshot - Fix memory leak of cxil_device_list structure in the slurmstepd.
* Fix slurmctld, slurmd, and slurmstepd memory leak resulting from the use of switch/hpe_slingshot with the stepmgr enabled or switch/nvidia_imex. The slurmctld and slurmstepd memory leaks only occur while the daemons were shutting down.
* Implement OR ('|') for job license requests. Add a new field LicensesAlloc to scontrol and squeue, and licenses_allocated to the corresponding rest API endpoints for scontrol and squeue. AND and OR are mutually exclusive. OR is not enabled in reservation license requests.
* Remove PerlAPI support for topology APIs.
* Make sackd respect the RUNTIME_DIRECTORY environment variable (if set), governing the location for both configless cache sackd maintains and the sack.socket to provide authentication services.
* Make clients check for SACK socket as '/run/slurm-<clustername>/sack.socket' before falling back to '/run/slurm/sack.socket'. This permits the SLURM_CONF to change both the configuration source and authentication sockets to support connections to multiple clusters from a shared login node.
* sackd - Add new --port <port> option to specify the port to listen for slurmctld reconfiguration updates in configless mode.
* sackd - Add a new '--disable-reconfig' option to fetch configurations in configless mode once, but won't register the daemon to receive automatic reconfiguration updates.
* sacct - fix assert failure when running dataparser v0.0.42.
* Ensure that a device constrain has been successfully applied to the job/step cgroup, so jobs do not run unconstrained after a failure.
* Consider all non-zero returns from SPANK functions an error.
* Add ability to specify if slurm_spank_init() failures should be treated as a node failure or a job failure.
* stepmgr - Fix step requests being rejected when other steps belonging to the same job are running and using requested resources.
* Add --wait-for-children option to srun to wait for all processes in a task to finish rather than just the parent process of each task.
* Add new EnableExtraControllers option in cgroup.conf which will enable a comma separated list of controllers in the job cgroup tree. When combined with EnableControllers it will enable them from the root cgroup to all levels of the tree. The available extra controllers are io, hugetlb, rdma, misc and pids. This is useful for sites that require enabling some controllers to get job stats that are not gathered by Slurm, directly from their cgroup.
* Clients no longer load the topology plugin for message fanout.
* Don't remove reservation that doesn't have any user resolvable in nsswitch configured data sources.
* Validate reservations after a uid that was previously missing is added by the assoc_mgr.
* Set the default connection processing thread count to 2x the number of detected allocatable cpus.
* sacctmgr - Return non-zero and print error if call to API function to commit/rollback changes fail when --immediate not used.
* Add SLURM_JOB_SEGMENT_SIZE environment variable to batch job environment when --segment was set.
* Set default User and Group for slurmrestd.service systemd unit to 'slurmrestd' instead of default of 'root' to intentionally prevent it from starting until a dedicated service account is created, or the site alters the service file.
* PMIX_NODEID was not being injected at rank level so other ranks could not get other's node_id.
* mpi/pmix - Add support for MPMD in Slurm's PMIx plugin by making use of heterogeneous jobs.
* accounting_storage/mysql - Refactor query for looking up accounts for a user where the UsersAreCoords flag is set. This should speed up starting slurmdbd for sites with a large association table.
* slurmrestd - Check that all requests are for HTTP or HTTPS when HTTP URL requested included URL scheme.
* slurmrestd - Reject connections requesting HTTPS but was not TLS wrapped.
* Fix core selection issue with gres affinity.
* Add new TaskPluginParam=SlurmdSpecOverride which allows a slurmd starting in a constrained cgroup to automatically set and override its CpuSpecList and MemSpecLimit based on the constraints found. For certain kinds of container setups this can be useful as the container has cgroup constraints but slurmd still has full hardware visibility. Slurmd will register itself with the new CpuSpecList and MemSpecList avoiding jobs to be able to use resources out of the actual slurmd cgroup constraints.
* Don't remove reservation that doesn't have any user resolvable in nsswitch configured data sources.
* Add ability to configure multiple topology plugins via the new topology.yaml configuration file.
* slurmrestd - Avoid fatal() during startup when unable to drop supplementary groups when running process lacks any supplementary groups.
* slurmrestd - Check to drop supplementary groups after resolving future process group ID is changing.
* slurmrestd - Avoid dropping supplementary groups when supplementary are redundantly to primary group.
* When running slurmd in a container and a reconfigure is issued, do not re-generate the cgroup hierarchy inside the actual cgroup, which would end up having unnecessary cgroup sub-levels.
* Fix stepd scope path generation after reconfiguring a non-daemonized and manually-started slurmd that is pid 1. This might happen only in containers and would cause incorrect cgroup hierarchy to be created.
* slurmctld - Avoid SEGFAULT when loading rpc_queue.yaml.
* Allow federated multi-cluster submissions to test each given cluster rather than one cluster per-federation.
* sched/backfill - Prevent pending jobs requesting licenses that can't run now due to license reservations from not being assigned a future start time until the reservations end.
* Add ability for dynamic nodes to join topology.
* cgroup/v1 started its deprecation process, this means that from now on no new features will be added and only support for critical bugs be provided. slurmd will emit a warning once it found it is running under a cgroup/v1 system. We recommend upgrading to cgroup/v2 as soon as possible in order to get full support and new features.
* scontrol - Add 'partitions[].topology' field to the output of 'scontrol show partitions --{json/yaml}'
* data_parser/v0.0.43 - Add 'partitions[].topology' field to the following REST API endpoints: 'GET /slurm/v0.0.43/partition/{partition_name}' 'GET /slurm/v0.0.43/partitions/'
* Fix security issue where a coordinator could add a user with elevated privileges. CVE-2025-43904.
* sched/backfill - Prevent jobs that are running on nodes not planned for a pending job from delaying the start of the pending job.
* Add ability to configure "Hierarchical Resources" via the new resources.yaml configuration file. Hierarchical resources are independent and orthogonal to any network topology (topology/tree or topology/block) already established. Each resource is independently defined and associated with specific nodes.
* squeue, scancel - When given numeric uids that no longer exist on the underlying system, show (or cancel) any old jobs in the queue that still use them.
* When an error occurs while serializing a yaml file, the line number and column number causing the error will now be logged.
* slurmctld - Fix regression where configured timeouts would not be enforced for new incoming RPC connections until after the incoming RPC packet has been read.
* slurmctld - Avoid timeouts not getting enforced due to race condition of when connections are examined for timeouts.
* slurmctld/slurmd/sackd/slurmrestd/slurmdbd/scrun - Modified internal monitoring of I/O activity to always wake up with a maximum sleep of 300s seconds to check for changes and to restart polling of file descriptors. This will avoid daemons from getting effectively stuck forever (or until a POSIX signal) while idle if another bug is triggered which could cause an I/O event to be missed by the internal monitoring.
* Fix race condition on x11 shutdown which caused other privileged cleanup operations to fail and leave stray cgroup or spool directories and errors in the logs.
* slurmctld - Prevent sending repeat job start messages to the slurmdbd that can cause the loss of the reservation a job used in the database when upgrading from <24.11.
* switch/hpe_slingshot - Fix defaulting VNI range to 1024-65535 when no SwitchParameters specified.
* srun - Allow --pty option to be interrupted with Ctrl+c before slurmstepd connects.
* slurmctld - Avoid read timeouts while waiting for connection EOF from client when EOF was received but not yet processed.
* slurmctld - Avoid race condition during shutdown when rpc_queue is enabled that could result in a SEGFAULT while processing job completions.
* slurmctld - Fix CONMGR_WAIT_WRITE_DELAY, CONMGR_READ_TIMEOUT, CONMGR_WRITE_TIMEOUT, and CONMGR_CONNECT_TIMEOUT being ignored in SlurmctldParameters in slurm.conf.
* Fix a minor and rare memory leak in slurmscriptd.
* Fix a race condition that can cause slurmctld to hang forever when trying to shutdown or reconfigure.
* slurmctld - Fix race condition in the ping logic that could result in the incorrect DOWNING of healthy/responding nodes.
