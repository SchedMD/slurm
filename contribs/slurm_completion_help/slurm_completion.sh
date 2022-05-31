###############################################################################
#
# Bash completion for Slurm
#
###############################################################################
#  Copyright (C) 2012 Damien François. <damien.francois@uclouvain.Be>
#  Written by Damien François. <damien.francois@uclouvain.Be>.
#
#  This file is part of Slurm, a resource management program.
#  For details, see <https://slurm.schedmd.com/>.
#  Please also read the included file: DISCLAIMER.
#
#  Slurm is free software; you can redistribute it and/or modify it under
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
#  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
#  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
#  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
#  details.
#
#  You should have received a copy of the GNU General Public License along
#  with Slurm; if not, write to the Free Software Foundation, Inc.,
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
echo $( scontrol -o show jobs | grep -E -o 'JobId=(\w,?)+' | cut -d'=' -f 2 ) ;
}
function _wckeys() {
echo $(sacctmgr -p -n list wckeys | cut -d'|' -f1) ;
}
function _qos() {
echo $(sacctmgr -p -n list qos | cut -d'|' -f1) ;
}
function _qos_id() {
echo $(sacctmgr -Pn list qos format=id | sort) ;
}
function _clusters() {
echo $(sacctmgr -p -n list clusters | cut -d'|' -f1) ;
}
function _clus_rpc() {
echo $(sacctmgr -Pn list clusters format=rpc | sort | uniq) ;
}
function _jobnames() {
echo $( scontrol -o show jobs | grep -E -o 'JobName=(\w,?)+' | cut -d'=' -f 2 ) ;
}
function _partitions() {
echo $(scontrol show partitions|grep PartitionName|cut -c 15- |cut -f 1 -d' '|paste -s -d ' ') ;
}
function _accounts() {
echo $(sacctmgr -pn list accounts | cut -d'|' -f1 | paste -s -d' ') ;
}
function _acct_org() {
echo $(sacctmgr show account format=org -nP | sort | uniq) ;
}
function _acct_desc() {
echo $(sacctmgr show account format=desc -nP | sort | uniq) ;
}
function _licenses() {
echo $(scontrol show license | grep LicenseName | sed 's/LicenseName=//') ;
}
function _nodes() {
echo $(scontrol show nodes | grep NodeName | cut -c 10- | cut -f 1 -d' ' | paste -s -d ' ') ;
}
function _features() {
echo $(scontrol -o show nodes | grep -E -o 'AvailableFeatures=(\w,?)+' | cut -d= -f2 | tr "," "\n" | sort -u) ;
}
function _users() {
echo $(sacctmgr -pn list users | cut -d'|' -f1) ;
}
function _reservations() {
echo $(scontrol -o show reservations | grep -E -o 'ReservationName=\w+' | cut -d= -f2 | sort -u) ;
}
function _gres() {
echo $(scontrol show config | grep GresTypes | cut -d= -f2)
}
function _resource() {
echo $(sacctmgr -pn list resource | cut -d'|' -f1 | paste -s -d' ')
}
function _resource_server() {
echo $(sacctmgr -pn list resource | cut -d'|' -f2 | paste -s -d' ')
}
function _step() {
echo $( scontrol -o show step | cut -d' ' -f 1 | cut -d'=' -f 2 ) ;
}
function _federation() {
echo $(sacctmgr -p -n list federation | cut -d'|' -f1) ;
}

_sacctmgr()
{
    _get_comp_words_by_ref cur prev words cword
    _split_long_opt

    local subopts=""
    local commands="add archive clear create delete dump help list load\
		    modify reconfigure remove show shutdown version"
    local shortoptions="-h -i -n -p -P -Q -r -s -v -V"
    local longoptions="--help --immediate --noheader --parsable	--parsable2\
		       --quiet --readonly --associations --verbose --version"

    local adminlevel="none admin coordinator operator"
    local assocparams="cluster= account= user= partition= qos="

    local assocbasedparams="defaultqos= fairshare= share= grptresmins=\
			    grptresrunmins= grptres= grpjobs= grpjobsaccrue=\
			    grpnodes= grpsubmitjobs= grpwall= maxtres=\
			    maxtresmins= maxtresperjob= maxtresminsperjob=\
			    maxtrespernode= maxjobs= maxjobsaccrue= maxnodes=\
			    maxsubmitjobs= maxwall= priority="

    local qosflags="DenyOnLimit EnforceUsageThreshold NoReserve\
		    PartitionMaxNodes PartitionMinNodes OverPartQos\
		    PartitionTimeLimit RequiresReservation\
		    NoDecay UsageFactorSafe"
    local qospreempt="cluster cancel requeue suspend"

    local clusflags="frontend multipleslurmd"

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

    case $command in
    add|create)
	objects="account cluster coordinator federation qos resource user"
	object=$(find_first_occurence "${COMP_WORDS[*]}" "$objects")
	case $object in
	account)
	    params="cluster= description= name= organization= parent="
	    if param "cluster" ; then  offer_list "$(_clusters)" ;
	    elif param "parent" ; then  offer_list "$(_accounts)" ;
	    else offer "$params"
	    fi
	    ;;
	cluster)
	    params="classification= name= features= federation= fedstate="
	    if param "federation" ; then  offer_list "$(_federation)" ;
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
	federation)
	    params="clusters= name="
	    if param "clusters" ; then offer_list "$(_clusters)" ;
	    else offer "$params"
	    fi
	    ;;
	qos)
	    params="flags= gracetime= grptresmins= grptresrunmins= grptres=\
		    grpjobs= grpjobsaccrue= grpsubmitjobs= grpwall=\
		    maxtresmins= maxtresperaccount= maxtres= maxtrespernode=\
		    maxtresperuser= maxjobspu= maxjobspa= maxjobsaccruepu=\
		    maxjobsaccruepa= maxsubmitjobspu= maxsubmitjobspa=\
		    maxwall= minpriothres= mintres= name= preempt=\
		    preemptmode= preemptexempttime= priority= usagefactor=\
		    usagethres="
	    if param "preemptmode" ; then  offer_list "$qospreempt" ;
	    elif param "flags" ; then  offer_list "$qosflags" ;
	    elif param "preempt" ; then  offer_list "$(_qos)" ;
	    else offer "$params"
	    fi
	    ;;
	resource)
	    params="clusters= count= description= servertype= name=\
		    percentallowed= server= type="
	    if param "clusters" ; then offer_list "$(_clusters)" ;
	    elif param "type" ; then offer_list "license" ;
	    else offer "$params"
	    fi
	    ;;
	user)
	    params="account= adminlevel= cluster= defaultaccount=\
		    defaultwckey= name= partition= wckey="
	    if param "defaultaccount" ; then  offer_list "$(_accounts)" ;
	    elif param "account" ; then  offer_list "$(_accounts)";
	    elif param "adminlevel" ; then  offer_list "$adminlevel" ;
	    elif param "cluster" ; then  offer_list "$(_clusters)" ;
	    elif param "defaultwckey" ; then  offer_list "$(_wckeys)" ;
	    elif param "partition" ; then offer_list "$(_partitions)" ;
	    elif param "wckey" ; then offer_list "$(_wckeys)" ;
	    else offer "$params"
	    fi
	    ;;
	*) offer "$objects" ;;
	esac
	;;
    archive)
	objects="dump load"
	object=$(find_first_occurence "${COMP_WORDS[*]}" "$objects")
	case $object in
	dump)
	    params="directory= events jobs purgeeventafter=\
		    purgejobafter= purgestepafter= purgesuspendafter=\
		    script= steps suspend"
	    if param "directory" ; then _filedir ;
	    elif param "script" ; then _filedir ;
	    else offer "$params"
	    fi
	    ;;
	load)
	    params="file= insert="
	    if param "file" ; then _filedir ;
	    else offer "$params"
	    fi
	    ;;
	*) offer "$objects"
	    ;;
	esac
	;;
    delete|remove)
	objects="account cluster coordinator federation qos resource user"
	object=$(find_first_occurence "${COMP_WORDS[*]}" "$objects")
	case $object in
	account)
	    if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ;
		    return ;fi
	    params="cluster= description= name= organization= parent="
	    if param "cluster" ; then  offer_list "$(_clusters)" ;
	    elif param "parent" ; then  offer_list "$(_accounts)" ;
	    elif param "name" ; then  offer_list "$(_accounts)" ;
	    else offer "$params"
	    fi
	    ;;
	cluster)
	    if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ;
		    return ;fi
	    params="name= federation= rpc="
	    if param "name" ; then offer_list "$(_clusters)" ;
	    elif param "federation" ; then offer_list "$(_federation)" ;
	    else offer "$params"
	    fi
	    ;;
	coordinator)
	    if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ;
		    return ;fi
	    params="accounts= names="
	    if param "names" ; then  offer_list "$(_users)" ;
	    elif param "accounts" ; then  offer_list "$(_accounts)" ;
	    else offer "$params"
	    fi
	    ;;
	federation)
	    if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ;
		    return ;fi
	    params="name="
	    if param "name" ; then offer_list "$(_federation)" ;
	    else offer "$params"
	    fi
	    ;;
	qos)
	    if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ;
		    return ;fi
	    params="name="
	    if param "name" ; then offer_list "$(_qos)" ;
	    else offer "$params"
	    fi
	    ;;
	resource)
	    if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ;
		    return ;fi
	    params="name= server="
	    if param "name" ; then offer_list "$(_resource)" ;
	    elif param "server" ; then offer_list "$(_resource_server)" ;
	    else offer "$params"
	    fi
	    ;;
	user)
	    params="account= adminlevel= cluster= defaultaccount=\
		    defaultwckey= name= partition= wckeys="
	    if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ;
		    return ;fi
	    if param "defaultaccount" ; then  offer_list "$(_accounts)" ;
	    elif param "account" ; then  offer_list "$(_accounts)";
	    elif param "adminlevel" ; then  offer_list "$adminlevel" ;
	    elif param "cluster" ; then  offer_list "$(_clusters)" ;
	    elif param "defaultwckey" ; then  offer_list "$(_wckeys)" ;
	    elif param "name" ; then offer_list "$(_users)" ;
	    elif param "partition" ; then offer_list "$(_partitions)" ;
	    elif param "wckeys" ; then  offer_list "$(_wckeys)" ;
	    else offer "$params" ;
	    fi
	    ;;
	*) offer "$objects"
	    ;;
	esac
	;;
    list|show)
	objects="account association cluster configuration event federation\
		 problem qos resource reservation runawayjobs stats\
		 transaction tres user wckey"
	object=$(find_first_occurence "${COMP_WORDS[*]}" "$objects")
	case $object in
	account)
	    if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ;
		    return ;fi
	    params="name= account= organization= description= withassoc\
		    withcoord withdeleted cluster= parent= partition=\
		    qos= defaultqos= user="
	    if param "name" ; then  offer_list "$(_accounts)" ;
	    elif param "account" ; then  offer_list "$(_accounts)" ;
	    elif param "organization" ; then offer_list "$(_acct_org)" ;
	    elif param "description" ; then offer_list "$(_acct_desc)" ;
	    elif param "cluster" ; then  offer_list "$(_clusters)" ;
	    elif param "parent" ; then  offer_list "$(_accounts)" ;
	    elif param "partition" ; then  offer_list "$(_partitions)" ;
	    elif param "defaultqos" ; then  offer_list "$(_qos)" ;
	    elif param "qos" ; then  offer_list "$(_qos)" ;
	    elif param "user" ; then  offer_list "$(_users)" ;
	    else offer "$params"
	    fi
	    ;;
	association)
	    if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ;
		    return ;fi
	    params="$assocparams onlydefaults tree withdeleted\
		    withsubaccounts wolimits wopinfo woplimits"
	    if param "cluster" ; then  offer_list "$(_clusters)" ;
	    elif param "account" ; then  offer_list "$(_accounts)" ;
	    elif param "user" ; then  offer_list "$(_users)" ;
	    elif param "partition" ; then  offer_list "$(_partitions)" ;
	    elif param "qos" ; then offer_list "$(_qos)" ;
	    else offer "$params"
	    fi
	    ;;
	cluster)
	    if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ;
		    return ;fi
	    params="name= cluster= flags= federation= rpc= withfed wolimits"
	    if param "name" ; then offer_list "$(_clusters)" ;
	    elif param "cluster" ; then  offer_list "$(_clusters)" ;
	    elif param "flags" ; then  offer_list "$clusflags" ;
	    elif param "federation" ; then  offer_list "$(_federation)" ;
	    elif param "rpc" ; then offer_list "$(_clus_rpc)" ;
	    else offer "$params"
	    fi
	    ;;
	event)
	    if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ;
		    return ;fi
	    params="all_clusters all_time clusters= end= event=\
		    nodes= reason= start= state= user= "
	    if param "clusters" ; then  offer_list "$(_clusters)" ;
	    elif param "nodes" ; then  offer_list "$(_nodes)" ;
	    elif param "event" ; then  offer_list "cluster node" ;
	    elif param "state" ; then  offer_list "alloc allocated down drain\
			fail failing idle mixed maint power_down power_up\
			resume" ;
	    elif param "users" ; then  offer_list "$(_users)" ;
	    else offer "$params"
	    fi
	    ;;
	qos)
	    if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ;
		    return ;fi
	    params="name= qos= id= withdeleted"
	    if param "name" ; then  offer_list "$(_qos)" ;
	    elif param "qos" ; then  offer_list "$(_qos)" ;
	    elif param "id" ; then  offer_list "$(_qos_id)" ;
	    else offer "$params"
	    fi
	    ;;
	resource)
	    if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ;
		    return ;fi
	    params="cluster= description= servertype= name=\
		    percentallowed= server="
	    if param "name" ; then  offer_list "$(_resource)" ;
	    elif param "cluster" ; then offer_list "$(_clusters)" ;
	    else offer "$params"
	    fi
	    ;;
	transaction)
	    if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ;
		    return ;fi
	    params="accounts= action= actor= clusters= end= start= users=\
		    withassoc"
	    if param "accounts" ; then  offer_list "$(_accounts)" ;
	    elif param "actor" ; then  offer_list "$(_users)" ;
	    elif param "clusters" ; then  offer_list "$(_clusters)" ;
	    elif param "users" ; then offer_list "$(_users)" ;
	    else offer "$params"
	    fi
	    ;;
	user)
	    if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ;
		    return ;fi
	    params="account= adminlevel= cluster= defaultaccount=\
		    defaultwckey= name= user= partition= wckeys= withassoc\
		    withcoord withdelted"
	    if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ;
		    return ;fi
	    if param "defaultaccount" ; then  offer_list "$(_accounts)" ;
	    elif param "account" ; then  offer_list "$(_accounts)";
	    elif param "adminlevel" ; then  offer_list "$adminlevel" ;
	    elif param "cluster" ; then  offer_list "$(_clusters)" ;
	    elif param "name" ; then offer_list "$(_users)" ;
	    elif param "user" ; then offer_list "$(_users)" ;
	    elif param "partition" ; then offer_list "$(_partitions)" ;
	    elif param "wckeys" ; then  offer_list "$(_wckeys)" ;
	    elif param "defaultwckey" ; then  offer_list "$(_wckeys)" ;
	    else offer "$params" ;
	    fi
	    ;;
	*) offer "$objects" ;;
	esac
	;;
    modify)
	objects="account cluster federation job qos resource user"
	object=$(find_first_occurence "${COMP_WORDS[*]}" "$objects")
	case $object in
	account)
	    if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ;
		    return ;fi
	    params="$assocparams name= description= organization=\
		    parent= rawusage= $assocbasedparams"
	    if param "account" ; then offer_list "$(_accounts)" ;
	    elif param "name" ; then offer_list "$(_accounts)" ;
	    elif param "cluster" ; then  offer_list "$(_clusters)" ;
	    elif param "parent" ; then  offer_list "$(_accounts)" ;
	    elif param "user" ; then offer_list "$(_users)" ;
	    elif param "partition" ; then offer_list "$(_partitions)" ;
	    elif param "qos" ; then offer_list "$(_qos)" ;
	    elif param "defaultqos" ; then offer_list "$(_qos)" ;
	    else offer "$params set"
	    fi
	    ;;
	cluster)
	    if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ;
		    return ;fi
	    params="cluster= name= features= federation= fedstate= rpc=\
		    qos= defaultqos= fairshare= share= features= grpjobs=\
		    grpjobsaccrue= grpnodes= grpsubmitjobs= grptres=\
		    maxjobs= maxjobsaccrue= maxnodes= maxsubmitjobs=\
		    maxtres= maxtresmins= maxtresminsperjob= maxtresperjob=\
		    maxtrespernode= maxwall= priority= withfed wolimits"
	    if param "name" ; then offer_list "$(_clusters)" ;
	    elif param "cluster" ; then offer_list "$(_clusters)" ;
	    elif param "federation" ; then  offer_list "$(_federation)" ;
	    elif param "qos" ; then  offer_list "$(_qos)" ;
	    elif param "defaultqos" ; then  offer_list "$(_qos)" ;
	    else offer "$params set"
	    fi
	    ;;
	federation)
	    if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ;
		    return ;fi
	    params="federation= name= cluster="
	    # No flags currently available.  May need to add "flags=" later
	    if param "federation" ; then offer_list "$(_federation)" ;
	    elif param "name" ; then offer_list "$(_federation)" ;
	    elif param "cluster" ; then offer_list "$(_clusters)" ;
	    else offer "$params set"
	    fi
	    ;;
	qos)
	    if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ;
		    return ;fi
	    params="flags= gracetime= grptresmins= grptresrunmins= grptres=\
		    grpjobs= grpjobsaccrue= grpsubmitjobs= grpwall=\
		    maxtresmins= maxtresperaccount= maxtres= maxtrespernode=\
		    maxtresperuser= maxjobspu= maxjobspa= maxjobsaccruepu=\
		    maxjobsaccruepa= maxsubmitjobspu= maxsubmitjobspa=\
		    maxwall= minpriothres= mintres= name= preempt=\
		    preemptmode= preemptexempttime= priority= rawusage=\
		    usagefactor= usagethres="
	    if param "flags" ; then  offer_list "$qosflags" ;
	    elif param "name" ; then offer_list "$(_qos)" ;
	    elif param "preemptmode" ; then  offer_list "$qospreempt" ;
	    elif param "preempt" ; then  offer_list "$(_qos)" ;
	    else offer "$params set"
	    fi
	    ;;
	user)
	    if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ;
		    return ;fi
	    params="adminlevel= cluster= defaultaccount= defaultwckey=\
		    name= newname= withassoc withcoord withdeleted"
	    if [[ "${COMP_WORDS[*]}" != *where* ]] ; then offer "where" ;
		    return ;fi
	    if param "name" ; then  offer_list "$(_users)" ;
	    elif param "defaultaccount" ; then  offer_list "$(_accounts)" ;
	    elif param "adminlevel" ; then  offer_list "none operator admin" ;
	    elif param "cluster" ; then  offer_list "$(_clusters)" ;
	    elif param "defaultwckey" ; then  offer_list "$(_wckeys)" ;
	    else offer "$params set" ;
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

    local shortoptions="-a -h -M -n -p -P -Q -t -T -v -V"
    local longoptions="--all_clusters --cluster --federation --help --local\
		       --noheader --parsable --parsable2 --quiet --tres\
		       --verbose --version"

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
	    elif param "Format" ; then  offer_list "Account Cluster Energy\
						    Login Proper Used" ;
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
	    params="$opts_all Names= Nodes= Accounts= Group TopCount= Users= "
	    if param "Clusters" ; then  offer_list "$(_clusters)" ;
	    elif param "Format" ; then  offer_list "Allocated Associations\
		 Clusters End Flags Idle Name Nodes ReservationId Start\
		 TotalTime TresCount TresName TresTime";
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
		Jobs= Nodes= Partitions= PrintJobCount Users= Wckeys="
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
		 Down Idle Overcommitted PlannedDown Reported Reserved\
		 TresCount TresName";
	    elif param "Nodes" ; then  offer_list "$(_nodes)" ;
	    else offer "$params"
	    fi
	    ;;
	AccountUtilizationByUser|UserUtilizationByAccount)
	    params="$opts_all Accounts= Tree Users= Wckeys="
	    if param "Clusters" ; then  offer_list "$(_clusters)" ;
	    elif param "Format" ; then  offer_list "Accounts Cluster Energy\
						    Login Proper TresCount\
						    Used" ;
	    elif param "Accounts" ; then  offer_list "$(_accounts)" ;
	    elif param "Users" ; then  offer_list "$(_users)" ;
	    elif param "Wckeys" ; then  offer_list "$(_wckeys)" ;
	    else offer "$params"
	    fi
	    ;;
	UserUtilizationByWCKey|WCKeyUtilizationByUser)
	    params="$opts_all Accounts= Tree Users= Wckeys="
	    if param "Clusters" ; then  offer_list "$(_clusters)" ;
	    elif param "Format" ; then  offer_list "Cluster Login Proper\
						    TresCount Used Wckey" ;
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

    local commands="abort cancel_reboot create completing delete\
		    errnumstr fsdampeningfactor help hold listpids notify\
		    pidinfo ping reboot_nodes reconfigure release requeue\
		    requeuehold resume schedloglevel setdebug setdebugflags\
		    show shutdown suspend takeover top token uhold update\
		    version wait_job write"

    local dependency_types="after: afterany: afternotok: afterok: singleton:"
    local mail_types="none begin end fail requeue all stage_out time_limit\
    		      time_limit_90 time_limit_80 time_limit_50 array_tasks"
    local shortoptions="-a -d -F -h -M -o -Q -u -v -V "
    local longoptions="--all --cluster --details --federation --future --help\
		       --hide --local --oneliner --quiet --sibling --uid\
		       --verbose --version"

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
    schedloglevel)
	offer "disable enable"
	;;
    setdebug) # scontrol setdebug value
	offer "quiet fatal error info verbose debug debug2 debug3 debug4 debug5 "
	;;
    setdebugflags)
	parameters="+accrue +agent +backfill +backfillmap +burstbuffer +cgroup\
		    +cpu_bind +cpufrequency +data +dependency +elasticsearch\
		    +energy +extsensors	+federation +frontend +gres +hetjob\
		    +gang +jobaccountgather +jobcontainer +license +network\
		    +networkraw +nodefeatures +no_conf_hash +power +priority\
		    +profile +protocol +reservation +route +script +selecttype\
		    +steps +switch +timecray +tracejobs +triggers +workqueue\
		    -accrue -agent -backfill -backfillmap -burstbuffer -cgroup\
		    -cpu_bind -cpufrequency -data -dependency -elasticsearch\
		    -energy -extsensors	-federation -frontend -gres -hetjob\
		    -gang -jobaccountgather -jobcontainer -license -network\
		    -networkraw -nodefeatures -no_conf_hash -power -priority\
		    -profile -protocol -reservation -route -script -selecttype\
		    -steps -switch -timecray -tracejobs -triggers -workqueue"

	param=$(find_first_partial_occurence "${COMP_WORDS[*]}" "$parameters")
	[[ $param == "" ]] && { offer "$parameters" ; return ; }

	offer "$parameters"
	;;
    uhold | suspend | release | requeue | requeuehold | resume | hold | notify | listpids | top)
	offer "$(_jobs)"
	;;
    reboot_nodes)
	parameters="all asap nextstate= reason= $(_nodes)"

	param=$(find_first_partial_occurence "${COMP_WORDS[*]}" "$parameters")
	[[ $param == "" ]] && { offer "$parameters" ; return ; }

	if param "nextstate" ; then offer_many "resume down"
	else offer "$parameters" ; fi
	;;
    cancel_reboot)
	offer "$(_nodes)"
	;;
    show) # scontrol show object [id]
	objects="aliases assoc_mgr bbstat burstbuffer config daemons dwstat\
		 federation frontend hostlist hostlistsorted hostnames job\
		 licenses nodes partitions reservations slurmd steps topology"

	# Search for the current object in the argument list
	object=$(find_first_occurence "${COMP_WORDS[*]}" "$objects")

	# If no object has yet been (fully) typed in, serve the list of objects
	[[ $object == "" ]] && { offer "$objects" ; return ; }

	# Otherwise, offer the ids depending on the object
	if param "job"          ; then offer  "$(_jobs)"         ; fi
	if param "nodes"        ; then offer_list "$(_nodes)"        ; fi
	if param "licenses"      ; then offer_list "$(_licenses)" ; fi
	if param "partitions"   ; then offer "$(_partitions)"   ; fi
	if param "reservations" ; then offer "$(_reservations)"  ; fi
	if param "steps"        ; then offer "$(_step)" ; fi
	;;
    delete) # scontrol delete objectname=id
	parameters="nodename= partitionname= reservationname="

	# If a parameter has been fully typed in, serve the corresponding
	# values, otherwise, serve the list of parameters.
	if   param "nodename"        ; then offer_many "$(_nodes)"
	elif param "partitionname"   ; then offer_many "$(_partitions)"
	elif param "reservationname" ; then offer_many "$(_reservations)"
	else offer "$parameters" ; fi
	;;
    update)
	parameters="jobid= step= nodename= partitionname=\
		    reservationname="

	param=$(find_first_partial_occurence "${COMP_WORDS[*]}" "$parameters")
	[[ $param == "" ]] && { offer "$parameters" ; return ; }

	# If a parameter has been fully typed in, serve the corresponding
	# values, if it is the first one.
	if   param "jobid"   ; then offer_many "$(_jobs)" ; return
	elif param "nodename" ; then offer_many "$(_nodes)"  ; return
	elif param "partitionname" ; then offer_many "$(_partitions)" ; return
	elif param "reservationname" ; then offer_many "$(_reservations)"  ; return
	elif param "step" ; then offer_many "$(_step)" ; return
	fi

	# Otherwise, process the others based on the first one
	case $param in
	jobid)
	    local parameters="account=<account> admincomment=<spec>\
			      arraytaskthrottle=<count> comment=<spec>\
			      contiguous=<yes|no> cpuspertask=<count>\
			      deadline=<time_spec> delayboot=<time_spec>\
			      dependency=<dependency_list>\
			      eligibletime=<time_spec> endtime=<time_spec>\
			      excnodelist=<nodes>\
			      features=<features> gres=<list>\
			      jobid=<job_id> jobname=<name> licenses=<name>\
			      mailtype=<types> mailuser=<name>\
			      mincpusnode=<count> minmemorycpu=<megabytes>\
			      minmemorynode=<megabytes>\
			      mintmpdisknode=<megabytes> name=<name>\
			      nice=<delta> nodelist=<nodes>\
			      numcpus=<min_count[-max_count]\
			      numnodes=<min_count[-max_count]>\
			      numtasks=<count> oversubscribe=<yes|no>\
			      partition=<name> priority=<number> qos=<name>\
			      reboot=<yes|no> reqcores=<count>\
			      reqnodelist=<nodes>\
			      reqnodes=<min_count[-max_count]>\
			      reqprocs=<count> reqsockets=<count>\
			      reqthreads=<count> requeue=<0|1>\
			      reservationname=<name> resetaccruetime\
			      shared=<yes|no> sitefactor=<number>\
			      starttime=<time_spec> stdout=<path>\
			      switches=<count>[@<max-time-to-wait>]\
			      taskspernode=<count>\
			      timelimit=<time_spec> timemin=<time_spec>\
			      userid=<UID_or_name> wait-for-switch=<seconds>\
			      wckey=<key> workdir=<path>"

	    remainings=$(compute_set_diff "$parameters" "${COMP_WORDS[*]}")

	    # If a new named argument is about to be entered, serve the list of options
	    [[ $cur == "" && $prev != "=" ]] && { offer "$remainings" ; return ; }

	    # Test all potential arguments and server corresponding values
	    if   param "account"         ; then offer_many "$(_accounts)"
	    elif param "excnodelist"     ; then offer_many "$(_nodes)"
	    elif param "nodelist"        ; then offer_many "$(_nodes)"
	    elif param "reqnodelist"     ; then offer_many "$(_nodes)"
	    elif param "features"        ; then offer_many "$(_features)"
	    elif param "gres"            ; then offer_many "$(_gres)"
	    elif param "jobid"           ; then offer_many "$(_jobs)"
	    elif param "jobname"         ; then offer_many "$(_jobnames)"
	    elif param "name"            ; then offer_many "$(_jobnames)"
	    elif param "licences"        ; then offer_many "$(_licenses)"
	    elif param "userid"          ; then offer_many "$(_users)"
	    elif param "mailuser"        ; then offer_many "$(_users)"
	    elif param "partition"       ; then offer_many "$(_partitions)"
	    elif param "reservationname" ; then offer_many "$(_reservations)"
	    elif param "qos"             ; then offer_many "$(_qos)"
	    elif param "wckey"           ; then offer_many "$(wckeys)"
	    elif param "contiguous"      ; then offer_many "yes no"
	    elif param "oversubscribe"   ; then offer_many "yes no"
	    elif param "reboot"          ; then offer_many "yes no"
	    elif param "shared"          ; then offer_many "yes no"
	    elif param "dependency"      ; then offer_many "$dependency_types"
	    elif param "mailtype"        ; then offer_many "$mail_types"
	    else offer "$(sed 's/\=[^ ]*/\=/g' <<< $remainings)"
	    fi
	    ;;
	nodename)
	    local parameters="activefeatures=<features> \
		  availablefeatures=<features> comment=<comment> \
		  cpubind=<binding> extra=<comment> gres=<gres> \
		  nodeaddr=<name> nodehostname=<name> nodename=<name> \
		  reason=<reason> state=<state> weight=<weight>"

	    remainings=$(compute_set_diff "$parameters" "${COMP_WORDS[*]}")

	    # If a new named argument is about to be entered, serve the list of options
	    [[ $cur == "" && $prev != "=" ]] && { offer "$remainings" ;
		    return ; }

	    # Test all potential arguments and server corresponding values
	    if param "activefeatures"      ; then offer_many "$(_features)"
	    elif param "availablefeatures" ; then offer_many "$(_features)"
	    elif param "cpubind"  ; then offer_many "none board socket ldom\
						     core thread off"
	    elif param "gres"     ; then offer_many "$(_gres)"
	    elif param "state"    ; then offer_many "noresp drain fail future\
						     resume power_down\
						     power_up undrain"
	    else offer "$(sed 's/\=[^ ]*/\=/g' <<< $remainings)"
	    fi
	    ;;
	partitionname)
	    local parameters="allowgroups=<name> allocnodes=<node_list>\
			      alternate=<partition_name> cpubind=<binding>\
			      default=<yes|no> defaulttime=<d-h:m:s|unlimited>\
			      defmempercpu=<MB> defmempernode=<MB>\
			      disablerootjobs=<yes|no> gracetime=<seconds>\
			      hidden=<yes|no> jobdefaults=<specs>\
			      maxmempercpu=<MB> maxmempernode=<MB>\
			      maxnodes=<count> maxtime=<d-h:m:s|unlimited>\
			      minnodes=<count> nodes=<name>\
			      overtimelimit=<count>\
			      oversubscribe=<yes|no|exclusive|force>[:<job_count>]\
			      preemptmode=<mode> priority=<count>\
			      priorityjobfactor=<count> prioritytier=<count>\
			      qos=<qos> rootonly=<yes|no> reqresv=<yes|no>\
			      shared=<yes|no|exclusive|force>[:<job_count>]\
			      state=<up|down|drain|inactive>\
			      tresbillingweights=<billing_weights>"

	    remainings=$(compute_set_diff "$parameters" "${COMP_WORDS[*]}")
	    # If a new named argument is about to be entered, serve the list of options
	    [[ $cur == "" && $prev != "=" ]] && { offer "$remainings" ;
		    return ; }

	    # Test all potential arguments and server corresponding values
	    if   param "allocnodes"  ; then offer_many "$(_nodes)"
	    elif param "alternate"   ; then offer_many "$(_partitions)"
	    elif param "cpubind"     ; then offer_many "none board socket ldom\
							core thread off"
	    elif param "default"     ; then offer_many  "yes no"
	    elif param "disablerootjobs" ; then offer_many "yes no"
	    elif param "hidden"      ; then offer_many "yes no"
	    elif param "jobdefaults" ; then offer_many "DefCpuPerGPU=\
							DefMemPerGPU"
	    elif param "nodes"       ; then offer_many "$(_nodes)"
	    elif param "oversubscribe"; then offer_many "yes no exclusive force"
	    elif param "preemptmode" ; then offer_many "off cancel\
							requeue suspend"
	    elif param "qos"         ; then offer_many "$(_qos)"
	    elif param "rootonly"    ; then offer_many "yes no"
	    elif param "reqresv"     ; then offer_many "yes no"
	    elif param "shared"      ; then offer_many "yes no exclusive force"
	    elif param "state"       ; then offer_many "up down drain inactive"
	    else offer "$(sed 's/\=[^ ]*/\=/g' <<< $remainings)"
	    fi
	    ;;
	reservationname)
	    local parameters="accounts=<account_list> burstbuffer=<buffer_spec>\
			      corecnt=<num>\
			      duration=<[days-]hours:minutes:seconds>\
			      endtime=<time_spec> features=<feature_list>\
			      flags=<flags> groups=<group_list>\
			      licenses=<licenses> maxstartdelay=<timespec>\
			      nodecnt=<count> nodes=<node_list>\
			      partitionname=<partition_list>\
			      reservationname=<name>\
			      skip starttime=<time_spec>\
			      tres=<tres_spec> users=<user_list>"

	    remainings=$(compute_set_diff "$parameters" "${COMP_WORDS[*]}")
	    # If a new named argument is about to be entered, serve the list of options
	    [[ $cur == "" && $prev != "=" ]] && { offer "$remainings" ;
		    return ; }

	    # test all potential arguments and server corresponding values
	    if   param "accounts" ; then offer_many  "$(_accounts)"
	    elif param "licences" ; then offer_many "$(_licenses)"
	    elif param "nodes"    ; then offer_many "$(_nodes)"
	    elif param "features" ; then offer_many "$(_features)"
	    elif param "users"    ; then offer_many "$(_users)"
	    elif param "flags"    ; then offer_many "any_nodes daily flex\
						     first_cores ignore_jobs\
						     license_only maint\
						     magnetic\
						     no_hold_jobs_after\
						     overlap part_nodes\
						     purge_comp replace\
						     replace_down spec_nodes\
						     static_alloc time_float\
						     weekday weekend weekly"
	    elif param "partitionname" ; then offer_many "$(_partitions)"
	    elif param "reservationname" ; then offer_many "$(_reservations)"
	    else offer "$(sed 's/\=[^ ]*/\=/g' <<< $remainings)"
	    fi
	    ;;
	step)
	    local parameters="stepid=<job_id>[.<step_id>]\
			     CompFile=<completion_file> TimeLimit=<time>"

	    remainings=$(compute_set_diff "$parameters" "${COMP_WORDS[*]}")
	    [[ $cur == "" && $prev != "=" ]] && { offer "$remainings" ;
		    return ; }
	    if param "stepid" ; then offer_list "$(_step)" ;
	    else offer "$(sed 's/\=[^ ]*/\=/g' <<< $remainings)"
	    fi
	    ;;

	esac
	;;
    create) # scontrol create (object attribute1=value1|objectname=id)
	parameters="nodename= partitionname= reservation reservationname="

	param=$(find_first_occurence "${COMP_WORDS[*]}" "$parameters")
	param+=$(find_first_partial_occurence "${COMP_WORDS[*]}" "$parameters")
	[[ $param == "" ]] && { offer "$parameters" ; return ; }

	# Process object
	case $param in
	nodename)
	    local parameters="bcastaddr=<name> boards=<count>\
			      corespeccount=<count> corespersocket=<count>\
			      cpubind=<none|socket|idom|core|thread>\
			      cpus=<count> cpuspeclist=<cpuspec_list>\
			      features=<feature_list> gres=<gres_list>\
			      memspeclimit=<MB> nodeaddr=<name>\
			      nodehostname=<name> port=<port> realmemory=<MB>\
			      reason=<reason> sockets=<count>\
			      socketsperboard=<count> state=<cloud|future>\
			      threadspercore=<count> tmpdisk=<MB>\
			      weight=<weight>"

	    remainings=$(compute_set_diff "$parameters" "${COMP_WORDS[*]}")
	    # If a new named argument is about to be entered, serve the list of options
	    [[ $cur == "" && $prev != "=" ]] && { offer "$remainings" ;
		    return ; }

	    if   param "cpubind"  ; then offer_many "core idom none socket thread"
	    elif param "features" ; then offer_many "$(_features)"
	    elif param "gres"     ; then offer_list "$(_gres)"
	    elif param "state"    ; then offer_many "cloud future"
	    else offer "$(sed 's/\=[^ ]*/\=/g' <<< $remainings)"
	    fi
	    ;;
	partitionname)
	    local parameters="allowgroups=<name> allocnodes=<node_list>\
			      alternate=<partition_name> cpubind=<binding>\
			      default=<yes|no> defaulttime=<d-h:m:s|unlimited>\
			      defmempercpu=<MB> defmempernode=<MB>\
			      disablerootjobs=<yes|no> gracetime=<seconds>\
			      hidden=<yes|no> jobdefaults=<specs>\
			      maxmempercpu=<MB> maxmempernode=<MB>\
			      maxnodes=<count> maxtime=<d-h:m:s|unlimited>\
			      minnodes=<count> nodes=<name>\
			      overtimelimit=<count>\
			      oversubscribe=<yes|no|exclusive|force>[:<job_count>]\
			      preemptmode=<mode> priority=<count>\
			      priorityjobfactor=<count> prioritytier=<count>\
			      qos=<qos> rootonly=<yes|no> reqresv=<yes|no>\
			      shared=<yes|no|exclusive|force>[:<job_count>]\
			      state=<up|down|drain|inactive>\
			      tresbillingweights=<billing_weights>"

	    remainings=$(compute_set_diff "$parameters" "${COMP_WORDS[*]}")
	    # If a new named argument is about to be entered, serve the list of options
	    [[ $cur == "" && $prev != "=" ]] && { offer "$remainings" ;
		    return ; }

	    # Test all potential arguments and server corresponding values
	    if   param "allocnodes"  ; then offer_many "$(_nodes)"
	    elif param "alternate"   ; then offer_many "$(_partitions)"
	    elif param "cpubind"     ; then offer_many "none board socket ldom\
							core thread off"
	    elif param "default"     ; then offer_many  "yes no"
	    elif param "disablerootjobs" ; then offer_many "yes no"
	    elif param "hidden"      ; then offer_many "yes no"
	    elif param "jobdefaults" ; then offer_many "DefCpuPerGPU=\
							DefMemPerGPU"
	    elif param "nodes"       ; then offer_many "$(_nodes)"
	    elif param "oversubscribe"; then offer_many "yes no exclusive force"
	    elif param "preemptmode" ; then offer_many "off cancel\
							requeue suspend"
	    elif param "qos"         ; then offer_many "$(_qos)"
	    elif param "rootonly"    ; then offer_many "yes no"
	    elif param "reqresv"     ; then offer_many "yes no"
	    elif param "shared"      ; then offer_many "yes no exclusive force"
	    elif param "state"       ; then offer_many "up down drain inactive"
	    else offer "$(sed 's/\=[^ ]*/\=/g' <<< $remainings)"
	    fi
	    ;;
	reservation|reservationname)
	    local parameters="accounts=<account_list> burstbuffer=<buffer_spec>\
			      corecnt=<num>\
			      duration=<[days-]hours:minutes:seconds>\
			      endtime=<time_spec> features=<feature_list>\
			      flags=<flags> groups=<group_list>\
			      licenses=<licenses> maxstartdelay=<timespec>\
			      nodecnt=<count> nodes=<node_list>\
			      partitionname=<partition_list>\
			      starttime=<time_spec>\
			      tres=<tres_spec> users=<user_list>"

	    remainings=$(compute_set_diff "$parameters" "${COMP_WORDS[*]}")
	    # If a new named argument is about to be entered, serve the list of options
	    [[ $cur == "" && $prev != "=" ]] && { offer "$remainings" ;
		    return ; }

	    # test all potential arguments and server corresponding values
	    if   param "accounts" ; then offer_many  "$(_accounts)"
	    elif param "licences" ; then offer_many "$(_licenses)"
	    elif param "nodes"    ; then offer_many "$(_nodes)"
	    elif param "features" ; then offer_many "$(_features)"
	    elif param "users"    ; then offer_many "$(_users)"
	    elif param "flags"    ; then offer_many "any_nodes daily flex\
						     first_cores ignore_jobs\
						     license_only maint\
						     magnetic\
						     no_hold_jobs_after\
						     overlap part_nodes\
						     purge_comp replace\
						     replace_down spec_nodes\
						     static_alloc time_float\
						     weekday weekend weekly"
	    elif param "partitionname" ; then offer_many "$(_partitions)"
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

    local shortoptions="-A -a -h -i -j -l -L -M -n -O -o -P -p -q -R -r -S -s\
			-t -u -V -v -w"
    local longoptions="--account=<account_list> --all --array --array-unique\
		       --clusters=<string> --federation --format=<fmtstring>\
		       --Format=<fmtstring> --help --hide --iterate=<seconds>\
		       --jobs=<job_id_list> --json --licenses=<license_list>\
		       --local --long --me --name=<name_list> --noconvert\
		       --nodelist=<hostlist> --noheader --partition=<part_list>\
		       --priority --qos=<qos_list> --reservation=<res_name>\
		       --sibling --sort=<sort_list> --start\
		       --states=<state_list> --steps --usage --user=<user_list>\
		       --verbose --version --yaml"

    local jobstates="pending running suspended completed cancelled failed\
		     timeout node_fail preempted boot_fail deadline\
		     out_of_memory completing configuring resizing\
		     revoked special_exit"

    local formatlong="Account AccrueTime admin_comment AllocNodes AllocSID\
		      ArrayJobID ArrayTaskID AssocID BatchFlag BatchHost\
		      BoardsPerNode BurstBuffer BurstBufferState Cluster\
		      ClusterFeature Command Comment Contiguous Cores\
		      CoreSpec CPUFreq cpus-per-task cpus-per-tres Deadline\
		      DelayBoot Dependency DerivedEC EligibleTime EndTime\
		      exit_code Feature GroupID GroupName HetJobID HetJobIDSet\
		      JetJobOffset JobArrayID JobID LastSchedEval Licenses\
		      MaxCPUs MaxNodes MCSLabel mem-per-tres MinCPUs MinMemory\
		      MinTime MinTmpDisk Name Network Nice NodeList Nodes\
		      NTPerBoard NTPerCore NTPerNode NTPerSocket NumCPUs\
		      NumNodes NumTasks Origin OriginRAW OverSubscribe\
		      Partition PreemptTime Priority PriorityLong Profile QOS\
		      Reason ReasonList Reboot ReqNodes ReqSwitch Requeue\
		      Reservation ResizeTime RestartCnt ResvPort SchedNodes\
		      SCT SelectJobInfo SiblingsActive SiblingsActiveRaw\
		      SiblingsViable SiblingsViableRaw Sockets SPerBoard\
		      StartTime State StateCompact STDERR STDIN STDOUT\
		      StepID StepName StepState SubmitTime system_comment\
		      Threads TimeLeft TimeLimit TimeUsed tres-alloc tres-bind\
		      tres-freq tres-per-job tres-per-node tres-per-socket\
		      tres-per-step tres-per-task UserID UserName Wait4Switch\
		      WCKey WorkDir"

    local sortoptions="B(BatchHost) C(NumCPUs) d(TmpDisk) D(NumNodes)\
		       e(EndTime) g(Group) G(gID) H(Sockets) i(JobID)\
		       I(Cores) j(name) J(Threads) l(TimeLimit) L(TimeLeft)\
		       m(Mem) M(TimeUsed) N(AllocNodes) p(Priority)\
		       P(Partition) Q(Priority) S(StartTime) t(StateCompact)\
		       T(State) u(User) U(uID) v(Reservation) V(TimeSubmit)\
		       z(S:C:T)"

    [[ $cur == - ]] && { offer "$shortoptions" ; return ; }
    [[ $cur == -- ]] && { offer "$longoptions" ; return ; }
    [[ $cur == --* ]] && { offer "$(sed 's/<[^>]*>//g' <<< $longoptions)"; return ; }

    if [[ $cur == *% ]] ;
    then
	offer "%all %a(Account) %A(NTasks/JobID) %B(BatchHost) %c(mincpu)\
	       %C(Ncpus) %d(minTmp) %D(NNodes) %e(end) %E(dependency)\
	       %f(features) %F(ArrayJobID) %g(Group) %G(gID) %h(shared)\
	       %H(Nsockets) %i(JobID) %I(Ncores/socket) %j(name)\
	       %J(threads/core) %k(comment) %K(arrayindex) %l(timelimit)\
	       %L(timeleft) %m(mem) %M(time) %n(reqnodes) %N(alloc_nodes)\
	       %o(command) %O(contiguous) %p(priority) %P(partition) %q(QOS)\
	       %Q(priority) %r(reason) %R(nodelist/reason) %s(selecplugin)\
	       %S(starttime) %t(state) %T(state) %u(user) %U(uID)\
	       %v(reservation) %V(submittime) %w(wckey) %W(license)\
	       %x(exclnodes) %X(corespec) %y(nice) %Y(schednodes) %z(S:C:T)\
	       %Z(workdir)" ;
	return;
    fi

    case $prev in
    --account|-A) offer_list "$(_accounts)" ;;
    --jobs|-j) offer_list "$(_jobs)" ;;
    --clusters|-M) offer_list "$(_clusters)" ;;
    --name|-n) offer_list "$(_jobnames)" ;;
    --qos|-q) offer_list "$(_qos)" ;;
    --user|-u) offer_list "$(_users)" ;;
    --states|-t) offer_list "$jobstates" ;;
    --format|-o) offer "\\\"%" ;;
    --Format|-O) offer "$formatlong" ;;
    --partition|-p) offer_list "$(_partitions)" ;;
    --reservation|-R) offer_list "$(_reservations)" ;;
    --sort|-S) offer_list "$sortoptions" ;;
    --nodelist|-w) offer_list "$(_nodes)" ;;
    --licenses|-L) offer_list "$(_licenses)" ;;
    esac
}
complete -F _squeue squeue

_scancel()
{
    _get_comp_words_by_ref cur prev words cword
    _split_long_opt

    local shortoptions=" -A -b -f -H -i -M -n -p -q -Q -R -s -t -u -v -V -w"
    local longoptions="--account=<account_list> --batch --clusters=<string>\
		       --ctld --full --help --hurry --interactive\
		       --jobname=<job_name> --me --name=<job_name>\
		       --nodelist=<hostlist> --partition=<partition_name>\
		       --qos=<qos_list> --quiet\
		       --reservation=<reservation_name> --sibling=<cluster>\
		       --signal=<signal_name> --state=<state_list> --usage\
		       --user<user_list> --verbose --version --wckey=<wckey>"

    [[ $cur == - ]] && { offer "$shortoptions" ; return ; }
    [[ $cur == -- ]] && { offer "$longoptions" ; return ; }
    [[ $cur == --* ]] && { offer "$(sed 's/<[^>]*>//g' <<< $longoptions)"; return ; }

    case $prev in
    --account|-A) offer_list "$(_accounts)" ;;
    --clusters|-M) offer_list "$(_clusters)" ;;
    --name|--jobname|-n) offer_list "$(_jobnames)" ;;
    --nodelist|-w) offer_list "$(_nodes)" ;;
    --partition|-p) offer_list "$(_partitions)" ;;
    --qos) offer_list "$(_qos)" ;;
    --reservation|-R) offer_list "$(_reservations)" ;;
    --sibling) offer_list "$(_clusters)" ;;
    --state) offer_list "pending running suspended" ;;
    --user|-u) offer_list "$(_users)" ;;
    --wckey) offer_list "$(_wckeys)" ;;
    *) offer_list "$(_jobs)";;
    esac
}
complete -F _scancel scancel

_sshare()
{
    _get_comp_words_by_ref cur prev words cword
    _split_long_opt

    local shortoptions="-a -A -e -h -l -m -M -n -o -p -P -u -U -v -V"
    local longoptions="--accounts=<accounts> --all --clusters=<string>\
		       --format=<format_options> --help --helpformat --long\
		       --noheader --parsable --parsable2 --partition --usage\
		       --users=<user_list> --Users --verbose --version"

    [[ $cur == - ]] && { offer "$shortoptions" ; return ; }
    [[ $cur == -- ]] && { offer "$longoptions" ; return ; }
    [[ $cur == --* ]] && { offer "$(sed 's/<[^>]*>//g' <<< $longoptions)"; return ; }

    case $prev in
    --accounts|-A) offer_list "$(_accounts)" ;;
    --clusters|-M) offer_list "$(_clusters)" ;;
    --format|-o) offer_list "Account Cluster EffectvUsage FairShare\
			     GrpTRESMins GrpTRESRaw ID LevelFS NormShares\
			     NormUsage Partition RawShares RawUsage\
			     TRESRunMins User" ;;
    --users|-u) offer_list "$(_users)" ;;
    esac
}
complete -F _sshare sshare

_sbcast()
{
    _get_comp_words_by_ref cur prev words cword
    _split_long_opt

    local shortoptions="-C -f -F -j -p -s -t -v -V"
    local longoptions="--compress --exclude=<path_list> --fanout=<number>\
		       --force --jobid=<number> --preserve --send-libs=<yes|no>\
		       --size=<size_in_bytes> --timeout=<time_in_seconds>\
		       --verbose --version"

    [[ $cur == - ]] && { offer "$shortoptions" ; return ; }
    [[ $cur == -- ]] && { offer "$longoptions" ; return ; }
    [[ $cur == --* ]] && { offer "$(sed 's/<[^>]*>//g' <<< $longoptions)"; return ; }

    case $prev in
    --jobid|-j) offer_list "$(_jobs)" ;;
    --send-libs) offer_list "yes no" ;;
    *) _filedir ;;
    esac
}
complete -F _sbcast sbcast

_sinfo()
{
    _get_comp_words_by_ref cur prev words cword
    _split_long_opt

    local shortoptions="-a -d -e -h -i -l -M -n -N -o -O -p -r -R -s -S -t -T\
			-v -V"
    local longoptions="--all --clusters=<string> --dead --exact --federation\
		       --format=<format> --Format=<format> --help --hide\
		       --iterate=<seconds> --json --list-reasons --local --long\
		       --noconvert --nodes=<nodelist> --Node --noheader\
		       --partition=<partition> --reservation --responding\
		       --sort=<sortlist> --state=<statelist> --summarize\
		       --usage --verbose --version --yaml"

    [[ $cur == - ]] && { offer "$shortoptions" ; return ; }
    [[ $cur == -- ]] && { offer "$longoptions" ; return ; }
    [[ $cur == --* ]] && { offer "$(sed 's/<[^>]*>//g' <<< $longoptions)"; return ; }

    if [[ $cur == *% ]] ;
    then
	offer "%all %a(Availability) %A(cpu_usage) %B(max_cpus)\
	       %c(cpus_per_node) %C(cpu_usage) %d(diskspace) %D(NNodes)\
	       %E(reason) %f(features) %F(nodes_usage) %g(group) %G(Gres)\
	       %h(shared) %H(timestamp) %l(time_limit) %L(default_time) %m(mem)\
	       %M(preemt_mode) %n(hostnames) %N(node_names) %o(node_addr)\
	       %O(cpu_load) %e(free_mem) %p(partition_prio) %P(partition) %r(root_jobs)\
	       %R(reason) %s(max_job_size) %S(allowed_allocating_nodes)\
	       %t(state) %T(state) %u(user) %U(uID) %w(weight)\
	       %X(sockets_per_node) %Y(cores_per_socket)\
	       %z(extend_process_info) %Z(threads_per_core)" ;
	return;
    fi

    case $prev in
    --clusters|-M) offer_list "$(_clusters)" ;;
    --nodes|-n) offer_list "$(_nodes)" ;;
    --partition|-p) offer_list "$(_partitions)" ;;
    --state) offer_list "alloc allocated cloud comp completing down drain\
			 drained draining fail future futr idle maint mix\
			 mixed no_respond npc perfctrs planned power_down\
			 powering_down powered_down powering_up reboot\
			 resv reserved unk unknown" ;;
    --Format|-O) offer "All AllocMem AllocNodes Available Cluster Comment\
			Cores CPUs CPUsLoad CPUsState DefaultTime Disk\
			Extra features_act FreeMem Gres GresUsed Groups\
			MaxCPUsPerNode Memory NodeAddr NodeAI NodeAIOT\
			NodeHost NodeList Nodes OverSubscribe Partition\
			PartitionName Port PreemptMode PriorityJobFactor\
			PriorityTier Reason Root Size SocketCoreThread\
			Sockets StateCompact StateLong StateComplete Threads\
			Time TimeStamp User UserLong Version Weight" ;;
    --format|-o) offer "\\\"%" ;;
    esac
}
complete -F _sinfo sinfo

_sprio()
{
    _get_comp_words_by_ref cur prev words cword
    _split_long_opt

    local shortoptions="-h -j -l -M -n -o -p -S -u -v -V -w"
    local longoptions="--clusters=<string> --federation --format=<fmtstr>\
		       --help --jobs=<jobids> --local --long --noheader\
		       --norm --partition=<partition> --sibling\
		       --sort=<sort_list> --usage --user=<userlist>\
		       --verbose --version --weights"

    [[ $cur == - ]] && { offer "$shortoptions" ; return ; }
    [[ $cur == -- ]] && { offer "$longoptions" ; return ; }
    [[ $cur == --* ]] && { offer "$(sed 's/<[^>]*>//g' <<< $longoptions)"; return ; }

    if [[ $cur == *% ]] ;
    then
	offer "%a(n_age) %A(w_age) %b(n_assoc_prio) %B(w_assoc_prio)\
	       %c(cluster_name) %f(n_fair-share) %F(w_fair-share) %i(JobId)\
	       %j(n_job_size) %J(w_job_size) %N(Nice adjustment)\
	       %p(n_partition) %P(w_partition) %q(n_qos) %Q(w_qos)\
	       %r(partition) %S(w_admin) %t(n_tres) %T(w_tres) %u(User)\
	       %Y(priority) %y(n_priority)" ;
	return;
    fi

    case $prev in
    --clusters|-M) offer_list "$(_clusters)" ;;
    --format|-o) offer "\\\"%" ;;
    --jobs|-j) offer_list "$(_jobs)" ;;
    --partition|-p) offer_list "$(_partitions)" ;;
    --user|-u) offer_list "$(_users)" ;;
    esac
}
complete -F _sprio sprio

_sacct()
{
    _get_comp_words_by_ref cur prev words cword
    _split_long_opt

    local shortoptions="-a -A -b -B -c -C -D -e -E -f -F -g -h -i -j -k -K -l\
			-L -M -n -N -o -p -P -q -R -s -S -T -u -v -V -W -x -X"
    local longoptions="--accounts=<account_list> --allclusters --allocations\
		       --allusers --associations=<assoc_list> --batch-script\
		       --brief --clusters=<cluster_list> --completion\
		       --constraints --delimiter --duplicates --endtime=<time>\
		       --env-vars --federation --file=<path> --flags=<flags>\
		       --format=<fmtstr> --gid=<gid_list> --group=<group_list>\
		       --help --helpformat --jobs=<joblist> --json --local\
		       --long --name=<jobname_list> --nnodes=<min[-max]>\
		       --noconvert --nodelist=<node_list> --noheader\
		       --parsable --parsable2 --partition=<partition_list>\
		       --qos=<qos> --reason=<reason_list> --starttime=<time>\
		       --state=<state_list> --timelimit-max=<time>\
		       --timelimit-min=<time> --truncate --user=<user_list>\
		       --uid=<uid_list> --units=<KMGTP> --usage --use-local-uid\
		       --verbose --version --wckeys=<wckey_list>\
		       --whole-hetjob=<yes|no> --yaml"

    [[ $cur == - ]] && { offer "$shortoptions" ; return ; }
    [[ $cur == -- ]] && { offer "$longoptions" ; return ; }
    [[ $cur == --* ]] && { offer "$(sed 's/<[^>]*>//g' <<< $longoptions)";
	    return ; }

    case $prev in
    --accounts|-A) offer_list "$(_accounts)" ;;
    --file|-f) _filedir ;;
    --flags|-F) offer_list "SchedBackfill SchedMain SchedSubmit" ;;
    --format|-o) offer "Account AdminComment AllocCPUS AllocNodes AllocTRES\
			AssocID AveCPU AveCPUFreq AveDiskRead AveDiskWrite\
			AvePages AveRSS AveVMSize BlockID Cluster Comment\
			Constraints Container ConsumedEnergy ConsumedEnergyRaw\
			CPUTime CPUTimeRAW DBIndex DerivedExitCode Elapsed\
			ElapsedRaw Eligible End ExitCode Flags GID Group JobID\
			JobIDRaw JobName Layout MaxDiskRead MaxDiskReadNode\
			MaxDiskReadTask MaxDiskWrite MaxDiskWriteNode\
			MaxDiskWriteTask MaxPages MaxPagesNode MaxPagesTask\
			MaxRSS MaxRSSNode MaxRSSTask MaxVMSize MaxVMSizeNode\
			MaxVMSizeTask McsLabel MinCPU MinCPUNode MinCPUTask\
			NCPUS NNodes NodeList NTasks Priority Partition QOS\
			QOSRAW Reason ReqCPUFreq ReqCPUFreqMin ReqCPUFreqMax\
			ReqCPUFreqGov ReqCPUS ReqMem ReqNodes ReqTRES\
			Reservation ReservationId Reserved ResvCPU ResvCPURAW\
			Start State Submit SubmitLine Suspended SystemCPU\
			SystemComment Timelimit TimelimitRaw TotalCPU\
			TRESUsageInAve TRESUsageInMax TRESUsageInMaxNode\
			TRESUsageInMaxTask TRESUsageInMin TRESUsageInMinNode\
			TRESUsageInMinTask TRESUsageInTot TRESUsageOutAve\
			TRESUsageOutMax TRESUsageOutMaxNode TRESUsageOutMaxTask\
			TRESUsageOutMin TRESUsageOutMinNode TRESUsageOutMinTask\
			TRESUsageOutTot UID User UserCPU WCKey WCKeyID\
			WorkDir" ;;
    --group|--gid|-g) _gids ;;
    --jobs|-j) offer_list "$(_jobs)" ;;
    --clusters|-M) offer_list "$(_clusters)" ;;
    --nodelist|-N) offer_list "$(_nodes)" ;;
    --name) offer_list "$(_jobs)" ;;
    --partition) offer_list "$(_partitions)" ;;
    --reason|-R) offer "None Prolog Priority Dependency Resources\
			PartitionNodeLimit PartitionTimeLimit PartitionDown\
			PartitionInactive JobHeldAdmin JobHeldUser BeginTime\
			Licenses AssociationJobLimit AssociationResourceLimit\
			AssociationTimeLimit Reservation ReqNodeNotAvail\
			FrontEndDown PartitionDown NodeDown BadConstraints\
			SystemFailure JobLaunchFailure NonZeroExitCode\
			TimeLimit InactiveLimit InvalidAccount InvalidQOS\
			QOSUsageThreshold QOSJobLimit QOSResourceLimit\
			QOSTimeLimit BlockMaxError BlockFreeAction Cleaning\
			QOSNotAllowed AccountNotAllowed\
			DependencyNeverSatisfied QOSGrpCpuLimit\
			QOSGrpCPUMinutesLimit QOSGrpCPURunMinutesLimit\
			QOSGrpJobsLimit QOSGrpMemLimit QOSGrpNodeLimit\
			QOSGrpSubmitJobsLimit QOSGrpWallLimit\
			QOSMaxCpuPerJobLimit QOSMaxCpuMinutesPerJobLimit\
			QOSMaxNodePerJobLimit QOSMaxWallDurationPerJobLimit\
			QOSMaxCpuPerUserLimit QOSMaxJobsPerUserLimit\
			QOSMaxNodePerUserLimit QOSMaxSubmitJobPerUserLimit\
			QOSMinCpuNotSatisfied AssocGrpCpuLimit\
			AssocGrpCPUMinutesLimit AssocGrpCPURunMinutesLimit\
			AssocGrpJobsLimit AssocGrpMemLimit AssocGrpNodeLimit\
			AssocGrpSubmitJobsLimit AssocGrpWallLimit\
			AssocMaxJobsLimit AssocMaxCpuPerJobLimit \
			AssocMaxCpuMinutesPerJobLimit AssocMaxNodePerJobLimit\
			AssocMaxWallDurationPerJobLimit AssocMaxSubmitJobLimit\
			JobHoldMaxRequeue JobArrayTaskLimit\
			BurstBufferResources BurstBufferStageIn\
			BurstBufferOperation PowerNotAvail PowerReserved\
			AssocGrpUnknown AssocGrpUnknownMinutes\
			AssocGrpUnknownRunMinutes AssocMaxUnknownPerJob\
			AssocMaxUnknownPerNode AssocMaxUnknownMinutesPerJob\
			AssocMaxCpuPerNode AssocGrpMemMinutes\
			AssocGrpMemRunMinutes AssocMaxMemPerJob\
			AssocMaxMemPerNode AssocMaxMemMinutesPerJob\
			AssocGrpNodeMinutes AssocGrpNodeRunMinutes\
			AssocMaxNodeMinutesPerJob AssocGrpEnergy\
			AssocGrpEnergyMinutes AssocGrpEnergyRunMinutes\
			AssocMaxEnergyPerJob AssocMaxEnergyPerNode\
			AssocMaxEnergyMinutesPerJob AssocGrpGRES\
			AssocGrpGRESMinutes AssocGrpGRESRunMinutes\
			AssocMaxGRESPerJob AssocMaxGRESPerNode\
			AssocMaxGRESMinutesPerJob AssocGrpLicense\
			AssocGrpLicenseMinutes AssocGrpLicenseRunMinutes\
			AssocMaxLicensePerJob AssocMaxLicenseMinutesPerJob\
			AssocGrpBB AssocGrpBBMinutes AssocGrpBBRunMinutes\
			AssocMaxBBPerJob AssocMaxBBPerNode\
			AssocMaxBBMinutesPerJob QOSGrpUnknown\
			QOSGrpUnknownMinutes QOSGrpUnknownRunMinutes\
			QOSMaxUnknownPerJob QOSMaxUnknownPerNode\
			QOSMaxUnknownPerUser QOSMaxUnknownMinutesPerJob\
			QOSMinUnknown QOSMaxCpuPerNode QOSGrpMemoryMinutes\
			QOSGrpMemoryRunMinutes QOSMaxMemoryPerJob\
			QOSMaxMemoryPerNode QOSMaxMemoryPerUser\
			QOSMaxMemoryMinutesPerJob QOSMinMemory\
			QOSGrpNodeMinutes QOSGrpNodeRunMinutes\
			QOSMaxNodeMinutesPerJob QOSMinNode QOSGrpEnergy\
			QOSGrpEnergyMinutes QOSGrpEnergyRunMinutes\
			QOSMaxEnergyPerJob QOSMaxEnergyPerNode\
			QOSMaxEnergyPerUser QOSMaxEnergyMinutesPerJob\
			QOSMinEnergy QOSGrpGRES QOSGrpGRESMinutes\
			QOSGrpGRESRunMinutes QOSMaxGRESPerJob QOSMaxGRESPerNode\
			QOSMaxGRESPerUser QOSMaxGRESMinutesPerJob\
			QOSMinGRES QOSGrpLicense QOSGrpLicenseMinutes\
			QOSGrpLicenseRunMinutes QOSMaxLicensePerJob\
			QOSMaxLicensePerUser QOSMaxLicenseMinutesPerJob\
			QOSMinLicense QOSGrpBB QOSGrpBBMinutes\
			QOSGrpBBRunMinutes QOSMaxBBPerJob QOSMaxBBPerNode\
			QOSMaxBBPerUser AssocMaxBBMinutesPerJob QOSMinBB\
			DeadLine MaxBBPerAccount MaxCpuPerAccount\
			MaxEnergyPerAccount MaxGRESPerAccount MaxNodePerAccount\
			MaxLicensePerAccount MaxMemoryPerAccount\
			MaxUnknownPerAccount MaxJobsPerAccount\
			MaxSubmitJobsPerAccount PartitionConfig\
			AccountingPolicy FedJobLock OutOfMemory MaxMemPerLimit\
			AssocGrpBilling AssocGrpBillingMinutes\
			AssocGrpBillingRunMinutes AssocMaxBillingPerJob\
			AssocMaxBillingPerNode AssocMaxBillingMinutesPerJob\
			QOSGrpBilling QOSGrpBillingMinutes\
			QOSGrpBillingRunMinutes QOSMaxBillingPerJob\
			QOSMaxBillingPerNode QOSMaxBillingPerUser\
			QOSMaxBillingMinutesPerJob MaxBillingPerAccount\
			QOSMinBilling ReservationDeleted" ;;
    --state|-s) offer "BOOT_FAIL CANCELLED COMPLETED DEADLINE FAILED NODE_FAIL\
		       OUT_OF_MEMORY PENDING PREEMPTED RUNNING REQUEUED\
		       RESIZING REVOKED SUSPENDED TIMEOUT" ;;
    --qos) offer_list "$(_qos)" ;;
    --user|--uid|-u) offer_list "$(_users)" ;;
    --units) offer "K M G T P" ;;
    --wckeys|-W) offer_list "$(_wckeys)" ;;
    --whole-hetjob) offer "yes no" ;;
    esac
}
complete -F _sacct sacct

_salloc()
{
    _get_comp_words_by_ref cur prev words cword
    _split_long_opt

    local shortoptions="-A -B -c -C -d -D -F -G -h -H -I -J -k -K -L -m -M -n \
			-N -O -p -q -Q -s -S -t -v -V -w -x"
    local longoptions="--account=<account> --acctg-freq=<datatype><interval>\
		       --bb=<spec> --bbf=<file_name> --begin=<time> --bell\
		       --chdir=<path> --cluster-constraint=<list>\
		       --clusters=<string> --comment=<string>\
		       --constraint=<list> --container=<path> --contiguous\
		       --cores-per-socket=<number> --core-spec=<num>\
		       --cpu-freq=<p1[-p2[:p3]]>\
		       --cpus-per-gpu=<ncpus> --cpus-per-task=<ncpus>\
		       --deadline=<opt> --delay-boot=<minutes>\
		       --dependency=<deplist> --distribution=<dist>\
		       --exclude=<nodelist>\
		       --exclusive --nodefile=<nodefile>\
		       --extra-node-info=<sockets[:cores[:threads]]>\
		       --get-user-env --gid=<group>\
		       --gpu-bind=<list> --gpu-freq=<list> --gpus=<list> \
		       --gpus-per-node=<list> --gpus-per-socket=<list>\
		       --gpus-per-task=<list> --gres=<list> --gres-flags=<list>\
		       --help --hint=<type> --hold\
		       --immediate\
		       --job-name=<jobname>\
		       --kill-command\
		       --licenses=<licenses>\
		       --mail-type=<type> --mail-user=<email> --mcs-label\
		       --mem=<MB> --mem-bind=<type> --mem-per-cpu=<MB>\
		       --mem-per-gpu=<MB> --mincpus=<number>\
		       --network=<type> --nice=<[adjustment]> --no-bell\
		       --no-kill --no-shell --nodefile=<nodefile>\
		       --nodelist=<nodelist> --nodes=<minnodes[-maxnodes]>\
		       --ntasks=<number>\
		       --ntasks-per-core=<number> --ntasks-per-gpu=<ntasks>\
		       --ntasks-per-node=<ntasks> --ntasks-per-socket=<ntasks>\
		       --overcommit --oversubscribe\
		       --partition=<partitionname> --power=<flags>\
		       --priority=<value>\
		       --profile=<all|none|energy|task|lustre|network>\
		       --qos=<qos> --quiet\
		       --reboot --reservation=<name>\
		       --signal=<spec>\
		       --sockets-per-node=<number> --spread-job\
		       --switches=<count><max-time>\
		       --thread-spec=<num> --threads-per-core=<number>\
		       --time=<time> --time-min=<time> --tmp=<MB>\
		       --uid=<user> --usage --use-min-nodes\
		       --verbose --version\
		       --wait-all-nodes=<0|1> --wckey=<wckey>\
		       --x11"


    [[ $cur == - ]] && { offer "$shortoptions" ; return ; }
    [[ $cur == -- ]] && { offer "$longoptions" ; return ; }
    [[ $cur == --* ]] && { offer "$(sed 's/<[^>]*>//g' <<< $longoptions)";
	    return ; }

    case $prev in
    --account|-A) offer_list "$(_accounts)" ;;
    --acctg-freq) offer "task= energy= network= filesystem=" ;;
    --bbf) _filedir ;;
    --chdir|-D) _filedir ;;
    --clusters|-M) offer_list "$(_clusters)" ;;
    --constraint|-C) offer_list "$(_features)" ;;
    --container) _filedir ;;
    --dependency|-d) offer_list "after: afterany: afterburstbuffer: aftercorr:\
				 afternotok: afterok: expand: singleton" ;;
    --distribution|-m) offer "block cyclic plane arbitrary" ;;
    --exclude|-x) offer_list "$(_nodes)" ;;
    --gid) _gids ;;
    --gpu-bind)
        if [[ "$cur" =~ ^verbose,.* ]]; then
            offer "verbose,none verbose,closest verbose,single:\
	    verbose,per_task: verbose,map_gpu: verbose,mask_gpu:"
        else
            offer "verbose, none closest single: per_task: map_gpu: mask_gpu:"
        fi
        ;;
    --gres) offer_list "$(_gres)" ;;
    --gres-flags) offer_list "disable-binding enforce-binding" ;;
    --hint) offer "compute_bound memory_bound multithread nomultithread help" ;;
    --licenses|-L) offer_list "$(_licenses)" ;;
    --mail-type) offer_list "BEGIN END FAIL REQUEUE ALL INVALID_DEPEND\
			     STAGE_OUT TIME_LIMIT TIME_LIMIT_90\
			     TIME_LIMIT_80 TIME_LIMIT_50" ;;
    --mem-bind) offer "help local map_mem: mask_mem: none prefer quiet rank\
		       sort verbose" ;;
    # TODO --network) _configured_interfaces ;;
    --nodefile|-F) _filedir ;;
    --nodelist|-w) offer_list "$(_nodes)" ;;
    --partition|-p) offer_list "$(_partitions)" ;;
    --profile) offer_list "all none energy task lustre network" ;;
    --qos|-q) offer_list "$(_qos)" ;;
    --reservation) offer_list "$(_reservations)" ;;
    --uid) offer_list "$(_users)" ;;
    --wait-all-nodes) offer_list "1 0" ;;
    --wckey) offer_list "$(_wckeys)" ;;
    --x11) offer_list "all first last" ;;
    :|afterany|after|afternotok|afterok|expand) offer_list "$(_jobs)" ;;
    esac
}
complete -F _salloc salloc

_sbatch()
{
    _get_comp_words_by_ref cur prev words cword
    _split_long_opt

    local shortoptions="-a -A -b -B -c -C -d -D -e -F -G -h -H -i -J -k -L\
			-m -M -n -N -o -O -p -q -Q -s -S -t -v -V -w -W -x"
    local longoptions="--account=<account> --acctg-freq=<datatype><interval>\
		       --array=<indexes>\
		       --batch=<list> --bb=<spec> --bbf=<file_name>\
		       --begin=<time>\
		       --chdir=<path> --cluster-constraint=<list>\
		       --clusters=<string> --comment=<string>\
		       --constraint=<list> --container=<path> --contiguous\
		       --core-spec=<num> --cores-per-socket=<number>\
		       --cpu-freq=<p1[-p2[:p3]]> --cpus-per-gpu=<ncpus>\
		       --cpus-per-task=<number>\
		       --deadline=<OPT> --delay-boot=<minutes>\
		       --dependency=<deplist> --distribution=<dist>\
		       --error=<filename_pattern> --exclude=<nodename_list>\
		       --exclusive --export=<environment_variables|ALL|NONE>\
		       --export-file=<filename|fd>\
		       --extra-node-info=<sockets[:cores[:threads]]>\
		       --get-user-env --gid=<group>\
		       --gpu-bind=<list> --gpu-freq=<list> --gpus=<list> \
		       --gpus-per-node=<list> --gpus-per-socket=<list>\
		       --gpus-per-task=<list> --gres=<list> --gres-flags=<list>\
		       --help --hint=<type> --hold\
		       --ignore-pbs --input=<filename>\
		       --job-name=<jobname>\
		       --kill-on-invalid-dep=<yes|no>\
		       --licenses=<license>\
		       --mail-type=<type> --mail-user=<user> --mcs-label=<mcs>\
		       --mem=<MB> --mem-bind=<type> --mem-per-cpu=<MB>\
		       --mem-per-gpu=<MB> --mincpus=<n>\
		       --network=<type> --nice --no-kill\
		       --no-requeue --nodefile=<nodefile>\
		       --nodelist=<nodename_list>\
		       --nodes=<minnodes[-maxnodes]> --ntasks=<number>\
		       --ntasks-per-core=<ntasks> --ntasks-per-gpu=<ntasks>\
		       --ntasks-per-node=<ntasks>\
		       --ntasks-per-socket=<ntasks>\
		       --open-mode --output=<filename> --overcommit\
		       --oversubscribe --parsable --partition=<partition_names>\
		       --power=<flags> --priority=<value> --profile=<type>\
		       --propagate=<limit>\
		       --qos=<qos> --quiet\
		       --reboot --requeue --reservation=<name>\
		       --signal=<spec> --sockets-per-node=<sockets>\
		       --spread-job --switches=<type>\
		       --test-only --thread-spec=<num>\
		       --threads-per-core=<threads> --time=<time>\
		       --time-min=<time> --tmp=<MB>\
		       --uid=<user> --usage --use-min-nodes\
		       --verbose --version\
		       --wait --wait-all-nodes=<value> --wckey=<wckey>\
		       --wrap=<command_string>"

    [[ $cur == - ]] && { offer "$shortoptions" ; return ; }
    [[ $cur == -- ]] && { offer "$longoptions" ; return ; }
    [[ $cur == --* ]] && { offer "$(sed 's/<[^>]*>//g' <<< $longoptions)"; return ; }

    case $prev in
    --account|-A) offer_list "$(_accounts)" ;;
    --acctg-freq) offer "task= energy= network= filesystem=" ;;
    --bbf) _filedir ;;
    --chdir|-D) _filedir ;;
    --clusters|-M) offer_list "$(_clusters)" ;;
    --constraint|-C) offer_list "$(_features)" ;;
    --container) _filedir ;;
    --dependency|-d) offer "after: afterany: afterburstbuffer: aftercorr:\
			    afternotok: afterok: expand: singleton" ;;
    --distribution|-m) offer_list "block cyclic plane arbitrary" ;;
    --exclude|-x) offer_list "$(_nodes)" ;;
    --gid) _gids ;;
    --gpu-bind)
        if [[ "$cur" =~ ^verbose,.* ]]; then
            offer "verbose,none verbose,closest verbose,single: verbose,per_task: verbose,map_gpu: verbose,mask_gpu:"
        else
            offer "verbose, none closest single: per_task: map_gpu: mask_gpu:"
        fi
        ;;
    --gres) offer_list "$(_gres)" ;;
    --gres-flags) offer_list "disable-binding enforce-binding" ;;
    --hint) offer "compute_bound memory_bound multithread nomultithread help" ;;
    --licenses|-L) offer_list "$(_licenses)" ;;
    --mail-type) offer_list "NONE BEGIN END FAIL REQUEUE ALL INVALID_DEPEND\
			     STAGE_OUT TIME_LIMIT TIME_LIMIT_90\
			     TIME_LIMIT_80 TIME_LIMIT_50" ;;
    --mem-bind) offer "help local quiet verbose none prefer rank sort local\
		       map_mem: mask_mem:" ;;
    # TODO --network) _configured_interfaces ;;
    --nodefile|-F) _filedir ;;
    --nodelist|-w) offer_list "$(_nodes)" ;;
    --partition|-p) offer_list "$(_partitions)" ;;
    --profile) offer_list "all none energy task lustre network" ;;
    --propagate) offer_list "ALL NONE AS CORE CPU DATA FSIZE MEMLOCK NOFILE \
			     NPROC RSS STACK" ;;
    --qos|-q) offer_list "$(_qos)" ;;
    --reservation) offer_list "$(_reservations)" ;;
    --uid) offer_list "$(_users)" ;;
    --wait-all-nodes) offer_list "1 0" ;;
    --wckey) offer_list "$(_wckeys)" ;;
    :|afterany|after|afternotok|afterok) offer_list "$(_jobs)" ;;
    *)  _filedir
    esac
}
complete -o filenames -F _sbatch sbatch

_srun()
{
    _get_comp_words_by_ref cur prev words cword
    _split_long_opt

    local shortoptions="-A -b -B -c -C -d -D -e -E -F -G -h -H -i -I -J -k -K\
			-l -L -m -M -n -N -o -O -p -q -Q -r -s -S -t -T -u -v\
			-V -w -W -x -X -Z"
    local longoptions="--accel-bind= --account=<account>\
		       --acctg-freq=<datatype><interval>\
		       --bb=<spec> --bbf=<file_name> --bcast\
		       --bcast-exclude=<NONE|path1,...,pathN> --begin=<time>\
		       --chdir=<path> --cluster-constraint=<list>\
		       --clusters=<string> --comment=<string>\
		       --compress --constraint=<list>\
		       --container=<path> --contiguous\
		       --core-spec=<num> --cores-per-socket=<cores>\
		       --cpu-bind=<type> --cpu-freq=<p1[-p2[:p3]]>\
		       --cpus-per-gpu=<ncpus> --cpus-per-task=<ncpus>\
		       --deadline=<OPT> --delay-boot=<minutes>\
		       --dependency=<dependency_list> --disable-status\
		       --distribution=<type>\
		       --epilog=<executable> --error=<filename_pattern>\
		       --exact --exclude= --exclusive\
		       --export=<[ALL,]environment variables|ALL|NONE>\
		       --extra-node-info=<spec>\
		       --gid=<group>\
		       --gpu-bind=<list> --gpu-freq=<list> --gpus=<list> \
		       --gpus-per-node=<list> --gpus-per-socket=<list>\
		       --gpus-per-task=<list> --gres=<list> --gres-flags=<list>\
		       --help --het-group=<expr> --hint=<type> --hold\
		       --immediate --input=<mode>\
		       --job-name=<jobname> --jobid=<jobid>\
		       --kill-on-bad-exit=<0|1>\
		       --label --licenses=<license>\
		       --mail-type=<type> --mail-user=<user> --mcs-label=<mcs>\
		       --mem=<MB> --mem-bind=<type> --mem-per-cpu=<MB>\
		       --mem-per-gpu=<size[units]> --mincpus=<n>\
		       --mpi=<mpi_type> --msg-timeout=<seconds> --multi-prog\
		       --network=<type> --nice --no-allocate --no-kill\
		       --nodefile=<nodefile>\
		       --nodelist=<host1,host2,... or filename>\
		       --nodes=<minnodes[-maxnodes]> --ntasks=<number>\
		       --ntasks-per-core=<ntasks> --ntasks-per-gpu=<ntasks>\
		       --ntasks-per-node=<ntasks>\
		       --ntasks-per-socket=<ntasks>\
		       --open-mode=<append|truncate>\
		       --output=<filename_pattern> --overcommit --overlap\
		       --oversubscribe\
		       --partition=<partition_names> --power=<flags>\
		       --preserve-env --priority=<value> --profile=<type>\
		       --prolog=<executable> --propagate --pty\
		       --qos=<qos> --quiet --quit-on-interrupt\
		       --reboot --relative=<n>\
		       --reservation=<reservation_names> --resv-ports\
		       --send-libs --signal=<spec>\
		       --slurmd-debug=<level> --sockets-per-node=<sockets>\
		       --spread-job --switches=<count>\
		       --task-epilog=<executable> --task-prolog=<executable>\
		       --test-only --thread-spec=<num> --threads=<nthreads>\
		       --threads-per-core=<threads> --time=<time>\
		       --time-min=<time> --tmp=<MB>\
		       --uid=<user> --unbuffered --usage --use-min-nodes\
		       --verbose --version\
		       --wait=<seconds> --wckey=<wckey>\
		       --x11\
"

    [[ $cur == - ]] && { offer "$shortoptions" ; return ; }
    [[ $cur == -- ]] && { offer "$longoptions" ; return ; }
    [[ $cur == --* ]] && { offer "$(sed 's/<[^>]*>//g' <<< $longoptions)"; return ; }

    case $prev in
    --accel-bind) offer "g n v" ;;
    --account|-A) offer_list "$(_accounts)" ;;
    --acctg-freq) offer "task= energy= network= filesystem=" ;;
    --bbf) _filedir ;;
    --begin|-b) offer $(date -dtomorrow +"%Y-%m-%d");;
    --chdir|-D) _filedir ;;
    --clusters|-M) offer_list "$(_clusters)" ;;
    --constraint|-C) offer_list "$(_features)" ;;
    --container) _filedir ;;
    --cpu-bind)
        if [[ "$cur" =~ ^verbose,.* ]]; then
            offer "verbose,none verbose,rank verbose,map_cpu: verbose,mask_cpu:\
		   verbose,rank_ldom verbose,map_ldom: verbose,mask_ldom:\
		   verbose,sockets verbose,cores verbose,threads verbose,ldoms\
		   verbose,boards"
        elif [[ "$cur" =~ ^quiet,.* ]]; then
            offer "quiet,none quiet,rank quiet,map_cpu: quiet,mask_cpu:\
		   quiet,rank_ldom quiet,map_ldom: quiet,mask_ldom:\
		   quiet,sockets quiet,cores quiet,threads quiet,ldoms\
		   quiet,boards"
        else
            offer "verbose, quiet, help none rank map_cpu: mask_cpu: rank_ldom\
		   map_ldom: mask_ldom: sockets cores threads ldoms boards"
        fi ;;
    --dependency|-d) offer "after: afterany: afterburstbuffer: aftercorr:\
			    afternotok: afterok: expand: singleton" ;;
    --distribution|-m) offer "block cyclic plane arbitrary pack nopack" ;;
    --exclude|-x) offer_list "$(_nodes)" ;;
    --gid) _gids ;;
    --gpu-bind)
        if [[ "$cur" =~ ^verbose,.* ]]; then
            offer "verbose,none verbose,closest verbose,single:\
		   verbose,per_task: verbose,map_gpu: verbose,mask_gpu:"
        else
            offer "verbose, none closest single: per_task: map_gpu: mask_gpu:"
        fi
        ;;
    --gres) offer_list "$(_gres)" ;;
    --gres-flags) offer_list "disable-binding enforce-binding" ;;
    --hint) offer "compute_bound memory_bound multithread nomultithread help" ;;
    --job-name|-J) "$(_jobnames)" ;;
    --jobid) offer_list "$(_jobs)" ;;
    --kill-on-bad-exit|-K) offer "0 1" ;;
    --licenses|-L) offer_list "$(_licenses)" ;;
    --mail-type) offer_list "NONE BEGIN END FAIL REQUEUE ALL INVALID_DEPEND\
			     STAGE_OUT TIME_LIMIT TIME_LIMIT_90\
			     TIME_LIMIT_80 TIME_LIMIT_50" ;;
    --mem-bind)
        if [[ "$cur" =~ ^verbose,.* ]]; then
            offer "verbose,local verbose,map_mem: verbose,mask_mem: verbose,none\
		   verbose,nosort verbose,prefer verbose,rank\
		   verbose,sort"
        elif [[ "$cur" =~ ^quiet,.* ]]; then
            offer "quiet,local quiet,map_mem: quiet,mask_mem: quiet,none\
		   quiet,nosort quiet,prefer quiet,rank quiet,sort"
        else
            offer "verbose, quiet, help local map_mem: mask_mem: none nosort\
		   prefer rank sort"
        fi ;;
    --mpi) offer "openmpi pmi2 pmix none" ;;
    # TODO --network) _configured_interfaces ;;
    --nodefile|-F) _filedir ;;
    --nodelist|-w) offer_list "$(_nodes)" ;;
    --open-mode) offer "append truncate" ;;
    --partition|-p) offer_list "$(_partitions)" ;;
    --profile) offer_list "all none energy task filesystem network" ;;
    --prolog|--task-epilog|--task-prolog) _filedir ;;
    --propagate) offer_list "ALL NONE AS CORE CPU DATA FSIZE MEMLOCK NOFILE \
			     NPROC RSS STACK" ;;
    --qos) offer_list "$(_qos)" ;;
    --reservation) offer_list "$(_reservations)" ;;
    --slurmd-debug) offer_list "quiet fatal error info verbose" ;;
    --uid) offer_list "$(_users)" ;;
    --wait-all-nodes) offer_list "1 0" ;;
    --wckey) offer_list "$(_wckeys)" ;;
    --x11) offer_list "all first last" ;;
    :|afterany|after|afternotok|afterok) offer_list "$(_jobs)" ;;
    *)  COMPREPLY=( $( compgen -c -- "$cur" ) ) ; return
    esac
}
complete -F _srun srun

_sattach()
{
    _get_comp_words_by_ref cur prev words cword
    _split_long_opt

    local shortoptions=" -h -l -Q -u -v -V"
    local longoptions="--error-filter=<task_number> --help\
		       --input-filter=<task_number> --label --layout\
		       --output-filter=<task_number> --pty --quiet --usage\
		       --verbose --version"

    [[ $cur == - ]] && { offer "$shortoptions" ; return ; }
    [[ $cur == -- ]] && { offer "$longoptions" ; return ; }
    [[ $cur == --* ]] && { offer "$(sed 's/<[^>]*>//g' <<< $longoptions)"; return ; }
}
complete -F _sattach sattach

_sdiag()
{
    _get_comp_words_by_ref cur prev words cword
    _split_long_opt

    local shortoptions="-a -h -i -M -r -t -T -V"
    local longoptions="--all --cluster=<string> --help --reset --sort-by-id\
                       --sort-by-time --sort-by-time2 --usage --version"

    [[ $cur == - ]] && { offer "$shortoptions" ; return ; }
    [[ $cur == -- ]] && { offer "$longoptions" ; return ; }
    [[ $cur == --* ]] && { offer "$(sed 's/<[^>]*>//g' <<< $longoptions)"; return ; }

    case $prev in
    --cluster|-M) offer_list "$(_clusters)" ;;
    esac
}
complete -F _sdiag sdiag

_sstat()
{
    _get_comp_words_by_ref cur prev words cword
    _split_long_opt

    local shortoptions="-a -e -h -i -j -n -o -p -P -v -V"
    local longoptions="--allsteps --fields=<format> --format=<format> --help\
		       --helpformat --jobs --noconvert --noheader --parsable\
		       --parsable2 --pidformat --usage --verbose --version"

    [[ $cur == - ]] && { offer "$shortoptions" ; return ; }
    [[ $cur == -- ]] && { offer "$longoptions" ; return ; }
    [[ $cur == --* ]] && { offer "$(sed 's/<[^>]*>//g' <<< $longoptions)"; return ; }

    case $prev in
    --fields|--format|-o) offer "AveCPU AveCPUFreq AveDiskRead AveDiskWrite\
				 AvePages AveRSS AveVMSize ConsumedEnergy\
				 ConsumedEnergyRaw JobID MaxDiskRead\
				 MaxDiskReadNode MaxDiskReadTask MaxDiskWrite\
				 MaxDiskWriteNode MaxDiskWriteTask MaxPages\
				 MaxPagesNode MaxPagesTask MaxRSS MaxRSSNode\
				 MaxRSSTask MaxVMSize MaxVMSizeNode\
				 MaxVMSizeTask MinCPU MinCPUNode MinCPUTask\
				 Nodelist NTasks Pids ReqCPUFreq ReqCPUFreqMin\
				 ReqCPUFreqMax ReqCPUFreqGov TRESUsageInAve\
				 TRESUsageInMax TRESUsageInMaxNode\
				 TRESUsageInMaxTask TRESUsageInMin\
				 TRESUsageInMinNode TRESUsageInMinTask\
				 TRESUsageInTot TRESUsageOutAve\
				 TRESUsageOutMax TRESUsageOutMaxNode\
				 TRESUsageOutMaxTask TRESUsageOutMin\
				 TRESUsageOutMinNode TRESUsageOutMinTask\
				 TRESUsageOutTot" ;;
    esac
}
complete -F _sstat sstat
