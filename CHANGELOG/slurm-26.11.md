## Changes in 26.11.0rc1

* Index time_start on the per-cluster usage and resv tables to speed up hourly, daily, and monthly rollups.
* Allow nodes to join partitions with nodesets after node feature updates.
* The slurm-perlapi RPM now installs Perl code into the vendor directory so that it is included in @inc.
* data_parser/v0.0.43 - Deprecate unused "BOARD" value for job_resources_t.cr_type and partition_info_t.cr_type.
* data_parser/v0.0.44 - Deprecate unused "BOARD" value for job_resources_t.cr_type and partition_info_t.cr_type.
* data_parser/v0.0.45 - Deprecate unused "BOARD" value for job_resources_t.cr_type and partition_info_t.cr_type.
* data_parser/v0.0.46 - Deprecate unused "BOARD" value for job_resources_t.cr_type and partition_info_t.cr_type.
* Removed update_part_msg_t typedef which was used interchangeably with partition_info_t.
* The slurm-smd-torque Debian package is now architecture dependent because it ships compiled plugins.
* Drop unused indexes from the job_table.
* HttpParserType will now default to http_parser/llhttp_parser if found, otherwise http_parser/libhttp_parser.
* UrlParserType will now default to url_parser/internal if found, otherwise url_parser/libhttp_parser.
* Fix memory leak when notifying systemd of a main PID change.
* slurmctld - Avoid possible hang during shutdown when threadpool is enabled.
* slurmctld - Fix use-after-free race between _threadpool_join() and thread detach when threadpool is enabled.
* Fixed socket bind()/connect() failures on BSD-derived platforms.
* Add specific _with_hpe_slingshot option to slurm.spec.
* Replace broken pipe error message with a clearer log when enable_async_reply is set and the client side of a connection closes before a response is sent.
* Fix minor conmgr logging bug when closing a connection.
* Fix a minor file descriptor leak of an anonymous pipe in scrun.
* Fix relinking scrun to libslurm_lua on a source code change.
* Fix scrun not accepting new connections and ignoring SRUN* RPCs. In practice this was mostly invisible, but one visible symptom was scrun not receiving messages via `scontrol notify`.
* Fix possible minor memory leak in salloc, srun, scrun, and scontrol.
* Fix minor memory leaks in scrun.
* Avoid fatal error on invalid conmgr_* parameter values in SlurmctldParameters and SlurmdParameters in slurm.conf.
* Fix conmgr_threads parameter validation to reject out-of-range values and log an error instead of silently ignoring them.
* Make xhash_add() return NULL on duplicate keys instead of silently inserting an unfindable second wrapper.
* Fix configless cache files/directories and pidfiles to have correct permissions regardless of daemon's umask.
* slurmctld - Fix race condition that could cause a crash during shutdown caused by forward threads racing with auth plugin being unloaded.
* slurmctld - Fix shutdown hang and crash caused by an RPC forwarded after the forwarding layer was disabled.
* Warn if BatchStartTimeout is below prolog timeout.
* Recover batch jobs that never start their batch step.
* Recover batch jobs left running with no batch step after a slurmctld reconfigure or restart during their prolog.
* slurmctld - threadpool is now enabled by default. Set SlurmctldParameters=threadpool=disabled to restore the prior behavior.
* slurmctld - Handle all supported RPC replies asynchronously instead of as only optionally. Remove SlurmctldParameters=enable_async_reply from slurm.conf.
* Detect libhdf5_hl so sh5util links its high-level API.
* Reservations can now validate users based on usernames, not just uids. Usernames are validated against the assoc_mgr.
* Add "omit" as a LogTimeFormat value to suppress the timestamp from text log output, useful when the log collector (e.g. systemd journal) provides its own timestamps.
* sacct, sacctmgr, scontrol, sdiag, sinfo, squeue and sshare now print the list of available data_parser plugins for --json=list and --yaml=list without first querying the controller or database.
* Fix slurmd logging each received RPC twice with AuditRPCs flag.
* scontrol - Allow updating SegmentSize for pending jobs.
* scontrol - Allow updating SpreadSegments and ConsolidateSegments for pending jobs.
* Fix PrivateData=reservations and USER_DELETE access checks for multiple associations.
* slurmrestd - Harden startup process lockdown by dropping all available Linux capabilities.
* slurmrestd - Harden the privilege drop when switching to the requesting user in become_user mode and adding support for SLURMRESTD_SECURITY=disable_unshare_files,disable_unshare_sysv.
* Report the PreserveCaseResource flag in slurmdbd configuration query output.
* Add the PreserveCaseAll slurmdbd parameter as a shortcut for PreserveCaseUser and PreserveCaseResource.
* Fix several resource leaks on namespace/linux error paths.
* Clarify the error when a DirConfs path is at or under BasePath.
* Fix UNKNOWN node being marked DOWN after a resume or undrain.
* switch/hpe_slingshot - Fix libcxi.so path detection on systems where the library installs to lib rather than lib64.
* data_parser/v0.0.46 - Fix printing empty list '[]' instead of empty strings for enumerated flag arrays in JSON/YAML.
* data_parser/v0.0.46 - Add "default" value for dumping default log levels instead of empty string for JSON/YAML.
* slurmstepd - Add new SlurmstepdParameters option in slurm.conf to allow for daemon-specific parameter configuration.
* Fix PurgeTXN to also purge txn_table entries with no cluster set.
* slurmrestd - Add new SlurmrestdParameters option in slurm.conf to allow for daemon-specific parameter configuration.
* Fix a slurmdbd race where reconfigure could free the database connection info while another thread was using it to open or reconnect a database connection.
* PerlAPI - Remove allocation_msg_thr_create()
* Slurmd sets SLURM_NODE_IS_HEALTHY in the HealthCheckProgram environment to indicate whether slurmctld considers the node healthy.
* Fix partial node updates losing topology and affecting scheduling.
* slurm.conf - Renamed conmgr_threads to workerpool_threads in the SlurmctldParameters, SlurmrestdParameters and SlurmstepdParameters.
* Fix srun quadratic slowdown forwarding signals to large steps.
* Added DefRuntimePlugin configuration option to select the default runtime plugin.
* Move OCI container execution out of slurmstepd core into the new runtime/oci plugin.
* Add the runtime/none no-op job runtime plugin.
* salloc/srun/sbatch - Renamed the unused --container-type job submission option to --runtime, including its SLURM_CONTAINER_TYPE environment variable (now SLURM_RUNTIME), the squeue/scontrol ContainerType field, and the burst_buffer/lua job_info.container_type field (now runtime).
* slurmstepd - Load per job step --runtime plugin selection.
* slurmrestd - Deprecate the SLURMRESTD_JSON environment variable in favor of SLURM_JSON.
* slurmrestd - Deprecate the SLURMRESTD_YAML environment variable in favor of SLURM_YAML.
* Fix user name case not being preserved in user and assoc tables when toggling PreserveCaseUser, or when modifying/removing a user with a different case than stored.
* Add dumping of debugflags to following CLI calls: sacctmgr show config --json sacctmgr show config --yaml
* Add dumping of debugflags to slurmrestd endpoint: GET /slurmdb/v0.0.46/conf
* slurmrestd - SLURMRESTD_SECURITY=become_user has been deprecated and will be removed in a future release.
* slurmrestd - Serve the 'GET /healthz', 'GET /readyz', and 'GET /livez' endpoints without authentication.
* slurmrestd - Avoid treating 'GET /' as an error path but only return suggestion on how to use slurmrestd.
* slurm.conf - Add SlurmrestdHttpAuthParameters configuration parameter.
* slurmrestd - Improve health checks in 'GET /healthz' endpoint.
* slurmrestd - Improve health checks in 'GET /livez' endpoint.
* Fix jobs with --overcommit and --cpus-per-task remaining pending with Reason Resources.
* Reduce srun IO initialization message size by sending a compact key derived from the step credential signature instead of the full signature.
* Add new SerializerPlugins option in slurm.conf to load only the listed serializer plugins.
* Add new SerializerParameters option in slurm.conf to set serializer presentation flags: compact, pretty, complex, no_tag.
* Fatal when two serializer plugins register the same MIME type.
* slurmdbd.conf - Add SerializerParameters and SerializerPlugins.
* sacctmgr - `sacctmgr show config` now dumps SerializerParameters and SerializerPlugins.
* SLURM_RESTART_COUNT will always be in a batch job's environment, even on a new allocation where SLURM_RESTART_COUNT is 0.
* slurmd - Convert to asynchronous incoming RPC handling instead of creating new thread per RPC.
* data_parser/v0.0.46 - Avoid hitting an xassert when there is an error while parsing arrays in topology.yaml. The source string in the error will now be the path to the field experiencing the error instead of the function name where the error happened.
* data_parser/v0.0.46 - If the "jobs" field in 'DELETE /slurm/v0.0.46/jobs/' request body is an invalid format report the path to the field as the source instead of the function name the error happened in.
* data_parser/v0.0.46 - Fixed misspelling of exclusive in topology.yaml parsing errors.
* interfaces/topology - When topology.yaml is used only load each type of topology plugin once instead of once per entry in the yaml file.
* Fix slurmctld quadratic slowdown building the job alias list for large cloud or dynamic allocations.
* Fix DebugFlags=BackfillMap logging nothing unless DebugFlags=Backfill was also set.
* Fix LaunchParameters=mpir_use_nodeaddr producing an empty debugger table when NodeName differs from NodeHostname.
* State save a job's allocated partition in a dedicated field and recover it directly on restart and reconfigure.
* List a pending multi-partition job's partitions in PriorityTier order (highest tier first) in the Partition field instead of submission order.
* Skip instead of fatal when two serializer plugins register the same MIME type. Warn about this when SerializerPlugins is set.
* Warn and ignore an unknown serializer flag instead of fatal.
* Report the YAML MIME type as "application/yaml" instead of the "application/x-yaml" alias in messages to comply with RFC9512. Requests using "application/x-yaml" or "text/yaml" are still accepted.
* Warn and use the last of conflicting serializer layout flags instead of aborting when serializing.
* Fixed sacctmgr load crash when looking up account associations.
* Fixed a memory leak in sacctmgr during config file load.
* Fixed sacctmgr load applying user DefaultAccount and DefaultWCKey changes to every cluster instead of the load file cluster.
* sacctmgr load now reports an error for unknown QOS names instead of ignoring them.
* Fixed sacctmgr load rolling back newly added account associations when a user's DefaultWCKey already existed.
* Fixed "sacctmgr add qos" reporting success when a QOS failed to be added to the database.
* Fixed accounting storage continuing default account and WCKey updates after a user modification failed.
* Fixed sacctmgr account modify ignoring other field updates when the specified parent account was unchanged.
* Fixed sacctmgr load not marking the DefaultAccount association as the user's default.
* Fixed sacctmgr load replacing an association's existing QOS list instead of adding QOS from the file.
* Fix parsing negative integers as floats which could cause partial loss of precision. Integers with an explicit leading '+' or '-' sign are now parsed as signed 64 bit integers.
* Fix parsing of integers outside the signed 64 bit range being silently clamped instead of rejected.
* Fix parsing of hex integers wider than 64 bits being silently clamped instead of rejected.
* salloc - Fix --ntasks-per-core greater than --threads-per-core being accepted when the SLURM_NTASKS_PER_CORE environment variable was set.
* slurmctld - Prevent hitting an abort on shutdown if HTTP requests are disabled and Slurm was configured with --enable-developer.
* Speed up scheduling when PreemptType is configured.
* Improve sinfo and scontrol show node response time by no longer recomputing node allocation information when nothing changed.
* Fix a node that loses the reserved state when you delete a maintenance reservation that overlaps it.
* Fix backfill cycles that stop early on a cluster with an active reservation.
* slurmstepd - Fix writing duplicate OCI container environment variables to config.json instead of collapsing repeated names to the last-set value.
* Suppress spurious EPIPE error log messages during connection shutdown in conmgr.
* Fix possible slurmstepd hang during X11 setup.
* Fix slurmstepd wrongly failing or continuing an X11 step.
* Fix slurmstepd leaving an unreaped process after X11 shutdown.
* Fix possible X11 forwarding failure from a bad Xauthority path.
* sbatch/srun - SLURM_UMASK now fully controls permissions of intermediate directories implicitly created for --output/--error paths. Previously, these directories could never be more permissive than 0755 regardless of SLURM_UMASK.
* Avoid warning about low thread counts for workerpool on machines with a smaller number of detected available cores.
* Fix SPANK option environment variables being left in the job environment when more than one was set.
* tls/s2n - Harden all conmgr RPC listening sockets to automatically reject non-TLS connections in case clients are misconfigured with TLSType=tls/none. This affects api/step_launch (srun/sattach), api/allocate_msg (srun/salloc), scrun and swait listening sockets. slurmctld and slurmd already reject non-TLS connections.
* Assign steps a StepId at submission (instead of TBD), StepIds are now ordered by step submission not launch.
* Allow scancel to target pending steps.
* A pending srun step announces its assigned StepId on stderr when queued.
* srun - SLURM_UMASK now also governs permissions of locally created --output/--error files (and their intermediate directories) for the common case of a single output file shared across all tasks. Previously it only affected files/directories created by slurmstepd.
* Fix --json output dropping Infinity and NaN values when a complex data_parser is requested.
* cgroup/v2 - Fix jobs hanging instead of hitting an OOM condition when AllowedRAMSpace is set above 100.
* srun, salloc and sbatch - A heterogeneous job component that does not give --runtime no longer inherits the runtime named by an earlier component, and falls back to DefRuntimePlugin as it should.
* srun, salloc and sbatch - A heterogeneous job component that does not give --container no longer inherits the container bundle path named by an earlier component.
* slurmctld,slurmd,slurmrestd - Fail an RPC that can not be packed instead of sending a truncated message body.
* slurmdbd.conf - Add support for workerpool parameters via the Parameters field.
* slurmdbd.conf - Add support for conmgr parameters via the Parameters field.
* Fix wrong usage seconds recorded against associations and QOS when a large reservation's idle time is redistributed.
* Fix slurmd not shutting down while a job is completing and slurmctld is unreachable.
* Reorder lua loading to prefer the version Slurm was built against and shuffle the (potentially different) liblua.so as a last resort. No longer implicitly default to lua5.1 if configured version isn't successfully loaded.
* Add support for detecting and loading liblua5.5.
* slurmdbd now logs rollup duration at debug level (was debug2).
* sacctmgr rollup now exits non-zero and prints to stderr when one or more per-cluster rollup threads fail.
* slurmdbd now runs ANALYZE TABLE on purged tables after each archive/purge cycle to keep InnoDB index statistics fresh for the rollup and sacct query planner. Suppress with DisableArchiveAnalyze in slurmdbd.conf.
* Drop FORCE INDEX (rollup) hint from slurmdbd hourly rollup query.
* Speed up slurmdbd hourly rollup on large job tables by using hash-keyed aggregation and a UNION ALL form of the job-window query.
* Avoid per-job mysql roundtrips for suspended jobs during hourly rollup.
* Fix accounting_storage/mysql rollup losing assoc, qos, and wckey usage rows on high-cardinality clusters by chunking the INSERT.
* Fix TRES energy usage being attributed to the wrong association, QOS and wckey during the slurmdbd rollup.
* Fix associations getting too little of a reservation's idle time when a job used only part of the reservation.
* job_submit/lua - Assigning an empty string to a whitelisted string field in slurm_job_modify now clears the field.
* Add pack_data() and unpack_data() to write a data_t into a buf_t and read it back.
* stepmgr - Support --stepmgr on heterogeneous jobs.
* slurmctld - Fix scontrol suspend/resume hanging instead of reporting an error when given an invalid job id.
* scrontab - Send #SCRON options that were previously parsed and then dropped, such as --reboot, --container-id and --segment, to slurmctld.
* scrontab - Follow the configured OOMKillStep default for crontab jobs instead of always disabling it.
* slurm.conf - Add LogTimeFormat=rfc5424_us for RFC 5424 timestamps with microsecond precision.
* slurm.conf - Add LogTimeFormat=thread_id as a flag.
* Improve the speed of decoding JWT-based credentials, most noticeably for the large credentials used to launch jobs.
* tls/s2n, auth/slurm, auth/jwt - Fix crash that may occur on shutdown in all daemons/commands due to an OpenSSL atexit cleanup race.
* For mode 3 HRES, enforce that layers in the same level cannot have any overlapping nodes.
* Enforce a documented requirement that HRES mode 3 must be a uniform depth tree.
* Fix losing a reservation's license count after a failed scontrol update reservation.
* Reservations that request specific HRES layers must now request by layer name instead of by node names.
* Add updates of hierarchical resources (HRES) with 'scontrol update HRESName' and the REST API, and the ability to disable an HRES or a layer. Dynamic nodes can now join HRES layers with the new HRES node parameter. resources.yaml now requires layer_name and accepts parent_name, disable_hres, and disable_layer. Runtime changes are not preserved across a restart or reconfigure.
* Allow setting a device UUID in gres.conf. Not just through AutoDetect.
* Fix spank_setenv() reporting success when the environment variable could not be set. spank_setenv() may now return ESPANK_ERROR in a case where it previously always returned ESPANK_SUCCESS; a SPANK plugin that treats that as fatal can now fail task launch where it previously did not.
* Report the failure to set an environment variable in salloc, sbatch and srun instead of silently ignoring it.
* Report the reason an environment variable could not be set in salloc, sbatch and srun, instead of "Unspecified error".
* Report the reason a SLURM_SPANK_ environment variable could not be set, instead of "Unspecified error".
* Enforce the same maximum environment variable length in every code path that sets one. A "name=value" string one byte under the limit that some paths accepted is now rejected consistently.
* Fix a slurmd memory leak for each gres.conf device file that cannot be initialized.
* Fix a user hold placed on a running job becoming an admin hold when the node prolog completed, which left the job owner unable to release the job after a requeue.
* Fix srun launching a step before the node prolog finished when the job was held while the prolog was running.
* Add SlurmctldParameters=fast_power_up_launch to launch jobs waiting on node power up as soon as all of the nodes register, rather than waiting for the next periodic background poll.
* Fix race condition with PreemptExemptTime and PreemptMode SUSPEND which opened brief window (worst case 1 second) where jobs might not be exempt from preemption.
* Fix hetjobs oversubscribing cores under GANG scheduling without being suspended. Hetjobs were already excluded from GANG scheduling, and are now excluded from SUSPEND preemption in both directions and from all oversubscription while GANG is enabled.
* CommunicationParameters=block_null_hash and block_zero_hash are removed; both behaviors are now always enforced.
* Added per-TRES TresDecayHalfLife on associations and QoS so GrpTRESMins limit usage can be kept from decaying independently of fairshare priority decay controlled by PriorityDecayHalfLife.
* Rename the CR_NO_DIST_TOPO_BLOCK SelectTypeParameters option to CR_NO_DIST_TOPO. The old name is still accepted.
* Emit SLURM_JOB_NODELIST, SLURM_NODELIST, SLURM_STEP_NODELIST, SLURM_JOB_CPUS_PER_NODE and SLURM_TASKS_PER_NODE in topology order so they match task placement, and follow the user's hostfile order for --distribution=arbitrary.
* Fix the wrong GRES devices being exported to the prolog, epilog and job prolog when a job's node order differs from its allocation bitmap order.
* Emit the external launcher step's node list in topology order so it matches task placement, and fix its per-node arrays for --distribution=arbitrary.
* Terminate a job using --distribution=arbitrary when one of its nodes fails, even with --no-kill. Its task layout comes from the requested node list and cannot be rebuilt for fewer nodes.
* Reject scontrol update NumTasks= for a job using --distribution=arbitrary. The count is the number of entries in the requested node list.
* Fix a step being placed on a node with no free CPUs when an earlier --overcommit step left the node over-allocated.
* Fix OOM detection for task/cgroup only detecting an OOM in the first task. Now any step or task OOM event will be accounted for.
* swait - Renumber the exit codes: 1 is now an error and 2 a --timeout expiry.
* swait - A summary line is now printed to stdout when the wait completes
* swait - Add ability to wait on a single step rather than all steps via a new input argument for specific step ID.
* swait - Return exit code 3 when the job drains without the target step being reported.
* swait - Add --follow option to streams every step's completion until drain.
* swait - Add --json and --yaml output options.
* cgroup/v2 - Automatically enable IgnoreSystemd when slurmd detects that systemd is not the init system.
* data_parser/v0.0.46 - Omit "required" from OpenAPI schemas without required fields instead of emitting an empty list, which is invalid under OpenAPI 3.0.3.
* Avoid aborting if the initialization of the topology plugin fails during slurm_load_topo() if Slurm is configured with --enable-developer.
* scontrol,sview - No longer require topology.yaml to be readable on the node running 'scontrol show topology' or 'sview' when the cluster is configured to use topology.yaml.
* interfaces/topology - Prevent using a variable after it is freed when unloading the topology plugin.
* data_parser/v0.0.46 - Always make DATA_PARSER_TOPOLOGY_FLAT dump a DATA_PARSER_TOPOLOGY_FLAT_CONFIG object. This affects the '[].flat' field in the output of 'scontrol show topoconf'.
* topology/flat - 'scontrol show topology' will now output if AlphaStepRank is configured.
* slurmrestd - Add new endpoints to query topology info: 'GET /slurm/{data_parser}/default/topology' 'GET /slurm/{data_parser}/topology/{topology_name}'.
* scontrol - Add --json/--yaml support to `show topology`.
* Fix 'scontrol show topology <name>' returning the previously queried topology instead of the requested one when run more than once in an interactive scontrol session.
* Add the missing AC_PROG_CPP call to configure.ac.
* Fix setsockopt() using the wrong level argument for SO_REUSEADDR in conmgr, which only happened to work on Linux by coincidence.
* Add SLURM_JOB_MEM_ON_NODE to the slurmd Prolog environment with effective memory allocation for the current node.
* Fixed jobs requesting several typed counts of one gres losing all but one of the requested gres in their allocation records, in cases where the node declared the gres without types in slurm.conf.
* Added support for dmem cgroup controller which allows constraining GPU memory at job or step levels. This requires configuring shards per each device. Memory will be split among the defined shard count, so a shard will mean memory.
* Add possibility to specify typed shards/mps in gres.conf to allow different shard/mps counts per GPU model. Previously Type was ignored on such lines and the Count was distributed evenly across all GPUs.
* Fix the slurmctld metrics endpoint for long HTTP response headers. This was especially visible for a 303 response from a standby controller when the primary SlurmctldHost name was long.
* Fixed sacctmgr load silently continuing after slurmdbd rejected an association modification.
* Fixed sacctmgr load applying a partition-less User line to that user's partition-specific associations.
* Fixed limits inherited from a parent account not reaching slurmctld when an association was moved to a different parent.
* slurm.conf - Add the following parameters to CommunicationParameters to allow control how buffers grow: buffer_alloc_linear={BYTES}: grow buffers linearly with initial buffer of size BYTES. buffer_alloc_geometric={BYTES}: grow buffers by double size with initial buffer of size BYTES. buffer_alloc_exponential={BYTES}: grow buffers by multiple of e (~2.71828) with initial buffer of size BYTES.
* Leave an open entries in node event table in database alone on node updates.
* Prevent instantaneous events in event table in database from sometimes counting as 1 second.
* Widen the database event table reason column past 255 bytes to prevent truncation.
* Prevent same-second event collisions from keeping data from a node's second to last event in the database's event table.
* Added a "declarative" option to "sacctmgr load" which resets options left out of the file to their defaults for the accounts, users and QOS the file lists, without removing anything.
* sacct - Add Billing and BillingRaw fields with the billable usage of a job in TRES-seconds, also dumped as 'billing_raw' with --json or --yaml. Add the billing_raw field also to job records returned by GET /slurmdb/v0.0.46/jobs/.
* Reject a host number of ** or larger, which could come out as a different host or make the parser hang.
* Speed up node registration, creation and deletion, and the parsing of long node lists.
* sinfo - Fix --exact grouping by node version.
* Fix --gpu-freq and GpuFreqDef acting only on the first GPU type when a step holds GPUs of more than one type.
* squeue - Add -J/--job-name and deprecate -n/--name.
* scancel - Add -J/--job-name and deprecate --name/--jobname.
* sacct - Add -J/--job-name and deprecate --name.
* slurmctld - Report slurm_node_memory_alloc_bytes in bytes rather than MiB in the metrics/nodes endpoint. Values increase by a factor of 1048576.
* slurmctld - Report slurm_node_memory_effective_bytes in bytes rather than MiB in the metrics/nodes endpoint. Values increase by a factor of 1048576.
* slurmctld - Report slurm_node_memory_free_bytes in bytes rather than MiB in the metrics/nodes endpoint. Values increase by a factor of 1048576.
* slurmctld - Report slurm_node_memory_bytes in bytes rather than MiB in the metrics/nodes endpoint. Values increase by a factor of 1048576.
* slurmctld - Correct the slurm_jobs_memory_alloc help text in the metrics/jobs endpoint to state that the value is in MiB.
* slurmctld - Correct the slurm_partition_jobs_memory_alloc help text in the metrics/partitions endpoint to state that the value is in MiB.
* slurmctld - Correct the slurm_user_jobs_memory_alloc and slurm_account_jobs_memory_alloc help text to state that the values are in MiB.
* slurmctld - Correct the slurm_partition_nodes_mem_alloc, slurm_partition_nodes_mem_avail, slurm_partition_nodes_mem_free and slurm_partition_nodes_mem_tot help text in the metrics/partitions endpoint to state that the values are in MiB.
* Speed up node and partition updates on Nodes=ALL partitions.
* slurmdbd - Fix startup failure against a newly created accounting database with MariaDB Connector/C 3.4.10 and newer.
* sinfo - Improve performance for large node counts.
* sinfo - Fix %h and -O OverSubscribe merging nodes from partitions with different OverSubscribe settings into one row.
* Fix accounting queries like sacct showing an empty reservation name field for a job that started in the same second its reservation became active.
* Fix acct_gather_energy/ipmi inflating a step's energy under EnergyIPMICalcAdjustment when the power sensor failed its check.
* Fix acct_gather_energy/gpu inflating the consumed energy of a step started right after slurmd starts or running across its restart.
* slurmctld - Attempt to resolve configured MailProg or default MailProg (`/bin/mail` or `/usr/bin/mail`) during startup. When the MailProg can not be found, disables all mail instead of calling a known invalid mailer binary. Sites will need to reconfigure or restart slurmctld daemon to enable mail service on the startup resolution failure.
* slurm.conf - Add MailProg=None to disable job mail entirely instead of having to point MailProg at a no-op program.
* Fix a slurmstepd segfault at step start with AcctGatherEnergyType=acct_gather_energy/gpu, a step that profiles energy (--profile=Energy), and an energy sampling interval, when the node has no GPU energy sensor or the first energy read from slurmd fails.
* srun - Reuse the step-request listener across step creation retries.
* topology/block - Sort nodes alphanumerically within each block
* topology/tree - Sort nodes alphanumerically within each switch
* Select the batch host as the lowest topology-ranked node in the allocation instead of the first node in the node table.
* slurmctld - With ignore_prefer_validation, fix issue when too few nodes satisfy a job's '--prefer' features where the job is held and not retried.
* srun - Report the specific reason when a step fails during setup on a node instead of "Unspecified error".
* salloc/sbatch/srun - Add --max-pids option to limit the number of processes and threads that may exist at the same time in each step of a job on a node. Requires task/cgroup with cgroup/v2 and the pids controller.
* srun - Fix a heterogeneous step getting the wrong reserved port when only one port was reserved.
* slurmctld - Cap an MpiParams ports= range at 65535 and log an error, instead of handing out ports that can not be used.
* acct_gather_profile/hdf5 - Fix a race where the first multi-node profiled job for a user could fail to write one node's HDF5 file because ProfileHDF5Dir/<user> was still root-owned.
* mpi/pmix - Fix heterogeneous job steps intermittently hanging in a collective when a later component's nodes sort by name before an earlier component's nodes.
* mpi/pmix - Fix heterogeneous job steps aborting with "Can't find address for host" or hanging when a dynamic, cloud or future node is allocated to a different component than other nodes in the step.
* slurmrestd - /slurmdb/ mutation endpoints now return HTTP 200 with a populated warnings array instead of HTTP 304 with an empty body when nothing changed.
* Fix acct_gather_energy/gpu recording a * watt sample in the energy profile for a GPU whose power read failed.
* sdiag - Under the "Backfill exit" section now report which component caused a "System state changed" exit of backfill. The components being job, node, partition, reservation, and configuration state.
* Add SchedulerParameters=bf_node_unavail_exit to end the current backfill cycle when a node planned for a future job becomes unavailable while backfill has yielded its locks.
* metrics/openmetrics - Add the following fields to the /metrics/scheduler endpoint: slurm_bf_exit_job_changed, slurm_bf_exit_node_changed, slurm_bf_exit_part_changed, slurm_bf_exit_resv_changed, slurm_bf_exit_config_changed, and slurm_bf_exit_node_unavail.
* openapi/slurmdbd - When Slurm is configured with --enable-developer, prevent crashing the slurmrestd when a 'POST /slurmdb/v0.0.XX/tres' request is submitted.
* Fix GRES selection to prefer devices with more links, not fewer.
* Fix GRES selection skipping the best-linked device after a pick.
* Fix nodes with too few GRES being considered for a --gpus job that can only run on one node.
* Add --gres-flags=enforce-links to require that every GRES device a job is given on a node be linked to all the others.
* Add --gres-flags=ignore-links to select a job's GRES without regard to the links between devices.
* Add SLUID to `scontrol completing` output.
* Fix scontrol update of a reservation created with Duration=INFINITE, where updating StartTime, or adjusting Duration with += or -=, produced a wrong EndTime.
* Fix cloud nodes with AutoDetect GRES draining when slurmd first registers.
* Fix job-level requests (--gpus, --gpus-per-*, --tres-per-*) for a typed GRES on a node without device files failing step creation with "Invalid generic resource (gres) specification".
* Allow AutoDetect GRES on cloud nodes.
* Fix reservations getting a new accounting record every time the cluster node list changed, which on clusters using dynamic nodes could grow the reservation table enough to slow down sacct and sreport queries.
* conmgr - Fix connections never timing out while the peer was not reading, spinning on write instead of honoring the write timeout.
* Fix jobs using --gres-flags=enforce-binding without a task count being allocated every free core on the node. This was a regression in 24.11.1.
* slurmdbd - Use conmgr and the worker pool to serve connections. The conmgr_* and workerpool_threads options in slurmdbd.conf now take effect, though the listener overrides the read, write and connect timeouts.
* slurmdbd - Default to 150 worker threads. Set workerpool_threads in Parameters to change it.
* Jobs are now in the CONFIGURING state while the Prolog runs with PrologFlags=Alloc, unless NoHold is also set.
* Fixed a job preempted while in the CONFIGURING state being recorded as CANCELLED instead of PREEMPTED.
* slurmstepd - Fix step RPCs that arrive before the step is running being delayed up to 60 seconds when more than one of them is waiting.
* Added the experimental task/resctrl plugin, which automatically confines the steps of jobs sharing a node to one of three pre-defined resctrl groups based on the GPUs the job was allocated on the node. The limits of the groups are set in the new resctrl.yaml file and apply on Arm MPAM, Intel RDT, and AMD PQoS hardware.
* salloc/sbatch/srun - Add --layout option to give a job a comma-separated list of key=value pairs for site plugins, prologs and epilogs. It is shown by scontrol, squeue and sacct, and can be changed with scontrol update while the job is pending.
* Fix updating a job's license request discarding its mode 3 HRES selection, causing the job to be scheduled without first validating the mode 3 HRES.
* Fix client commands connecting to an invalid address, rather than reporting an error, when a node's address cannot be found.
* Fix "now+Ndays", "now+Nweeks" and "tomorrow" time specifications shifting one hour across Daylight Saving Time changes.
* Raise minimum MySQL/MariaDB version to 8.0.21/10.2.7.
* Fix slurmstepd crash on a job step create in an array task when stepmgr is enabled.
* Added DefSegmentSize job default, providing a segment size for jobs which do not request one with --segment. Can be set per partition or cluster wide, and is only honored by topology plugins which support segments.
* Fix a heterogeneous step started with --jobid=<array_job_id>_<task_id> failing with "Invalid job id specified" when the array task runs under its own job id.
* accounting_storage/mysql - Fix sacct showing an Unknown start time for a job step that was queued before it started.
* SchedulerParameters=step_retry_count now limits only retries of pending steps whose resources are still busy, so every pending step that fits starts when resources free up.  Still-busy --async steps now count toward this limit.
* A queued srun job step is now allocated as soon as resources free up and pushed to srun, rather than srun re-requesting it. Such steps report a CONFIGURING state flag until srun launches them.
* stepmgr - Queued srun job steps are now recorded in accounting as PENDING, matching --async steps.
* Fix srun hanging at step launch with acct_gather_energy/gpu, DebugFlags=Energy, and slurmd logging at verbose or higher when a multi-node step holds no GPU on one of its nodes.
* Fix a slurmd memory leak that occurred each time a task launch was rejected by the task plugin, such as for an invalid cpu bind.
* Add IPC namespace (CLONE_NEWIPC) support to namespace/linux.
* Fix slurmctld losing track of the nodes still completing a requeued job when a node in its allocation is removed from the configuration and slurmctld restarts, showing nodes as idle while they are still completing.
* scontrol - Allow updating a node's SuspendTime with scontrol update node. A finite per-node value enables automatic power save for the node without a reconfigure.
* slurmctld - Reject a finite SuspendTime in scontrol create node when automatic power save is not configured.
* slurmctld - Restore a node SuspendTime set with scontrol when slurmctld recovers full state (-R or backup takeover).
* slurmrestd - Allow setting suspend_time when updating a node with POST /slurm/v0.0.46/node/{node_name}.
* slurmrestd - Fix some slurmctld and slurmdbd errors being reported as HTTP 500 instead of 422.
* Fix missing error string mapping to HetJob error codes returned when a specified HetJob has a non-numeric component and when a HetJob is submitted with --array.
* Report more specific error codes when failing to submit or operate on a job array due to an invalid array specification, or when a job array is combined with a heterogeneous job.
* scancel - Exit non-zero when slurmctld rejects an array task index.
* Reject array specifications that select no task, such as an empty value, a bare separator, a throttle with no index, or an unterminated range.
* slurmctld - Fix memory leak when a job signal request contains an invalid job id.
* Fix bug that can reject a partition or account update on a job when AccountingStorageEnforce does not enforce associations and the job's user has no association.
* Fix rejecting jobs that request socket/core-bound GRES when --cpus-per-task only allows one task per socket, even though the job fits by total resources across sockets. --gres-flags=disable-binding did not avoid the failure.
* A node feature declared with Flags=rebootless in helpers.conf can now be activated without RebootProgram being configured.
* accounting_storage/mysql - Fix a job's derived exit code being set to * when a step that never launched completed with an unset exit code, masking the real exit codes of the job's other steps.
* swait now waits for job steps that are queued waiting on busy resources, not only for running steps and pending --async steps.
* swait reports a job step that was torn down before it ever launched as cancelled instead of failing the wait as unobserved.
* Fix memory corruption when sorting host lists with very large node numbers.
* Fix memory corruption and wrong results when sorting host lists that mix zero-padded and unpadded node numbers.
* Reject host lists such as "a[]b[1]c[1]" instead of treating them as empty.
* slurmd - Fix a memory leak when step setup fails.
* mpi/pmix - Fix memory leaks on some error paths.
* Jobs that cannot use magnetic reservations will not be hindered by the presence of a magnetic reservation.
* Add an AMD GPU plugin (AutoDetect=amd) that detects AMD GPUs from the amdgpu kernel driver without the ROCm SMI library. It fills in each GPU's type, device files, UUID, CPU affinity, and XGMI links. The type is the board model name, or the PCI device ID (e.g. 0x67df) on boards without one, so it differs from AutoDetect=rsmi. Each GPU is a MultipleFiles GRES of its render and card nodes. AutoDetect=full tries amd after rsmi. Only the SPX compute partition mode is supported, and GPU frequency control is not.
* Reject WEEKDAY and WEEKEND reservations whose StartTime, including "now", does not fall on a day covered by the flag, both at creation and on updates that set StartTime or a reoccurring flag.
* Fix WEEKDAY/WEEKEND reservations moving to a day not covered by the flag when skipped, or when advanced on a later day than the occurrence started, such as an occurrence spanning midnight.
* slurmctld - Fix Extra, InstanceId, InstanceType, and Topology not being applied from the registering slurmd when a node powers up.
* Fix slurmd exiting with "fatal: clone: Operation not permitted" when a job requests the user's environment and pid or mount namespaces cannot be created, e.g. in a container without CAP_SYS_ADMIN.
* job_submit: Change job_submit() arguments to send msg instead of submit_uid.
* Fixed node reboot/fail/drain Reason being erroneously cleared when a new job was allocated to a node with a pending reboot, drain, or fail request.
* slurmctld - Fix crash on a malformed REQUEST_KILL_JOB RPC that carries no job id.
* cli_filter/lua - Log an error when a Lua hook assigns a value to an option that does not exist or does not apply to the client command.
* Add pids as a static TRES with the peak number of PIDs of each task and shown by sacct and sstat through the AvePids, MaxPids, MaxPidsNode, and MaxPidsTask fields. Show the --max-pids limit of jobs and steps as MaxPids in scontrol, squeue, and slurmrestd.
* Fix SLURM_JOB_NODELIST and GRES environment variables being unset in the Epilog when slurmctld resends a job termination request, and naming the wrong node when it kills a job on a single node.
* Add HRES plugins, enabled per resource with a new plugin section in resources.yaml, and an hres/lua plugin that lets a Lua script reserve, prepare and release a hierarchical resource for each job. Jobs may pass arguments to the plugin with --licenses=<name>:<count>[<arg>=<value>,...].
* srun - Fix a step staying RUNNING until the job ended, with enable_stepmgr, when its launch failed on a node or a later het component could not be created.
* Fix GRES environment variables naming another node's devices in the Epilog when slurmctld aborts a job on a node.
* Initialise mutex io_mutex/io_cond in client_io_t (src/api/step_io.c) and stepd_step_rec_t (slurmstepd_job.c).
* burst_buffer/lua - Fix a possible slurmctld crash when a job has an invalid burst buffer spec.
* slurmctld - Name the reservation in the state description of a job held because its reservation no longer exists.
* slurmctld - Return HTTP 200 instead of 204 from /healthz, /livez, and /readyz probe endpoints, with READY, LIVE, or HEALTHY as the response body.
* slurmd - Return HTTP 200 instead of 204 from /healthz, /livez, and /readyz probe endpoints, with READY, LIVE, or HEALTHY as the response body.
* slurmrestd - Return HTTP 200 instead of 204 from /healthz, /livez, and /readyz probe endpoints, with READY, LIVE, or HEALTHY as the response body.
* jobcomp/kafka - Fix job start records reporting a null "state" field.
* jobcomp - Report REQUEUED instead of PENDING as the state of the job finish record written when a running job is requeued.
* jobcomp/elasticsearch, jobcomp/kafka - Add the array_job_id field to the records of a job array standing for all of its pending tasks.
* slurmctld - Fix job state description accumulating repeated text when a job is held for an invalid reservation more than once.
* slurmctld - Fix the state reason of heterogeneous job components not reflecting the hold when the whole job is held.
* slurmctld - Fix a stale state description shown for a job held after a burst buffer begin failure.
* slurmctld - Fix job state changes being logged twice with DebugFlags=TraceJobs.
* jobcomp/elasticsearch, jobcomp/kafka - Add "event" and "@event_time" fields to the serialized job record.
* jobcomp/elasticsearch, jobcomp/kafka - Add "state_reason" and "state_desc" fields to the serialized job record.
* jobcomp/elasticsearch, jobcomp/kafka - Add a "restart_cnt" field to the serialized job record.
* jobcomp/kafka - Produce messages keyed by job ID, so the records of a job sent to the same topic go to the same partition. Partition assignment of new messages on existing topics will change.
* jobcomp/kafka - Add the JobCompParams option topic_all to send the records of every enabled event to a single Kafka topic.
* jobcomp/kafka - An empty topic parameter in JobCompParams, such as "topic=", now selects the default topic name instead of an empty one.
* jobcomp/kafka - Add optional job queue, requeue, hold, and release event records, enabled with the new JobCompParams options enable_job_queue, enable_job_requeue, enable_job_hold, and enable_job_release and sent to per-event Kafka topics configurable with topic_job_queue, topic_job_requeue, topic_job_hold, and topic_job_release.
* scrontab - The jobs of a crontab rejected for a later entry are now ended in accounting instead of being dropped without an end record.
* job_submit/lua, jobcomp/lua - Add the state_reason and state_desc fields to the job record available to the Lua scripts.
* jobcomp/lua - Add optional job start, queue, requeue, hold, and release events, enabled with the JobCompParams options enable_job_start, enable_job_queue, enable_job_requeue, enable_job_hold, and enable_job_release. The event name is passed to slurm_jobcomp_log_record() as a second argument.
* jobcomp/lua - Fix a Lua stack leak when the script fails, which made the plugin stop recording after enough failures.
* Improve the error message printed when setting MaxJobId higher than its maximum by printing the maximum accepted value instead of the raw string "MAX_JOB_ID".
* slurmd - Fix forwarded RPCs hanging forever when an invalid message header values are sent, or when the slurmd is unable to start forwarding the RPC.
* Add node write locks to several sites that might deallocate nodes.
* slurmdbd - Fix memory leak when sacctmgr show event is given a node list that resolves to no hosts.
* Fix hostlist expansion returning truncated hostnames when the prefix and numeric suffix together exceed HOST_NAME_MAX.
* accounting_storage/mysql - Fix slurmdbd abort in sacct -N when an event record in the database has an unparsable node list.
* Reject node and number ranges that have other characters after the first number, such as "n[1a-3]", instead of expanding them.
* Remove support for multi-dimensional (BlueGene) node names.
* Remove the unused dimensions and dim_size fields from slurmdb_cluster_rec_t, and dimensions from the Perl API cluster hash.
* acct_gather_interconnect/sysfs - Fix interconnect accounting reporting packet counts as bytes, network profiling InMB/OutMB values being sixteen times too large, and traffic being counted twice.
* srun - Fix job steps queued waiting on busy resources inside a batch or salloc job not being started, cancelled or signaled promptly when TLS is enabled.
* sched/backfill - Emit the backfill "Started JobId=" log at debug level info again. This reverts a change added in 26.05.0rc1 that made the log guarded behind the Hetjob DebugFlag.
* Add gres.conf ChunkSizes and ChunkOrder to allocate GRES devices in aligned chunks of configured sizes.
* Add support for lld and mold.
* Avoid an xassert in slurmstepd with acct_gather_energy/gpu under ConstrainDevices when AutoDetect finds more GPUs than configured.
* slurmctld - Fix heterogeneous job components after the first not counting against QOS and association submit job limits (GrpSubmitJobs, MaxSubmitJobsPerUser, MaxSubmitJobsPerAccount and MaxSubmitJobs). The limits could be exceeded, and cancelling the job then dropped the count by every component. Regression in 26.05.0.
* slurmctld - Fix a heterogeneous job submit of two or more components subtracting other jobs from QOS and association running job and TRES usage (GrpJobs, MaxJobsPerUser, GrpTRES), which allowed those limits to be exceeded. Regression in 26.05.0.
* slurmctld - Fix a heterogeneous job rejected at submit by a QOS or association limit leaving the over-limit component pending as its own job, still holding a submit slot. Regression in 26.05.0.
* slurmctld - Fix a heterogeneous job component submitted to several partitions losing its submit job count in the QOS of the partitions it does not run in. QOS submit job limits (GrpSubmitJobs, MaxSubmitJobsPerUser and MaxSubmitJobsPerAccount) could be exceeded. Regression in 26.05.0.
* slurmctld - Fix a heterogeneous job submit leaving QOS running job and TRES usage (GrpJobs, MaxJobsPerUser, GrpTRES) charged when a component's job QOS is the partition QOS of another component, which could block jobs of that QOS until slurmctld restarted. Regression in 26.05.0.
* slurmctld - Fix heterogeneous job submission not checking the sum of all components against the limits of parent associations. Regression in 26.05.0.
* Fix slurmd misnumbering the GRES devices listed after a File that is missing or not a device node, which gave jobs the wrong device, or none, in their device cgroup and environment, and refuse one listed twice.
* accounting_storage/mysql - Close the suspend record of a suspended job when sacctmgr fixes it as a runaway job, so that PurgeSuspendAfter can remove it.
* accounting_storage/mysql - Fix sacct --state=SUSPENDED reading every job record of the cluster, which could make sacct time out waiting for slurmdbd on a large job table.
* accounting_storage/mysql - Fix sacct --state with SUSPENDED in a state list reading every job record of the cluster. A job is now selected as SUSPENDED only for windows it existed in.
* slurmdbd - Fix "sacctmgr modify qos set preempt+=" corrupting the preempt list when the added QOS was already in it.
* Fix jobs being allocated fewer GPUs than requested when RestrictedCoresPerGPU is configured.
* sched/backfill - Fix race in backfill that prevented the slurmctld from saving state on shutdown.
* Fix gres/gpuutil accounting with gpu/rsmi recording * or another value above 100 per process on GPUs with no CU occupancy data.
* Fix scontrol update of a job's memory being silently ignored when the requested value matches the memory the job inherited from the partition's DefMemPerCPU/DefMemPerNode.
* slurmctld - Fix federated jobs hanging in COMPLETING when requeued while their nodes power up.
* slurmctld - Allow external jobs to be requeued, both when preempted with PreemptMode=REQUEUE and with scontrol requeue/requeuehold.
* runtime/lua - The commands that submit jobs and steps load runtime.lua too, and call its optional slurm_runtime_apply_* functions.
* Send runtime.lua to client commands in configless mode.
* runtime/oci - Provide --container. It must be given as --container=<path>, only runtime/oci uses it, and SRUN_CONTAINER_PACK_GROUP_<n> is no longer read; use SRUN_CONTAINER_HET_GROUP_<n>.
* Reject an unknown --runtime, or two different ones, when the job is submitted. A #SBATCH --runtime now overrides SBATCH_RUNTIME.
* salloc - With --container, the interactive step's srun runs in the container bundle, and the command salloc runs gets SLURM_CONTAINER.
* slurmrestd - Load DefRuntimePlugin at startup, and fail to start if it cannot be loaded.
* sacctmgr - Avoid possible crash due to malformed RPC statistics response from slurmdbd.
* slurmrestd - Avoid possible crash due to malformed RPC statistics response from slurmdbd.
* libslurm - Declare slurm_xfree_ptr() in slurm.h, for freeing memory that API calls say to free with it.
* libslurm - Free the results of slurm_hostlist_shift() and slurm_read_hostfile() with slurm_xfree_ptr() instead of free().
* sstat - Fix the node list of large steps being cut off.
* libslurm - Remove slurm_hostlist_ranged_string(). Use slurm_hostlist_ranged_string_xmalloc() instead.
* Remove blank lines printed after some error messages.
* Allow ResumeTimeout to exceed 65533 seconds, globally and per partition, up to * seconds.
* libslurm - resume_timeout in partition_info_t and slurm_conf_t is now a uint32_t. Compare it with NO_VAL and INFINITE instead of NO_VAL16 and INFINITE16.
* Fix ResumeTimeout=INFINITE killing ResumeProgram, SuspendProgram and ResumeFailProgram, and aborting batch job launches, after 65535 seconds.
* Fix updates of a job whose ExcNodeList names a node that no longer exists failing, and the job then running on its excluded nodes.
* Fix a job whose ReqNodeList names a node that no longer exists running on other nodes after a failed update.
* Fix a job update with a new ReqNodeList skipping the check that its nodes are in the job's partition, and a rejected update keeping the new ReqNodeList.
* Fix a job whose ReqNodeList is longer than its node count never starting after a rejected job update.
* Fix a node's preexisting DRAIN flag being cleared when powered down. The DRAIN set by "power down asap" is still cleared.
* slurmctld - Fix running jobs on cloud nodes ignoring SlurmctldParameters=cloud_dns after a restart or reconfigure.
* slurmstepd - Fix a memory leak when a step manager sends messages to dynamic or cloud nodes.
* Add the dynamic_node_dns SlurmctldParameters option. Each host resolves a dynamic node's NodeName through its own resolver. Node-to-node traffic can use a different network than the one slurmctld uses.
* sched/backfill - Fix planning a preemptor job on different nodes than it will start on, which let lower priority jobs take its nodes and delay it.
* Avoid accounting rollup failures on Galera 4 by enabling streaming replication for the rollup connection.
* slurmctld - Prevent job owners from suspending and resuming their own jobs.
* A job step's exit code is now the highest exit status of all its tasks. Previously a step with several failing tasks could report a lower status than the highest one, which also affected the job's DerivedExitCode.
* slurmdbd - Added slurm_rc column to the job table, holding a job's Slurm return code.
* Allow RequeueExit/RequeueExitHold to match Slurm error codes larger than 255.
* slurmctld - A job that Slurm fails now reports an exit code of 1:0 rather than 0:1, matching what sacct already displayed.
* slurmdbd - A job killed by SIGHUP is now recorded in accounting as killed by signal 1 (0:1) rather than as having exited with code 1 (1:0).
* RequeueExit=1 and RequeueExitHold=1 now match a job that Slurm failed without a return code, which is recorded with an exit status of 1. Such a job was matched as exit status 0 before.
* slurmctld - A job submitted with --requeue=expedite is now requeued when Slurm fails it without a return code, as it already was for a failure with one.
* SLURM_JOB_EXIT_CODE and SLURM_JOB_EXIT_CODE2 in the Epilog environment no longer report a Slurm error code for a job that Slurm failed. They now report an exit status of 1; the error code is available as SLURM_JOB_RC.
* slurmctld - A batch job that fails to launch now has reason NonZeroExitCode instead of RaisedSignal.
* API - Removed job_info_t.exit_code and slurmdb_job_rec_t.exitcode; use job_exit_rc.exit_code instead.
* job_submit/lua, jobcomp/lua, burst_buffer/lua - Added slurm_rc, a job's Slurm return code, to the job record.
* squeue - Added SlurmRC format field, showing a job's Slurm return code.
* scontrol - Job output with the --details flag now includes SlurmRC, a job's Slurm return code.
* sview - The job info view now includes a SlurmRC column, showing a job's Slurm return code.
* jobcomp/filetxt - Job completion records now include SlurmRC, a job's Slurm return code. Older sacct binaries warn about the new field when reading these logs.
* jobcomp/elasticsearch - Job completion records now include slurm_rc, a job's Slurm return code, on jobs that Slurm failed.
* jobcomp/kafka - Job completion records now include slurm_rc, a job's Slurm return code, on jobs that Slurm failed.
* jobcomp/script - Completion scripts now receive SLURM_RC, a job's Slurm return code.
* sacct - Added SlurmRC format field, showing a job's Slurm return code. Included in --long output.
* slurmrestd - Added slurm_rc, a job's Slurm return code, to the job objects returned by /slurm/v0.0.46/job(s) and /slurmdb/v0.0.46/job(s).
* sacct, scontrol, squeue - Added slurm_rc to the job records in --json and --yaml output (data_parser v0.0.46).
* perlapi - Job hashes now carry slurm_rc, a job's Slurm return code.
* Added SLURM_JOB_RC to the Epilog and EpilogSlurmctld environment, holding a job's Slurm return code.
* Epilog - SLURM_JOB_EXIT_CODE, SLURM_JOB_EXIT_CODE2 and SLURM_JOB_DERIVED_EC are no longer set when the Epilog runs with PrologFlags=RunInJob, where they were always 0.
* Fix bf_busy_nodes planning pending jobs onto one more node than requested.
* Single-node jobs use the complete node selection by default. Add enable_one_node_select option to topology.yaml to use the first-fit single-node selection instead, which ignores LLN, CR_LLN, bf_busy_nodes and pack_serial_at_end.
* slurmctld - Changing a partition's QOS at runtime (scontrol update) is now refused while jobs are running or suspended in the partition.
* slurmctld - Fix QOS usage leaking onto a partition's previous QOS when the partition QOS was changed while jobs were pending in it.
* slurmctld - Fix a held job reporting its stale pre-hold per-partition priorities instead of zero.
* slurmctld - Order a pending job that requests multiple QOS by the highest-priority QOS it requests.
* slurmctld - Report the highest requested QOS priority for a pending job that requests multiple QOS.
* slurmctld - Fix a possible out-of-bounds write when a job's requested QOS list is changed.
* slurmctld - Fix a single-partition multi-QOS job keeping stale per-partition priorities when it is held or its priority is set.
* slurmrestd - Avoid a crash dumping a job's per-partition priorities when the priority array has no partition names.
* slurmctld - Report one priority per requested partition for a pending job, instead of misassociating QOS priorities with partitions.
* slurmctld - Fix a non-cloud node that was powering up during a slurmctld restart never being downed by ResumeTimeout if it failed to boot.
* slurmctld - Fix a powering up node losing its InstanceId, InstanceType, Extra and Topology when scontrol update ran on it before slurmd registered.
* Fix slurmd and slurmctld terminating when they parse a node list with more than 256K comma-separated ranges inside one pair of brackets.
* pam_slurm_adopt - Fix action_unknown=newest sometimes adopting ssh sessions into a job other than the newest one when the user has multiple jobs on the node.
* Fix the task profile writing garbage GPUMemMB and GPUUtilization values when gres/gpumem or gres/gpuutil is tracked without the other.
* Fix the first task profile sample writing a garbage GPUMemMB and a zero GPUUtilization.
* Fix the task profile writing huge RSS, VMSize, Pages, GPUMemMB and GPUUtilization values for a TRES not sampled yet.
* Fix commands and daemons treating a hostname as a member of a node list it is not in, when it shares the list's prefix and has the same length as its entries, for example nodegpu1 against node[0000-0099].
* Fix finding and removing hosts in a host list written as a bracket followed by a suffix that ends in a digit, such as cn[1-4]-1.
* slurmctld - Fix pending array tasks showing Reason=JobArrayTaskLimit after the array's task limit has room again.
* Add tpu.yaml configuration file and tpu.yaml(5) man page to describe accelerator slice geometries and PCI identifiers for Google Cloud TPU families.
* Add the gres/tpu plugin (and AutoDetect=tpu in gres.conf) for Google Cloud TPU PCI/VFIO device discovery, multi-core chip grouping, and NUMA CPU affinity mapping.
* Add the job_submit/tpu plugin to validate --layout requests against tpu.yaml and populate node counts and GRES requirements.
* Fix possible crashes when freeing job information responses.
* Fix possible crashes when freeing RPC return-list messages.
* Fix possible crashes when freeing nested federation messages.
* slurmrestd - Fix possible crashes when freeing WCKey accounting records.
* slurmrestd - Fix possible crashes when cleaning up invalid WCKey queries.
* slurmdbd - Fix invalid cleanup when queuing coordinator-account updates fails.
* slurmctld - Fix crash when "scontrol update SuspendExcNodes", "SuspendExcParts" or "SuspendExcStates" is sent without a value.
* scontrol - Reject "scontrol update SuspendExcNodes", "SuspendExcParts", "SuspendExcStates" and "SlurmctldDebug" requests with extra arguments instead of sending the wrong value to slurmctld.
* scontrol - Reject "scontrol update SlurmctldDebug=" instead of setting the debug level to quiet.
* scrontab - Do not load the user's login environment for a crontab entry that sets "#SCRON --export=NIL", matching sbatch.
* mpi/pmix - Fix MPI_Abort() exit code not being reported by srun when TLS is enabled.
* slurmdbd - Fix slurmdbd hanging at startup, or when listing, adding, or modifying associations, when an account's parent account has no association on that cluster.
* slurmdbd - Reject adding an account or association whose account, user, or partition name matches an existing one in its first 42 characters. Previously the add renamed the existing association.
* switch/nvidia_imex - Fix compatibility with namespace plugins to allow per-job IMEX channel isolation when a namespace plugin is configured.
* Display step dependencies and pending reasons in scontrol.
* Add srun --step-dependency for steps within one job.
* Fix heterogeneous jobs not being requeued when the Prolog fails on one of their nodes.
* slurmctld - Fix the jobs_wait_part_node_limit metric (partition_jobs_wait_part_node_limit in the openmetrics plugin) overcounting jobs pending for reasons other than a partition node limit, and counting jobs that are no longer pending.
* slurmdbd - Detect TLS per connection on DbdPort instead of requiring it up front. RPCs that are not TLS wrapped are still refused when TLSType is set.
* slurmdbd.conf - Add HttpAuthParameters.
* sacctmgr - `sacctmgr show config` now dumps HttpAuthParameters.
* sacctmgr - Add HttpAuthParameters to the output of `sacctmgr show config --json` and `sacctmgr show config --yaml`.
* slurmrestd - Add HttpAuthParameters to the output of 'GET /slurmdb/v0.0.46/conf'.
* slurm_completion.sh - Fix later --dependency clause completion.
* slurmctld - Fix scontrol release and scontrol update returning an error for a pending job that exceeds a partition's CPU limit. Replace Reason=PartitionConfig with Reason=PartitionCpuLimit.
* slurmctld - Save runtime changes to the nodes of HRES layers in state. Dynamic and cloud nodes keep them across restart and reconfigure, static nodes keep them with -R or when a backup takes control, and nodes return to their configured layers when powered down.
* Fix SLURM_STEP_ID and SLURM_STEPID being set to a negative number for special steps. The batch step again has no SLURM_STEP_ID.
* scontrol - Add tasknode command to print the node each task of a job step runs on. It must be run on a node of the step and queries the local slurmstepd instead of slurmctld.
* slurmctld - Reject hierarchical resources requested with "|" (OR) in license requests.
* slurmctld - Reject license requests with whitespace before the count, such as "lic: 2".
* slurmctld - Reject reservation license requests with anything but a count after the layer list, such as "lic(layer)x:2".
* sacctmgr - Reject adding a resource whose name or server contains any of ",;|:=[]" or whitespace.
* slurmctld - Ignore remote licenses whose name or server contains any of ",;|:=[]" or whitespace, which a license request cannot name.
* data_parser/v0.0.46 - Add "runtime/data" to job descriptions to carry runtime plugin specific data with the job.
* slurmctld - Fix a node held for an expedited requeue being given to another job after it was set DOWN and resumed.
* Add "sluid" as a job_info field for lua.
* job_submit/lua - Allow setting job_desc.network, and clearing it when a job is modified.
* Add switch/lua to provide a lua-based plugin for managing system interconnects. New SlurmctldParameters switch_timeout and switch_retries set how long slurmctld waits for its hooks and how many times it retries a teardown. switch_drain_on_failure sets whether a failed or timed out teardown drains the job's nodes.
* Add arbitrary JSON metadata to jobs, nodes, partitions, and reservations, with KV/KVPatch updates through scontrol.
* Avoid terminating slurmdbd on unauthenticated requests.
* slurmctld - Fix job updates of TRES fields. Every TRES field is now validated before any is applied, so an invalid value is rejected instead of being stored or applied in part. Clearing a field with "" now works on a job that still requests GRES, and scontrol update Gres= now clears TresPerNode instead of storing an invalid value. Updates now reach every heterogeneous job component, job array task and federation sibling. A TresPerTask license update now multiplies the license count by the job's task count. A TRES update of a job array is now rejected for its running tasks, as other updates that only apply to pending jobs are.
* Add RelayIOPlugin, RelayIOParameters, and RelayIODefaultEnabled configuration options.
* Add --relay-io option to salloc, sbatch, and srun.
* scontrol - Add RelayIO field to scontrol update job.
* scontrol - Show a job's RelayIO setting in scontrol show job.
* Expose the job relay_io field to job_submit/lua.
* Fix crash when a connection is closed while TLS is being set up on it, which could happen during daemon shutdown. Only affects configurations with TLS enabled.
* Fix crash when a connection closes with a partially received RPC while connection is being extracted from conmgr. Only affects configurations with TLS enabled.
* auth/slurm - Permit operation with mismatched UIDs for SlurmUser across the cluster.
* Fix shard and mps counts not following a GPU count change on nodes with both, such as after scontrol update NodeName=... Gres=.
* Honor config_overrides set in a node's Parameters= or slurmd --parameters, not only in SlurmdParameters.
* Fix slurmctld double free after a node registered with a different socket and core layout under a node features plugin.
* slurmctld - Fix a job being given a GPU of another type after slurmd restarts with its GPUs on other device files.
* Add the ability to drain and resume individual GRES devices, CPUs or memory on a node without draining the whole node. This adds "scontrol update TresName=", "scontrol show drainedtres", the DEGRADED node state, the MaxDrainedTres node option and SlurmctldParameters=drain_absent_gres, and records drained TRES as node events in accounting. The never-populated node field gres_drain (gres_drained in slurmrestd) is removed.
* salloc - Add --kv to give a job its KV metadata document at submission.
* sbatch - Add --kv to give a job its KV metadata document at submission, on the command line, on #SBATCH lines and on scrontab #SCRON lines.
* srun - Add --kv to give a job its KV metadata document at submission, when srun creates the allocation.
* Prevent SlurmUser from running jobs in Partitions with DisableRootJobs=yes.
* auth/jwt - Permit use of credentials that do not resolve to a uid/gid on the slurmctld/slurmdbd host.
* Add experimental SlurmctldParameters=enable_deferred_user_lookup option to allow jobs to be submitted through auth/jwt tokens that do not map to a user on the slurmctld host.
* Fix slurmd writing two bytes past its GPU sensor counter under AcctGatherEnergyType=acct_gather_energy/gpu with an AutoDetect gpu plugin.
* srun/salloc/sbatch - Exit 0 for --cpu-bind, --mem-bind, and --hint help.
* slurmctld - Fix jobs starting after a slurmctld restart on a node whose reboot was already issued.
* slurmctld - Fix the backfill scheduler leaving a job PENDING with Reason=None instead of starting it on a node with free resources that is waiting on a non-ASAP reboot with nextstate=resume.
* slurmctld - Fix "scontrol cancel_reboot" clearing an administrative DRAIN and its reason, and an unexpected reboot clearing the DRAIN, on a node whose reboot was requested without nextstate.
* scancel - Ignore --batch when used with --full, as documented.
* scancel - Make --full send SIGSTOP and SIGCONT to all steps of the job.
* slurmd - Require authentication for the 'GET /readyz?verbose' endpoint, restricting access to only root and SlurmUser. Unauthenticated verbose probes, previously allowed when PrivateData was unset, now get HTTP 401.
* Fix sacct and other accounting job queries returning a job that never started once per reservation record created after it was submitted, which could exhaust slurmdbd memory for reservations with the REPLACE flag.
* slurmctld - Prevent a crash caused by a NULL pointer dereference if a running exclusive topology job's partition is removed during a restart or reconfigure.
* slurmctld - Fix memory leak when a federated job is submitted to a sibling running an older Slurm version than the client.
* slurmctld - Fix a federated job not being created on a sibling running an older Slurm version than the client.
* slurmctld - Fix possible crash when a federation sibling connection closes right after it opens.
* slurmctld - Fix crash when a federation sibling is removed while one of its job submissions is still queued.
* Widen the database event table cluster_nodes column past 64 KiB to prevent truncation on large clusters.
* Fix UINT32 OpenAPI fields to use the int64 format.
* data_parser/v0.0.46 - Reject out of range values for unsigned 32-bit integer fields instead of silently unsetting the field.
* data_parser/v0.0.46 - Reject out of range values for unsigned 32-bit integer fields with flags instead of silently setting them to infinite.
* slurmdbd - Load serializer plugins for HTTP endpoints.
* slurmdbd - Add / HTTP endpoint.
* slurmdbd - Serve HTTP requests on the RPC port.
* slurmdbd - Load HTTP plugins at startup.
* slurmdbd - Add /readyz HTTP endpoint.
* slurmdbd - Add /livez HTTP endpoint.
* slurmdbd - Add /healthz HTTP endpoint.
* slurmdbd - Add /{data_parser}/conf HTTP endpoint.
* slurmdbd - Add /{data_parser}/diag HTTP endpoint.
* stepmgr - Refuse new steps once the job has been cancelled or has ended, the same as for jobs without stepmgr.
* slurmctld - Fix memory leak when updating partition Alternate.
* select/cons_tres - Stop an --exclusive=topo job on a partition whose topology plugin does not support exclusive topology from blocking its topology unit when another configured plugin does.
* select/cons_tres - Fix the will-run test next to such a job freeing its GRES on the node.
* Speed up the scheduling of a job whose required node list is as long as its node count by evaluating it on those nodes only. Blocked by a job it can not preempt, such a job no longer simulates preempting every job of its partition on each scheduling cycle.
* List as preemption candidates of a pending job only the jobs on nodes it can be allocated, or in the topology units of those nodes, shortening the backfill cycles of jobs with a required or excluded node list.
* slurmdbd - Store the name of the user that cancelled a job or step instead of the uid, so sacct shows "CANCELLED by <user>".
* slurmdbd - Store the name of the user that set a node's reason instead of the uid, and show it in sacctmgr show events.
* slurmctld - Record the name of the user that cancelled a job or step from the request, instead of resolving its uid on the slurmdbd host.
* stepmgr - Record the user that cancelled a step of a --stepmgr job instead of root.
* Fix standby backup slurmctld overwriting the primary's registration with AccountingStorageExternalHost slurmdbds, which broke sinfo -M from the other cluster and caused DBD_REGISTER_CTLD errors. Only the active controller now registers with these slurmdbds.
* The slurm-smd Debian package now depends on the PMIx runtime library, which the mpi/pmix plugin loads at run time.
* slurmrestd - Do not set the process as dumpable when started as root and switching to another user with -u or -g.
