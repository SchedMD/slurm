slurm-helper
============

Bunch of helper files for the Slurm resource manager

Vim syntax file
---------------

The Vim syntax file renders the Slurm batch submission scripts easier to read and to spot errors in the submission options.

As submission scripts are indeed shell scripts, and all Slurm options are actually Shell comments, it can be difficult to spot errors in the options.

This syntax file allows vim to understand the Slurm option and highlight them accordingly. Whenever possible, the syntax rules check the validity of the options and put in a special color what is not recognized as a valid option, or valid parameters values.

__Installation__

Under Linux or MacOS, simply copy the file in the directory

    .vim/after/syntax/sh/

or whatever shell other than ``sh`` you prefer.

For system wide use with bash put the file in

    /etc/bash_completion.d/

The syntax file is then read and applied on a Shell script after the usual syntax file has been processed.

__Known issues__

* Some regex needed to validate options or parameter values are not exactly correct, but should work in most cases.
  * Currently, value completions do not support complex types (e.g. `param=key0:val0,key1:val1,`).
* Any new option unknown to the syntax file will be spotted as an error.
* On a Debian system (Ubuntu) you may see messages like...
  _get_comp_words_by_ref: command not found
  after a tab.
  Based on http://askubuntu.com/questions/33440/tab-completion-doesnt-work-for-commands you need to alter your /etc/bash.bashrc to make this work correctly.

Bash completion
---------------

The Bash completion script offers <TAB> completion for Slurm commands.

__Instalation__

Source the [`slurm_completion.sh`](./__slurm_completion.sh) script. It is
recommended to automate this by adding a bash sourcing line to your
`.bashrc` or `.profile`.

Additionally, there are a number of environment variables that can be set to
alter/customize completion behavior. They are documented in
[`slurm_completion.sh`](./__slurm_completion.sh).

```sh
# .bashrc
source /path/to/slurm_completion.sh
```

__Examples__

```sh
$ sacct --<tab><tab>
--accounts=       --endtime=        --local           --state=
--allclusters     --env-vars        --long            --timelimit-max=
--allocations     --federation      --name=           --timelimit-min=
--allusers        --fields=         --ncpus=          --truncate 
--associations=   --file=           --nnodes=         --uid=
--autocomplete=   --flags=          --noconvert       --units=
--batch-script    --format=         --nodelist=       --usage 
--brief           --gid=            --noheader        --use-local-uid 
--cluster=        --group=          --parsable        --user=
--clusters=       --help            --parsable2       --verbose 
--completion      --help-fields     --partition=      --version 
--constraints=    --helpformat      --qos=            --wckeys=
--delimiter=      --jobs=           --reason=         --whole-hetjob=
--duplicates      --json            --starttime=      --yaml 
```

```sh
$ squeue --<tab><tab>
--accounts=      --hide           --nodelist=      --states=
--all            --iterate=       --nodes=         --steps=
--array          --jobs=          --noheader       --usage 
--array-unique   --json           --partitions=    --user=
--autocomplete=  --licenses=      --priority       --users=
--cluster=       --local          --qos=           --verbose 
--clusters=      --long           --reservation=   --version 
--federation     --me             --sib            --yaml 
--format=        --name=          --sibling        
--Format=        --noconvert      --sort=          
--help           --node=          --start          

$ squeue --users=<tab><tab>
root,    slurm,    user0,    user1,    user2,    user3,  

$ squeue --Format=<tab><tab>
Display all 118 possibilities? (y or n)
account,            licenses,           resvport,
accruetime,         maxcpus,            schednodes,
admin_comment,      maxnodes,           sct,
allocnodes,         mcslabel,           selectjobinfo,
allocsid,           mem-per-tres,       siblingsactive,
arrayjobid,         mincpus,            siblingsactiveraw,
arraytaskid,        minmemory,          siblingsviable,
associd,            mintime,            siblingsviableraw,
batchflag,          mintmpdisk,         sockets,
batchhost,          name,               sperboard,
boardspernode,      network,            starttime,
burstbuffer,        nice,               state,
burstbufferstate,   nodelist,           statecompact,
cluster,            nodes,              stderr,
clusterfeature,     ntperboard,         stdin,
command,            ntpercore,          stdout,
comment,            ntpernode,          stepid,
container,          ntpersocket,        stepname,
contiguous,         numcpus,            stepstate,
cores,              numnodes,           submittime,
corespec,           numtasks,           system_comment,
cpufreq,            origin,             threads,
cpus-per-task,      originraw,          timeleft,
--More--
```

```sh
$ scontrol <tab><tab>
abort               pidinfo             shutdown 
cancel_reboot       ping                suspend 
cluster             reboot              takeover 
completing          reconfigure         token 
create              release             top 
delete              requeue             uhold 
errnumstr           requeuehold         update 
fsdampeningfactor   resume              version 
help                schedloglevel       wait_job 
hold                setdebug            write 
listpids            setdebugflags       
notify              show                

$ scontrol update <tab><tab>
frontendname=     nodename=         reservationname=  
jobid=            partitionname=    stepid=           

$ scontrol update nodename=node<tab><tab>
node00,  node03,  node06,  node09,  node12,  node15,  node18,
node01,  node04,  node07,  node10,  node13,  node16,  node19,
node02,  node05,  node08,  node11,  node14,  node17,  

$ scontrol update nodename=node00 <tab><tab>
activefeatures=     extra=              nodename=
availablefeatures=  gres=               reason=
comment=            nodeaddr=           state=
cpubind=            nodehostname=       weight=

$ scontrol update nodename=node00 state=<tab><tab>
cancel_reboot      future             power_down_asap    undrain 
down               idle               power_down_force   
drain              noresp             power_up           
fail               power_down         resume             
```
