# Release Notes for Slurm 25.11

## Upgrading

Slurm 25.11 supports upgrading directly from 25.05, 24.11, and 24.05.

See the [Upgrade Guide](https://slurm.schedmd.com/upgrades.html) for further details.

## Highlights

* Added new "Expedited Requeue" mode for batch jobs. Batch jobs with --requeue=expedite will automatically requeue on node failure, or if the batch script returns a non-zero exit code and one or more Epilog scripts fail. Expedited requeue jobs are eligible to restart immediately, are treated as the highest priority job in the system, and their previously allocated set of nodes will be prevented from launching other work.
* Added a new "Mode 3" of operation to Hierarchical Resources. This mode complements the existing Mode 1 and Mode 2 by summing usage from lower levels automatically. This can be used, e.g., to implement a power-capping mode modeling power distribution between the datacenter, local distribution, and individual racks.
* Added direct support for exporting OpenMetrics (Prometheus) telemetry from slurmctld. This is accessible on SlurmctldPort on SlurmctldHost by default, or can be disabled if desired.
* Added an experimental asynchronous-reply mode to slurmctld. If enabled with "SlurmctldParameters=enable_async_reply", RPC responses are offloaded to the kernel for further processing, freeing individual worker threads for new traffic.

## New Features

* Added additional Reservation access controls - AllowQOS and AllowPartition.
* Allow cloud nodes to dynamically set topology at registration with "slurmd --conf=topology=...".
* Renamed "job_container" plugin interface to "namespace".
* Added new namespace/linux plugin with support for managing filesystem, pid, and user namespaces.
* Added support to launching the slurmd in a cgroup slice other than system.slice.
* Added --running-over and --running-under time options to squeue to filter running jobs based on their execution time.
* Added --consolidate-segments option to salloc/sbatch/srun for topology/block. This will ensure segments are packed as few blocks as possible.
* Added --spread-segments option to salloc/sbatch/srun for topology/block. This will ensure segments are allocated on unique base blocks.
* Added limited support for using --segment in conjunction with --nodelist for topology/block.
* Added IMEX channel setup to the batch and interactive steps for switch/nvidia_imex plugin.
* Added --network=unique-channel-per-segment option to salloc/sbatch/srun to allocate a unique IMEX channel per segment when using topology/block and switch/nvidia_imex plugins.
* Support running HealthCheckProgram only at startup with HealthCheckNodeState=START_ONLY.
* Export select metrics from the slurmctld in the Prometheus format.
* Support responding to both http and slurm protocol requests on the same port.
* Added --parameters option to slurmd to set Parameters options directly, rather than in the configuration file. This can help test alternate CPU topologies. E.g., "slurmd -C --parameters=l3cache_as_socket" will report the corresponding topology.
* Added basic HTTP service probe support to slurmctld and slurm daemons, accessible through /livez, /readyz, and /healthz endpoints.
* Allow retroactive changes by administrators to the AllocTRES for a job through the sacctmgr command. This can be used, e.g., to add energy usage data to the accounting records if unavailable through Slurm's internal jobacct_gather plugins.
* If not previously configured to operate with SlurmDBD, slurmctld will not attempt to automatically register with its existing ClusterID.

## Configuration Changes

* Default SlurmdParameters=conmgr_threads has been changed to 6.
* NamespaceType replaces JobContainerType in slurm.conf.
* Added JobCompPassScript and StoragePassScript to enable database password rotation.
* The "configless" mode now supports distributing TaskProlog and TaskEpilog scripts.
* Added Parameters option to NodeName lines to vary SlurmdParameters on an individual node. This can be used to set options such as "l3cache_as_socket" in a more granular manner.
* Added DisableArchiveCommands option to slurmdbd.conf. This disables the RPC processing for "sacctmgr archive" and "sacctmgr load" commands.
* Added MaxPurgeLimit to slurmdbd.conf.
* Added CommunicationParameters=disable_http option to disable HTTP processing completely in slurmctld and slurmd.
* For performance reasons the PMIx server (mpi/pmix plugin) will no longer store the node's HWLOC topology in the job-level information under the PMIX_LOCAL_TOPO key by default. The new PMIxShareServerTopology=true option in mpi.conf can restore the prior behavior if necessary.

## Packaging Changes

* deb - change pam_slurm_adopt install location to the correct multiarch location.
* rpm - add --with cgroupv2 option to force the requirements for the cgroup/v2 plugin to be present at build time.

## REST API Changes

[Slurm OpenAPI Plugin Release Notes](https://slurm.schedmd.com/openapi_release_notes.html)

* Added new v0.0.44 API endpoints.
* Deprecated v0.0.41 API endpoints (will be removed in Slurm 26.05).

## Deprecations and Removals
* Removed node_features/knl_generic plugin.
