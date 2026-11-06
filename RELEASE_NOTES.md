# Release Notes for Slurm 26.11

## Upgrading

Slurm 26.11 supports upgrading directly from 26.05, 25.11, and 25.05.

Changes to the database schema may make upgrading SlurmDBD more time consuming than usual. It is strongly recommended to test upgrading the database before upgrading the production SlurmDBD instance.

See the [Upgrade Guide](https://slurm.schedmd.com/upgrades.html) for further details.

## Highlights

* Add plugin support to the Hierarchical Resources (HRES) subsystem, and the hres/lua plugin script interface.
* Add arbitrary key-value metadata (a JSON document) to jobs, nodes, partitions, and reservations. Set it with "scontrol update KV=<json>", "KVPatch=<json>", or "--kv=<json>". Store it in accounting with AccountingStoreFlags=job_kv and resv_kv.
* Add the switch/lua plugin to manage interconnects with a Lua script.
* Add step dependencies ("srun --step-dependency=")
* Add stepmgr support for heterogeneous jobs. Note that slurmctld, slurmd, and srun all need to be upgraded to 26.11 to support this mode of operation.
  * Sites are encouraged to enable stepmgr globally through SlurmctldParameters=enable_stepmgr and report any issues before this becomes the default in Slurm 27.05.
* Add swait support for waiting on specific steps, as well as a "--follow" option that reports the completion of individual steps while continuing to block until all steps have completed.
* Add gpu/amd plugin (AutoDetect=amd) as an alternative to gpu/rsmi for AMD GPUs. This plugin does not require AMD's RSMI library, and will build on all systems.
* Add gres/tpu and job_submit/tpu plugins to support Google TPUs.
* Add the experimental task/resctrl plugin to confine job steps to resctrl groups (Arm MPAM, Intel RDT, AMD PQoS). Define the groups in resctrl.yaml.
* Add support for draining individual GRES on a compute node as a way to mitigate a single failed accelerator card.
* Add the dmem cgroup controller to limit shard GRES device memory (ConstrainDeviceMemory=yes).

## New Features

* Add the relay_io plugin type to copy job stdout and stderr, as JSON records, to a UNIX datagram socket on each node.
* Answer HTTP requests in slurmdbd on DbdPort (/livez, /readyz, /healthz, /conf, /diag).
* Add "--gres-flags=enforce-links" to require directly linked GRES devices (for example NVLink). Add "--gres-flags=ignore-links" to select devices in index order.
* Add ChunkSizes and ChunkOrder to gres.conf to allocate devices in aligned chunks.
* Add "--layout=<key>=<value>[,...]" to salloc, sbatch, and srun as a job tag for site plugins, prologs, and epilogs.
* Add "--max-pids" and a "pids" TRES to limit and report processes per step (cgroup/v2).
* Add CLONE_NEWIPC support to namespace/linux.
* Add per-device drain of GRES devices, CPUs, and memory with "scontrol update TresName=... Action=Drain". Add the DEGRADED node state for nodes with drained TRES.
* Add the Slurm return code (SlurmRC) to jobs. RequeueExit and RequeueExitHold match it when it is not zero. Add SLURM_JOB_RC to the Epilog environment.
* Add job queue, requeue, hold, and release events to jobcomp/kafka and jobcomp/lua.
* Add "sacctmgr load ... declarative" mode.
* Add TresDecayHalfLife for associations and QOS to override PriorityDecayHalfLife for each TRES.
* Add DefSegmentSize as a default for jobs that do not use "--segment".
* Use the full node selection logic for single-node jobs by default. Set "enable_one_node_select: true" in topology.yaml to restore the first-fit path.
* Assign StepIds to steps at submission instead of launch.
* Change swait exit codes: 1 is an error, 2 is "--timeout", and 3 is a missing step.
* Record the name, not the UID, of the user that cancelled a job or set a node reason.
* Send all supported slurmctld RPC replies asynchronously.
* Enable the slurmctld threadpool by default.
* Require authentication as root or SlurmUser for slurmd "/readyz?verbose" probes. Return HTTP 200 in place of 204 from /livez and /readyz.
* Keep a job in CONFIGURING while a PrologFlags=Alloc Prolog runs, unless NoHold is set.
* Report node memory metrics in bytes in the openmetrics endpoint. They were in MiB.
* Refuse to change or remove a partition's QOS while jobs are running or suspended in it.
* Add new serializer/xjson plugin as an alternative to serializer/json that does not require json-c. Plugin must be configured explicitly to be used over serializer/json (when compiled).

## Configuration Changes - slurm.conf

* Add DefRuntimePlugin, RelayIOPlugin, RelayIOParameters, and RelayIODefaultEnabled.
* Add SlurmstepdParameters, SlurmrestdParameters, SerializerParameters, SerializerPlugins, and SlurmrestdHttpAuthParameters.
* Add DefSegmentSize.
* Add MaxDrainedTres as a NodeName parameter.
* Add AccountingStoreFlags=job_kv and resv_kv.
* Add SlurmctldParameters=switch_timeout, switch_retries, switch_drain_on_failure, dynamic_node_dns, drain_absent_gres, fast_power_up_launch, and enable_deferred_user_lookup (experimental).
* Add SchedulerParameters=bf_node_unavail_exit.
* Add MailProg=None as a way to explicitly disable MailProg.
* Add LogTimeFormat=omit and rfc5424_us.
* Rename CR_NO_DIST_TOPO_BLOCK to CR_NO_DIST_TOPO.
* Require SlurmdUser to be root or SlurmUser.
* Remove SlurmctldParameters=enable_async_reply.
* Remove CommunicationParameters=block_null_hash and block_zero_hash.

## Configuration Changes - gres.conf

* Add AutoDetect=amd, AutoDetect=tpu, ChunkSizes, ChunkOrder, UUID, and DmemRegion.

## Configuration Changes - cgroup.conf

* Add ConstrainDeviceMemory.

## Configuration Changes - topology.yaml

* Add enable_one_node_select.

## Configuration Changes - slurmdbd.conf

* Add HttpAuthParameters and Parameters=PreserveCaseAll.
* Add support for CommunicationParameters=disable_http.
* Add SerializerParameters, SerializerPlugins, and the conmgr and workerpool options in Parameters.

## Configuration Changes - Other Files

* Add resctrl.yaml, tpu.yaml, and switch.lua.

## Packaging Changes

* Raise the minimum MySQL and MariaDB versions to 8.0.21 and 10.2.7.
* Prefer llhttp over libhttp_parser by default.
* Add Lua 5.5 support.

## REST API Changes

[Slurm OpenAPI Plugin Release Notes](https://slurm.schedmd.com/openapi_release_notes.html)

* Add new v0.0.46 API endpoints.
* Deprecate v0.0.43 API endpoints (will be removed in Slurm 27.05).
* Remove v0.0.42 API endpoints.

## Deprecations and Removals

* Deprecate the slurmrestd "become_user" mode.
* Deprecate the SLURMRESTD_JSON and SLURMRESTD_YAML environment variables. Use SLURM_JSON and SLURM_YAML.
