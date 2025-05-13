## Changes in Slurm 23.02.9

* sattach - Fix regression from 23.02.8 security fix leading to crash.

## Changes in Slurm 23.02.8

* Fix rare deadlock when a dynamic node registers at the same time that a once per minute background task occurs.
* Fix assertion in developer mode on a failed message unpack.
* switch/hpe_slingshot - Fix security issue around managing VNI access. CVE-2024-42511.

## Changes in Slurm 23.02.7

* libslurm_nss - Avoid causing glibc to assert due to an unexpected return from slurm_nss due to an error during lookup.
* Fix job requests with --tres-per-task sometimes resulting in bad allocations that cannot run subsequent job steps.
* Fix issue with slurmd where srun fails to be warned when a node prolog script runs beyond MsgTimeout set in slurm.conf.
* gres/shard - Fix plugin functions to have matching parameter orders.
* gpu/nvml - Fix issue that resulted in the wrong MIG devices being constrained to a job
* gpu/nvml - Fix linking issue with MIGs that prevented multiple MIGs being used in a single job for certain MIG configurations
* Add JobAcctGatherParams=DisableGPUAcct to disable gpu accounting.
* Fix file descriptor leak in slurmd when using acct_gather_energy/ipmi with DCMI devices.
* sview - avoid crash when job has a node list string > 49 characters.
* Prevent slurmctld crash during reconfigure when packing job start messages.
* Preserve reason uid on reconfig.
* Update node reason with updated INVAL state reason if different from last registration.
* acct_gather_energy/ipmi - Improve logging of DCMI issues.
* conmgr - Avoid NULL dereference when using auth/none.
* data_parser/v0.0.39 - Fixed how deleted QOS and associations for jobs are dumped.
* burst_buffer/lua - fix stage in counter not decrementing when a job is cancelled during stage in. This counter is used to enforce the limit of 128 scripts per stage.
* gpu/oneapi - Add support for new env vars ZE_FLAT_DEVICE_HIERARCHY and ZE_ENABLE_PCI_ID_DEVICE_ORDER.
* data_parser/v0.0.39 - Fix how the "INVALID" nodes state is dumped.
* data_parser/v0.0.39 - Fix parsing of flag arrays to allow multiple flags to be set.
* Avoid leaking sockets when an x11 application is closed in an allocation.
* Fix missing mutex unlock in group cache code which could cause slurmctld to freeze.
* Fix scrontab monthly jobs possibly skipping a month if added near the end of the month.
* Fix loading of the gpu account gather energy plugin.
* Fix slurmctld segfault when reconfiguring after a job resize.
* Fix crash in slurmstepd that can occur when launching tasks via mpi using the pmi2 plugin and using the route/topology plugin.
* data_parser/v0.0.39 - skip empty string when parsing QOS ids.
* Fix "qos <id> doesn't exist" error message in assoc_mgr_update_assocs to print the attempted new default qos, rather than the current default qos.
* Remove error message from assoc_mgr_update_assocs when purposefully resetting the default qos.
* data_parser/v0.0.39 - Fix segfault when POSTing data with association usage.
* Prevent message extension attacks that could bypass the message hash. CVE-2023-49933.
* Prevent message hash bypass in slurmd which can allow an attacker to reuse root-level MUNGE tokens and escalate permissions. CVE-2023-49935.
* Prevent NULL pointer dereference on size_valp overflow. CVE-2023-49936.
* Prevent double-xfree() on error in _unpack_node_reg_resp(). CVE-2023-49937.
* Prevent modified sbcast RPCs from opening a file with the wrong group permissions. CVE-2023-49938.

## Changes in Slurm 23.02.6

* Fix CpusPerTres= not upgreadable with scontrol update
* Fix unintentional gres removal when validating the gres job state.
* Fix --without-hpe-slingshot configure option.
* Fix cgroup v2 memory calculations when transparent huge pages are used.
* Fix parsing of sgather --timeout option.
* Fix regression from 22.05.0 that caused srun --cpu-bind "=verbose" and "=v" options give different CPU bind masks.
* Fix "_find_node_record: lookup failure for node" error message appearing for all dynamic nodes during reconfigure.
* Avoid segfault if loading serializer plugin fails.
* slurmrestd - Correct OpenAPI format for 'GET /slurm/v0.0.39/licenses'.
* slurmrestd - Correct OpenAPI format for 'GET /slurm/v0.0.39/job/{job_id}'.
* slurmrestd - Change format to multiple fields in 'GET /slurmdb/v0.0.39/assocations' and 'GET /slurmdb/v0.0.39/qos' to handle infinite and unset states.
* When a node fails in a job with --no-kill, preserve the extern step on the remaining nodes to avoid breaking features that rely on the extern step such as pam_slurm_adopt, x11, and job_container/tmpfs.
* auth/jwt - Ignore 'x5c' field in JWKS files.
* auth/jwt - Treat 'alg' field as optional in JWKS files.
* Allow job_desc.selinux_context to be read from the job_submit.lua script.
* Skip check in slurmstepd that causes a large number of errors in the munge log: "Unauthorized credential for client UID=0 GID=0". This error will still appear on slurmd/slurmctld/slurmdbd start up and is not a cause for concern.
* slurmctld - Allow startup with zero partitions.
* Fix some mig profile names in slurm not matching nvidia mig profiles.
* Prevent slurmscriptd processing delays from blocking other threads in slurmctld while trying to launch {Prolog|Epilog}Slurmctld.
* Fix sacct printing ReqMem field when memory doesn't exist in requested TRES.
* Fix how heterogeneous steps in an allocation with CR_PACK_NODE or -mpack are created.
* Fix slurmctld crash from race condition within job_submit_throttle plugin.
* Fix --with-systemdsystemunitdir when requesting a default location.
* Fix not being able to cancel an array task by the jobid (i.e. not <jobid>_<taskid>) through scancel, job launch failure or prolog failure.
* Fix cancelling the whole array job when the array task is the meta job and it fails job or prolog launch and is not requeable. Cancel only the specific task instead.
* Fix regression in 21.08.2 where MailProg did not run for mail-type=end for jobs with non-zero exit codes.
* Fix incorrect setting of memory.swap.max in cgroup/v2.
* Fix jobacctgather/cgroup collection of disk/io, gpumem, gpuutil TRES values.
* Fix -d singleton for heterogeneous jobs.
* Downgrade info logs about a job meeting a "maximum node limit" in the select plugin to DebugFlags=SelectType. These info logs could spam the slurmctld log file under certain circumstances.
* prep/script - Fix [Srun|Task]<Prolog|Epilog> missing SLURM_JOB_NODELIST.
* gres - Rebuild GRES core bitmap for nodes at startup. This fixes error: "Core bitmaps size mismatch on node [HOSTNAME]", which causes jobs to enter state "Requested node configuration is not available".
* slurmctd - Allow startup with zero nodes.
* Fix filesystem handling race conditions that could lead to an attacker taking control of an arbitrary file, or removing entire directories' contents. CVE-2023-41914.

## Changes in Slurm 23.02.5

* Add the JobId to debug() messages indicating when cpus_per_task/mem_per_cpu or pn_min_cpus are being automatically adjusted.
* Fix regression in 23.02.2 that caused slurmctld -R to crash on startup if a node features plugin is configured.
* Fix and prevent reoccurring reservations from overlapping.
* job_container/tmpfs - Avoid attempts to share BasePath between nodes.
* Change the log message warning for rate limited users from verbose to info.
* With CR_Cpu_Memory, fix node selection for jobs that request gres and --mem-per-cpu.
* Fix a regression from 22.05.7 in which some jobs were allocated too few nodes, thus overcommitting cpus to some tasks.
* Fix a job being stuck in the completing state if the job ends while the primary controller is down or unresponsive and the backup controller has not yet taken over.
* Fix slurmctld segfault when a node registers with a configured CpuSpecList while slurmctld configuration has the node without CpuSpecList.
* Fix cloud nodes getting stuck in POWERED_DOWN+NO_RESPOND state after not registering by ResumeTimeout.
* slurmstepd - Avoid cleanup of config.json-less containers spooldir getting skipped.
* slurmstepd - Cleanup per task generated environment for containers in spooldir.
* Fix scontrol segfault when 'completing' command requested repeatedly in interactive mode.
* Properly handle a race condition between bind() and listen() calls in the network stack when running with SrunPortRange set.
* Federation - Fix revoked jobs being returned regardless of the -a/--all option for privileged users.
* Federation - Fix canceling pending federated jobs from non-origin clusters which could leave federated jobs orphaned from the origin cluster.
* Fix sinfo segfault when printing multiple clusters with --noheader option.
* Federation - fix clusters not syncing if clusters are added to a federation before they have registered with the dbd.
* Change pmi2 plugin to honor the SrunPortRange option. This matches the new behavior of the pmix plugin in 23.02.0. Note that neither of these plugins makes use of the "MpiParams=ports=" option, and previously were only limited by the systems ephemeral port range.
* node_features/helpers - Fix node selection for jobs requesting changeable features with the '|' operator, which could prevent jobs from running on some valid nodes.
* node_features/helpers - Fix inconsistent handling of '&' and '|', where an AND'd feature was sometimes AND'd to all sets of features instead of just the current set. E.g. "foo|bar&baz" was interpreted as {foo,baz} or {bar,baz} instead of how it is documented: "{foo} or {bar,baz}".
* Fix job accounting so that when a job is requeued its allocated node count is cleared. After the requeue, sacct will correctly show that the job has 0 AllocNodes while it is pending or if it is canceled before restarting.
* sacct - AllocCPUS now correctly shows 0 if a job has not yet received an allocation or if the job was canceled before getting one.
* Fix intel oneapi autodetect: detect the /dev/dri/renderD[0-9]+ gpus, and do not detect /dev/dri/card[0-9]+.
* Format batch, extern, interactive, and pending step ids into strings that are human readable.
* Fix node selection for jobs that request --gpus and a number of tasks fewer than gpus, which resulted in incorrectly rejecting these jobs.
* Remove MYSQL_OPT_RECONNECT completely.
* Fix cloud nodes in POWERING_UP state disappearing (getting set to FUTURE) when an `scontrol reconfigure` happens.
* openapi/dbv0.0.39 - Avoid assert / segfault on missing coordinators list.
* slurmrestd - Correct memory leak while parsing OpenAPI specification templates with server overrides.
* slurmrestd - Reduce memory usage when printing out job CPU frequency.
* Fix overwriting user node reason with system message.
* Remove --uid / --gid options from salloc and srun commands.
* Prevent deadlock when rpc_queue is enabled.
* slurmrestd - Correct OpenAPI specification generation bug where fields with overlapping parent paths would not get generated.
* Fix memory leak as a result of a partition info query.
* Fix memory leak as a result of a job info query.
* slurmrestd - For 'GET /slurm/v0.0.39/node[s]', change format of node's energy field "current_watts" to a dictionary to account for unset value instead of dumping 4294967294.
* slurmrestd - For 'GET /slurm/v0.0.39/qos', change format of QOS's field "priority" to a dictionary to account for unset value instead of dumping 4294967294.
* slurmrestd - For 'GET /slurm/v0.0.39/job[s]', the 'return code' code field in v0.0.39_job_exit_code will be set to -127 instead of being left unset where job does not have a relevant return code.
* data_parser/v0.0.39 - Add required/memory_per_cpu and required/memory_per_node to `sacct --json` and `sacct --yaml` and 'GET /slurmdb/v0.0.39/jobs' from slurmrestd.
* For step allocations, fix --gres=none sometimes not ignoring gres from the job.
* Fix --exclusive jobs incorrectly gang-scheduling where they shouldn't.
* Fix allocations with CR_SOCKET, gres not assigned to a specific socket, and block core distribion potentially allocating more sockets than required.
* gpu/oneapi - Store cores correctly so CPU affinity is tracked.
* Revert a change in 23.02.3 where Slurm would kill a script's process group as soon as the script ended instead of waiting as long as any process in that process group held the stdout/stderr file descriptors open. That change broke some scripts that relied on the previous behavior. Setting time limits for scripts (such as PrologEpilogTimeout) is strongly encouraged to avoid Slurm waiting indefinitely for scripts to finish.
* Allow slurmdbd -R to work if the root assoc id is not 1.
* Fix slurmdbd -R not returning an error under certain conditions.
* slurmdbd - Avoid potential NULL pointer dereference in the mysql plugin.
* Revert a change in 23.02 where SLURM_NTASKS was no longer set in the job's environment when --ntasks-per-node was requested.
* Limit periodic node registrations to 50 instead of the full TreeWidth. Since unresolvable cloud/dynamic nodes must disable fanout by setting TreeWidth to a large number, this would cause all nodes to register at once.
* Fix regression in 23.02.3 which broken x11 forwarding for hosts when MUNGE sends a localhost address in the encode host field. This is caused when the node hostname is mapped to 127.0.0.1 (or similar) in /etc/hosts.
* openapi/[db]v0.0.39 - fix memory leak on parsing error.
* data_parser/v0.0.39 - fix updating qos for associations.
* openapi/dbv0.0.39 - fix updating values for associations with null users.
* Fix minor memory leak with --tres-per-task and licenses.
* Fix cyclic socket cpu distribution for tasks in a step where --cpus-per-task < usable threads per core.

## Changes in Slurm 23.02.4

* Fix sbatch return code when --wait is requested on a job array.
* switch/hpe_slingshot - avoid segfault when running with old libcxi.
* Avoid slurmctld segfault when specifying AccountingStorageExternalHost.
* Fix collected GPUUtilization values for acct_gather_profile plugins.
* Fix slurmrestd handling of job hold/release operations.
* Make spank S_JOB_ARGV item value hold the requested command argv instead of the srun --bcast value when --bcast requested (only in local context).
* Fix step running indefinitely when slurmctld takes more than MessageTimeout to respond. Now, slurmctld will cancel the step when detected, preventing following steps from getting stuck waiting for resources to be released.
* Fix regression to make job_desc.min_cpus accurate again in job_submit when requesting a job with --ntasks-per-node.
* scontrol - Permit changes to StdErr and StdIn for pending jobs.
* scontrol - Reset std{err,in,out} when set to empty string.
* slurmrestd - mark environment as a required field for job submission descriptions.
* slurmrestd - avoid dumping null in OpenAPI schema required fields.
* data_parser/v0.0.39 - avoid rejecting valid memory_per_node formatted as dictionary provided with a job description.
* data_parser/v0.0.39 - avoid rejecting valid memory_per_cpu formatted as dictionary provided with a job description.
* slurmrestd - Return HTTP error code 404 when job query fails.
* slurmrestd - Add return schema to error response to job and license query.
* Fix handling of ArrayTaskThrottle in backfill.
* Fix regression in 23.02.2 when checking gres state on slurmctld startup or reconfigure. Gres changes in the configuration were not updated on slurmctld startup. On startup or reconfigure, these messages were present in the log: "error: Attempt to change gres/gpu Count".
* Fix potential double count of gres when dealing with limits.
* switch/hpe_slingshot - support alternate traffic class names with "TC_" prefix.
* scrontab - Fix cutting off the final character of quoted variables.
* Fix slurmstepd segfault when ContainerPath is not set in oci.conf
* Change the log message warning for rate limited users from debug to verbose.
* Fixed an issue where jobs requesting licenses were incorrectly rejected.
* smail - Fix issues where e-mails at job completion were not being sent.
* scontrol/slurmctld - fix comma parsing when updating a reservation's nodes.
* cgroup/v2 - Avoid capturing log output for ebpf when constraining devices, as this can lead to inadvertent failure if the log buffer is too small.
* Fix --gpu-bind=single binding tasks to wrong gpus, leading to some gpus having more tasks than they should and other gpus being unused.
* Fix main scheduler loop not starting after failover to backup controller.
* Added error message when attempting to use sattach on batch or extern steps.
* Fix regression in 23.02 that causes slurmstepd to crash when srun requests more than TreeWidth nodes in a step and uses the pmi2 or pmix plugin.
* Reject job ArrayTaskThrottle update requests from unprivileged users.
* data_parser/v0.0.39 - populate description fields of property objects in generated OpenAPI specifications where defined.
* slurmstepd - Avoid segfault caused by ContainerPath not being terminated by '/' in oci.conf.
* data_parser/v0.0.39 - Change v0.0.39_job_info response to tag exit_code field as being complex instead of only an unsigned integer.
* job_container/tmpfs - Fix %h and %n substitution in BasePath where %h was substituted as the NodeName instead of the hostname, and %n was substituted as an empty string.
* Fix regression where --cpu-bind=verbose would override TaskPluginParam.
* scancel - Fix --clusters/-M for federations. Only filtered jobs (e.g. -A, -u, -p, etc.) from the specified clusters will be canceled, rather than all jobs in the federation. Specific jobids will still be routed to the origin cluster for cancellation.

## Changes in Slurm 23.02.3

* Fix regression in 23.02.2 that ignored the partition DefCpuPerGPU setting on the first pass of scheduling a job requesting --gpus --ntasks.
* openapi/dbv0.0.39/users - If a default account update failed, resulting in a no-op, the query returned success without any warning. Now a warning is sent back to the client that the default account wasn't modified.
* srun - fix issue creating regular and interactive steps because *_PACK_GROUP* environment variables were incorrectly set on non-HetSteps.
* Fix dynamic nodes getting stuck in allocated states when reconfiguring.
* Avoid job write lock when nodes are dynamically added/removed.
* burst_buffer/lua - allow jobs to get scheduled sooner after slurm_bb_data_in completes.
* mpi/pmix - fix regression introduced in 23.02.2 which caused PMIx shmem backed files permissions to be incorrect.
* api/submit - fix memory leaks when submission of batch regular jobs or batch HetJobs fails (response data is a return code).
* openapi/v0.0.39 - fix memory leak in _job_post_het_submit().
* Fix regression in 23.02.2 that set the SLURM_NTASKS environment variable in sbatch jobs from --ntasks-per-node when --ntasks was not requested.
* Fix regression in 23.02 that caused sbatch jobs to set the wrong number of tasks when requesting --ntasks-per-node without --ntasks, and also requesting one of the following options: --sockets-per-node, --cores-per-socket, --threads-per-core (or --hint=nomultithread), or -B,--extra-node-info.
* Fix double counting suspended job counts on nodes when reconfiguring, which prevented nodes with suspended jobs from being powered down or rebooted once the jobs completed.
* Fix backfill not scheduling jobs submitted with --prefer and --constraint properly.
* Avoid possible slurmctld segfault caused by race condition with already completed slurmdbd_conn connections.
* Slurmdbd.conf checks included conf files for 0600 permissions
* slurmrestd - fix regression "oversubscribe" fields were removed from job descriptions and submissions from v0.0.39 end points.
* accounting_storage/mysql - Query for individual QOS correctly when you have more than 10.
* Add warning message about ignoring --tres-per-tasks=license when used on a step.
* sshare - Fix command to work when using priority/basic.
* Avoid loading cli_filter plugins outside of salloc/sbatch/scron/srun. This fixes a number of missing symbol problems that can manifest for executables linked against libslurm (and not libslurmfull).
* Allow cloud_reg_addrs to update dynamically registered node's addrs on subsequent registrations.
* switch/hpe_slingshot - Fix hetjob components being assigned different vnis.
* Revert a change in 22.05.5 that prevented tasks from sharing a core if --cpus-per-task > threads per core, but caused incorrect accounting and cpu binding. Instead, --ntasks-per-core=1 may be requested to prevent tasks from sharing a core.
* Correctly send assoc_mgr lock to mcs plugin.
* Fix regression in 23.02 leading to error() messages being sent at INFO instead of ERR in syslog.
* switch/hpe_slingshot - Fix bad instant-on data due to incorrect parsing of data from jackaloped.
* Fix TresUsageIn[Tot|Ave] calculation for gres/gpumem and gres/gpuutil.
* Avoid unnecessary gres/gpumem and gres/gpuutil TRES position lookups.
* Fix issue in the gpu plugins where gpu frequencies would only be set if both gpu memory and gpu frequencies were set, while one or the other suffices.
* Fix reservations group ACL's not working with the root group.
* slurmctld - Fix backup slurmctld crash when it takes control multiple times.
* Fix updating a job with a ReqNodeList greater than the job's node count.
* Fix inadvertent permission denied error for --task-prolog and --task-epilog with filesystems mounted with root_squash.
* switch/hpe_slingshot - remove the unused vni_pids option.
* Fix missing detailed cpu and gres information in json/yaml output from scontrol, squeue and sinfo.
* Fix regression in 23.02 that causes a failure to allocate job steps that request --cpus-per-gpu and gpus with types.
* sacct - when printing PLANNED time, use end time instead of start time for jobs cancelled before they started.
* Fix potentially waiting indefinitely for a defunct process to finish, which affects various scripts including Prolog and Epilog. This could have various symptoms, such as jobs getting stuck in a completing state.
* Hold the job with "(Reservation ... invalid)" state reason if the reservation is not usable by the job.
* Fix losing list of reservations on job when updating job with list of reservations and restarting the controller.
* Fix nodes resuming after down and drain state update requests from clients older than 23.02.
* Fix advanced reservation creation/update when an association that should have access to it is composed with partition(s).
* auth/jwt - Fix memory leak.
* sbatch - Added new --export=NIL option.
* Fix job layout calculations with --ntasks-per-gpu, especially when --nodes has not been explicitly provided.
* Fix X11 forwarding for jobs submitted from the slurmctld host.
* When a job requests --no-kill and one or more nodes fail during the job, fix subsequent job steps unable to use some of the remaining resources allocated to the job.
* Fix shared gres allocation when using --tres-per-task with tasks that span multiple sockets.

## Changes in Slurm 23.02.2

* Fix regression introduced with the migration to interfaces which caused sshare to core dump. Sshare now initialized the priority context correctly when calculating with PriorityFlags=NO_FAIR_TREE.
* Fix IPMI DCMI sensor initialization.
* For the select/cons_tres plugin, improve the best effort GPU to core binding, for requests with per job task count (-n) and GPU (--gpus) specification.
* scrontab - don't update the cron job tasks if the whole crontab file is left untouched after opening it with "scrontab -e".
* mpi/pmix - avoid crashing when running PMIx v5.0 branch with shmem support.
* Fix building switch topology after a reconfig with the correct nodes.
* Allow a dynamic node to register with a reason, using --conf, when the state is DOWN or DRAIN.
* Fix slurmd running tasks before RPC Prolog is run.
* Fix slurmd deadlock iff the controller were to give a bad alias_list.
* slurmrestd - correctly process job submission field "exclusive" with boolean True or False.
* slurmrestd - correctly process job submission field "exclusive" with strings "true" or "false".
* slurmctld/step_mgr - prevent non-allocatable steps from decrementing values that weren't previously incremented when trying to allocate them.
* auth/jwt - Fix memory leak in slurmctld with 'scontrol token'.
* Fix shared gres (shard/mps) leak when using --tres-per-task
* Fix sacctmgr segfault when listing accounts with coordinators.
* slurmrestd - improve error logging when client connections experience polling errors.
* slurmrestd - improve handling of sockets in different states of shutdown to avoid infinite poll() loop causing a thread to max CPU usage until process is killed.
* slurmrestd - avoid possible segfault caused by race condition of already completed connections.
* mpi/cray_shasta - Fix PMI shared secret for hetjobs.
* gpu/oneapi - Fix CPU affinity handling.
* Fix dynamic nodes powering up when already up after adding/deleting nodes when using power_save logic.
* slurmrestd - Add support for setting max connections.
* data_parser/v0.0.39 - fix sacct --json matching associations from a different cluster.
* Fix segfault when clearing reqnodelist of a pending job.
* Fix memory leak of argv when submitting jobs via slurmrestd or CLI commands.
* slurmrestd - correct miscalculation of job argument count that could cause memory leak when job submission fails.
* slurmdbd - add warning on startup if max_allowed_packet is too small.
* gpu/nvml - Remove E-cores from NVML's cpu affinity bitmap when "allow_ecores" is not set in SlurmdParameters.
* Fix regression from 23.02.0rc1 causing a FrontEnd slurmd to assert fail on startup and don't be configured with the appropriate port.
* Fix dynamic nodes not being sorted and not being included in topology, which resulted in suboptimal dynamic node selection for jobs.
* Fix slurmstepd crash due to potential division by zero (SIGFPE) in certain edge-cases using the PMIx plugin.
* Fix issue with PMIx HetJob requests where certain use-cases would end up with communication errors due to incorrect PMIx hostname info setup.
* openapi/v0.0.39 - revert regression in job update requests to accept job description for changes instead of requiring job description in "job" field.
* Fix regression in 23.02.0rc1 that caused a step to crash with a bad --gpu-bind=single request.
* job_container/tmpfs - skip more in-depth attempt to clean up the base path when not required. This prevents unhelpful, and possibly misleading, debug2 messages when not using the new "shared" mode.
* gpu/nvml - Fix gpu usage when graphics processes are running on the gpu.
* slurmrestd - fix regression where "exclusive" field was removed from job descriptions and submissions.
* Fix issue where requeued jobs had bad gres allocations leading to gres not being deallocated at the end of the job, preventing other jobs from using those resources.
* Fix regression in 23.02.0rc1 which caused incorrect values for SLURM_TASKS_PER_NODE when the job requests --ntasks-per-node and --exclusive or --ntasks-per-core=1 (or CR_ONE_TASK_PER_CORE) and without requesting --ntasks. SLURM_TASKS_PER_NODE is used by mpirun, so this regression caused mpirun to launch the wrong number of tasks and to sometimes fail to launch tasks.
* Prevent jobs running on shards from being canceled on slurmctld restart.
* Fix SPANK prolog and epilog hooks that rely on slurm_init() for access to internal Slurm API calls.
* oci.conf - Populate %m pattern with ContainerPath or SlurmdSpoolDir if ContainerPath is not configured.
* Removed zero padding for numeric values in container spool directory names.
* Avoid creating an unused task-4294967295 directory in container spooldir.
* Cleanup container step directories at step completion.
* sacctmgr - Fix segfault when printing empty tres.
* srun - fix communication issue that prevented slurmctld from connecting to an srun running outside of a compute node.

## Changes in Slurm 23.02.1

* job_container/tmpfs - cleanup job container even if namespace mount is already unmounted.
* When cluster specific tables are be removed also remove the job_env_table and job_script_table.
* Fix the way bf_max_job_test is applied to job arrays in backfill.
* data_parser/v0.0.39 - Avoid dumping -1 value or NULL when step's consumed_energy is unset.
* scontrol - Fix showing Array Job Steps.
* scontrol - Fix showing Job HetStep.
* openapi/dbv0.0.38 - Fix not displaying an error when updating QOS or associations fails.
* data_parser/v0.0.39 - Avoid crash while parsing composite structures.
* sched/backfill - fix deleted planned node staying in planned node bitmap.
* Fix nodes remaining as PLANNED after slurmctld save state recovery.
* Fix parsing of cgroup.controllers file with a blank line at the end.
* Add cgroup.conf EnableControllers option for cgroup/v2.
* Get correct cgroup root to allow slurmd to run in containers like Docker.
* Fix "(null)" cluster name in SLURM_WORKING_CLUSTER env.
* slurmctld - add missing PrivateData=jobs check to step ContainerID lookup requests originated from 'scontrol show step container-id=<id>' or certain scrun operations when container state can't be directly queried.
* Automatically sort the TaskPlugin list reverse-alphabetically. This addresses an issue where cpu masks were reset if task/affinity was listed before task/cgroup on cgroup/v2 systems with Linux kernel < 6.2.
* Fix some failed terminate job requests from a 23.02 slurmctld to a 22.05 or 21.08 slurmd.
* Fix compile issues on 32-bit systems.
* Fix nodes un-draining after being drained due to unkillable step.
* Fix remote licenses allowed percentages reset to 0 during upgrade.
* sacct - Avoid truncating time strings when using SLURM_TIME_FORMAT with the --parsable option.
* data_parser/v0.0.39 - fix segfault when default qos is not set.
* Fix regression in 22.05.0rc1 that broke Nodes=ALL in a NodeSet.
* openapi/v0.0.39 - fix jobs submitted via slurmrestd being allocated fewer CPUs than tasks when requesting multiple tasks.
* Fix job not being scheduled on valid nodes and potentially being rejected when using parentheses at the beginning of square brackets in a feature request, for example: "feat1&[(feat2|feat3)]".
* Fix a job being scheduled on nodes that do not match a feature request that uses parentheses inside of brackets and requests additional features outside of brackets, for example: "feat1&[feat2|(feat3|feat4)]".
* Fix regression in 23.02.0rc1 which made --gres-flags=enforce-binding no longer enforce optimal core-gpu job placement.
* switch/hpe_slingshot - add option to disable VNI allocation per-job.
* switch/hpe_slingshot - restrict CXI services to the requesting user.
* switch/hpe_slingshot - Only output tcs once in SLINGSHOT_TCS env.
* switch/hpe_slingshot - Fix updating LEs and ACs limits.
* switch/hpe_slingshot - Use correct Max for EQs and CTs.
* switch/hpe_slingshot - support configuring network options per-job.
* switch/hpe_slingshot - retry destroying CXI service if necessary.
* Fix memory leak caused by job preemption when licenses are configured.
* mpi/pmix - Fix v5 to load correctly when libpmix.so isn't in the normal lib path.
* data_parser/v0.0.39 - fix regression where "memory_per_node" would be rejected for job submission.
* data_parser/v0.0.39 - fix regression where "memory_per_cpu" would be rejected for job submission.
* slurmctld - add an assert to check for magic number presence before deleting a partition record and clear the magic afterwards to better diagnose potential memory problems.
* Clean up OCI containers task directories correctly.
* slurm.spec - add "--with jwt" option.
* scrun - Run under existing job when SLURM_JOB_ID is present.
* Prevent a slurmstepd crash when the I/O subsystem has hung.
* common/conmgr - fix memory leak of complete connection list.
* data_parser/v0.0.39 - fix memory leak when parsing every field in a struct.
* job_container/tmpfs - avoid printing extraneous error messages when running a spank plugin that implements slurm_spank_job_prolog() or slurm_spank_job_epilog().
* Fix srun < 23.02 always getting an "exact" core allocation.
* Prevent scontrol < 23.02 from setting MaxCPUsPerSocket to 0.
* Add ScronParameters=explicit_scancel and corresponding scancel --cron option.

## Changes in Slurm 23.02.0

* scrun - Install into /bin instead of /sbin, which also ensures it is included in the RPM packages.
* data_parser/v0.0.39 - avoid erroring on unknown association id.
* data_parser/v0.0.39 - avoid fatal while dumping TRES values.
* Improve error message for using --cpus-per-gpus without any GPUs.
* switch/hpe_slingshot - fix rolling upgrades from 22.05 to 23.02
* Change cgroup/v1 behavior when waiting for a pid to be moved by waiting some time between retries. This helps with stray cgroup dirs on slow kernels.
* Workaround a bug in kernels < 3.18 with cpuset which randomly leaves stray cgroups after jobs have ended.
* Rebuild the prepacked buf going to the slurmstepd if TRES changed.
* Add new 'make clean-contrib' target to build system to clean contribs dir.
* Treat newlines as delimiters for hostlist_create(). (This is used internally by "scontrol show hostlist" and for handling SLURM_JOB_NODELIST.)
* scontrol - Print "Invalid job id specified" for 'scontrol show step ...' when the job does not exist, rather than "Unexpected error".
* Handle mismatched TRES count from the slurmstepd's instead of fatal()'ing.
* Fix GPU setup on CRAY systems when using the CRAY_CUDA_MPS environment variable. GPUs are now correctly detected in such scenarios.
* data_parser/v0.0.39 - improve flag handling to avoid flags having bits left unset after parsing.
* topology/tree - Add new TopologyParam=SwitchAsNodeRank option to reorder nodes based on switch layout. This can be useful if the naming convention for the nodes does not natually map to the network topology.
* openapi/v0.0.38 - avoid signed math errors while dumping node resources.
* openapi/dbv0.0.39 - avoid error while dumping account coordinators.
* data_parser/v0.0.39 - correct dump of stats backfill mean table size.
* openapi/dbv0.0.39 - avoid error while adding account coordinators.
* scrun - catch invalid values of environment SCRUN_FILE_DEBUG.
* scrun - avoid memory leak due to invalid annotation.
* scrun - avoid false error when pidfile not requested.
* Do not constrain memory in task/cgroup unless CR_Memory is set.
* Fix the job prolog not running for jobs with the interactive step (salloc jobs with LaunchParameters=use_interactive_step set in slurm.conf) that were scheduled on powered down nodes. The prolog not running also broke job_container/tmpfs, pam_slurm_adopt, and x11 forwarding.
* switch/hpe_slingshot - fix issues using 22.05 commands with 23.02 slurmd.
* switch/hpe_slingshot - avoid slurmd segfault if there is a protocol error.
* task/affinity - fix slurmd segfault when request launch task requests of type "--cpu-bind=[map,mask]_cpu:<list>" have no <list> provided.
* salloc/sbatch/srun - error out if "--cpu-bind=[map,mask]_cpu:<list>" fails to extract a list of cpus.
* job_container/tmpfs - cleanup job_mount when container creation fails.
* job_container/tmpfs - don't attempt to remove job_mount directory when it is still mounted.
* Fix regression in rc1 causing sacctmgr to segfault when printing unrelated fields.
* jobcomp/kafka - don't use the purge API if librdkafka < v1.0.0.
* Change shown start time of pending array job to be start time of earliest pending array task.
* job_container/tmpfs - ensure that step_ns_fd is closed before cleaning up the namespace.
* Fix assert when suspending or requeueing array tasks with scontrol.
* Fix federated job submissions.
* Fix sinfo returning non-zero exit code when querying specific clusters.
* openapi/v0.0.39 - correct OpenAPI schema for job update requests.
* openapi/v0.0.39 - prevent assertion on jobs endpoint by altering how the "memory_per_cpu" and "memory_per_node" fields are managed.
* Fix regression in 23.02.0rc1 which caused slurmctld to segfault when gres is added or removed in the configuration.
* Removed the default setting for GpuFreqDef. If unset, no attempt to change the GPU frequency will be made if --gpu-freq is not set for the step.
* Fixed GpuFreqDef option. When set in slurm.conf, it will be used if --gpu-freq was not explicitly set by the job step.
* Update database index for usage tables used for archive and purge.
* Fix configure script on FreeBSD.

## Changes in Slurm 23.02.0rc1

* Make scontrol reconfigure and sending a SIGHUP to the slurmctld behave the same. If you were using SIGHUP as a 'lighter' scontrol reconfigure to rotate logs please update your scripts to use SIGUSR2 instead.
* Add Account and QOS name type specifications for sprio output formatting.
* openapi/[db]v0.0.36 - plugins have been removed.
* openapi/[db]v0.0.37 - tagged as deprecated.
* openapi/[db]v0.0.39 - forked plugin openapi/[db]v0.0.38.
* Add SRUN_{ERROR,INPUT,OUTPUT} input environment variables for --error, --input and --output options respectively.
* Add MaxCPUsPerSocket to partition configuration, similar to MaxCPUsPerNode.
* openapi/v0.0.39 - change nice request field from string to integer.
* sacctmgr - no longer force updates to the AdminComment, Comment, or SystemComment to lower-case.
* openapi/dbv0.0.39 - more graceful handling of POSTs when expected field lists are missing or unparsable.
* burst_buffer/lua - pass the job's UID and GID to slurm_bb_pre_run, slurm_bb_data_in, slurm_bb_post_run, and slurm_bb_data_out in burst_buffer.lua.
* openapi/v0.0.39 - add new field default_memory_per_node for partitions.
* Change cloud nodes to show by default. PrivateData=cloud is no longer needed.
* Add -F/--future option to sinfo to display future nodes.
* openapi/dbv0.0.39 - resolve job user from uid when user is not provided.
* sacct/sinfo/squeue - use openapi/[db]v0.0.39 for --json and --yaml modes.
* sdiag - Add --json and --yaml arguments.
* Cgroupv2 plugin will not show "Controller is not enabled" error when started from cmd.
* sreport - Count planned (FKA reserved) time for jobs running in IGNORE_JOBS reservations. Previously was lumped into IDLE time.
* job_container/tmpfs - support running with an arbitrary list of private mount points (/tmp and /dev/shm are nolonger required, but are the default).
* sacct - Rename 'Reserved' field to 'Planned' to match sreport and the nomenclature of the 'Planned' node.
* oci.conf: Add %U as OCI pattern replacement to numeric user id.
* cli_filter/lua, jobcomp/lua, job_submit/lua - expose all Slurm error codes.
* jobcomp/elasticsearch - expose features field.
* jobcomp/elasticsearch - post non-NULL but empty string fields.
* Make advanced reservation flag MAINT not replace nodes, similar to STATIC_ALLOC
* NVML - Add usage gathering for Nvidia gpus.
* node_features plugins - invalid users specified for AllowUserBoot will now result in fatal() rather than just an error.
* job_container/tmpfs - Set more environment variables in InitScript.
* Make all cgroup directories created by Slurm owned by root.
* sbatch - add parsing of #PBS -d and #PBS -w.
* Avoid network receive error on heavily loaded machines where the network operation can be restarted.
* Deprecate AllowedKmemSpace, ConstrainKmemSpace, MaxKmemPercent, and MinKmemSpace.
* accounting_storage/mysql - change purge/archive to calculate record ages based on end time, rather than start or submission times.
* Add strigger --draining and -R/--resume options.
* Allow updating SLURM_NTASKS environment variable (from scontrol update job).
* Limit het job license requests to the leader job only.
* Change --oversubscribe and --exclusive to be mutually exclusive for job submission. Job submission commands will now fatal if both are set. Previously, these options would override each other, with the last one in the job submission command taking effect.
* job_submit/lua - add support for log_user() from slurm_job_modify().
* openapi/v0.0.39 - add submission of job->prefer value.
* Fix tasks binding to GPUs when using --ntasks-per-gpu and GPUs have different core/socket affinity.
* Add support for padding StdOut/StdErr/StdIn format specifiers to scontrol show job.
-- Fix sacctmgr qos filter to properly select assocs by qos inheritance rules.
* Prevent slurmctld from starting with invalid jobcomp plugin.
* Fix `sinfo -i` and interactive `scontrol> show node` replies for nodes going from powering down to powered down state.
* auth/jwt - support Azure (among other) JWKS files by preferring x5c field over locally reconstructing an RSA256 key file.
* auth/jwt - add optional AuthAltParameters field "userclaimfield=" to allow overriding "sun" claim to site specific field.
* scontrol - Requested TRES and allocated TRES will now always be printed when showing jobs, instead of one TRES output that was either the requested or allocated.
* Improve scheduling performance for dynamic nodes and nodes with features by consolidating like config records.
* Expose argc and entire argv to job_submit and cli_filter plugins for jobs submitted via salloc/srun or sbatch <script> (not via --wrap).
* Job command/script name is now packed with no arguments, thus clients displaying job information will only show the command/script name.
* Make acct_gather_energy plugins handle slurmd reconfiguration and support restart for gpu and xcc implementations.
* Fix --slurmd-debug to treat its argument as log level (as documented) instead of previous approach with it being an offset to configured log level.
* srun --slurmd-debug option is now only allowed for root and SlurmUser
* Change 'scontrol requeue' behavior for scron jobs to use cronspec to determine the next start time.
* serializer/url-encoded - interpret query keys without a value (flags) to be true instead of NULL.
* Add --autocomplete= option to all client commands.
* Allow for concurrent processing of job_submit_g_submit() and job_submit_g_modify() calls.
* Allow jobs to queue even if the user is not in AllowGroups when EnforcePartLimits=no is set. This ensures consistency for all the Partition access controls, and matches the documented behavior for EnforcePartLimits.
* Fix srun tasks binding/allocation when using --ntasks-per-core option. Now, --ntasks-per-core=1 implies --cpu-bind=cores and --ntasks-per-core>1 implies --cpu-bind=threads.
* salloc/sbatch/srun check and abort if ntasks-per-core > threads-per-core.
* burst_buffer/lua - fix building heterogeneous job scripts where the directive is not the default "BB_LUA".
* If a node is in a reservation add the reservation name to it for scontrol/sinfo/sview.
* common/cgroup - improve possible TOCTOU situation when reading cgroup files.
* slurmctld will fatal() when reconfiguring the job_submit plugin fails.
* Add ResumeAfter=<secs> option to "scontrol update nodename=".
* slurmrestd - switch to returning UNPROCESSABLE_CONTENT when parsing succeeds but an error about the actual contents causes a request to fail.
* slurmrestd - queries that result in empty contents will now be treated as SUCCESS. While the query worked, nothing was returned, which allows callers to determine how to then proceed.
* Add PowerDownOnIdle partition option.
* Make it so the [Allow|Deny]Accounts parameter for a partition is hierarchical.
* Add the ability for a add (+=) or remove (-=) nodes from a reservation
* Fix several problems when creating/editing partitions in sview.
* Enhanced burst_buffer.lua - pass UID and GID to most hooks; pass a table containing detailed job information to many hooks. See etc/burst_buffer.lua.example for a complete list of changes.
* Add a new "nodes=" argument to scontrol setdebug to allow the debug level on the slurmd processes to be temporarily altered.
* Add InfluxDBTimeout parameter to acct_gather.conf.
* job_container/tmpfs - add support for expanding %h and %n in BasePath.
* Prevent job submission/update with afterok/afternotok dependency set to an unknown jobid.
* Change to a new FAIL_SIGNAL job state reason (instead of FAIL_LAUNCH) for jobs that have raised a signal on setup.
* Add "[jobid.stepid]" prefix from slurmstepd and "slurmscriptd" prefix from slurmcriptd to Syslog logging. Previously was only happening when logging to a file.
* Make it so scrontab prints client-side the job_submit() err_msg (which can be set i.e. by using the log_user() function for the lua plugin).
* Remove ability for reservation to have STATIC_ALLOC or MAINT flags and REPLACE[_DOWN] flags simultaneously.
* Accept only one reoccurring flag when creating/updating any reservation.
* Remove ability to update a reservation to have a reoccurring flag if it is a floating reservation.
* squeue - removed unused '%s' and 'SelectJobInfo' formats.
* Add purge and archive functionality for job environment and job batch script records.
* srun - return an error if step suffers a node failure.
* squeue - align print format for exit and derived codes with that of other components (<exit_status>:<signal_number>).
* Add new SlurmctldParameters=validate_nodeaddr_threads=<number> option to allow concurrent hostname resolution at slurmctld startup.
* Have 'scontrol show hostlist -' read from stdin.
* Extend support for Include files to all "configless" client commands.
* Make node weight usable for powered down and rebooting nodes.
* node_features plugins - node_features_p_reboot_weight() function removed.
* Removed 'launch' plugin.
* Add "Extra" field to job to store extra information other than a comment.
* Add new AccountingStoreFlags=job_extra option to store a job's extra field in the database.
* slurmrestd - add option to allow rest_auth/local to switch user id on first user connection.
* oci.conf - add DisableHooks option.
* oci.conf - add SrunPath and SrunArgs options.
* oci.conf - add StdIODebug, SyslogDebug, FileDebug and DebugFlags options to allow controlling container specific logging.
* oci.conf - remove requirement of RunTimeQuery when RunTimeRun is set.
* slurmstepd - cleanup generated environment file for containers when step completes instead of deferring cleanup to OCI runtime.
* oci.conf - add configuration option "IgnoreFileConfigJson=" to allow a site to choose to ignore config.json.
* oci.conf - Add PID (%p) pattern replacement.
* slurmrestd - correct ordering issue of saving changes to a container's modified config.json where config changes by job modifying plugins would be silently lost.
* slurmstepd - merge container config.json environment into job environment and then overwrite config.json environment. Avoids instance where processes in container will not have access to job environment variables.
* slurmstepd - defer creating container environment file until after all plugin based modifications to the job environment are complete.
* oci.conf - modify configuration option "CreateEnvFile=" to support creating
* oci.conf - add RunTimeEnvExclude and EnvExclude.
* slurmstepd - fix container pattern bundle path replacement (%b) with bundle path instead of spool dir path.
* oci.conf - add MountSpoolDir.
* slurmstepd - create per task spool directory per container to avoid conflicts when there are more than 1 container tasks on any given node.
* mpi/pmix - set PMIX_SERVER_TMPDIR to container spool directory instead of slurmd spool directory which caused PMIx to fail to load in containers.
* Add new "defer_batch" option to SchedulerParameters to only defer scheduling for batch jobs.
* Reject jobs submitted with impossible --time, --deadline, and --begin combinations at submission time
* Add new DebugFlags option 'JobComp' to replace 'Elasticsearch'.
* sacct - Add --array option to expand job arrays and display array tasks on separate lines.
* Fix jobs submitted with a deadline never being cleared after the reservation is done.
* Change srun/salloc to wait indefinitely for their allocations to be ready.
* Do not attempt to "fix" malformed input to 'scontrol show hostlist'.
* RSMI - Add usage gathering for AMD gpus (requires ROCM 5.5+).
* Add job's allocated nodes, features, oversubscribe, partition, and reservation to SLURM_RESUME_FILE output for power saving.
* Automatically create directories for stdout/stderr output files. Paths may use %j and related substitution characters as well.
* Add --tres-per-task to salloc/sbatch/srun.
* Add configurable job requeue limit parameter - MaxBatchRequeue - in slurm.conf to permit changes from the old hard-coded value of 5.
* Add a new "nodes=" argument to "scontrol setdebugflags" as well.
* slurmd - add --authinfo option to allow AuthInfo options to be changed during the configless startup.
* helpers.conf - Allow specification of node specific features.
* helpers.conf - Allow many features to one helper script.
* Allow nodefeatures plugin features to work with cloud nodes.
* squeue - removed --array-unique option.
* openapi/v0.0.39 - expand job stderr, stdin, and stdout path replacement symbols when dumping job information via dump_job_info().
* Make slurmstepd cgroups constrained by total configured memory from slurm.conf (NodeName=<> RealMemory=#) instead of total physical memory.
* node_features/helpers - add support for the OR and parentheses operators in a --constraint expression.
* Fix race condition between PMIx authentication and slurmstepd task launches that can lead to job launch failures.
* fatal() when [Prolog|Epilog]Slurmctld are defined but not executable.
* slurmctld - Add new RPC rate limiting feature. This is enabled through SlurmctldParameters=rl_enable, otherwise disabled by default.
* Validate node registered active features are a super set of node's currently active changeable features.
* On clusters without any PrologFlags options, batch jobs with failed prologs nolonger generate an output file.
* sreport - print "Top ALL Users" for Top User report when topcount = -1.
* Add SLURM_JOB_START_TIME and SLURM_JOB_END_TIME environment variables.
* Add SuspendExcStates option to slurm.conf to avoid suspending/powering down specific node states.
* job_container/tmpfs - Add "Shared" option to support shared namespaces. This allows autofs to work with the job_container/tmpfs plugin when enabled.
* Fix incorrect min node estimation for pending jobs as displayed by client commands (e.g. squeue, scontrol show job).
* Add support for DCMI power readings in IPMI plugin.
* Prevent the backfill counter of tested jobs being reset by sdiag -r.
* sacctmgr - Add --json and --yaml arguments.
* openapi/v0.0.39 - Conversion of parsing and dumping of data to data_parser/v0.0.39 plugins. Significant restructuring of OpenAPI specification compared to previous plugin version.
* openapi/dbv0.0.39 - Conversion of parsing and dumping of data to data_parser/v0.0.39 plugins. Significant restructuring of OpenAPI specification compared to previous plugin version.
* Validate --gpu-bind options more strictly.
* Fix slurmd segfault on bad EnergyIPMIPowerSensors= parameter.
* Allow for --nodelist to contain more nodes than required by --nodes.
* Add missing logic for JobCompParams configuration key pair to be available for consumers of the get/print/write configuration API functions.
* mpi/cray_shasta - Add new PALS/PMI environment variables.
* Add cache around calls to getnameinfo().
* Add CommunicationParameters=getnameinfo_cache_timeout to tune or disable the getnameinfo cache.
* Limit the number of successive times a batch job may be requeued due to a prolog failure. This limit is defined by the value of the max_batch_requeue parameter (default 5). Jobs over this limit will be put on admin hold.
* Rename "nodes" to "nodes_resume" in SLURM_RESUME_FILE job output.
* Rename "all_nodes" to "all_nodes_resume" in SLURM_RESUME_FILE output.
* Add new PrologFlags=ForceRequeueOnFail option to automatically requeue batch jobs on Prolog failures regardless of the job --requeue setting.
* Add HealthCheckNodeState=NONDRAINED_IDLE option.
* Add 'explicit' to Flags in gres.conf
* Improve validation of --constraint expression syntax.
* srun - support job array ids as an argument to --jobid.
* Add jobcomp/kafka plugin.
* Automatically filter out E-Cores on Intel processors to avoid issues from differing threads-per-core values within a single socket.
* Add LaunchParameters=ulimit_pam_adopt, which enables setting RLIMIT_RSS in adopted processes.
* Add new PreemptParameters=reclaim_licenses option which will allow higher priority jobs to preempt jobs to free up used licenses. (This is only enabled for with PreemptModes of CANCEL and REQUEUE, as Slurm cannot guarantee suspended jobs will release licenses correctly.)
* Make it so that the REQUEST_STEP_STAT handler only polls jobacct_gather information once per RPC. This improves sstat results performance time.
* Fix sbcast --force option to work with --send-libs option. Now when --force is set, library directories transmitted will overwrite pre-existing libraries that were previously transmitted.
* hpe/slingshot - Add support for user requestible job VNIs.
* hpe/slingshot - Add support for user requestible single node vnis.
* hpe/slingshot - Add support for the instant-on feature.
* Add ability to update SuspendExc* parameters with scontrol.
* Add ability to preserve SuspendExc* parameters on reconfig with ReconfigFlags=KeepPowerSaveSettings.
* Add ability to restore SuspendExc* parameters on restart with slurmctld -R option.
* Fix leaving allocated gres on a non-cloud node after being allocated from a powered down state after a slurmctld restart.
* 'scontrol update job' can now set a GRES specification to "0" to effectively clear a GRES specification from a job.
* Jobs with old GRES that are no longer configured in any configuration files can now be successfully updated to use currently configured GRES.
* Add tres_per_node to resource_allocation_response_msg to fix srun jobs running with outdated GRES. This is necessary for srun jobs whose GRES are updated while they are pending. By having tres_per_node in the resourece_allocation_response_msg, slurmctld can send back the correct and updated GRES, whereas without it, the srun job would use the old GRES.
* Cleaned up various "warning", "Warning", and "WARNING" messages with a new warning() logging call. All will now print as "warning: ...".
* Add SLURM_JOB_OVERSUBSCRIBE environment variable for Epilog, Prolog, EpilogSlurmctld, PrologSlurmctld, and mail output.
* Fix node reason time skew when setting a reboot message.
* Append ResumeTimeout message to an existing node reason instead of always clearing it first.
* slurmrestd - added support for Bearer Authentication using auth/jwt.
* Add check to detect invalid HetJob component and abort job if identified.
* sched/backfill - fix job test against advanced reservations if job uses a preemptable QOS with Flags=NoReserve.
* New command scrun has been added. scrun acts as an Open Container Initiative (OCI) runtime proxy to run containers seamlessly via Slurm.
* Added support for Linux kernel user namespaces. Process user and group ids are no longer used unless explicitly requested as an argument. They are left as SLURM_AUTH_NOBODY (99) by default.
