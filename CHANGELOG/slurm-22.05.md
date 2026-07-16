## Changes in Slurm 22.05.11

* Prevent message extension attacks that could bypass the message hash. CVE-2023-49933.
* Prevent NULL pointer dereference on size_valp overflow. CVE-2023-49936.
* Prevent double-xfree() on error in _unpack_node_reg_resp(). CVE-2023-49937.
* Prevent modified sbcast RPCs from opening a file with the wrong group permissions. CVE-2023-49938.

## Changes in Slurm 22.05.10

* Fix filesystem handling race conditions that could lead to an attacker taking control of an arbitrary file, or removing entire directories' contents. CVE-2023-41914.

## Changes in Slurm 22.05.9

* Allocate correct number of sockets when requesting gres and running with CR_SOCKET*.
* Fix handling of --prefer for job arrays.
* Fix regression in 22.05.5 that causes some jobs that request --ntasks-per-node to be incorrectly rejected.
* Fix slurmctld crash when a step requests fewer tasks than nodes.
* Fix incorrect task count in steps that request --ntasks-per-node and a node count with a range (e.g. -N1-2).
* Fix some valid step requests hanging instead of running.
* slurmrestd - avoid possible race condition which would cause slurmrestd to silently no longer accept new client connections.
* Fix GPU setup on CRAY systems when using the CRAY_CUDA_MPS environment variable. GPUs are now correctly detected in such scenarios.
* Fix the job prolog not running for jobs with the interactive step (salloc jobs with LaunchParameters=use_interactive_step set in slurm.conf) that were scheduled on powered down nodes. The prolog not running also broke job_container/tmpfs, pam_slurm_adopt, and x11 forwarding.
* task/affinity - fix slurmd segfault when request launch task requests of type "--cpu-bind=[map,mask]_cpu:<list>" have no <list> provided.
* sched/backfill - fix segfault when removing a PLANNED node from system.
* sched/backfill - fix deleted planned node staying in planned node bitmap.
* Fix nodes remaining as PLANNED after slurmctld save state recovery.
* Fix regression in 22.05.0rc1 that broke Nodes=ALL in a NodeSet.
* Fix incorrect memory constraint when receiving a job from 20.11 that uses cpu count for memory calculation.
* openapi/v0.0.[36-38] - avoid possible crash from jobs submitted with argv.
* openapi/v0.0.[36-38] - avoid possible crash from rejected jobs submitted with batch_features.
* srun - fix regression in 22.05.7 that prevented slurmctld from connecting to an srun running outside of a compute node.

## Changes in Slurm 22.05.8

* Fix potential deadlock at slurmctld startup when job has invalid qos.
* Avoid unnecessary call to clusteracct_storage_g_cluster_tres() when pinging dynamic nodes. This avoids significant slowdowns for slurmctld when ping_nodes() calls all nodes to re-register.
* openapi/dbv0.0.3[6-8] - fix segfault that could arrise from Slurm database connection failing.
* Fix regression introduced in 22.05.0rc1 when updating a NodeName=<nodelist> with NodeAddr and/or NodeHostname if the specified nodelist wasn't sorted.
* openapi/v0.0.38 - change type of nice field for job submissions to integer from string.
* openapi/v0.0.38 - add oversubscribe option to job submission properties.
* openapi/v0.0.38 - fix incorrect data used to populate "time/start" field for jobs.
* openapi/dbv0.0.38 - avoid dumping failure if preempt qos with level 0 is provided by slurmdbd query.
* Avoid an fd leak when lib dir for sbcast fails to be created.
* common/slurmdbd_pack - fix env and script hash when unpacking a 21.08 dbd_job_start_msg_t.
* Fix a race between job_container/tmpfs, cncu, x11 setup and adoption of pids in cray with pam_slurm_adopt.

## Changes in Slurm 22.05.7

* Fix slurmctlds in a federation not recovering a lost connection until a slurmctld restart or a federation update.
* Fix incorrectly rejecting jobs that request GRES on multiple sockets and --gres-flags=enforce-binding.
* Fix job core selection with --gres-flags=enforce-binding: this fixes cases where enforce-binding was not respected.
* openapi/v0.0.38 - set assoc_id resolution failure as a debug log instead of an error log.
* Fix issues when running in FrontEnd mode.
* Fix srun and salloc I/O code to set keepaliveinterval/keepalivetime on connections.
* Fix overwrite race of cont_id in cgroup/v2 when using task/cgroup with proctrack/cray_aries.
* Fix node remaining allocated after a reconfig with a completing job that has an EpilogSlurmctld instance still running.
* openapi/dbv0.0.38 - fix a cast to a wrong type
* openapi/dbv0.0.38 - correct issues where modifying association fields were ignored.
* openapi/dbv0.0.38 - correct issue where 16 bit integers were not able to be unset.
* openapi/dbv0.0.38 - correct issue where 16 & 32 bit integers were not able to be unset.
* Fix 'scontrol show hostlist' to read off a pipe correctly.
* Fix 'scontrol reconfigure' with configless enabled to send updated configs to 21.08 nodes correctly. (22.05 nodes are unaffected.)
* Fix assertion and locks for dynamic node registrations with rpc queueing.
* rest_auth/local - avoid logging root access as an error but instead as a normal info log.
* slurmrestd - switch users earlier on startup to avoid sockets being made as root.
* slurmrestd - disable umask during socket creation as the authentication is handled by Slurm instead of file ACLs.
* Fix sbcast, srun --bcast, sstat not working with cloud/dynamic nodes.
* sacctmgr - Allow removal of user's default account when AllowNoDefAcct=yes.
* Fix segfault caused by race condition when a reconfiguration is requested while slurmd initializes.
* openapi/v0.0.38 - avoid division by zero causing slurmrestd to crash during diag queries.
* Update default values for text/blob columns in the database if upgrading to MariaDB >= 10.2.1 from a pre-10.2.1 version or from MySQL to avoid sacctmgr failures.
* Fix several minor memory leaks in the cgroup/v2 plugin.
* Fix possible issue when enabling controllers with long names in cgroup/v2.
* Fix potential segfault in slurmd when using acct_gather_energy/ipmi.
* Fix controller being able to contact non-addressable step hosts.
* Allow setting SuspendTime to -1 when used on a partition.
* openapi/v0.0.38 - fix maximum_memory_per_node description typo.
* openapi/v0.0.38 - add maximum_memory_per_cpu to GET /partitions
* openapi/v0.0.38 - add default_memory_per_node to GET /partitions
* Fix dynamic node losing dynamic node status when changing state to FUTURE.
* Fix adoption into most recent job in pam_slurm_adopt and cgroup/v2.
* gres/shard - Fix plugin_name definition to print correctly in debug lines.

## Changes in Slurm 22.05.6

* Fix a partition's DisableRootJobs=no from preventing root jobs from working.
* Fix the number of allocated cpus for an auto-adjustment case in which the job requests --ntasks-per-node and --mem (per-node) but the limit is MaxMemPerCPU.
* Fix POWER_DOWN_FORCE request leaving node in completing state.
* Do not count magnetic reservation queue records towards backfill limits.
* Clarify error message when --send-libs=yes or BcastParameters=send_libs fails to identify shared library files, and avoid creating an empty "<filename>_libs" directory on the target filesystem.
* Fix missing CoreSpec on dynamic nodes upon slurmctld restart.
* Fix node state reporting when using specialized cores.
* Fix number of CPUs allocated if --cpus-per-gpu used.
* Add flag ignore_prefer_validation to not validate --prefer on a job.
* Fix salloc/sbatch SLURM_TASKS_PER_NODE output environment variable when the number of tasks is not requested.
* Permit using wildcard magic cookies with X11 forwarding.
* cgroup/v2 - Add check for swap when running OOM check after task termination.
* Fix deadlock caused by race condition when disabling power save with a reconfigure.
* Fix memory leak in the dbd when container is sent to the database.
* openapi/dbv0.0.38 - correct dbv0.0.38_tres_info.
* Fix node SuspendTime, SuspendTimeout, ResumeTimeout being updated after altering partition node lists with scontrol.
* jobcomp/elasticsearch - fix data_t memory leak after serialization.
* Fix issue where '*' wasn't accepted in gpu/cpu bind.
* Fix SLURM_GPUS_ON_NODE for shared GPU gres (MPS, shards).
* Add SLURM_SHARDS_ON_NODE environment variable for shards.
* Fix srun error with overcommit.
* Fix bug in core selection for the default cyclic distribution of tasks across sockets, that resulted in random task launch failures.
* Fix core selection for steps requesting multiple tasks per core when allocation contains more cores than required for step.
* gpu/nvml - Fix MIG minor number generation when GPU minor number (/dev/nvidia[minor_number]) and index (as seen in nvidia-smi) do not match.
* Fix accrue time underflow errors after slurmctld reconfig or restart.
* Suppress errant errors from prolog_complete about being unable to locate "node:(null)".
* Fix issue where shards were selected from multiple gpus and failed to allocate.
* Fix step cpu count calculation when using --ntasks-per-gpu=.
* Fix overflow problems when validating array index parameters in slurmctld and prevent a potential condition causing slurmctld to crash.
* Remove dependency on json-c in slurmctld when running with power saving. Only the new "SLURM_RESUME_FILE" support relies on this, and it will be disabled if json-c support is unavailable instead.

## Changes in Slurm 22.05.5

* Fix node becoming IDLE while in an invalid registration state.
* When a job is completing avoid potential dereference.
* Avoid setting preempt_time for a job erroneously.
* Fix situation where we don't requeue correctly when a job is finishing.
* job_container/tmpfs - Avoid leaking namespace file descriptor.
* common/slurm_opt - fix memory leak in client commands or slurmrestd when the --chdir option is set after option reset.
* openapi/dbv0.0.38 - gracefully handle unknown associations assigned to jobs.
* openapi/dbv0.0.38 - query all associations to avoid errors while dumping jobs.
* Load hash plugin at slurmstepd launch time to prevent issues loading the plugin at step completion if the Slurm installation is upgraded.
* Fix gcc 12.2.1 compile errors.
* Fix future magnetic reservations preventing heterogeneous jobs from starting.
* Prevent incorrect error message from being generated for operator/admins using the 'scontrol top' command.
* slurmrestd - correct issue where larger requests could result in a single byte getting removed from inside of the POST request.
* Fix regression in task count calculation for --ntasks-per-gpu with multiple nodes.
* Update nvml plugin to match the unique id format for MIG devices in new Nvidia drivers.
* Fix segfault on backup slurmdbd if no QoS is present in DB.
* Fix clang 11 compile errors.
* Fix task distribution calculations across sockets with --distribution=cyclic.
* Fix task distribution calculations with --ntasks-per-gpu specified without an explicit --ntasks value.
* Fix job arrays not showing correct features.
* Fix job having wrong features used when using preferred features.
* Fix task/cray_aries error finishing an interactive step, avoiding correct cleanup.
* Correctly set max_nodes when --ntasks=1.
* Fix configure script on FreeBSD.

## Changes in Slurm 22.05.4

* Fix return code from salloc when the job is revoked prior to executing user command.
* Fix minor memory leak when dealing with gres with multiple files.
* Fix printing for no_consume gres in scontrol show job.
* sinfo - Fix truncation of very large values when outputting memory.
* Fix multi-node step launch failure when nodes in the controller aren't in natural order. This can happen with inconsistent node naming (such as node15 and node052) or with dynamic nodes which can register in any order.
* job_container/tmpfs - Prevent reading the plugin config multiple times per step.
* Fix wrong attempt of gres binding for gres w/out cores defined.
* Fix build to work with '--without-shared-libslurm' configure flag.
* Fix power_save mode when repeatedly configuring too fast.
* Fix sacct -I option.
* Prevent jobs from being scheduled on future nodes.
* Fix memory leak in slurmd happening on reconfigure when CPUSpecList used.
* Fix sacctmgr show event [min|max]cpus.
* Fix regression in 22.05.0rc1 where a prolog or epilog that redirected stdout to a file could get erroneously killed, resulting in job launch failure (for the prolog) and the node being drained.
* cgroup/v1 - Make a static variable to remove potential redundant checking for if the system has swap or not.
* cgroup/v1 - Add check for swap when running OOM check after task termination.
* job_submit/lua - add --prefer support
* cgroup/v1 - fix issue where sibling steps could incorrectly be accounted as OOM when step memory limit was the same as the job allocation. Detect OOM events via memory.oom_control oom_kill when exposed by the kernel instead of subscribing notifications with eventfd.
* Fix accounting of oom_kill events in cgroup/v2 and task/cgroup.
* Fix segfault when slurmd reports less than configured gres with links after a slurmctld restart.
* Fix TRES counts after node is deleted using scontrol.
* sched/backfill - properly handle multi-reservation HetJobs.
* sched/backfill - don't try to start HetJobs after system state change.
* openapi/v0.0.38 - add submission of job->prefer value.
* slurmdbd - become SlurmUser at the same point in logic as slurmctld to match plugins initialization behavior. This avoids a fatal error when starting slurmdbd as root and root cannot start the auth or accounting_storage plugins (for example, if root cannot read the jwt key).
* Fix memory leak when attempting to update a job's features with invalid features.
* Fix occasional slurmctld crash or hang in backfill due to invalid pointers.
* Fix segfault on Cray machines if cgroup cpuset is used in cgroup/v1.

## Changes in Slurm 22.05.3

* job_container/tmpfs - cleanup containers even when the .ns file isn't mounted anymore.
* Ignore the bf_licenses option if using sched/builtin.
* Do not clear the job's requested QOS (qos_id) when ineligible due to QOS.
* Emit error and add fail-safe when job's qos_id changes unexpectedly.
* Fix timeout value in log.
* openapi/v0.0.38 - fix setting of DefaultTime when dumping a partition.
* openapi/dbv0.0.38 - correct parsing association QOS field.
* Fix LaunchParameters=mpir_use_nodeaddr.
* Fix various edge cases where accrue limits could be exceeded or cause underflow error messages.
* Fix issue where a job requesting --ntasks and --nodes could be wrongly rejected when spanning heterogeneous nodes.
* openapi/v0.0.38 - detect when partition PreemptMode is disabled
* openapi/v0.0.38 - add QOS flag to handle partition PreemptMode=within
* Add total_cpus and total_nodes values to the partition list in the job_submit/lua plugin.
* openapi/dbv0.0.38 - reject and error on invalid flag values in well defined flag fields.
* openapi/dbv0.0.38 - correct QOS preempt_mode flag requests being silently ignored.
* accounting_storage/mysql - allow QOS preempt_mode flag updates when GANG mode is requested.
* openapi/dbv0.0.38 - correct QOS flag modifications request being silently ignored.
* sacct/sinfo/squeue - use openapi/[db]v0.0.38 for --json and --yaml modes.
* Improve error messages when using configless and fetching the config fails.
* Fix segfault when reboot_from_controller is configured and scontrol reboot is used.
* Fix regression which prevented a cons_tres gpu job to be submitted to a cons_tres cluster from a non-con_tres cluster.
* openapi/dbv0.0.38 - correct association QOS list parsing for updates.
* Fix rollup incorrectly divying up unused reservation time between associations.
* slurmrestd - add SLURMRESTD_SECURITY=disable_unshare_files environment variable.
* Update rsmi detection to handle new default library location.
* Fix header inclusion from slurmstepd manager code leading to multiple definition errors when linking --without-shared-libslurm.
* slurm.spec - explicitly disable Link Time Optimization (LTO) to avoid linking errors on systems where LTO-related RPM macros are enabled by default and the binutils version has a bug.
* Fix issue in the api/step_io message writing logic leading to incorrect behavior in API consuming clients like srun or sattach, including a segfault when freeing IO buffers holding traffic from the tasks to the client.
* openapi/dbv0.0.38 - avoid job queries getting rejected when cluster is not provided by client.
* openapi/dbv0.0.38 - accept job state filter as verbose names instead of only numeric state ids.
* Fix regression in 22.05.0rc1: if slurmd shuts down while a prolog is running, the job is cancelled and the node is drained.
* Wait up to PrologEpilogTimeout before shutting down slurmd to allow prolog and epilog scripts to complete or timeout. Previously, slurmd waited 120 seconds before timing out and killing prolog and epilog scripts.
* GPU - Fix checking frequencies to check them all and not skip the last one.
* GPU - Fix logic to set frequencies properly when handling multiple GPUs.
* cgroup/v2 - Fix typo in error message.
* cgroup/v2 - More robust pattern search for events.
* Fix slurm_spank_job_[prolog|epilog] failures being masked if a Prolog or Epilog script is defined (regression in 22.05.0rc1).
* When a job requested nodes and can't immediately start, only report to the user (squeue/scontrol et al) if nodes are down in the requested list.
* openapi/dbv0.0.38 - Fix qos list/preempt not being parsed correctly.
* Fix dynamic nodes registrations mapping previously assigned nodes.
* Remove unnecessarily limit on count of 'shared' gres.
* Fix shared gres on CLOUD nodes not properly initializing.

## Changes in Slurm 22.05.2

* Fix a segfault in slurmctld when requesting gres in job arrays.
* Prevent jobs from launching on newly powered up nodes that register with invalid config.
* Fix a segfault when there's no memory.swap.current interface in cgroup/v2.
* Fix memleak in cgroup/v2.

## Changes in Slurm 22.05.1

* Flush the list of Include config files on SIGHUP.
* Fix and update Slurm completion script.
* jobacct_gather/cgroup - Add VMem support both for cgroup v1 and v2.
* Allow subset of node state transitions when node is in INVAL state.
* Remove INVAL state from cloud node after being powered down.
* When showing reason UID in scontrol show node, use the authenticated UID instead of the login UID.
* Fix calculation of reservation's NodeCnt when using dynamic nodes.
* Add SBATCH_{ERROR,INPUT,OUTPUT} input environment variables for --error, --input and --output options respectively.
* Prevent oversubscription of licenses by the backfill scheduler when not using the new "bf_licenses" option.
* Jobs with multiple nodes in a heterogeneous cluster now have access to all the memory on each node by using --mem=0. Previously the memory limit was set by the node with the least amount of memory.
* Don't limit the size of TaskProlog output (previously TaskProlog output was limited to 4094 characters per line, which limited the size of exported environment variables or logging to the task).
* Fix usage of possibly uninitialized buffer in proctrack/cgroup.
* Fix memleak in proctrack/cgroup proctrack_p_wait.
* Fix cloud/remote het srun jobs.
* Fix a segfault that may happen on gpu configured as no_consume.

## Changes in Slurm 22.05.0

* openapi/v0.0.38 - add group name to job info
* openapi/v0.0.38 - add container field to job description.
* openapi/v0.0.38 - add container to job submission.
* openapi/[db]v0.0.38 - add container field to job description.
* openapi/dbv0.0.38 - fix bug where QOS update set various limits to 0.
* openapi/dbv0.0.38 - properly initialize wckey record.
* openapi/dbv0.0.38 - properly initialize cluster record.
* openapi/dbv0.0.38 - gracefully update existing QOSs.
* Fix x11 forwarding with job_container/tmpfs and without home_xauthority.
* Fix reconfig of dynamic nodes with gres.
* Fix corner case issues when removing assocs/qos' while jobs are completing and a reconfigure happens.
* Add SlurmdParameters=numa_node_as_socket to use the numa node as a socket.
* Avoid creating and referencing NULL script/env hash entries when not storing the job script or job environment.
* slurmd - If a het component job fails to launch due to a node prolog or other failure, properly handle the whole het job.
* Fix possible race condition while handling forwarded messages.
* Add new SchedulerParameters option "bf_licenses" to track licenses as within the backfill scheduler.
* Fix resuming nodes not part of an allocation.
* Minor memory leak fixes.
* Use accept4() and pipe2() to ensure new file descriptors are always set with close-on-exec flag.
* slurmctld - allow users to request tokens for themselves by name.
* srun - adjust output of "--mpi=list".
* Fix "--gpu-bind=single:" having wrong env variables.
* Sync missing pieces in slurmctld SIGHUP handler with respect to REQUEST_RECONFIGURE RPC handler.
* Fixed the behavior of the --states=all option in squeue to actually show all of the states.
* job_container - Don't constrain a job using --no-allocate.
* Fix slurmctld segfault when a step requests a gres without a file.
* Fix slurmctld segfault when cleaning up an overlapping step.
* Dynamic node fixes and updates.
* Added error logging when a node goes into an invalid state so that the user can still see the reason even if the user set a custom reason beforehand.
* Remove redundant message when libdbus is found during configure.
* Fix pmix to honor SrunPortRange.
* Fix issue with ntasks_per_socket and ntasks_per_core with --exclusive
* openapi/v0.0.38 - change job parsing error message to reflect reality.
* Avoid segfault/assert when --thread-spec used with config_overrides
* Don't send SIGTERM to prematurely in run_command.
* Add logging to inform where networking errors are occurring on slurmd end.
* openapi/dbv0.0.38 - Enforce GET /association/ to dump only a single association.
* openapi/dbv0.0.38 - Enforce DELETE /association/ to delete only a single association.
* openapi/dbv0.0.38 - Add parameters to filter to GET /associations/.
* openapi/dbv0.0.38 - Add DELETE /associations using parameters to set filters.
* openapi/dbv0.0.38 - Fix plural form of delete association response schema.

## Changes in Slurm 22.05.0rc1

* gres/gpu - Avoid stripping GRES type field during normalization if any other GRES have a defined type field.
* burst_buffer/datawarp - free bb_job after stage-out or teardown are done.
* acct_gather_energy_rsmi has been renamed acct_gather_energy_gpu.
* Remove support for (non-functional) --cpu-bind=boards.
* accounting_storage/mysql - ensure new non-HetJobs have het_job_offset NO_VAL in the database and fix the same field when retrieving older bad records.
* PreemptMode now works as a condition for qos in sacctmgr.
* scancel - add "--me" option.
* gres/gpu - Fix configured/system-detected GRES match for some combinations for which the sorting affected the expected selection result.
* openapi/v0.0.38 - Fork existing openapi/v0.0.37 plugin.
* openapi/dbv0.0.38 - Fork existing openapi/dbv0.0.37 plugin.
* openapi/v0.0.35 - Plugin has been removed.
* scrontab - Don't accept extra letters after a '@' repeating pattern.
* openapi/dbv0.0.38 - Add missing method POST for /associations/.
* Make DefMemPerCPU/MaxMemPerCPU and DefMemPerNode/MaxMemPerNode precedence at the global level the same as in partition level, and print an error if both of a pair are set.
* openapi/v0.0.38 - Allow strings for JobIds instead of only numerical JobIds for GET, DELETE, and POST job methods.
* openapi/v0.0.38 - enable job priority field for job submissions and updates.
* openapi/v0.0.38 - request node states query includes MIXED state instead of only allocated.
* openapi/dbv0.0.38 - Correct tree position of dbv0.0.38_job_step.
* Enforce all Slurm plugin requirements for plugins specified with absolute path.
* Added --prefer option at job submission to allow for 'soft' constraints.
* Remove support for PMIx 1.x
* slurmrestd - Unlink unix sockets before binding to avoid errors where previous run of slurmrestd did not properly cleanup the sockets.
* Add extra 'EnvironmentFile=-/etc/default/$service' setting to service files.
* slurmctld/agent - spawn more agent threads in a single _agent_retry().
* openapi/v0.0.38 - change job response types to more specific types than generic string.
* srun - refuse to run on malformed SPANK environment variable.
* Allow jobs to pack onto nodes already rebooting with the desired features.
* Reset job start time after nodes are rebooted.
* Node features (if any) are passed to RebootProgram if run from slurmctld.
* Fix sending multiple features to RebootProgram.
* If a task/srun prolog fails don't allow the step to continue running.
* Fail srun when using invalid --cpu-bind options.
* cgroup/v1 - Set swappiness at job level instead of at root level.
* Fix issues where a step's GRES request could never be satisfied but the step remained pending forever instead of being rejected.
* openapi/v0.0.38 - Remove errant extra space after JOB_CPUS_SET flag.
* slurmrestd - refuse to run with gid 0, or under SlurmUser's gid.
* Storing batch scripts and env vars are now in indexed tables using substantially less disk space. Those storing scripts in 21.08 will all be moved and indexed automatically.
* Run MailProg through slurmscriptd instead of directly fork+exec()'ing from slurmctld.
* Add acct_gather_interconnect/sysfs plugin.
* Fix gpus spanning more resources than needed when using --cpus-per-gpu.
* burst_buffer plugins - err_msg added to bb_p_job_validate().
* burst_buffer plugins - Send user specific error message if permission denied due to AllowUsers or DenyUsers in burst_buffer.conf.
* Fatal if the mutually-exclusive JobAcctGatherParams options of UsePss and NoShared are both defined.
* Future and Cloud nodes are treated as "Planned Down" in usage reports.
* Add "condflags=open" to sacctmgr show events to return open/currently down events.
* Skip --mem-per-gpu impact on GRES availability when CR_MEMORY not set.
* Add reservation start time as a new schedulers sorting factor for magnetic or multi-reservation job queue records.
* Improve Dynamic Future node startup to load in config with mapped nodename.
* Add new shard plugin for sharing gpus but not with mps.
* acct_gather_energy/xcc - add support for Lenovo SD650 V2.
* Remove cgroup_allowed_devices_file.conf support.
* sacct -f flag implies -c flag.
* Node state flags (DRAIN, FAILED, POWERING UP, etc.) will be cleared now if node state is updated to FUTURE.
* Clear GRES environment variables for jobs or steps that request --gres=none.
* slurmctld - Avoid requiring environment to be set when container job is specified.
* Move the KeepAliveTime option into CommunicationParameters.
* slurm.spec - stop explicitly packaging pkgconfig directory to avoid a conflict with the pkgconfig package.
* Fix sacctmgr load dump printing incorrect default QOS when no value is set.
* Fix very early logging in the controller when daemonized
* Fix possibility of primary group going missing when running jobs submitted after a user has switched primary groups (eg: with sg).
* Make it so srun no longer inherits SLURM_CPUS_PER_TASK. It now will only inherit SRUN_CPUS_PER_TASK.
* Make HetJob signaling requests using the HetJobId+HetJobOffset format behave the same as with direct JobId requests with regards to job state and whole_hetjob considerations.
* scancel - avoid issuing a RPC for each HetJob non-leader components hit by using filters (not using jobids) when a RPC is issued for their leader.
* Make slurmctld call the JobCompPlugin set location operation on SIGUSR2. As a relevant consequence, the filetxt plugin reopens the file for potential logrotation.
* openapi/v0.0.38 - added fields bf_table_size and bf_table_size_mean to diag query.
* Handle scontrol setdebug[flags] and scontrol reconfigure in slurmscriptd.
* openapi/v0.0.38 - add /licenses endpoint.
* slurmrestd - only compile JWT authentication plugin if libjwt is present.
* Remove connect_timeout and timeout options from JobCompParams as there's no longer a connectivity check happening in the jobcomp/elasticsearch plugin when setting the location off of JobCompLoc.
* Add %n for NodeName substitution in SlurmdSpoolDir in nss_slurm.conf.
* job_container/tmpfs - add basic environment variables to InitScript.
* openapi/dbv0.0.38 - split security entries in openapi.json.
* openapi/v0.0.38 - Add schema for Slurm meta in responses.
* openapi/dbv0.0.38 - Add schema for Slurm meta in responses.
* openapi/v0.0.38 - Correct field "errno" to "error_number" in schema for errors.
* openapi/dbv0.0.38 - add failure response contents in openapi.json.
* openapi/v0.0.38 - add failure response contents in openapi.json.
* openapi/dbv0.0.38 - add requestBody for /users/ in openapi.json.
* openapi/dbv0.0.38 - add requestBody for /accounts/ in openapi.json.
* openapi/dbv0.0.38 - add missing response field "removed_associations" in openapi.json
* openapi/dbv0.0.38 - Correct type from object to array for user associations list in openapi.json.
* openapi/dbv0.0.38 - Add missing field for tres minutes per qos in qos list in openapi.json.
* openapi/dbv0.0.38 - Add missing field for name in qos list in openapi.json.
* openapi/dbv0.0.38 - Correct tres field in qos list in openapi.json.
* openapi/dbv0.0.38 - Correct het job details types in jobs in openapi.json
* openapi/dbv0.0.38 - Correct task type for job steps in openapi.json.
* openapi/dbv0.0.38 - Sync fields for errors to source in openapi.json.
* openapi/dbv0.0.38 - Add missing field for adding clusters in clusters list in openapi.json.
* openapi/v0.0.38 - correct parameter styles from "simple" to "form".
* openapi/dbv0.0.38 - correct parameter styles from "simple" to "form".
* openapi - add flags to slurm_openapi_p_get_specification().
* openapi/v0.0.38 - use new operationId generation to ensure uniqueness.
* openapi/dbv0.0.38 - use new operationId generation to ensure uniqueness.
* slurmstepd - avoid possible race condition while updating step return code during job startup.
* slurmstepd - report and log I/O setup failure using corresponding error code during job start.
* Do not run the job if PrologSlurmctld times out.
* Fix SPANK plugin calls slurm_spank_job_prolog and slurm_spank_job_epilog not properly respecting PrologEpilogTimeout.
* Add support for 'scontrol delete <ENTITY> <ID>' format alongside the already expected 'scontrol delete <ENTITY>=<ID>'.
* Fix task/cgroup plugin which was not adding stepd to memory cgroup.
* preempt/qos - add support for WITHIN mode to allow for preemption between jobs within the same qos.
* Fix sattach for interactive step.
* Fix srun -Z when using nss_slurm.
* Avoid memory leak in srun -Z.
* openapi/dbv0.0.38 - disable automatic lookup of all HetJob components on specific job lookups.
* Add support for hourly reoccurring reservations.
* openapi/v0.0.38 - Fix misspelling of job parameter time_minimum.
* openapi/v0.0.38 - Fix misspelling of job parameter cpu_binding_hint.
* openapi/v0.0.38 - Fix misspelling of job parameter mcs_label
* openapi/dbv0.0.38 - Fix issue where association's QOS list consisted of IDs instead of names.
* Allow nodes to be dynamically added and removed from the system.
* srun --overlap now allows the step to share all resources (CPUs, memory, and GRES), where previously --overlap only allowed the step to share CPUs with other steps.
* openapi/v0.0.38 - new format for how core and socket allocations are dumped for jobs.
* openapi/v0.0.38 - add RPC call statistics to diag endpoint.
* openapi/[db]v0.0.36 - plugins have been marked as deprecated.
* sacct - allocations made by srun will now always display the allocation and step(s). Previously, the allocation and step were combined when possible.
* Steps now allocate gres according to topo data.
* Add validation of numbers provided to --gpu-bind=map_gpu and --gpu-bind=mask_gpu=.
* Fatal error if CgroupReleaseAgentDir is configured in cgroup.conf. The option has long been obsolete.
* cons_tres - change definition of the "least loaded node" (LLN) to the node with the greatest ratio of available cpus to total cpus.
* Fatal if more than one burst buffer plugin is configured.
* Added keepaliveinterval and keepaliveprobes options to CommunicationParameters.
* Correctly failover to backup slurmdbd if the primary slurmdbd node crashes.
* Add support to ship Include configuration files with configless.
* Fix issues with track_script cleanup.
* Add new max_token_lifespan limit to AuthAltParameters.
* sacctmgr - allow Admins to update AdminComment and SystemComment fields.
* slurmd - Cache node features at startup and scontrol reconfig. It avoids executing node_features/helpers scripts in the middle of a job. This clould potentially affect job performance.
* Pass and use alias_list through credential instead of environment variable.
* Add ability to get host addresses from nss_slurm.
* Enable reverse fanout for cloud+alias_list jobs.
* Disallow slurm.conf node configurations with NodeName=ALL.
* Add support to delete/update nodes by specifying nodesets or the 'ALL' keyword alongside the delete/update node message nodelist expression (i.e. 'scontrol delete/update NodeName=ALL' or 'scontrol delete/update NodeName=ns1,nodes[1-3]').
* Add support for PMIx v4
* Add support for PMIx v5
* Correctly load potentially NULL values from slurmdbd archive files.
* Add slurmdbd.conf option AllowNoDefAcct to remove requirement for users to have a default account.
* Fix issues related to users with very large uids.
* common/openapi - fix bug populating methods for parameter-less endpoint.
* slurmrestd/operations - fix memory leak when resolving bad path.
* sacctmgr - improve performance of query generation for archive load.
* sattach - allow connecting to interactive steps with JOBID.interactive
* Fix for --gpus-per-task parsing when using --cpus-per-gpu and multiple gpu types in the same request.
* rest_auth/local - always log when new slurmdbd connection fails.
* openapi/dbv0.0.38 - gracefully update existing associations.
* Improve auto-detection of pmix
* openapi/dbv0.0.38 - Add missing method POST for /qos/.
* scrontab - On errors, print 1-index line numbers instead of 0-indexing them.
* openapi/dbv0.0.38 - Correct OpenAPI specification for diag request.
* openapi/dbv0.0.38 - reject requests with incorrect TRES specification.
* sacctmgr - reject requests with incorrect TRES specification.
* Correct issue where conversion to/from JSON/YAML may have resulted in empty strings being replaced with strings containing "null" instead.
* Attempt to requeue jobs terminated by slurm.conf changes (node vanish, node socket/core change, etc). Processes may still be running on excised nodes. Admin should take precautions when removing nodes that have jobs on running on them.
* Fix race with task/cgroup memory and jobacctgather/cgroup, the first was removing the pid from the task_X cgroup directory.
* openapi/dbv0.0.38 - set default account org and desc to be account name.
* openapi/dbv0.0.38 - Allow strings for JobId instead of only numerical JobId for GET job methods.
* Add switch/hpe_slingshot plugin.
* cloud_reg_addrs - use hostname from slurmd rather than from munge credential for NodeHostName.
* Store assoc usage in parent assoc when deleted to preserve account fairshare.
* CVE-2022-29500 - Prevent credential abuse.
* CVE-2022-29501 - Prevent abuse of REQUEST_FORWARD_DATA.
* CVE-2022-29502 - Correctly validate io keys.
* openapi/dbv0.0.38 - set with_deleted to false by default for /user[s].
* openapi/dbv0.0.38 - add with_deleted input parameter to GET /user[s].
* openapi/dbv0.0.38 - add deleted flag to /user[s] output.
* openapi/dbv0.0.38 - set with_deleted to false by default for GET /qos.
* openapi/dbv0.0.38 - add with_deleted input to GET /qos.
* openapi/dbv0.0.38 - set with_deleted to false by default for GET /account[s]
* openapi/dbv0.0.38 - add with_deleted input to GET /account[s].
* Include k12 hash of the RPC message body in the auth/munge tokens to provide for additional communication resiliency.
* Allow job steps to continue to launch during OverTimeLimit.
* slurmctld - avoid crash when attempting to load job without a script.
* Fix exclusive jobs breaking MaxCPUsPerNode limit
* Error if ConstrainSwapSpace is set and system doesn't have the kernel with CONFIG_MEMCG_SWAP set. This will avoid bad configurations.
* Add SlurmdParameters=numa_node_as_socket.
* Fix incorrect node allocation when using multiple counts with --constraint and --ntasks-per-node.
