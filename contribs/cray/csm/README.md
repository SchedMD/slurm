Cray System Management Support Files
====================================

The files in this directory are used to configure Slurm for Cray System
Management software starting with CLE 6.0. See the
*XC Series System Administration Guide* for background information about
Cray System Management.

Configuration Generator for Cray Systems
----------------------------------------

`slurmconfgen_smw.py` is a `slurm.conf` and `gres.conf` generator for
Cray systems that runs on the System Management Workstation (SMW).
It detects system hardware and creates a simple single-partition Slurm
configuration. After generation, customize the configuration files to
your needs.

Place the generated configuration files and other Slurm configuration in
the config set directory
`/var/opt/cray/imps/config/sets/<cfgset>/files/roles/simple_sync/classes/common/etc/opt/slurm/`.
They will be copied to every node at boot time.

Ansible Play
------------

`slurm_playbook.yaml` is an ansible play which configures the XC environment
for Slurm use. Customize the play to your needs and copy it to the config set
directory `/var/opt/cray/imps/config/sets/<cfgset>/ansible/` to be run at
boot time on every node.
See the *XC Series Ansible Play Writing Guide* for more detail.
