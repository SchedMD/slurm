Copyright (C) 2014 Silicon Graphics International Corp.
All rights reserved.

The SGI hypercube topology plugin for Slurm enables Slurm to understand the
hypercube topologies on some SGI ICE InfiniBand clusters. With this
understanding about where nodes are physically located in relation to each
other, Slurm can make better decisions about which sets of nodes to allocate to
jobs.

The plugin requires a properly set up topology.conf file. This is built using
the contribs/sgi/netloc_to_topology program which in turn uses the OpenMPI
group's netloc and hwloc tools. Please execute the following steps:

1) Ensure that hwloc and netloc are installed on every node in your cluster

2) Create a temporary directory in a shared filesystem available to each node
   in your cluster. In this example we'll call it /data/slurm/cluster_data/.

3) Create a subdirectory called hwloc, ie. /data/slurm/cluster_data/hwloc/.

4) Create the following script in /data/slurm/cluster_data/create.sh
   #!/bin/sh
   HN=`hostname`
   hwloc-ls /data/slurm/cluster_data/hwloc/$HN.xml

5) Run the script on each compute node
   $ cexec /data/slurm/cluster_data/create.sh

6) Ensure that hwloc output files got put into /data/slurm/cluster_data/hwloc/.
   If you have any nodes down right now, their missing data may cause you
   problems later.

7) Run netloc discovery on the primary InfiniBand fabric
   $ cd /data/slurm/cluster_data/
   $ netloc_ib_gather_raw --out-dir ib-raw --sudo --force-subnet mlx4_0:1
   $ netloc_ib_extract_dats

8) Run netloc_to_topology to turn the netloc and hwloc data into a Slurm
   topology.conf.
   $ netloc_to_topology -d /data/slurm/cluster_data/
   netloc_to_topology assumes a InfiniBand fabric ID of "fe80:0000:0000:0000".
   If you have a different fabric ID, then you'll need to specify it with the
   "-f" option. You can find the fabric ID with `ibv_devinfo -v`. E.g.
   $ ibv_devinfo -v 
   Look down the results and for the HCA and port that you want to key off of,
   look at its GID field. E.g.
   GID[ 0]: fec0:0000:0000:0000:f452:1403:0047:36d1
   Use the first four couplets:
   $ netloc_to_topology -d /data/slurm/cluster_data/ -f fec0:0000:0000:0000 

9) Copy the resulting topology.conf file into Slurm's location for configuration
   files. The following command copies it to the compute nodes. Make sure to
   copy it to the node(s) running slurmctld as well.
   $ cpush topology.conf /etc/slurm/topology.conf

10) Restart Slurm
