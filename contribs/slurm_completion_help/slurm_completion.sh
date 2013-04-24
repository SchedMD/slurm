###############################################################################
#
# Bash completion for Slurm
#
###############################################################################
#  Copyright (C) 2012 Damien François. <damien.francois@uclouvain.Be>
#  Written by Damien François. <damien.francois@uclouvain.Be>.
#
#  This file is part of SLURM, a resource management program.
#  For details, see <http://slurm.schedmd.com/>.
#  Please also read the included file: DISCLAIMER.
#
#  SLURM is free software; you can redistribute it and/or modify it under
#  the terms of the GNU General Public License as published by the Free
#  Software Foundation; either version 2 of the License, or (at your option)
#  any later version.
#
#  In addition, as a special exception, the copyright holders give permission
#  to link the code of portions of this program with the OpenSSL library under
#  certain conditions as described in each individual source file, and
#  distribute linked combinations including the two. You must obey the GNU
#  General Public License in all respects for all of the code used other than
#  OpenSSL. If you modify file(s) with this exception, you may extend this
#  exception to your version of the file(s), but you are not obligated to do
#  so. If you do not wish to do so, delete this exception statement from your
#  version.  If you delete this exception statement from all source files in
#  the program, then also delete it here.
#
#  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
#  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
#  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
#  details.
#
#  You should have received a copy of the GNU General Public License along
#  with SLURM; if not, write to the Free Software Foundation, Inc.,
#  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
#
###############################################################################

function compute_set_diff(){
    res=""
    for i in $1; do
        [[ "$2" =~ ${i%%=*} ]] && continue 
        res="$i $res"
    done
    echo $res 
}

_split_long_opt() {
    [[ $cur == = || $cur == : ]] && cur=""
    [[ $prev == = ]] && prev=${COMP_WORDS[$cword-2]}
}

function find_first_partial_occurence(){
    res=""
    for item1 in $1; do
        for item2 in $2; do
            if [[ $item2 == "$item1=" ]]; then
                res="$item1"
                break
            fi
        done
        if [[ $res != "" ]]; then
            break
        fi
    done
    echo $res 
}

function find_first_occurence(){
    res=""
    for item1 in $1; do
        for item2 in $2; do
            if [[ $item1 = $item2 ]]; then
                res="$item1"
                break
            fi
        done
        if [[ $res != "" ]]; then
            break
        fi
    done
    echo $res 
}

function offer (){
    remainings=$(compute_set_diff "$1" "${COMP_WORDS[*]}")
    COMPREPLY=( $( compgen -W "$remainings" -- $cur ) ) 
    if [[ "$1" == *=* || "$1" == *%* || "$1" == *:* ]];
    then
        #echo "NO SPACE $1" >> loglog
        compopt -o nospace
    fi
}

function offer_list () {
    curlist=${cur%,*} 
    curitem=${cur##*,} 

    if [[ $curlist == $curitem ]]      
    then
        COMPREPLY=( $( compgen -W "${1}" -- $cur ) ) ; return
    elif [[ $cur == *,  ]]  ; 
    then 
        compvalues=""
        for i in $1;do
            [[ $cur =~ $i ]] && continue 
            compvalues="$i $compvalues "
        done
        uniqueprefix=1
        prefix=${compvalues:0:1}
        for i in $compvalues;do
            [[ ${i:0:1} == $prefix ]] || uniqueprefix=0
        done
        if [[ $uniqueprefix == 1  ]]
        then
            compvalues=""
            for i in $1;do
                [[ $cur =~ $i ]] && continue 
                compvalues="$compvalues $curlist,$i"
            done
        fi
        COMPREPLY=( $( compgen -W "${compvalues}" -- "" ) ) ; return
    else                                 
        compvalues=""
        for i in $1;do
            [[ $cur =~ $i ]] && continue 
            compvalues="$compvalues $curlist,$i"
        done
        COMPREPLY=( $( compgen -W "${compvalues}" -- $cur ) ) ; 
    fi
} 

function offer_many () {
    availablevalues=""
    for i in $1;do
        [[ $cur =~ $i ]] && continue 
        availablevalues="$i $availablevalues"
    done

    # Check that there is no unique prefix for all remaining options (God knows why I have to do this. Must be missing something)
    # TODO when all suboptions start with the same prefix, it is not working great
    uniqueprefix=1
    prefix=${availablevalues:0:1}
    for i in $availablevalues;do
        [[ ${i:0:1} == $prefix ]] || uniqueprefix=0
    done


    #if [[ "$1" == *'\"'% ]];
    #then
    #    compopt -o nospace
    #fi #added for --format in squeue

    if [[  ${COMP_WORDS[COMP_CWORD-1]} == "$argname" ]]; then
        # echo  "The first value is about to be entered" >> loglog
        cur=""
        COMPREPLY=( $( compgen -W "${1}" -- $cur ) ) ; return
    fi
    if [[ ${COMP_WORDS[COMP_CWORD-1]} == '='  && "$cur" != *,* ]]; then
        # echo  "A supplementary value is being entered" >> loglog
        COMPREPLY=( $( compgen -W "${1}" -- $cur ) ) ; return
    fi
    if [[ ${cur:${#cur}-1:1} == "," && $uniqueprefix == 0  ]]; then
         echo  "A supplementary value is about to be entered and there is a no unique suffix" >> loglog
        compvalues=""
        for i in $1;do
            [[ $cur =~ $i ]] && continue 
            compvalues="$i $compvalues"
        done
        cur=""
        COMPREPLY=( $( compgen -W "${compvalues}" -- $cur ) ) ;
        return
    fi
    if [[ "$cur" =~ ","  ]] ; then 
         echo  "A supplementary value is about to be entered and there is a unique prefix or we are in the middle of one" >> loglog
        compvalues=""
        for i in $1;do
            [[ $cur =~ $i ]] && continue 
            compvalues="$compvalues ${cur%,*},$i"
            #compvalues="$compvalues $i"
        done
        COMPREPLY=( $( compgen -W "${compvalues}" -- $cur ) ) ; 

        # This is lame, we show complete list rather than last element
        return
    fi
    return 255
}

function param () {
    argname="$1"
    [[ ${COMP_WORDS[COMP_CWORD]} == "=" && ${COMP_WORDS[COMP_CWORD-1]} == $1 ]] && return 0
    [[ ${COMP_WORDS[COMP_CWORD-1]} == "=" && ${COMP_WORDS[COMP_CWORD-2]} == $1 ]] && return 0
    [[ ${COMP_WORDS[COMP_CWORD-1]} == $1 ]] && return 0
    return 255
}

function _jobs() {
echo $( scontrol -o show jobs | cut -d' ' -f 1 | cut -d'=' -f 2 ) ; 
}
function _wckeys() {
echo $(sacctmgr -p -n list wckeys | cut -d'|' -f1) ; 
}
function _qos() {
echo $(sacctmgr -p -n list qos | cut -d'|' -f1) ; 
}
function _clusters() {
echo $(sacctmgr -p -n list clusters | cut -d'|' -f1) ; 
}
function _jobnames() {
echo $( scontrol -o show jobs | cut -d' ' -f 2 | cut -d'=' -f 2 ) ; 
}
function _partitions() {
echo $(scontrol show partitions|grep PartitionName|cut -c 15- |cut -f 1 -d' '|paste -s -d ' ') ; 
}
function _nodes() {
echo $(scontrol show nodes | grep NodeName | cut -c 10- | cut -f 1 -d' ' | paste -s -d ' ') ; 
}
function _accounts() {
echo $(sacctmgr -pn list accounts | cut -d'|' -f1 | paste -s -d' ') ; 
}
function _licenses() {
echo $(scontrol show config| grep Licenses | sed 's/Licenses *=//'| paste -s -d' ') ; 
}
function _nodes() {
echo $(scontrol show nodes | grep NodeName | cut -c 10- | cut -f 1 -d' ' | paste -s -d ' ') ; 
}
function _features() {
echo $(scontrol -o show nodes|cut -d' ' -f7|sed 's/Features=//'|sort -u|tr -d '()'|paste -d, -s) ; 
}
function _users() {
echo $(sacctmgr -pn list users | cut -d'|' -f1) ; 
}
function _reservations() {
echo $(scontrol -o show reservations | cut -d' ' -f1 | cut -d= -f2) ; 
}
function _gres() {
echo $(scontrol show config | grep GresTypes | cut -d= -f2)
}
function _jobname() {
echo $(scontrol show -o jobs | cut -d' ' -f 2 | sed 's/Name=//') 
}

_sacctmgr()
{
    _get_comp_words_by_ref cur prev words cword
    _split_long_opt

    local subopts="" 
    local commands="add create delete dump list load modify show "
    local entities="account association cluster configuration coordinator\
            event job qos transaction user wckeys"
    
    local shortoptions="-h -i -n -p -P -Q -r -s -s -v -V"
    local longoptions="--help --immediate --noheader --parsable \
        --parsable2 --quiet --readonly --associations --verbose --version"


    local assocparams="clusters= accounts= users= partition= "
    local assocbasedparams="defaultqos= fairshare= grpcpumins= grpcpus= \
        grpjobs= grpnodes= grpsubmitjobs= grpwall= maxcpumins= maxcpus= maxjobs= \
        maxnodes= maxsubmitjobs= maxwall= qoslevel="

    # Check whether we are in the middle of an option. If so serve them. 
    remainings=$(compute_set_diff "$longoptions" "${COMP_WORDS[*]}")
    [[ $cur == - ]] && { offer "$shortoptions" ; return ; }
    [[ $cur == --* ]] && { offer "$remainings" ; return ; }

    # Search for a command in the argument list (first occurence)
    # the command might be in any position because of the options
    command=$(find_first_occurence "${COMP_WORDS[*]}" "$commands")
    
    # If no command has been entered, serve the list of valid commands
    [[ $command == "" ]] && { offer "$commands" ; return ; }

    # Load command has a specific syntax. Treat it first
    [[ $command == "load" ]] && { _filedir ; return ; }

    entity=$(find_first_occurence "${COMP_WORDS[*]}" "$entities")

    [[ $entity == "" ]] && { offer "$entities" ; return ; }

    case $command in
    add|create) 
        objects="account cluster coordinator qos user "
        object=$(find_first_occurence "${COMP_WORDS[*]}" "$objects")
        case $object in
        account)  
            params="cluster= description= name= organization= parent= "
            if param "cluster" ; then  offer_list "$(_clusters)" ; 
            elif param "parent" ; then  offer_list "$(_accounts)" ; 
            else offer "$params" 
            fi
            ;;
        cluster)
            params="classification= flags= name= rpc="
            if param "flags" ; then  offer_list "aix bgl bgq bluegene crayxt frontend \
                multipleslumd sunconstellation xcpu" ; 
            else offer "$params" 
            fi
            ;;
        coordinator)
            params="accounts= names="
            if param "names" ; then  offer_list "$(_users)" ; 
            elif param "accounts" ; then  offer_list "$(_accounts)" ; 
            else offer "$params" 
            fi
            ;;
        qos)  
            params="flags= grpcpumins= grpcpus= grpjobs= grpnodes= grpsubmitjobs= grpwall= \
                maxcpumins= maxcpus= maxjobs= maxsubmitjobs= maxwall= name= preempt= \
                preemptmode= priority= usagefactor= usagethreshold= "
            if param "flags" ; then  offer_list "EnforceUsageThreshold NoReserve \
                                            PartitionMaxNodes PartitionMinNodes PartitionTimeLimit" ; 
            elif param "preemptmode" ; then  offer_list "cluster cancel checkpoint requeue suspend" ; 
            elif param "flags" ; then  offer_list "enforceusagethreshold noreserve \
                parittionmaxnodes partitionminnodes partitiontimelimit" ; 
            elif param "preempt" ; then  offer_list "$(_qos)" ; 
            else offer "$params" 
            fi
            ;;
        user)  
            params="account= adminlevel= cluster= defaultaccount= defaultwckey= name= "
            if param "defaultaccount" ; then  offer_list "$(_accounts)" ; 
            elif param "account" ; then  offer_list "$(_accounts)"; 
            elif param "adminlevel" ; then  offer_list "none operator admin" ; 
            elif param "cluster" ; then  offer_list "$(_cluster)" ; 
            elif param "defaultwckey" ; then  offer_list "$(_wckey)" ; 
            else offer "$params" 
            fi
            ;;
        *) offer "$objects" ;;
        esac
        ;;
    delete)
        objects="account cluster coordinator qos user"
        object=$(find_first_occurence "${COMP_WORDS[*]}" "$objects")
        case $object in
        account)  
            if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ; return ;fi
            params="cluster= description= name= organization= parent="
            if param "cluster" ; then  offer_list "$(_clusters)" ; 
            elif param "parent" ; then  offer_list "$(_accounts)" ; 
            elif param "name" ; then  offer_list "$(_accounts)" ; 
            else offer "$params" 
            fi
            ;;
        cluster)
            if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ; return ;fi
            params="classification= flags= name= rpc= $assocbasedparams"
            if param "flags" ; then  offer_list "aix bgl bgq bluegene crayxt frontend \
                multipleslumd sunconstellation xcpu" ; 
            elif param "defaultqos" ; then  offer_list "$(_qos)" ; 
            else offer "$params" 
            fi
            ;;
        coordinator)
            if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ; return ;fi
            params="accounts= names="
            if param "names" ; then  offer_list "$(_users)" ; 
            elif param "accounts" ; then  offer_list "$(_accounts)" ; 
            else offer "$params" 
            fi
            ;;
        user)
            params="account= adminlevel= cluster= defaultaccount= defaultwckey= name= wckeys= withassoc"
            if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ; return ;fi
            if param "defaultaccount" ; then  offer_list "$(_accounts)" ; 
            elif param "account" ; then  offer_list "$(_accounts)"; 
            elif param "adminlevel" ; then  offer_list "none operator admin" ; 
            elif param "cluster" ; then  offer_list "$(_cluster)" ; 
            elif param "wckeys" ; then  offer_list "$(_wckeys)" ; 
            elif param "defaultwckey" ; then  offer_list "$(_wckey)" ; 
            else offer "$params" ;
            fi
            ;;
        *) offer "$objects" 
            ;;
        esac
        ;;
    list|show)
        objects="account association cluster configuration \
            event problem qos transaction user wckey"
        object=$(find_first_occurence "${COMP_WORDS[*]}" "$objects")
        case $object in
        account)  
            if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ; return ;fi
            params="cluster= description= name= organization= parent= withassoc \ 
                withcoord withdeleted $assocparams $assocbasedparams"
            if param "cluster" ; then  offer_list "$(_clusters)" ; 
            elif param "parent" ; then  offer_list "$(_accounts)" ; 
            elif param "users" ; then  offer_list "$(_users)" ; 
            elif param "partition" ; then  offer_list "$(_partition)" ; 
            elif param "defaultqos" ; then  offer_list "$(_qos)" ; 
            elif param "name" ; then  offer_list "$(_accounts)" ; 
            else offer "$params" 
            fi
            ;;
        association) 
            if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ; return ;fi
            params="$assocparams onlydefaults tree withdeleted withsubaccounts \
                wolimits wopinfo woplimits"
            if param "clusters" ; then  offer_list "$(_clusters)" ; 
            elif param "accounts" ; then  offer_list "$(_accounts)" ; 
            elif param "users" ; then  offer_list "$(_users)" ; 
            elif param "partition" ; then  offer_list "$(_partitions)" ; 
            else offer "$params" 
            fi
            ;;
        cluster)
            if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ; return ;fi
            params="classification= flags= name= rpc= $assocbasedparams"
            if param "flags" ; then  offer_list "aix bgl bgq bluegene crayxt frontend \
                multipleslumd sunconstellation xcpu" ; 
            elif param "defaultqos" ; then  offer_list "$(_qos)" ; 
            else offer "$params" 
            fi
            ;;
        event)
            if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ; return ;fi
            params="all_clusters all_time clusters= end= event= maxcpu= mincpus= \
                nodes= reason= start= states= user= "
            if param "clusters" ; then  offer_list "$(_clusters)" ; 
            elif param "nodes" ; then  offer_list "$(_nodes)" ; 
            elif param "event" ; then  offer_list "cluster node" ; 
            elif param "states" ; then  offer_list "alloc allocated down drain \
                        fail failing idle mixed maint power_down power_up resume" ; 
            elif param "users" ; then  offer_list "$(_users)" ; 
            else offer "$params" 
            fi
            ;;
        qos)  
            if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ; return ;fi
            params="flags= grpcpumins= grpcpus= grpjobs= grpnodes= grpsubmitjobs= grpwall= \
                maxcpumins= maxcpus= maxjobs= maxsubmitjobs= maxwall= name= preempt= \
                preemptmode= priority= usagefactor= usagethreshold= withdeleted"
            if param "flags" ; then  offer_list "EnforceUsageThreshold NoReserve \
                                            PartitionMaxNodes PartitionMinNodes PartitionTimeLimit" ; 
            elif param "preemptmode" ; then  offer_list "cluster cancel checkpoint requeue suspend" ; 
            elif param "flags" ; then  offer_list "enforceusagethreshold noreserve \
                parittionmaxnodes partitionminnodes partitiontimelimit" ; 
            elif param "preempt" ; then  offer_list "$(_qos)" ; 
            else offer "$params" 
            fi
            ;;
        transaction)
            if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ; return ;fi
            params="accounts= action= actor= clusters= endtime= startime= users= withassoc"
            if param "accounts" ; then  offer_list "$(_accounts)" ; 
            elif param "actor" ; then  offer_list "$(_users)" ; 
            elif param "clusters" ; then  offer_list "$(_clusters)" ; 
            else offer "$params" 
            fi
            ;;
        user)  
            if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ; return ;fi
            params="account= adminlevel= cluster= defaultaccount= defaultwckey= name= wckeys= withassoc"
            if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ; return ;fi
            if param "defaultaccount" ; then  offer_list "$(_accounts)" ; 
            elif param "account" ; then  offer_list "$(_accounts)"; 
            elif param "adminlevel" ; then  offer_list "none operator admin" ; 
            elif param "cluster" ; then  offer_list "$(_cluster)" ; 
            elif param "wckeys" ; then  offer_list "$(_wckeys)" ; 
            elif param "defaultwckey" ; then  offer_list "$(_wckey)" ; 
            else offer "$params" ;
            fi
            ;;
        *) offer "$objects" ;;
        esac
        ;;
    modify)
        objects="account cluster job qos user"
        object=$(find_first_occurence "${COMP_WORDS[*]}" "$objects")
        case $object in
        account)
            if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ; return ;fi
            params="cluster= description= name= organization= parent="
            if param "cluster" ; then  offer_list "$(_clusters)" ; 
            elif param "parent" ; then  offer_list "$(_accounts)" ; 
            elif param "name" ; then  offer_list "$(_accounts)" ; 
            else offer "$params set" 
            fi
            ;;
        cluster)
            if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ; return ;fi
            params="classification= flags= name= rpc= $assocbasedparams"
            if param "flags" ; then  offer_list "aix bgl bgq bluegene crayxt frontend \
                multipleslumd sunconstellation xcpu" ; 
            elif param "defaultqos" ; then  offer_list "$(_qos)" ; 
            else offer "$params set" 
            fi
            ;;
        qos)
            if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ; return ;fi
            params="flags= grpcpumins= grpcpus= grpjobs= grpnodes= grpsubmitjobs= grpwall= \
                maxcpumins= maxcpus= maxjobs= maxsubmitjobs= maxwall= name= preempt= \
                preemptmode= priority= usagefactor= usagethreshold= withdeleted"
            if param "flags" ; then  offer_list "EnforceUsageThreshold NoReserve \
                                            PartitionMaxNodes PartitionMinNodes PartitionTimeLimit" ; 
            elif param "preemptmode" ; then  offer_list "cluster cancel checkpoint requeue suspend" ; 
            elif param "flags" ; then  offer_list "enforceusagethreshold noreserve \
                parittionmaxnodes partitionminnodes partitiontimelimit" ; 
            elif param "preempt" ; then  offer_list "$(_qos)" ; 
            else offer "$params set" 
            fi
            ;;
        user)
            if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ; return ;fi
            params="account= adminlevel= cluster= defaultaccount= defaultwckey= name= wckeys= withassoc"
            if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ; return ;fi
            if param "defaultaccount" ; then  offer_list "$(_accounts)" ; 
            elif param "account" ; then  offer_list "$(_accounts)"; 
            elif param "adminlevel" ; then  offer_list "none operator admin" ; 
            elif param "cluster" ; then  offer_list "$(_cluster)" ; 
            elif param "wckeys" ; then  offer_list "$(_wckeys)" ; 
            elif param "defaultwckey" ; then  offer_list "$(_wckey)" ; 
            else offer "$params" ;
            fi
            ;;
        *) offer "$objects" 
            ;;
        esac
        ;;
            
    esac
}
complete -F _sacctmgr sacctmgr

_sreport()
{
    _get_comp_words_by_ref cur prev words cword
    _split_long_opt

    local subopts="" 
    local commands="cluster job user reservation"
    
    local shortoptions="-a -n -h -p -P -t -v -V"
    local longoptions="--all_clusters --help --noheader --parsable\
                        --parsable2--quiet --verbose --version"

    # Check whether we are in the middle of an option. If so serve them. 
    remainings=$(compute_set_diff "$longoptions" "${COMP_WORDS[*]}")
    [[ $cur == - ]] && { offer "$shortoptions" ; return ; }
    [[ $cur == --* ]] && { offer "$remainings" ; return ; }

    # Search for a command in the argument list (first occurence)
    # the command might be in any position because of the options
    command=$(find_first_occurence "${COMP_WORDS[*]}" "$commands")
    
    # If no command has been entered, serve the list of valid commands
    [[ $command == "" ]] && { offer "$commands" ; return ; }

    opts_all="All_Clusters Clusters= End= Format= Start="

    case $command in
    user) 
        objects="TopUsage"
        object=$(find_first_occurence "${COMP_WORDS[*]}" "$objects")
        case $object in
        TopUsage)  
            params="$opts_all Accounts= Group TopCount= Users="
            if param "Clusters" ; then  offer_list "$(_clusters)" ; 
            elif param "Format" ; then  offer_list "Account Cluster Login Proper User" ; 
            elif param "Accounts" ; then  offer_list "$(_accounts)" ; 
            elif param "Users" ; then  offer_list "$(_users)" ; 
            else offer "$params" 
            fi
            ;;
        *) offer "$objects" ;;
        esac
        ;;
    reservation) 
        objects="Utilization"
        object=$(find_first_occurence "${COMP_WORDS[*]}" "$objects")
        case $object in
        Utilization)  
            params="$opts_all Names= Nodes="
            if param "Clusters" ; then  offer_list "$(_clusters)" ; 
            elif param "Format" ; then  offer_list "Allocated Associations \
                Clusters CPUCount CPUTime End Idle Name Nodes Start TotalTime" ;  
            elif param "Nodes" ; then  offer_list "$(_nodes)" ; 
            else offer "$params" 
            fi
            ;;
        *) offer "$objects" ;;
        esac
        ;;
    job) 
        objects="SizesByAccount SizesByAccountAndWckey SizesByWckey" 
        object=$(find_first_occurence "${COMP_WORDS[*]}" "$objects")
        case $object in
        SizesByAccount|SizesByAccountAndWckey)  
            params="$opts_all Accounts= FlatView GID= Grouping= \
                Jobs= Nodes= OPartitions= PrintJobCount Users= Wckeys="
            if param "Clusters" ; then  offer_list "$(_clusters)" ; 
            elif param "Format" ; then  offer_list "Account Cluster" ;  
            elif param "Accounts" ; then  offer_list "$(_accounts)" ; 
            elif param "GID" ; then  _gids ; 
            elif param "Users" ; then  offer_list "$(_users)" ; 
            elif param "Wckeys" ; then  offer_list "$(_wckeys)" ; 
            else offer "$params" 
            fi
            ;;
        SizesByWckey)  
            params="$opts_all Accounts= FlatView GID= Grouping= \
                Jobs= Nodes= OPartitions= PrintJobCount Users= Wckeys="
            if param "Clusters" ; then  offer_list "$(_clusters)" ; 
            elif param "Format" ; then  offer_list "Wckey Cluster" ; 
            elif param "Accounts" ; then  offer_list "$(_accounts)" ; 
            elif param "GID" ; then  _gids ; 
            elif param "Users" ; then  offer_list "$(_users)" ; 
            elif param "Wckeys" ; then  offer_list "$(_wckeys)" ; 
            else offer "$params" 
            fi
            ;;
        *) offer "$objects" ;;
        esac
        ;;
    cluster) 
        objects="AccountUtilizationByUser UserUtilizationByAccount \
                    UserUtilizationByWCKey Utilization WCKeyUtilizationByUser"
        object=$(find_first_occurence "${COMP_WORDS[*]}" "$objects")
        case $object in
        Utilization)
            params="$opts_all Names= Nodes="
            if param "Clusters" ; then  offer_list "$(_clusters)" ; 
            elif param "Format" ; then  offer_list "Allocated Cluster \
                CPUCount Down Idle Overcommited PlannedDown Reported Reserved" ;
            elif param "Nodes" ; then  offer_list "$(_nodes)" ; 
            else offer "$params" 
            fi
            ;;
        AccountUtilizationByUser|UserUtilizationByAccount)
            params="$opts_all Accounts= Tree Users= Wckeys="
            if param "Clusters" ; then  offer_list "$(_clusters)" ; 
            elif param "Format" ; then  offer_list "Accounts Cluster CPUCount \
                                                    Login Proper Used" ;
            elif param "Accounts" ; then  offer_list "$(_accounts)" ; 
            elif param "Users" ; then  offer_list "$(_users)" ; 
            elif param "Wckeys" ; then  offer_list "$(_wckeys)" ; 
            else offer "$params" 
            fi
            ;;
        UserUtilizationByWCKey|WCKeyUtilizationByUser)
            params="$opts_all Accounts= Tree Users= Wckeys="
            if param "Clusters" ; then  offer_list "$(_clusters)" ; 
            elif param "Format" ; then  offer_list "Cluster CPUCount Login \
                                                    Proper Used Wckey" ;  
            elif param "Accounts" ; then  offer_list "$(_accounts)" ; 
            elif param "Users" ; then  offer_list "$(_users)" ; 
            elif param "Wckeys" ; then  offer_list "$(_wckeys)" ; 
            else offer "$params" 
            fi
            ;;
        *) offer "$objects" ;;
        esac
        ;;
            
    esac
}
complete -F _sreport sreport

_scontrol()
{
    local cur=${COMP_WORDS[COMP_CWORD]}
    local prev=${COMP_WORDS[COMP_CWORD-1]}
    
    local commands="abort checkpoint create completing delete hold notify \
                    pidinfo listpids ping reconfigure release requeue resume\
                    setdebug show shutdown suspend takeover uhold update version"
    
    local shortoptions="-a -d -h -M -o -Q -v -V "
    local longoptions="--all --details --help --hide --cluster --oneliner \
                        --quiet --verbose --version"
    
    # Check whether we are in the middle of an option. If so serve them. 
    remainings=$(compute_set_diff "$longoptions" "${COMP_WORDS[*]}")
    [[ $cur == - ]] && { offer "$shortoptions" ; return ; }
    [[ $cur == -- ]] && { offer "$remainings" ; return ; }
    [[ $cur == --* ]] && { offer "$(sed 's/<[^>]*>//g' <<< $remainings)"; return ; }
    
    # Search for a command in the argument list (first occurence)
    # the command might be in any position because of the options
    command=$(find_first_occurence "${COMP_WORDS[*]}" "$commands")
    
    # If no command has been entered, serve the list of valid commands
    [[ $command == "" ]] && { offer "$commands" ; return ; }
    
    # Otherwise process command
    case $command in 
    shutdown) # scontrol shutdown object
        offer "slurmctld controller" 
        ;;
    setdebug) # scontrol setdebug value
        offer "quiet info warning error debug debug2 debug3 debug4 debug5 " # FIXME
        ;;
    uhold | suspend | release | requeue | resume | hold ) 
        offer "$(_jobs)" 
        ;; #TODO notify
    checkpoint) # scontrol checkpoint create jobid [parameter1=value1,...]
        # This one has unsusual ordering: object is before command. 
        # command subcommand argument #TODO add support for additional options cfr manpage
        objects="disable enable able create vacate error restart"
    
        if [[ $prev == checkpoint ]]; then 
            offer "$objects"; 
        elif [[ $objects == *$prev* ]]; then 
            offer "$(_jobs)"; 
        else 
            echo todo
            #TODO
        fi
        ;;
    show) # scontrol show object [id]
        objects="config daemons job nodes partitions reservations \
                 slurmd steps topology hostlist hostnames"
    
        # Search for the current object in the argument list
        object=$(find_first_occurence "${COMP_WORDS[*]}" "$objects")
    
        # If no object has yet been (fully) typed in, serve the list of objects
        [[ $object == "" ]] && { offer "$objects" ; return ; }
    
        # Otherwise, offer the ids depending on the object
        if param "job"          ; then offer  "$(_jobs)"         ; fi
        if param "nodes"        ; then offer_list "$(_nodes)"        ; fi
        if param "partitions"   ; then offer "$(_partitions)"   ; fi
        if param "reservations" ; then offer "$(_reservations)"  ; fi
        #TODO if object "steps"    
        ;;
    delete) # scontrol delete objectname=id
        parameters="partitionname= reservationname="
    
        # If a parameter has been fully typed in, serve the corresponding
        # values, otherwise, serve the list of parameters.
        if   param "partitionname"   ; then offer_many "$(_partitions)"  
        elif param "reservationname" ; then offer_many "$(_reservations)" 
        else offer "$parameters" ; fi
        ;;
    update) 
        parameters="jobid= step= nodename= partitionname= reservationname="
    
        param=$(find_first_partial_occurence "${COMP_WORDS[*]}" "$parameters")
        [[ $param == "" ]] && { offer "$parameters" ; return ; }
    
        # If a parameter has been fully typed in, serve the corresponding
        # values, if it is the first one. 
        if   param "jobid"   ; then offer_many "$(_jobs)" ; return  
        elif param "nodename" ; then offer_many "$(_nodes)"  ; return
        elif param "partitionname" ; then offer_many "$(_partitions)" ; return 
        elif param "reservationname" ; then offer_many "$(_reservations)"  ; return
        fi
    
        # Otherwise, process the others based on the first one
        case $param in
        jobid)
            local parameters="account=<account> conn-type=<type> \
              contiguous=<yes|no> dependency=<dependency_list> \
              eligibletime=yyyy-mm-dd excnodelist=<nodes>\
              features=<features> geometry=<geo> gres=<list> \
              licenses=<name> mincpusnode=<count> minmemorycpu=<megabytes> \
              mintmpdisknode=<megabytes> name=<name> nice[=delta] \
              nodelist=<nodes> numcpus=<min_count[-max_count] \
              numnodes=<min_count[-max_count]> numtasks=<count> \
              partition=<name> priority=<number> qos=<name> \
              reqcores=<count> reqthreads=<count> requeue=<0|1> \
              reservationname=<name> rotate=<yes|no> shared=<yes|no> \
              starttime=yyyy-mm-dd timelimit=[d-]h:m:s wckey=<key>"
    
            remainings=$(compute_set_diff "$parameters" "${COMP_WORDS[*]}")
    
            # If a new named argument is about to be entered, serve the list of options
            [[ $cur == "" && $prev != "=" ]] && { offer "$remainings" ; return ; }
    
            # Test all potential arguments and server corresponding values
            if   param "account"         ; then offer_many "$(_accounts)"
            elif param "excnodelist"     ; then offer_many "$(_nodes)"
            elif param "nodelist"        ; then offer_many "$(_nodes)"
            elif param "features"        ; then offer_many "$(_features)"
            elif param "gres"            ; then offer_many "$(_gres)"
            elif param "licences"        ; then offer_many "$(_licenses)"
            elif param "partition"       ; then offer_many "$(_partitions)"
            elif param "reservationname" ; then offer_many "$(_reservations)"
            elif param "qos"             ; then offer_many "$(_qos)"
            elif param "wckey"           ; then offer_many "$(wckeys)"
            elif param "conn-type"       ; then offer_many "MESH TORUS NAV"
            elif param "rotate"          ; then offer_many "yes no"
            elif param "shared"          ; then offer_many "yes no"
            else offer "$(sed 's/\=[^ ]*/\=/g' <<< $remainings)"
            fi
            ;;
        nodename)  
            local parameters="features=<features> gres=<gres> \
               reason=<reason> state=<state> weight=<weight>"
    
            remainings=$(compute_set_diff "$parameters" "${COMP_WORDS[*]}")
    
            # If a new named argument is about to be entered, serve the list of options
            [[ $cur == "" && $prev != "=" ]] && { offer "$remainings" ; return ; }
    
            # Test all potential arguments and server corresponding values
            if param "features"   ; then offer_many "$(_features)"
            elif param "gres"     ; then offer_many "$(_gres)"
            elif param "state"    ; then offer_many "alloc allocated down drain \
                        fail failing idle mixed maint power_down power_up resume"
            else offer "$(sed 's/\=[^ ]*/\=/g' <<< $remainings)"
            fi
            ;;
        partitionname) 
            local parameters="nodes=<node_list> alternate=<partition_name> default=yes|no 
              defaulttime=d-h:m:s|unlimited disablerootjobs=yes|no hidden=yes|no \
              maxnodes=<count> maxtime=d-h:m:s|unlimited minnodes=<count> \
              allocnodes=<node_list>  preemptmode=off|cancel|checkpoint|requeue|suspend \
              priority=count rootonly=yes|no shared=yes|no|exclusive|force \
              state=up|down|drain|inactive allowgroups=<name>" 
    
            remainings=$(compute_set_diff "$parameters" "${COMP_WORDS[*]}")
            # If a new named argument is about to be entered, serve the list of options
            [[ $cur == "" && $prev != "=" ]] && { offer "$remainings" ; return ; }
    
            # Test all potential arguments and server corresponding values
            if   param "allocnodes"  ; then offer_many "$(_nodes)"
            elif param "alternate"   ; then offer_many "$(_partitions)"
            elif param "default"     ; then offer_many  "yes no"
            elif param "preemptmode" ; then offer_many "off cancel checkpoint requeue suspend"
            elif param "shared"      ; then offer_many "yes no exclusive force"
            elif param "state"       ; then offer_many "up down drain inactive"
            else offer "$(sed 's/\=[^ ]*/\=/g' <<< $remainings)"
            fi
            ;;
        reservationname)
            local parameters="users=<user_list> nodecnt=<count> \
              nodes=<node_list> starttime=yyyy-mm-dd[thh:mm[:ss]] \
              endtime=yyyy-mm-dd[thh:mm[:ss]] duration=[days-]hours:minutes:seconds \
              flags=maint,overlap,ignore_jobs,daily,weekly \
              partitionname=<partition_list> features=<feature_list> \
              accounts=<account_list> licenses=<license>"
    
            remainings=$(compute_set_diff "$parameters" "${COMP_WORDS[*]}")
            # If a new named argument is about to be entered, serve the list of options
            [[ $cur == "" && $prev != "=" ]] && { offer "$remainings" ; return ; }
            
            # test all potential arguments and server corresponding values
            if   param "accounts" ; then offer_many  "$(_accounts)" 
            elif param "licences" ; then offer_many "$(_licenses)"
            elif param "nodes"    ; then offer_many "$(_nodes)"
            elif param "features" ; then offer_many "$(_features)"
            elif param "users"    ; then offer_many "$(_users)"
            elif param "flags"    ; then offer_many " maint overlap ignore_jobs daily weekly "
            else offer "$(sed 's/\=[^ ]*/\=/g' <<< $remainings)"
            fi
            ;;
        esac
        ;;
    create) # command object attribute1=value1 etc.
        parameters="partition reservation"
    
        param=$(find_first_occurence "${COMP_WORDS[*]}" "$parameters")
        [[ $param == "" ]] && { offer "$parameters" ; return ; }
    
        # Process object
        case $param in
        partition)
            local parameters="partitionname=<name> nodes=<node_list> \
              alternate=<partition_name> default=yes|no \
              defaulttime=days-hours:minutes:seconds|unlimited \
              disablerootjobs=yes|no hidden=yes|no maxnodes=<count> \
              maxtime=days-hours:minutes:seconds|unlimited minnodes=<count> \
              allocnodes=<node_list>  \
              preemptmode=off|cancel|checkpoint|requeue|suspend \
              priority=count rootonly=yes|no shared=yes|no|exclusive|force \
              state=up|down|drain|inactive allowgroups=<name>" 
    
            remainings=$(compute_set_diff "$parameters" "${COMP_WORDS[*]}")
            # If a new named argument is about to be entered, serve the list of options
            [[ $cur == "" && $prev != "=" ]] && { offer "$remainings" ; return ; }
            
            if   param "allocnodes" ; then offer_many  "$(_nodes)" 
            elif param "alternate" ; then offer_many "$(_partitions)"
            elif param "partitionname" ; then offer_many "$(_partitions)"
            elif param "nodes"    ; then offer_many "$(_nodes)"
            elif param "preemptmode" ; then offer_many "off cancel checkpoint requeue suspend"
            elif param "shared" ; then offer_many "yes no exclusive force"
            else offer "$(sed 's/\=[^ ]*/\=/g' <<< $remainings)"
            fi
            ;;
        reservation)
            local parameters="reservation=<name> users=<user_list> nodecnt=<count> \
              nodes=<node_list> starttime=yyyy-mm-dd[thh:mm[:ss]] \
              endtime=yyyy-mm-dd[thh:mm[:ss]] duration=[days-]hours:minutes:seconds \
              flags=maint,overlap,ignore_jobs,daily,weekly \
              partitionname=<partition_list> features=<feature_list> \
              accounts=<account_list> licenses=<license>"
    
            remainings=$(compute_set_diff "$parameters" "${COMP_WORDS[*]}")
            # If a new named argument is about to be entered, serve the list of options
            [[ $cur == "" && $prev != "=" ]] && { offer "$remainings" ; return ; }
            
            # Test all potential arguments and server corresponding values
            if   param "accounts" ; then offer  "$(_accounts)" 
            elif param "licences" ; then offer_many "$(_licenses)"
            elif param "nodes"    ; then offer_many "$(_nodes)"
            elif param "features" ; then offer_many "$(_features)"
            elif param "users"    ; then offer_many "$(_users)"
            elif param "flags"    ; then offer_many " maint overlap ignore_jobs daily weekly "
            else offer "$(sed 's/\=[^ ]*/\=/g' <<< $remainings)"
            fi
            ;;
        esac
        ;;
    esac
}
complete -F _scontrol scontrol

_squeue()
{
    _get_comp_words_by_ref cur prev words cword
    _split_long_opt
    
    local shortoptions="-A -i -j -M -n -o -p -q -s -S -t -u -a -h -l -s -V -v"
    local longoptions="--help --hide --steps --start --usage --verbose \
            --version --noheader --account<account_list> \
            --iterate<seconds> --jobs<job_id_list> \
            --clusters<string> --nodes<hostlist> --format<fmtstring> \
            --partition<part_list> --qos<qos_list> --sort<sort_list> \
            --state<state_list> --user<user_list> "
    
    [[ $cur == - ]] && { offer "$shortoptions" ; return ; }
    [[ $cur == -- ]] && { offer "$longoptions" ; return ; }
    [[ $cur == --* ]] && { offer "$(sed 's/<[^>]*>//g' <<< $longoptions)"; return ; }

    if [[ $cur == *% ]] ; 
    then 
        offer "%a(Account) %A(NTasks) %b(gres) %c(mincpu) %C(Ncpus) %d(minTmp) \
            %D(NNodes) %e(end) %E(dependency) %f(features) %g(group) %G(gID) %h(shared) \
            %H(Nsockets) %i(id) %I(Ncores/socket) %j(name) %k(comment) %l(limit) \
            %L(timeleft) %m(mem) %M(time) %n(reqnodes) %N(alloc_nodes) %O(contiguous) \
            %p(priority) %r(reason)  %R(reason) %s(selecplugin) %t(state) %T(state) \
            %u(user) %U(uID) %v(reservation) %x(excnodes)" ; 
        return; 
    fi

    case $prev in 
    --partition|-p) offer_list "$(_partitions)" ;;
    --jobs|-j) offer_list "$(_jobs)" ;;
    --account|-A) offer_list "$(_accounts)" ;;
    --clusters|-M) offer_list "$(_clusters)" ;;
    --nodes|-N) offer_list "$(_nodes)" ;;
    --qos) offer_list "$(_qos)" ;;
    --user|-u) offer_list "$(_users)" ;;
    --state|-s) offer_list "pending running suspended completing completed" ;;
    --format|-o) offer "\\\"%" ;;
    esac
}
complete -F _squeue squeue

_scancel()
{
    _get_comp_words_by_ref cur prev words cword
    _split_long_opt
    
    local shortoptions=" -a -i -v -V -A -b -M -n -p -q -R -s -t -u -w"
    local longoptions="--batch --ctld --help --quiet --usage --verbose --version \
            --account<account_list>  --name<job_name> \
            --clusters<string> --nodelist<hostlist> --reservation<reservation_name>\
            --partition<part_list> --qos<qos_list> --signal<SIGXXX>\
            --state<state_list> --user<user_list> --wckeys<wckey>"
    
    [[ $cur == - ]] && { offer "$shortoptions" ; return ; }
    [[ $cur == -- ]] && { offer "$longoptions" ; return ; }
    [[ $cur == --* ]] && { offer "$(sed 's/<[^>]*>//g' <<< $longoptions)"; return ; }

    case $prev in
    --partition|-p) offer_list "$(_partitions)" ;;
    --account|-A) offer_list "$(_accounts)" ;;
    --clusters|-M) offer_list "$(_clusters)" ;;
    --qos) offer_list "$(_qos)" ;;
    --wckeys) offer_list "$(_wckeys)" ;;
    --user|-u) offer_list "$(_users)" ;;
    --nodelist|-w) offer_list "$(_nodes)" ;;
    --name) offer_list "$(_jobnames)" ;;
    --reservation|-R) offer_list "$(_reservations)" ;;
    --state) offer_list "pending running suspended completing completed" ;;
    *) offer_list "$(_jobs)";;
    esac
}
complete -F _scancel scancel

_sshare()
{
    _get_comp_words_by_ref cur prev words cword
    _split_long_opt
    
    local shortoptions="-a -h -l -p -P -v -V -A -M -u"
    local longoptions="--noheader --parsable --parsable2 --verbose --version \
                --accounts<accounts> --clusters<string> --users<user_list>"
    
    [[ $cur == - ]] && { offer "$shortoptions" ; return ; }
    [[ $cur == -- ]] && { offer "$longoptions" ; return ; }
    [[ $cur == --* ]] && { offer "$(sed 's/<[^>]*>//g' <<< $longoptions)"; return ; }

    case $prev in
    --accounts|-A) offer_list "$(_accounts)" ;;
    --clusters|-M) offer_list "$(_clusters)" ;;
    --users|-u) offer_list "$(_users)" ;;
    esac
}
complete -F _sshare sshare

_sbcast()
{
    _get_comp_words_by_ref cur prev words cword
    _split_long_opt
    
    local shortoptions="-C -f -p -v -V -F -s -t"
    local longoptions="--compress --force --preserve --verbose --version \
                    fanout<number> --size<bytes> --timeout<seconds>"
    
    [[ $cur == - ]] && { offer "$shortoptions" ; return ; }
    [[ $cur == -- ]] && { offer "$longoptions" ; return ; }
    [[ $cur == --* ]] && { offer "$(sed 's/<[^>]*>//g' <<< $longoptions)"; return ; }

    _filedir
}
complete -F _sbcast sbcast

_sinfo()
{
    _get_comp_words_by_ref cur prev words cword
    _split_long_opt
    
    local shortoptions="-a -b -d -e -h -i -l -n -N -o -p -R -s -S -t -v -V"
    local longoptions="--all --exact --noheader --help --hide --iterate<seconds> \
            --long --clusters<clusternames> --nodes<nodelist> --Node --format<fmtstr> \
            --partition<partition> --summarize --sort<sortlist> --states<statelist> \
            --usage --verbose --version"
    
    [[ $cur == - ]] && { offer "$shortoptions" ; return ; }
    [[ $cur == -- ]] && { offer "$longoptions" ; return ; }
    [[ $cur == --* ]] && { offer "$(sed 's/<[^>]*>//g' <<< $longoptions)"; return ; }

    if [[ $cur == *% ]] ; 
    then 
        offer "%a(Availabilit) %A(cpu_usage) %c(cpus_per_node) %C(cpu_usage) %d(diskspace) \
            %D(NNodes) %E(reason) %f(features) %F(nodes_usage) %g(group) %G(Gres) %h(shared) \
            %H(timestamp) %l(time_limit)  %L(default_time) %m(mem) %M(preemt_mode) \
            %N(node_names) %P(partition)  %r(root_jobs)  %R(reason) %s(max_job_size) \
            %S(allowed_allocating_nodes) %t(state) %T(state) %u(user) %U(uID) %w(weight)\
            %X(sockets_per_node) %Y(cores_per_socket) %Z(threads_per_core)" ; 
        return; 
    fi

    case $prev in 
    --partition|-p) offer_list "$(_partitions)" ;;
    --clusters|-M) offer_list "$(_clusters)" ;;
    --nodes|-n) offer_list "$(_nodes)" ;;
    --state) offer_list "pending running suspended completing completed" ;;
    --format|-o) offer "\\\"%" ;;
    esac
}
complete -F _sinfo sinfo

_sprio()
{
    _get_comp_words_by_ref cur prev words cword
    _split_long_opt
    
    local shortoptions="-h -j -l -M -n -o -u -v -V -w"
    local longoptions="--noheader --help --job<jobids> --long --clusters<clustername> \
            --norm --format<fmtstr> --user<userlist> --usage --verbose --version --weights"
    
    [[ $cur == - ]] && { offer "$shortoptions" ; return ; }
    [[ $cur == -- ]] && { offer "$longoptions" ; return ; }
    [[ $cur == --* ]] && { offer "$(sed 's/<[^>]*>//g' <<< $longoptions)"; return ; }

    if [[ $cur == *% ]] ; 
    then 
        offer "%a(n_age) %A(w_age) %f(n_fair-share) %F(w_fair-share) %i(JobId) \
            %j(n_job_size) %J(w_job_size) %N(Nice adjustmen) %p(n_partition) \
            %P(w_partition) %q(n_qos) %Q(w_qos) %u(User) %Y(priority) %y(n_priority) " ; 
        return; 
    fi

    case $prev in 
    --jobs|-j) offer_list "$(_jobs)" ;;
    --clusters|-M) offer_list "$(_clusters)" ;;
    --format|-o) offer "\\\"%" ;;
    --user|-u) offer_list "$(_users)" ;;
    esac
}
complete -F _sprio sprio

_sacct()
{
    _get_comp_words_by_ref cur prev words cword
    _split_long_opt
    
    local shortoptions="-a -A -b -c -d -e -E -f -g -h -j -k -K -l -L -M -n \
                        -N -o -O -p -P -q -r -s -S -T -u -v -V -W -x -X"
    local longoptions="--allusers --accounts<accountlist> --brief --completion \
        --dump --duplicates --helpformat --endtime<time> --file<path> --group<gidlist> \
        --help -- jobs<joblist> --timelimit-min<time> --timelimit-max<time> --long \
        --allclusters --clusters<clusterlist> --noheader --nodes<nodes> \
        --format<itemlist> --formatted_dump --parsable --parsable2 --qos<qos> \
        --partition<partitionlist> --state<statelist> --starttime<time> --truncate \
        --user<userlist> --usage --verbose --version --wckeys<wckeyslist> \
        --associations<assoclist> --allocations"
    
    [[ $cur == - ]] && { offer "$shortoptions" ; return ; }
    [[ $cur == -- ]] && { offer "$longoptions" ; return ; }
    [[ $cur == --* ]] && { offer "$(sed 's/<[^>]*>//g' <<< $longoptions)"; return ; }

    case $prev in 
    --group|--gid|-g) _gids ;;
    --partition) offer_list "$(_partitions)" ;;
    --jobs) offer_list "$(_jobs)" ;;
    --accounts|-A) offer_list "$(_accounts)" ;;
    --clusters|-M) offer_list "$(_clusters)" ;;
    --nodes) offer_list "$(_nodes)" ;;
    --qos) offer_list "$(_qos)" ;;
    --wckeys|-W) offer_list "$(_wckeys)" ;;
    --associations|-x) offer_list "$(_associations)" ;;
    --user|-u) offer_list "$(_users)" ;;
    --state|-s) offer_list "pending running suspended completing completed" ;;
    --format) offer_list "$(sacct -e)" ;;
    esac
}
complete -F _sacct sacct

_salloc()
{
    _get_comp_words_by_ref cur prev words cword
    _split_long_opt
    
    local shortoptions="-A -B -C -c -d -D -F -h -H -I -J -K -l -L -m -N \
                        -n -O -Q -s -t -u -V -v -W -w -x"
    local longoptions="--account<account> --acctg-freq<seconds> \
        --extra-node-info<sockets[:cores[:threads]]> --sockets-per-node<number>\
        --cores-per-sopcket<number> --threads-per-core<number> --begin<time> --bell \
        --comment<string> --constraint<list> --contiguous --cpu-bind<type> \
        --cpus-per-task<number> --dependency<deplist> --chdir<path> --exclusive \
        --nodefile<nodefile> --get-user-env --gid<group> --gres<list> --hold \
        --help --hint<type> --immediate[<seconds>] --jobid<jobid> --killcommand \
        --no-kill --licenses<licenses> --distribution<dist> --mail-type<type> \
        --mail-user<email> --mem<MB> --mem-per-cpu<MB> --mem-bind<type> \
        --min-cpus<number> --nodes<minnodes[-maxnodes]> --ntasks<number> \
        --network<type> --nice<[adjustment]> --ntasks-per-core<number> \
        --no-bell --no-shell --overcommit --partition<partitionname> --quiet \
        --qos<qos> --reservation<name> --share --signal<sig_num>[@<sig_time>] \
        --time<time> --time-min<time> --tmp<MB> --usage --uid<user> --version \
        --verbose --wait<seconds> --nodelist<nodelist> --wait-all-nodes<0|1> \
        --wckey<wckey> --exclude<nodelist>"
   
    [[ $cur == - ]] && { offer "$shortoptions" ; return ; }
    [[ $cur == -- ]] && { offer "$longoptions" ; return ; }
    [[ $cur == --* ]] && { offer "$(sed 's/<[^>]*>//g' <<< $longoptions)"; return ; }

    case $prev in 
    --account|-A) offer_list "$(_accounts)" ;;
    --constraint|-C) offer_list "$(_features)" ;;
    --cpu-bind) offer_list "none rank map_cpu: mask_cpu: sockets cores \
                             threads ldoms" ;;
    --dependency) offer_list "after: afterany: afternotok: 
                             afterok: singleton" ;; 
    --gid) _gids ;; 
    --partition|-p) offer_list "$(_partitions)" ;;
    --gres) offer_list "$(_gres)" ;; 
    --hint) offer "compute_bound memory_bound multithread nomultithread" ;;
    --jobid) offer_list "$(_jobs)" ;;
    --licenses|-L) offer_list "$(_licenses)" ;;
    --distribution|-d) offer "block cyclic plane arbitrary" ;;
    --mail-type) offer_list "BEGIN END FAIL REQUEUE ALL" ;;
    --mem-bind) offer "none rank local map_mem: mask_mem:" ;;
    # TODO --network) _configured_interfaces ;; 
    --reservation) offer_list "$(_reservations)" ;;
    --clusters) offer_list "$(_clusters)" ;;
    --nodelist) offer_list "$(_nodes)" ;;
    --exclude) offer_list "$(_nodes)" ;;
    --qos) offer_list "$(_qos)" ;;
    :|afterany|after|afternotok|afterok) offer_list "$(_jobs)" ;;
    esac
    #TODO options for blue gene systems
}
complete -F _salloc salloc
    
_sbatch()
{
    _get_comp_words_by_ref cur prev words cword
    _split_long_opt

    local shortoptions="-A -B -C -c -d -D -F -h -H -I -J -K -l -L -m -N -n -O \
                        -Q -s -t -u -V -v -W -w -x -q -r -T -X -Z"
    local longoptions="--account<account> --acctg-freq<seconds> \
        --extra-node-info<sockets[:cores[:threads]]> --sockets-per-node<number> \
        --cores-per-sopcket<number> --threads-per-core<number> --begin<time> \
        --bell --comment<string> --constraint<list> --contiguous --cpu-bind<type> \
        --cpus-per-task<number> --dependency<deplist> --chdir<path> --exclusive \
        --nodefile<nodefile> --get-user-env --gid<group> --gres<list> --hold \
        --help --hint<type> --immediate[<seconds>] --jobid<jobid> --no-kill \
        --licenses<licenses> --distribution<dist> --mail-type<type> \
        --mail-user<email> --mem<MB> --mem-per-cpu<MB> --mem-bind<type> \
        --min-cpus<number> --nodes<minnodes[-maxnodes]> --ntasks<number> \
        --network<type> --nice<[adjustment]> --ntasks-per-core<number>  \
        --overcommit --partition<partitionname> --quiet --qos<qos> \
        --reservation<name> --share --signal<sig_num>[@<sig_time>] \
        --time<time> --time-min<time> --tmp<MB> --usage --uid<user> \
        --version --verbose --wait<seconds> --nodelist<nodelist> \
        --wait-all-nodes<0|1> --wckey<wckey> --exclude<nodelist> \
        --checkpoint<time> --checkpoint-dir<directory> --error<file> \
        --preserve-env --epilog<path> --input<file> --job-name<name> \
        --kill-on-bad-exit --label --msg-timeout --mpi<type> \
        --multi-prog --output<file> --open-mode<append|truncate> \ 
        --prolog<path> --propagate<rlimits> --pty --quit-on-interrupt \
        --relative<number> --resv-ports --restart-dir<dir> --slurmd-debug<level> \
        --threads<number> --task-epilog<path> --task-prolog<path> --test-only \
        --unbuffered --disable-status --no-allocate --export<env_var> \
        --ntasks-per-socket<number> --ntasks-per-node<number> --tasks-per-node<number>"
   
    [[ $cur == - ]] && { offer "$shortoptions" ; return ; }
    [[ $cur == -- ]] && { offer "$longoptions" ; return ; }
    [[ $cur == --* ]] && { offer "$(sed 's/<[^>]*>//g' <<< $longoptions)"; return ; }

    case $prev in 
    --account|-A) offer_list "$(_accounts)" ;;
    --constraint|-C) offer_list "$(_features)" ;;
    --cpu-bind) offer "none rank map_cpu: mask_cpu: sockets \
                            cores threads ldoms" ;;
    --dependency|-d) offer "after: afterany: afternotok: \
                            afterok: singleton" ;; 
    --gid) _gids ;; 
    --partition|-p) offer_list "$(_partitions)" ;;
    --gres) offer_list "$(_gres)" ;; 
    --hint) offer "compute_bound memory_bound multithread \
                        nomultithread" ;;
    --jobid) offer_list "$(_jobs)" ;;
    --licenses|-L) offer_list "$(_licenses)" ;;
    --distribution|-m) offer_list "block cyclic plane arbitrary" ;;
    --mail-type) offer_list "begin end fail requeue all" ;;
    --mem-bind) offer "none rank local map_mem: mask_mem:" ;;
    --mpi) offer "lam mpich1_shmem mpichgm mvapich openmpi none" ;;
    --propagate) offer_list "all as core cpu data fsize memlock \
                              nofile nproc rss stack" ;;
    # TODO --network) _configured_interfaces ;;
    --reservation) offer_list "$(_reservations)" ;;
    --clusters|-M) offer_list "$(_clusters)" ;;
    --nodelist) offer_list "$(_nodes)" ;;
    --exclude|-x) offer_list "$(_nodes)" ;;
    --qos) offer_list "$(_qos)" ;;
    :|afterany|after|afternotok|afterok) offer_list "$(_jobs)" ;;
    *)  _filedir
    esac
    #TODO options for blue gene systems
}
complete -o filenames -F _sbatch sbatch

_srun()
{
    _get_comp_words_by_ref cur prev words cword
    _split_long_opt
    
    local shortoptions="-A -B -C -c -d -D -F -h -H -I -J -K -l -L -m -N -n -O \
                        -Q -s -t -u -V -v -W -w -x -q -r -T -X -Z"
    local longoptions=" --open-mode<append|truncate> --account<account> --acctg-freq<seconds> \
        --extra-node-info<sockets[:cores[:threads]]> --sockets-per-node<number> \
        --cores-per-socket<number> --threads-per-core<number> --begin<time> \
        --bell --comment<string> --constraint<list> --contiguous --cpu-bind<type> \
        --cpus-per-task<number> --dependency<deplist> --chdir<path> --exclusive \
        --nodefile<nodefile> --get-user-env --gid<group> --gres<list> --hold \
        --help --hint<type> --immediate<[seconds>] --jobid<jobid> --no-kill \
        --licenses<licenses> --distribution<dist> --mail-type<type> \
        --mail-user<email> --mem<MB> --mem-per-cpu<MB> --mem-bind<type> \
        --min-cpus<number> --nodes<minnodes[-maxnodes]> --ntasks<number> \
        --network<type> --nice<[adjustment]> --ntasks-per-core<number>  \
        --overcommit --partition<partitionname> --quiet --qos<qos> \
        --reservation<name> --share --signal<sig_num[@sig_time]> \
        --time<time> --time-min<time> --tmp<MB> --usage --uid<user> \
        --version --verbose --wait<seconds> --nodelist<nodelist> \
        --wait-all-nodes<0|1> --wckey<wckey> --exclude<nodelist> \
        --checkpoint<time> --checkpoint-dir<directory> --error<file> \
        --preserve-env --epilog<path> --input<file> --job-name<name> \
        --kill-on-bad-exit --label --msg-timeout --mpi<type> \
        --multi-prog --output<file> \
        --prolog<path> --propagate<rlimits> --pty --quit-on-interrupt \
        --relative<number> --resv-ports --restart-dir<dir> --slurmd-debug<level> \
        --threads<number> --task-epilog<path> --task-prolog<path> --test-only \
        --unbuffered --disable-status --no-allocate --export<env_var> \
        --ntasks-per-socket<number> --ntasks-per-node<number> --tasks-per-node<number>"
   
    [[ $cur == - ]] && { offer "$shortoptions" ; return ; }
    [[ $cur == -- ]] && { offer "$longoptions" ; return ; }
    [[ $cur == --* ]] && { offer "$(sed 's/<[^>]*>//g' <<< $longoptions)"; return ; }

    case $prev in 
    --gid) _gids ;; 
    --nodefile) _filedir ;;
    # TODO --network) _configured_interfaces ;; 
    --prolog|--task-epilog|--task-prolog) _filedir ;;
    --chdir|--restart-dir|--checkpoint-dir) _filedir ;;
    --reservation) offer_list "$(_reservations)" ;;
    --constraint|-C) offer_list "$(_features)" ;;
    --clusters) offer_list "$(_clusters)" ;;
    --account|-A) offer_list "$(_accounts)" ;;
    --qos) offer_list "$(_qos)" ;;
    --gres) offer_list "$(_gres)" ;; 
    --jobid) offer_list "$(_jobs)" ;;
    --exclude|-x) offer_list "$(_nodes)" ;;
    --nodelist|-w) offer_list "$(_nodes)" ;;
    --licenses|-L) offer_list "$(_licenses)" ;; 
    --partition|-p) offer_list "$(_partitions)" ;;
    --begin) offer $(date -dtomorrow +"%Y-%m-%d");; 
    --open-mode) offer "append truncate" ;; 
    --mail-type) offer_list "begin end fail requeue all" ;;
    --distribution|-m) offer "block cyclic plane arbitrary" ;;
    --mpi) offer "lam mpich1_shmem mpichgm mvapich openmpi none" ;;
    --hint) offer "compute_bound memory_bound multithread \
                       nomultithread" ;;
    --propagate) offer_list "all as core cpu data fsize memlock \
                              nofile nproc rss stack" ;;
    --mem-bind) offer "none rank local map_mem: mask_mem:" ;;
    --cpu-bind) offer "none rank map_cpu: mask_cpu: sockets \
                           cores threads ldoms" ;;
    --dependency|-d) offer "after: afterany: afternotok: afterok: singleton" ;; 
    :|afterany|after|afternotok|afterok) offer_list "$(_jobs)" ;;
    *)  COMPREPLY=( $( compgen -c -- "$cur" ) ) ; return
    esac
    #TODO options for blue gene systems
}
complete -F _srun srun

# vim: sw=4:ts=4:expandtab
