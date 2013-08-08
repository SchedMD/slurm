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
* Any new option unknown to the syntax file will be spotted as an error.
* On a Debian system (Ubuntu) you may see messages like...
  _get_comp_words_by_ref: command not found
  after a tab.
  Based on http://askubuntu.com/questions/33440/tab-completion-doesnt-work-for-commands you need to alter your /etc/bash.bashrc to make this work correctly.

Bash completion
---------------

The Bash completion script offers <TAB> completion for Slurm commands.

At present the following Slurm commands are considered
* scontrol
* sreport

__Instalation__

Simply source the script in your .bashrc or .profile

__Examples__

    root@frontend:~ # squeue --<tab><tab>
    --account<account_list>  --iterate<seconds>       --qos<qos_list>          --usage
    --clusters<string>       --jobs<job_id_list>      --sort<sort_list>        --user<user_list>
    --format<fmtstring>      --nodes<hostlist>        --start                  --verbose
    --help                   --noheader               --state<state_list>      --version
    --hide                   --partition<part_list>   --steps                  
    root@frontend:~ # squeue --us<tab><tab>
    --usage  --user   
    root@frontend:~ # squeue --user <tab><tab>
    user1     user2     user3     user4 
    
    root@frontend:~ # scontrol <tab><tab>
    abort        delete       pidinfo      requeue      shutdown     update       
    checkpoint   hold         ping         resume       suspend      version      
    completing   listpids     reconfigure  setdebug     takeover     
    create       notify       release      show         uhold        
    root@frontend:~ # scontrol update <tab><tab>
    jobid=            nodename=         partitionname=    reservationname=  step=
    root@frontend:~ # scontrol update nodename=<tab><tab>
    root@frontend:~ # scontrol update nodename=node<tab><tab>
    node01  node03  node05  node07  node09  node11  node13  node15  node17  node19  
    node02  node04  node06  node08  node10  node12  node14  node16  node18  node20  
    root@frontend:~ # scontrol update nodename=node12 
    features=<features>  reason=<reason>      weight=<weight>      
    gres=<gres>          state=<state>        
    root@frontend:~ # scontrol update nodename=node12 state=<tab><tab>
    alloc       down        fail        idle        mixed       power_up    
    allocated   drain       failing     maint       power_down  resume      
    root@frontend:~ # scontrol update nodename=node12 state=resume 
    
    root@frontend:~ # squeue --format "%<TAB><TAB>
    %a(Account)        %E(dependency)     %i(id)             %M(time)           %s(selecplugin)
    %A(NTasks)         %e(end)            %I(Ncores/socket)  %N(alloc_nodes)    %t(state)
    %b(gres)           %f(features)       %j(name)           %n(reqnodes)       %T(state)
    %c(mincpu)         %G(gID)            %k(comment)        %O(contiguous)     %U(uID)
    %C(Ncpus)          %g(group)          %l(limit)          %p(priority)       %u(user)
    %d(minTmp)         %H(Nsockets)       %L(timeleft)       %r(reason)         %v(reservation)
    %D(NNodes)         %h(shared)         %m(mem)            %R(reason)         %x(excnodes)
    
