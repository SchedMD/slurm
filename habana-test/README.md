# Intel GAUDI SLURM testing
This directory contains scripts and instructions for building, installing, and testing SLURM with Intel's GAUDI accelerators. Follow the steps below to get started.

## Building the Packages
Compile and build SLURM OS packages (deb/rpm) based on /etc/os-release. In case VERSION was provided, it sets it packages version to it, else it uses SLURM's default version.<br />
For the compilation to include habana's code, the compilation is required to run on a machine which contains Intel GAUDI's software stack pre-installed (ensure Habana runtime, hlml.h and libhlml.so are present on the server).
```sh
make build-package VERSION=[MAJOR].[MINOR].[MICRO]-[RELEASE]
```

## Installing the Packages
Install the locally created package and configure the SLURM configuration files at the following locations:
* /etc/slurm/slurm.conf
* /etc/slurm/gres.conf
* /etc/slurm/cgroup.conf
```sh
make install-package
```

## Testing
SLURM checks GAUDI accelerator availability and allocates devices by accelerator ID (0-7) to a process. The allocation is done by setting the `HABANA_VISIBLE_DEVICES` environment variable with a comma-separated array of IDs and running a container using the Habana container runtime with this environment variable.

### Single Card Allocation
Request SLURM for a single GAUDI accelerator on a single node and run `hl-smi` to ensure that only one card was allocated.
```sh
# User can ovveride the default test image with TEST_CONTAINER_IMAGE=<TEST IMAGE PATH>
make test-single-card
```

### 8 cards allocation with ports
Request SLURM for 8 GAUDI accelerators on a single node and perform an HCCL test.
```sh
# User can ovveride the default test image with TEST_CONTAINER_IMAGE=<TEST IMAGE PATH>
make test-8-cards
```

### Multi-box provisioning (16 cards)
Request SLURM for two nodes, each with 8 GAUDI accelerators, and perform an HCCL test.<br />
The second node is expected to have:
* Intel GAUDI software stack (drivers + hl-smi)
* Docker package installed
* Intel GAUDI container runtime for docker with the proper /etc/docker/daemon.json configuration
* Intel GAUDI cards External networking ports to be up (Relevant for scale out test, so if not configured on the first node, configure it as well)

 The second node will be automatically installed using the `hlctl` CLI.
```sh
# User can ovveride the default test image with TEST_CONTAINER_IMAGE=<TEST IMAGE PATH>
make test-16-card SECOND_NODE=[second node name]
```
