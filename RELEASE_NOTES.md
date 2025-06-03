# Release Notes for Slurm 25.05

## Upgrading

Slurm 25.05 supports upgrading directly from 24.11, 24.05, and 23.11.

See the [Upgrade Guide](https://slurm.schedmd.com/upgrades.html) for further details.

## Highlights

* Optional support for [TLS encryption](https://slurm.schedmd.com/tls.html) for all Slurm RPC traffic.
* Support for defining [multiple topologies](https://slurm.schedmd.com/topology.yaml.html) which can be applied to specific partitions.
* Support for tracking and allocating [hierarchical resources](https://slurm.schedmd.com/hres.html) (beta).
* Dynamic nodes can be [dynamically added](https://slurm.schedmd.com/dynamic_nodes.html#topology) to the network topologies.
* Added support for an [OR operator](https://slurm.schedmd.com/sbatch.html#OPT_licenses) in license requests.
* sacctmgr - Add support for [dumping and loading QOSes](https://slurm.schedmd.com/sacctmgr.html#SECTION_FLAT-FILE-DUMP-AND-LOAD).
* slurmrestd - Add support for [TLS encryption](https://slurm.schedmd.com/tls.html) to listening sockets.
* srun - Add new [\-\-wait-for-children](https://slurm.schedmd.com/srun.html#OPT_wait-for-children) option to keep the step running until all launched processes have exited (cgroup/v2 only).
* topology/block - Permit gaps within the block definitions.
* jobcomp/kafka - New [option](https://slurm.schedmd.com/slurm.conf.html#OPT_enable_job_start) to send job information at job start in addition to job completion.
* switch/hpe_slingshot - Support for > 252 ranks per node.
* switch/hpe_slingshot - Support for [mTLS authentication](https://slurm.schedmd.com/slurm.conf.html#OPT_fm_mtls_ca) to the fabric manager.

## Configuration Changes

* Autodetect the available cpus, and automatically set the connection management thread pool size to 2x that value. The ["conmgr_threads"](https://slurm.schedmd.com/slurm.conf.html#OPT_conmgr_threads) settings can be used to override this.
* jobcomp/elasticsearch and jobcomp/kafka - Stop sending the batch script as part of the message by default. A new ["send_script"](https://slurm.schedmd.com/slurm.conf.html#OPT_send_script) option has been added if the batch script should be sent.

## REST API Changes

[Slurm OpenAPI Plugin Release Notes](https://slurm.schedmd.com/openapi_release_notes.html)

* Added new v0.0.43 API endpoints.
* Deprecated v0.0.40 API endpoints (will be removed in Slurm 25.11).
* Added [new endpoint](https://slurm.schedmd.com/rest_api.html#slurmV0043PostReservation) for creating reservations.

## Deprecations and Removals

* Removed support for "FrontEnd" mode of operation. (Required compilation with "--enable-front-end".)
* Marked cgroup/v1 support as deprecated.
