## Changes in 25.05.2

* sbatch - Fix case where --get-user-env and some --export flags could make a job fail and get requeued+held if Slurm's installation path was too long.
* srun - Increase --multi-prog configuration file size limit from 60 kB to 512 MiB.
* sreport - Fix Planned being printed instead of Planned Down by default in the cluster utilization report.
* slurmstepd - Avoid regression requiring slurmstepd (and all library dependencies) needing to exist inside of job container's mount namespace to execute TaskProlog and TaskEpilog.
* Fix issue with shared gres_per_task.
* Fix issue with --wait-for-children incorrectly implying --gres-flags=allow-task-sharing and vice-versa. These options are now handled independently as originally intended. Note that upgraded daemons will not honor the --wait-for-children option from older clients, and clients will need to be upgraded immediately alongside daemons in order to use --wait-for-children.
* Log case-insensitive collation exceptions in the slurm database to alert admins and to aid in investigating collation issues.
* Fix new QOS getting in bad state when attempting to remove flags at QOS at creation.
* Fix potential segfault of slurmstepd when acct_gather_profile/influxdb plugin fails to send data.
* Fix potential segfault when jobcomp/elasticsearch fails to send data.
* Fix parsing SlurmctldParameters=node_reg_mem_percent when it is followed by other comma-separated parameters.
* Fix stepmgr enabled srun allocations failing when excluding nodes.
* Fix bug where tres-per-task is ignored.
* Add topology.yaml to the list of files sent with configless
* Increase default thread stack size to 8 MB.
* When using --wait-for-children, a task's behavior in regards to the parent process exit will now depend on --kill-on-bad-exit. If --kill-on-bad-exit=1 and the parent process exits non-zero, the task will end. If --kill-on-bad-exit=0 and the parent process exits with an error, the task will continue.  Note that default KillOnBadExit setting in slurm.conf is 0, which will result in different behavior for --wait-for-children as described above.
* Fix x11 forwarding issues causing applications (e.g. matlab) to intermittently crash on startup.
* Print errors for write failures in half_duplex code used for x11 forwarding connections
* tls/s2n - Do not print S2N_ERR_IO_BLOCKED error when it is expected.
* tls/s2n - Fix x11 forwarding issues
* Fix slurmdbd crash when failing to open a persistent connection to slurmctld
* Fix missing error logs for failures to send messages on persistent connections.
* Add support for PMIx v6.x

## Changes in 25.05.1

* slurmd - Fixed a few minor memory leaks
* sackd - Fix successive fetch and reconfiguration in configless mode used via DNS SRV records.
* slurmstepd - Correct memory leak for --container steps before executing job.
* slurmrestd - Set "deprecated: true" property for all "v0.0.40" versioned endpoints.
* Prevent slurmd -C from potentially crashing.
* slurmdbd - Fix memory leak resulting from adding accounts.
* slurmdbd - Prevent account associations from being incorrectly marked as default.
* slurmrestd - Correct crash when empty request submitted to 'POST slurm/*/job/submit' endpoints.
* Fix slurmctld crash when updating a partition's QOS with an invalid QOS and not having AccountingStorageEnforce=QOS.
* slurmrestd - Remove need to set both become_user and disable_user_check in SLURMRESTD_SECURITY when running slurmrestd as root in  become_user mode.
* Fix a race that could incorrectly drain nodes due to "Kill task failed"
* slurmrestd - Prevent potential crash when using the 'POST /slurmdb/*/accounts_association' endpoints.
* squeue - Add support for multi-reservation filtering when --reservation specified.
* Fix jobs requesting --ntasks-per-gpu and --cpus-per-task staying in pending state forever.
* Fix interactive step being rejected by incorrect validation of SLURM_TRES_PER_TASK and NTASKS_PER_GPUS environment variables.
* slurmctld - Prevent crash on start up if SelectType is invalid.
* Fix memory leak in slurmctld agent when TLS is enabled
* Fix memory leak when DebugFlags=TLS is configured
* slurmctld - Prevent segfault when freeing job arrays that request one partition and a QOS list.
* tls/s2n - Fix various malformed s2n-tls error messages
* tls/s2n - Disable generating error backtrace unless configured in developer mode.
* preempt/partition_prio - Prevent partition PreemptMode defaulting to PRIORITY which caused jobs on higher priority tier partitions to not preempt jobs on lower priority tier partitions.
* Fix "undefined symbol" errors when using libslurm built with the tls/s2n plugin. This affected anything using libslurm, including seff.
* tls/s2n - Fix leaked file descriptors after failed connection creation.
* Fix race condition during extern step termination when external pids were being added to or removed from the step. This could cause a segfault in the extern slurmstepd.
* Avoid potentially waiting forever while attempting to establish new TLS connection due to race condition during TLS negotiation.
* Avoid delayed response during TLS negotiation due to socket being closed by remote side while expecting more incoming data.
* tls/s2n - Fix segfault when running scontrol shutdown
* Avoid incorrect error logging during CPU frequency cpuset validation when no CPU binding is enforced.
* Remove undocumented gen_self_signed_cert/gen_private_key scripts from certmgr. This functionality is covered by the certgen plugin interface, and these scripts were already unused.
* sched/backfill - Prevent running jobs from delaying the start of pending jobs planned for nodes not used by the running jobs.
* Make the --test-only job option completely ignore hierarchical resources used by running jobs instead of partially ignoring them.
* When specifying TaskPluginParam=SlurmdSpecOverride, the slurmd will register with the CpuSpecList and MemSpecLimit, not MemSpecList as was stated in the 25.05.0 changelog.
* slurmrestd - Fix support for https when slurm.conf has TLSType=None or lacks TLSType entirely.
* topology/tree - Insure the number of nodes selected when scheduling a job does not exceed the job's maximum nodes limit.
* Fix allowing job submission to empty partitions when EnforcePartLimits=NO.
* accounting_storage/mysql - Speed up account deletion by optimizing underlying sql query.
* Fix slurmstepd unintentionally killing itself if proctrack/cgroup and cgroup/v2 configured while deferring killing tasks due to any still core dumping.
* When a coordinator is altering association's parents make sure they are a coordinator over both the current and new parent account.
* Remove the ability to allow moving a child to be a parent in the same association tree.
* Correctly set lineage on all affected associations when reordering the association hierarchy.
* Lower rpm libyaml version requirement to version in RHEL 8
* Fix infinite loop in sacctmgr when prompting with invalid stdin, such as when run from cron or with input redirected from /dev/null.
* Fix regression introduced in 24.05 for srun --bcast=<path> when libraries are also sent and <path> ends with a '/' (slash).
* certmgr - Change several messages from "TLS" debug flag to the "AUDIT_TLS" debug flag. This includes logging for CSR generation and token validation.
* tls/s2n - Suppress benign error messages for messages sent by slurmctld to srun clients that may have already exited.
* certmgr - Retrieve signed certificate on slurmd/sackd before processing any RPCs.
* Add SALLOC_SEGMENT_SIZE input variable for salloc.
* Add SBATCH_SEGMENT_SIZE input variable for sbatch.
* Add SRUN_SEGMENT_SIZE input variable for srun.
* Fix slurmdbd crash when preparing to return a list of jobs that include ones that have been suspended.
* Prevent slurmd crash at startup when tmpfs job containers configured but no job_container.conf file exists.
* slurmctld - Fix a regression that allowed the same gres to be allocated to multiple jobs.
* slurmctld - Prevent fatalling with "Resource deadlock avoided" when array jobs start being able to accrue age priority.
* Fix rpmbuild when specifying a custom prefix.
* Fix potential incorrect group listing when using nss_slurm and requesting info for a single group.
* Fix orphaning pending federated jobs when using scancel --clusters/-M to a non-origin cluster.
* Fix QOS Relative flag printing as Relative and Deleted flags.
* certmgr - slurmd will now save signed certificates and corresponding private keys in the spooldir, and reload them on startup.
* Allow '_' in scrontab environment variables.

## Changes in 25.05.0

* Prevent slurmctld from allocating to many MPI ports to jobs using the stepmgr.
* Prevent slurmctld from crashing due to avoiding a deadlock in the assoc_mgr. The crash was triggered if slurmctld was started before slurmdbd, a partition had either AllowQOS or DenyQOS defined, and something triggered a message to the slurmdbd (like a job running).
* Fix case where member users are not account coordinators when the account has MembersAreCoords set and there are multiple clusters in use.
* Remove possible race conditions when pids are orphaned in a pam_slurm_adopt ssh session and another incoming RPC is trying to add a ssh session into the extern cgroup. In general the plugin was not thread safe and the only way threads can be created and conflict is with the handle_add_extern_pid API function, that pam_slurm_adopt is using. This caused segfaults in extern slurmstepd in some specific cases.
* Fixed issue where slurmctld could segfault in specific cases of heavy controller load and job requeues.
* Fix backwards compatibility for RESPONSE_BUILD_INFO RPC ("scontrol show config").
* Fix sacctmgr ping to be able to connect to newer versioned slurmdbd.
* Add 'scontrol show topoconf' command.
* In a TLS-enabled cluster, incoming non-TLS wrapped connections are now rejected properly instead of causing a segfault.
* Fix x11 forwarding not working with TLS enabled.
* Improve slurmctld performance with SlurmctldParameters=enable_job_state_cache by favoring changes to the internal cache by the scheduling threads instead of servicing clients.
* Fix double slash in logging message in cgroup/v1.
* Remove cpuset and memory limits of slurmd cgroup at startup or reconfigure, in cgroup/v1.
* openapi/slurmctld - Removed unused positional parameter {reservation_name} from the following endpoint: 'POST /slurm/v0.0.43/reservation'
* openapi/slurmctld - Add required positional parameter {reservation_name} to the following endpoint: 'DELETE /slurm/v0.0.43/reservation/{reservation_name}'
* switch/hpe_slingshot - Improve error handling for fm_mtls_{ca,cert,key} files by verifying they can be read by the SlurmUser.
* Prevent srun from hanging while initializing MPI/PMIx if srun is used to launch a non heterogeneous step in an heterogeneous job.
* squeue - Add field in Output Format for scron jobs
* Allow intermediate switches to be dynamically created on node creation and update.
* slurmdbd: when a QOS is deleted, remove it from the preempt lists of remaining QOSes
* Consider DefMemPerGPU when setting job requested memory
* Prevent slurmd segfault when starting in a container with no memory or cpuset controller in cgroup/v1.
* Do not fail when the memory or cpuset controller are not available in cgroup/v1. This restores previous behavior but still tries to reset the limits of these controllers if found.
* slurmrestd - Don't require script to be populated for external jobs.
* Add rpc_queue.yaml option to exempt RPCs from rate limiting.
* slurmrestd - Add missing systemd scriptlets to slurm.spec for updating systemd presets on install/uninstall/upgrade.
* scontrol - Add support to update job MCS label
* There were some informational warning messages printed by default directly to the user, but they are not important enough to be shown as they affect internal crun parameters, and the user can easily be spammed with those. Hide them under SCRUN_DEBUG=debug flag.
* slurmdbd - Avoid crash while accessing RPC stats in slurmdbd
* Empty reservations can no longer be created with the ANY_NODES flag.
* Allow reservations with heirarchal resources.
* slurmctld - Avoid waiting for TLS connection blinding to start reconfigure.
* Activate timeouts for incoming stepd_proxy connections.
* slurmd - Enable timeouts on incoming RPC requests.
* sackd - Enable timeouts on incoming RPC requests.
* Delay closing sockets in eio code which fixes issues in X11 forwarding when using applications such as Emacs or Matlab.
* Change oci.conf ContainerPath and "%m" replacement pattern to set the pattern of the per-step container spool directory. Per-task and MPI container related resources will be automatically created as child directories of this path to avoid conflicts from per-step and per-task resources. For extern steps, the per-step spool directory name has changed from "oci-job%j-%s" to "oci-job%j-extern". The default value for MountSpoolDir has changed from /var/run/slurm/ to ContainerPath. This change fixes always mounting the task spool dir to "/var/run/slurm/", while PMIx needs the step (not only the task) spool directory mounted.
* certmgr - Allow certificate renewals to proceed based on successful mTLS authentication, rather than re-sending the certmgr token.
* Avoid logging connection errors when trying to send to srun client commands that have already exited at job/step termination.
* slurmctld: Avoid crash causing by race condition when job state cache is enabled with a large number of jobs.
* Fix storing dynanmic future node's instance id and type on registration.
* Clear dynamic future node's InstanceID and InstanceType fields when setting back to future state.
* Fix bad shard distribution staying after invalid gres.
* Add ability to specify INFINITE hierarchical resources.
* Validate topology switch and block names exist.
* Add warning about ignoring children when both 'nodes' and 'children' are set in tree topologies.
* Make typed GRES reservations work without having to also include the non-typed GRES
* Gres reservations now work as expected when shard is not defined as a GresType in the slurm.conf.
* slurmrestd - Only force YAML plugin to load if SLURMRESTD_YAML environment variable is set.
* Add documentation for tls plugin
* Add documentation for certgen plugin
* Add documentation for certmgr plugin
* Use first wckey as new default only when user has none already
* Clear user's default wckey when the wckey is being deleted
* Disallow '*' prefixed wckey on job submission
* Adjust scontrol show topology args to be: scontrol topology [topology_name] [unit=NAME] [node=NAME]
* openapi/slurmctld - Prevent dumping all reservation flags when none are set when posting reservations. This affects the following slurmrestd endpoints: 'POST /slurm/v0.0.43/reservation' 'POST /slurm/v0.0.43/reservations'
* Fix slurmd not starting when run with memcheck tool of valgrind.
* Fix regression in srun I/O forwarding that would lead to step launch failures across more than TreeWidth nodes. (Defaults to 16.)

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
