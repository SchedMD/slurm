# Slurm on Google Cloud Platform

The following describes setting up a Slurm cluster using [Google Cloud
Platform](https://cloud.google.com), bursting out from an on-premise cluster to
nodes in Google Cloud Platform and setting a multi-cluster/federated setup with
a cluster that resides in Google Cloud Platform.

The supplied scripts can be modified to work with your environment.

SchedMD provides professional services to help you get up and running in the
cloud environment. [SchedMD Commercial Support](https://www.schedmd.com/support.php)

## Stand-alone Cluster in Google Cloud Platform

The supplied scripts can be used to create a stand-alone cluster in Google Cloud
Platform. The scripts setup the following scenario:

* 1 - controller node
* 1 - login node
* N - compute nodes with a configured number of nodes that can be dynamically
created to match workload.

The default image for the instances is CentOS 7.

On the controller node, slurm is installed in:
/apps/slurm/<slurm_version>
with the symlink /apps/slurm/current pointing to /apps/slurm/<slurm_version>.

The login nodes mount /apps and /home from the controller node.

To deploy, you must have a GCP account and have the GCP Cloud SDK installed.  
See [Cloud Cloud SDK](https://cloud.google.com/sdk/downloads).


Steps:
1. Edit the `slurm-cluster.yaml` file and specify the required values

   For example:

   ```
   imports:
   - path: slurm.jinja

   resources:
   - name: slurm-cluster
     type: slurm.jinja
     properties:
       cluster_name            : google1
       static_node_count       : 2
       max_node_count          : 10
       zone                    : us-east1-b
       region                  : us-east1
       cidr                    : 10.10.0.0/16

       controller_machine_type : n1-standard-2
       compute_machine_type    : n1-standard-2
       login_machine_type      : n1-standard-1

       slurm_version           : 17.11.5
       default_account         : default
       default_users           : bob,joe
       munge_key               : <put munge key here>
   ```

   You can generate a munge key by doing:
   ```
   date +%s | sha512sum | cut -d' ' -f1
   ```
   See also [Creating a Secret Key](https://github.com/dun/munge/wiki/Installation-Guide#user-content-creating-a-secret-key)

   **NOTE:** The version number must match the version of the link name found
   at (https://www.schedmd.com/downloads.php).

2. Spin up the cluster.

   Assuming that you have gcloud configured for your account, you can just run:

   ```
   $ gcloud deployment-manager deployments [--project=<project id>] create slurm --config slurm-cluster.yaml
   ```

3. Check the cluster status.

   You can see that status of the deployment by viewing:
   https://console.cloud.google.com/deployments

   and viewing the new instances:
   https://console.cloud.google.com/compute/instances

   To verify that the deployment scripts all worked, ssh to the login node and
   run `sinfo` to see how many nodes have registered and in an IDLE state.

   A message will be broadcast to the terminal when the installation is
   complete. If you log in before the installation is complete, you will either
   need to re-log in after the installation is complete or start a new shell
   (e.g. /bin/bash) to get the correct bash profile.

   ```
   $ gcloud compute [--project=<project id>] ssh [--zone=<zone>] login1
   ...
   [bob@login1 ~]$ sinfo
   PARTITION AVAIL  TIMELIMIT  NODES  STATE NODELIST
   debug*       up   infinite      8  idle~ compute[3-10]
   debug*       up   infinite      2   idle compute[1-2]
   ```

   **NOTE:** By default, Slurm will hide nodes that are in a power_save state --
   "cloud" nodes. The GCP Slurm scripts configure **PrivateData=cloud** in the
   slurm.conf so that the "cloud" nodes are always shown. This is done so that
   nodes that get marked down can be easily seen.

4. Submit jobs on the cluster.

   ```
   [bob@login1 ~]$ sbatch -N2 --wrap="srun hostname"
   Submitted batch job 2
   [bob@login1 ~]$ cat slurm-2.out
   compute1
   compute2
   ```

5. Tearing down the deployment.

   ```
   $ gcloud deployment-manager [--project=<project id>] deployments delete slurm
   ```

   **NOTE:** If additional resources (instances, networks) are created other
   than the ones created from the default deployment then they will need to be
   destroyed before deployment can be removed.

### Accessing Compute Nodes

   There are multiple ways to connect to the compute nodes:
   1. If the compute nodes have external IPs you can connect directly to the
      compute nodes. From the GCP Compute Engine page, the SSH drop down next to
      the compute instances gives severval options for connecting to the compute
      nodes.
   2. Whether the compute nodes have external IPs or not, they can be connected
      to from within the cluster. Because /home is mounted on all of the
      instances an ssh key can be created and added to your authorized keys
      which can then be used to access all other instances in the cluster.  
      e.g.
      ```
      [bob@login1 ~]$ ssh-keygen -t rsa
      [bob@login1 ~]$ cat .ssh/id_rsa.pub >> .ssh/authorized_keys
      [bob@login1 ~]$ ssh compute1
      [bob@compute1 ~]$
      ```

### Preemptible VMs
   Currently, when a preemptible VM is preempted by GCP, Slurm will detect that
   the node is not responding and will mark the node as down after
   SlurmdTimeout. The node will remain in a "down" state until it is manually
   returned to an "idle" state. Nodes can be put back into an "idle" state with
   the command:
   ```
   $ scontrol update nodename=<nodename> state=idle
   ```
   See the [scontrol man page](https://slurm.schedmd.com/scontrol.html#OPT_SPECIFICATIONS-FOR-UPDATE-COMMAND,-NODES)
   for more state information.

## Bursting out from on-premise cluster

Bursting out from an on-premise cluster is done by configuring the
**ResumeProgram** and the **SuspendProgram** in the slurm.conf. The scripts
*resume.py*, *suspend.py* and *startup-script.py* in the scripts directory can
be modified and used create new compute instances in a GCP project. See the
[Slurm Elastic Computing](https://slurm.schedmd.com/elastic_computing.html) for more
information.

### Bursting out playground

You can use the deployment scripts to create a playground to test bursting from
an on-premise cluster by using two separate projects in GCP. This requires
setting up a gateway-to-gateway VPN in GCP between the two projects. The
following are the steps to do this.

1. Create two projects in GCP (e.g. project1, project2).
2. Create a slurm cluster in project1 using the deployments scripts.

   e.g.
   ```
   $ cat slurm-cluster.yaml
   resources:
   - name: slurm-cluster
     type: slurm.jinja
     properties:
       ...
       cluster_name            : google1
       ...
       cidr                    : 10.10.0.0/16
       ....

   $ gcloud deployment-manager --project=<project1> deployments create slurm --config slurm-cluster.yaml
   ```

3. Create a network in project2.
   1. From the GCP console, navigate to VPC Network->VPC Networks->CREATE VPC
      NETWORK
   2. Fill in the following fields:
      ```
      Name                  : slurm-network2
      Subnets:
      Subnet creation mode  : custom
      Name                  : slurm-subnetwork2
      Region                : choose a region
      IP address range      : 10.20.0.0/16
      Private Google Access : Disabled
      Dynamic routing mode  : Regional
      ```

4. Setup a gateway-to-gateway VPN.

   For each project, from the GCP console, create a VPN by going to
   Hybrid Connectivity->VPN->Create VPN connection.

   Fill in the following fields:
   ```
   Gateway:
   Name       : slurm-vpn
   Network    : choose project's network
   Region     : choose same region as slurm-network2's
   IP Address : choose or create a static IP

   Tunnels:
   Name                     : slurm-vpn-tunnel
   Remote peer IP Address   : static IP of other project
   IKE version              : IKEv2
   Shared secret            : string used by both vpns
   Routing options          : Policy-based
   Remote network IP ranges : IP range of network of other project (Enter 10.20.0.0/16 for project1 and 10.10.0.0/16 for project2)
   Local subnetworks        : For project1 choose "slurm-network" and for project2 choose "slurm-network2"
   Local IP ranges          : Should be filled in with the subnetwork's IP range.
   ```
   Then click Create.

   If all goes well then the VPNs should show a green check mark for the VPN
   tunnels.

5. Add permissions for project1 to create instances in project2.

   By default, GCE will create a service account in the instances that are
   created. We need to get this account name and give it permissions in
   project2.
   1. gcloud compute ssh to controller in project1
     * $ gcloud compute [--project=<project1>] ssh [--zone=<zone>] controller
   2. Run:
      ```
      gcloud config list
      ```
   3. Grab the account name.
   4. From project2's GCP Console, navigate to: IAM & Admin.
   5. Click ADD at the top.
   6. Add the account name to the Members field.
   7. Select the **Compute Admin** and **Service Account User** roles.
   8. Click ADD

6. Modify *resume.py* and *suspend.py* in the /apps/slurm/scripts directory on
   project1's controller instance to communicate with project2.

   Modify the following fields with the appropriate values:
   e.g.
   ```
   # resume.py, suspend.py
   PROJECT      = '<project2 id>'
   ZONE         = '<project2 zone>'

   # resume.py
   REGSION      = '<project2 region>'

   # Set to True so that it can install packages from the other network
   EXTERNAL_IP  = True
   ```

7. Configure the instances to be able to find the controller node.

   Modify *startup-script.py* to put the controller's IP address in /etc/hosts.
   You can find the controller's internal IP address by navigating to Compute
   Engine in project1's GCP Console.

   e.g.
   ```
   diff --git a/scripts/startup-script.py b/scripts/startup-script.py
   index 018c270..6103c5e 100644
   --- a/scripts/startup-script.py
   +++ b/scripts/startup-script.py
   @@ -704,6 +704,12 @@ PATH=$PATH:$S_PATH/bin:$S_PATH/sbin

    def mount_nfs_vols():

   +    f = open('/etc/hosts', 'a')
   +    f.write("""
   +<controller ip> controller
   +""")
   +    f.close()
   +
        f = open('/etc/fstab', 'a')
        f.write("""
    controller:{0}    {0}     nfs      rw,sync,hard,intr  0     0
   ```

8. Since the scripts rely on getting the Slurm configuration and binaries
   from the shared /apps file system, the firewall on the project1 must be
   modified to allow NFS through.

   1. On project1's GCP Console, navigate to VPC network->Firewall rules
   2. Click CREATE FIREWALL RULE at the top of the page.
   3. Fill in the following fields:
      ```
      Name                 : nfs
      Network              : slurm-network
      Priority             : 1000
      Direction of traffic : Ingress
      Action to match      : Allow
      Tagets               : Specified target tags
      Target tags          : controller
      Source Filter        : IP ranges
      Source IP Ranges     : 0.0.0.0/0
      Second source filter : none
      Protocols and ports  : Specified protocols and ports
      tcp:2049,1110,4045; udp:2049,1110,4045
      ```
   4. Click Create

9. Open ports on project1 for project2 to be able to contact the slurmctld
   (tcp:6817) and the slurmdbd (tcp:6819) on project1.

   1. On project1's GCP Console, navigate to VPC network->Firewall rules
   2. Click CREATE FIREWALL RULE at the top of the page.
   3. Fill in the following fields:
      ```
      Name                 : slurm
      Network              : slurm-network
      Priority             : 1000
      Direction of traffic : Ingress
      Action to match      : Allow
      Tagets               : Specified target tags
      Target tags          : controller
      Source Filter        : IP ranges
      Source IP Ranges     : 0.0.0.0/0
      Second source filter : none
      Protocols and ports  : Specified protocols and ports
      tcp:6817,6819
      ```
   4. Click Create

10. Open ports on project2 for project1 to be able to contact the slurmd's
    (tcp:6818) in project2.

    1. On project2's GCP Console, navigate to VPC network->Firewall rules
    2. Click CREATE FIREWALL RULE at the top of the page.
    3. Fill in the following fields:
       ```
       Name                 : slurmd
       Network              : project2-network
       Priority             : 1000
       Direction of traffic : Ingress
       Action to match      : Allow
       Tagets               : Specified target tags
       Target tags          : compute
       Source Filter        : IP ranges
       Source IP Ranges     : 0.0.0.0/0
       Second source filter : none
       Protocols and ports  : Specified protocols and ports
       tcp:6818
       ```
    4. Click Create

11. If you plan to use srun to submit jobs from the login nodes to the compute
    nodes in project2, then ports need to be opened up for the compute nodes to
    be able to talk back to the login nodes. srun open's several ephemeral ports
    for communications. It's recommended to define which ports srun can use when
    using a firewall. This is done by defining SrunPortRange=<IP Range> in the
    slurm.conf.

    e.g.
    ```
    SrunPortRange=60001-63000
    ```

    These ports need to opened up in project1 and project2's firewalls.

    1. On project1 and project2's GCP Consoles, navigate to VPC network->Firewall rules
    2. Click CREATE FIREWALL RULE at the top of the page.
    3. Fill in the following fields:
       ```
       Name                 : srun
       Network              : slurm-network
       Priority             : 1000
       Direction of traffic : Ingress
       Action to match      : Allow
       Tagets               : All instances in the network
       Source Filter        : IP ranges
       Source IP Ranges     : 0.0.0.0/0
       Second source filter : none
       Protocols and ports  : Specified protocols and ports
       tcp:60001-63000
       ```
    4. Click Create

12. Slurm should now be able to burst out into project2.

## Mutli-Cluster / Federation
Slurm allows you to use a central SlurmdDBD for multiple clusters. By doing this
it also allows the clusters to be able to communicate with each other. This is
done by the client commands first checking with the SlurmDBD for the requested
cluster's IP address and port which the client can then communicate directly
with the cluster.

For more information see:  
[Multi-Cluster Operation](https://slurm.schedmd.com/multi_cluster.html)  
[Federated Scheduling Guide](https://slurm.schedmd.com/federation.html)

**NOTE:** Either all clusters and the SlurmDBD must share the same MUNGE key
or use a separate MUNGE key for each cluster and another key for use between
each cluster and the SlurmDBD. In order for cross-cluster interactive jobs to
work, the clusters must share the same MUNGE key. See the following for more
information:  
[Multi-Cluster Operation](https://slurm.schedmd.com/multi_cluster.html)  
[Accounting and Resource Limits](https://slurm.schedmd.com/accounting.html)

**NOTE:** All clusters attached to a single SlurmDBD must share the same user
space (e.g. same uids across all the clusters).

### Playground

1. Create another project in GCP (e.g. project3) and create another Slurm
   cluster using the deployment scripts -- except with a different cluster name
   (e.g. google2) and possible IP range.

2. Open ports on project1 so that project3 can communicate with project1's
   slurmctld (tcp:6817) and slurmdbd (tcp:6819).

   1. On project1's GCP Console, navigate to VPC network->Firewall rules
   2. Click CREATE FIREWALL RULE at the top of the page.
   3. Fill in the following fields:
      ```
      Name                 : slurm
      Network              : slurm-network
      Priority             : 1000
      Direction of traffic : Ingress
      Action to match      : Allow
      Tagets               : Specified target tags
      Target tags          : controller
      Source Filter        : IP ranges
      Source IP Ranges     : 0.0.0.0/0
      Second source filter : none
      Protocols and ports  : Specified protocols and ports
      tcp:6817,6819
      ```
   4. Click Create

3. In project3 open up ports for slurmctld (tcp:6817) so that project1 can
   communicate with project3's slurmctld.
   1. On project3's GCP Console, navigate to VPC network->Firewall rules
   2. Click CREATE FIREWALL RULE at the top of the page.
   3. Fill in the following fields:
      ```
      Name                 : slurm
      Network              : slurm-network
      Priority             : 1000
      Direction of traffic : Ingress
      Action to match      : Allow
      Tagets               : Specified target tags
      Target tags          : controller
      Source Filter        : IP ranges
      Source IP Ranges     : 0.0.0.0/0
      Second source filter : none
      Protocols and ports  : Specified protocols and ports
      tcp:6817
      ```
   4. Click Create

4. Optional ports for interactive jobs.

   If you plan to use srun to submit jobs from one cluster to another, then
   ports need to be opened up for srun to be able to communicate with the
   slurmds on the remote cluster and ports need to be opened for the
   slurmds to be able to talk back to the login nodes on the remote cluster.
   srun open's several ephemeral ports for communications. It's recommended to
   define which ports srun can use when using a firewall. This is done by
   defining SrunPortRange=<IP Range> in the slurm.conf.

   e.g.
   ```
   SrunPortRange=60001-63000
   ```

   **NOTE:** In order for cross-cluster interactive jobs to work, the compute
   nodes must be accessible from the login nodes on each cluster
   (e.g. a vpn connection between project1 and project3).

   slurmd ports:  
   1. On project1 and project3's GCP Console, navigate to VPC network->Firewall rules
   2. Click CREATE FIREWALL RULE at the top of the page.
   3. Fill in the following fields:
      ```
      Name                 : slurmd
      Network              : slurm-network
      Priority             : 1000
      Direction of traffic : Ingress
      Action to match      : Allow
      Tagets               : Specified target tags
      Target tags          : compute
      Source Filter        : IP ranges
      Source IP Ranges     : 0.0.0.0/0
      Second source filter : none
      Protocols and ports  : Specified protocols and ports
      tcp:6818
      ```
   4. Click Create

   srun ports:  
   1. On project1 and project3's GCP Consoles, navigate to VPC network->Firewall rules
   2. Click CREATE FIREWALL RULE at the top of the page.
   3. Fill in the following fields:
      ```
      Name                 : srun
      Network              : slurm-network
      Priority             : 1000
      Direction of traffic : Ingress
      Action to match      : Allow
      Tagets               : All instances in the network
      Source Filter        : IP ranges
      Source IP Ranges     : 0.0.0.0/0
      Second source filter : none
      Protocols and ports  : Specified protocols and ports
      tcp:60001-63000
      ```
   4. Click Create

5. Modify both project1 and project3's slurm.confs to talk to the slurmdbd
   on project1's external IP.

   e.g.
   ```
   AccountingStorageHost=<external IP of project1's controller instance>
   ```

6. Add the cluster to project1's database.

   e.g.
   ```
   $ sacctmgr add cluster google2
   ```

7. Add user and account associations to the google2 cluster.

   In order for a user to run a job on a cluster, the user must have an
   association on the given cluster.

   e.g.
   ```
   $ sacctmgr add account=<default account> [cluster=<cluster name>]
   $ sacctmgr add user <user> account=<default account> [cluster=<cluster name>]
   ```

8. Restart the slurmctld on both controllers.

   e.g.
   ```
   $ systemctl restart slurmctld
   ```

9. Verify that the slurmdbd shows both slurmctld's have registered with their
   external IP addresses.

   * When the slurmctld registers with the slurmdbd, the slurmdbd records the
     IP address the slurmctld registered with. This then allows project1 to
     communicate with project3 and vise versa.

   e.g.
   ```
   $ sacctmgr show clusters format=cluster,controlhost,controlport
      Cluster     ControlHost  ControlPort
   ---------- --------------- ------------
      google1 ###.###.###.###         6817
      google2 ###.###.###.###         6817
   ```
10. Now you can communicate with each cluster from the other side.

    e.g.
    ```
    [bob@login1 ~]$ sinfo -Mgoogle1,google2
    CLUSTER: google1
    PARTITION AVAIL  TIMELIMIT  NODES  STATE NODELIST
    debug*       up   infinite      8  idle~ compute[3-10]
    debug*       up   infinite      2   idle compute[1-2]

    CLUSTER: google2
    PARTITION AVAIL  TIMELIMIT  NODES  STATE NODELIST
    debug*       up   infinite      8  idle~ compute[3-10]
    debug*       up   infinite      2   idle compute[1-2]

    [bob@login1 ~]$ sbatch -Mgoogle1 --wrap="srun hostname; sleep 300"
    Submitted batch job 17 on cluster google1

    [bob@login1 ~]$ sbatch -Mgoogle2 --wrap="srun hostname; sleep 300"
    Submitted batch job 8 on cluster google2

    [bob@login1 ~]$ squeue -Mgoogle1,google2
    CLUSTER: google1
                 JOBID PARTITION     NAME     USER ST       TIME  NODES NODELIST(REASON)
                    17     debug     wrap      bob  R       0:31      1 compute1

    CLUSTER: google2
                 JOBID PARTITION     NAME     USER ST       TIME  NODES NODELIST(REASON)
                     8     debug     wrap      bob  R       0:12      1 compute1
    ```
