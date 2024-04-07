# Intel SLURM testing
This directory contains scripts and instructions for building, installing, and testing SLURM with Intel's GAUDI accelerators. Follow the steps below to get started.

## Building the Packages
To compile and package SLURM, execute the following command on a machine with the Habana software stack pre-installed (ensure hlml.h and libhlml.so are present on the server):
```sh
make build-debian-package
```

## Installing the Packages
Install the locally created DEB packages and configure the SLURM configuration files at the following locations:
* /etc/slurm/slurm.conf
* /etc/slurm/gres.conf
* /etc/slurm/cgroup.conf
```sh
make install-debian-package
```

## Testing
SLURM checks GAUDI accelerator availability and allocates devices by accelerator ID (0-7) to a process. The allocation is done by setting the `HABANA_VISIBLE_DEVICES` environment variable with a comma-separated array of IDs and running a container using the Habana container runtime with this environment variable.

### Single Card Allocation
Request SLURM for a single GAUDI accelerator on a single node and run `hl-smi` to ensure that only one card was allocated.
```sh
make test-single-card
```

### 8 cards allocation with ports
Request SLURM for 8 GAUDI accelerators on a single node and perform an HCCL test.
```sh
make test-8-cards
```

### Multi-box provisioning (16 cards)
Request SLURM for two nodes, each with 8 GAUDI accelerators, and perform an HCCL test. The second node will be automatically installed using the `hlctl` CLI.
```sh
make test-16-card
```