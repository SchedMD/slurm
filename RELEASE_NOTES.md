# Release Notes for Slurm 26.05

## Upgrading

Slurm 26.05 supports upgrading directly from 25.11, 25.05, and 24.11.

See the [Upgrade Guide](https://slurm.schedmd.com/upgrades.html) for further details.

## Highlights

* New "srun --async" step mode that submits step processes to stepmgr to queue and eventually launch. This avoids issues with keeping a large number of srun processes backgrounded to queue up future step-based workflows. (This was previously described as "mini-batch" in the Slurm roadmap presentations.)
* New topology/ring and topology/torus3d topology plugins. These implement a single-dimensional and three-dimensional topology respectively.
* Slurm REST API - Add support for creating/updating/removing partitions, and fror viewing the active slurmctld and slurmdbd configurations.
* Dynamic Memory Resizing. A job can now release memory (and have the cgroup limits updated) using 'scontrol update' while running. A new "sbatch --mem-update=<margin>@<delay>" option can also automatically reduce the memory limit to the current usage plus a given margin percentage after a specified time.
* Add topology-based sorting for node ranks when using dynamic nodes with the topology plugins. This is also available generically for topology/flat (no topology) with a new alpha_step_rank option.
* Add an optimized single-node path through the scheduling logic for increased performance.
* Expanded the openmetrics (Prometheus) nodes, partitions, and jobs endpoints with gpu allocation statistics.
* namespace/linux - Add support for custom mount options and paths for each target directory.

## Configuration Changes - slurm.conf

* Add SuspendTime as a NodeName parameter, enabling per-node power save configuration.
* Exclusive=[NO|NODE|USER|TOPO] replaces ExclusiveUser and ExclusiveTopo when defining Partitions.
* jobcomp/elasticsearch and jobcomp/kafka - Send 'admin_comment' and 'comment' fields if JobCompParams=send_comment is set.
* Add DebugFlags=thread.

## Configuration Changes - gres.conf

* Add AutoDetect=full option to try all GRES plugin AutoDetection types on slurmd start.

## Configuration Changes - oci.conf

* Add %Z filename expansion pattern for the job's working directory.

## Configuration Changes - slurmdbd.conf

* Add Parameters=PreserveCaseResource to make resources (remote licenses) case sensitive.
* Add DisableRollups option.
* Add DebugFlags=thread.

## Packaging Changes

* HTML documentation (man and otherwise) is no longer built or packaged by default. New 'make html' and 'make install-html' targets can be used to generate the HTML documentation if desired.
* MUNGE is now a weak dependency to Slurm RPM and DEB packages.
* Use pkgconf to get information about most dependencies in spec file.

## API Changes

The Slurm API has been updated to use slurm_step_id_t in lieu of a job_id in API calls. This allows the API to be queried by SLUID instead of by JobId. Backwards compatibility is available through the SLURM_BACKWARD_COMPAT define when including <slurm/slurm.h>.

## REST API Changes

[Slurm OpenAPI Plugin Release Notes](https://slurm.schedmd.com/openapi_release_notes.html)

* Added new v0.0.45 API endpoints.
* Deprecated v0.0.42 API endpoints (will be removed in Slurm 26.11).

## Deprecations and Removals

* Remove SchedulerParameters=enable_job_state_cache.
