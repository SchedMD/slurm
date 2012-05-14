###############################################################################
#
# Bash completion for Slurm
#
###############################################################################
#  Copyright (C) 2012 Damien François. <damien.francois@uclouvain.Be>
#  Written by Damien François. <damien.francois@uclouvain.Be>.
#
#  This file is part of SLURM, a resource management program.
#  For details, see <http://www.schedmd.com/slurmdocs/>.
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

_scontrol()
{
	local cur=${COMP_WORDS[COMP_CWORD]}
	local prev=${COMP_WORDS[COMP_CWORD-1]}

	local subopts=""

	case "$prev" in
	node)
		local pprev=${COMP_WORDS[COMP_CWORD-2]}
		if [[ "$pprev" == "show" ]]; then
			subopts=$( scontrol show nodes | grep NodeName | cut -c 10- | cut -f 1 -d' ')
		elif [[ "$pprev" == "update" ]]; then
			subopts="NodeName= Features= Gres= Reason= State= Weight="
		else
			subopts=""
		fi
		;;

	job)
		local pprev=${COMP_WORDS[COMP_CWORD-2]}
		if [[ "$pprev" == "show" ]]; then
			subopts=$( scontrol -o show jobs | cut -d' ' -f 1 | cut -d'=' -f 2 )
		elif [[ "$pprev" == "update" ]]; then
			subopts="Account= Conn-Type= Contiguous= Dependency= EligibleTime="
			subopts="$subopts ExcNodeList= Features= Geometry= Gres= JobId="
			subopts="$subopts JobId= MinCpusNode= MinMemoryNode= MinTmpDiskNodea"
			subopts="$subopts Name= Nice= NodeList= NumCPUs= NumNodes= NumTasks="
			subopts="$subopts Partition= Priority= QOS= ReqCores= ReqNodelist="
			subopts="$subopts ReqSockets= ReqThreads= Requeue= ReservationName="
			subopts="$subopts Rotate= Shared= StartTime= TimeLimit= WCKey="
		else
			subopts=""
		fi
		;;
	show)
		subopts="config daemons job node partition reservation slurmd step topology"
		subopts="$subopts hostlist hostnames"
		;;
	shutdown)
		subopts="slurmctld controller"
		;;
	setdebug)
		subopts="quiet fatal error info verbose debug debug2 debug3 debug4 debug5"
		;;
	notify | uhold | suspend |  release | requeue | resume | hold )
		subopts=$( scontrol -o show jobs | cut -d' ' -f 1 | cut -d'=' -f 2 )
		;;
	step)
		subopts="StepId= TimeLimit="
		;;
	delete)
		subopts="PartitionName= Reservation="
		;;
	reservation)
		subopts="Reservation= Accounts= Licences= NodeCnt= Nodes= StartTime="
		subopts="$subopts EndTime= Duration= PartitionName=Flags= Features= Users="
		;;
	partition)
		subopts="AllowGroups= AllocNodes= Alternate= Default= DefaultTime="
		subopts="$subopts DisableRootJobs= Hidden= MaxNodes= MatxTime= MinNodes= Nodes="
		subopts="$subopts PartitionName= PreemtpMode= Piority= RootOnly= Shared= State="
		;;
	create)
		subopts="partition reservation"
		;;
	update)
		subopts="job step node partition reservation"
		;;
	disable | enable | able | create | vacate | error | restart)
		subopts=$( scontrol -o show jobs | cut -d' ' -f 1 | cut -d'=' -f 2 )
		;;
	checkpoint)
		subopts="disable enable able create vacate error restart"
		;;
	scontrol)
		if [[ "$cur" == - ]]; then
			subopts="-a -d -h -M -o -Q -v -V"
		elif [[ "$cur" == -- ]]; then
			subopts="--all --details --help --hide --cluster"
			subopts="$subopts --oneliner --quiet --verbose --version"
		else
			subopts="abort checkpoint create completing delete hold notify"
			subopts="$subopts pidinfo listpids ping reconfigure release requeue"
			subopts="$subopts resume setdebug show shutdown suspend takeover"
			subopts="$subopts uhold update version"
		fi
		;;
	*)
		return 0
		;;
	esac
	COMPREPLY=( $(compgen -W "${subopts}" -- $cur) )
}
complete -F _scontrol scontrol

_sreport()
{
	local cur=${COMP_WORDS[COMP_CWORD]}
	local prev=${COMP_WORDS[COMP_CWORD-1]}

	local subopts=""
	local opts4all="All_Clusters Clusters= End= Format= Start="

	case "$prev" in
	user)
		subopts="TopUsage"
		;;
	TopUsage)
		subopts="$opts4all Accounts= Group TopCount= Users="
		;;
	reservation)
		subopts="Utilization"
		;;
	Utilization)
		subopts="$opts4all Names= Nodes="
		;;
	job)
		subopts="SizesByAccount SizesByAccountAndWckey SizesByWckey"
		;;
	SizesByAccount|SizesByAccountAndWckey|SizesByWckey)
		subopts="$opts4all Accounts= FlatView GID= Grouping= Jobs= Nodes= OPartitions= PrintJobCount Users= Wckeys="
		;;
	cluster)
		subopts="AccountUtilizationByUser UserUtilizationByAccount UserUtilizationByWCKey Utilization WCKeyUtilizationByUser"
		;;
	AccountUtilizationByUser|UserUtilizationByAccount|UserUtilizationByWCKey|Utilization|WCKeyUtilizationByUser)
		subopts="$opts4all Accounts= Tree Users= Wckeys="
		;;
	sreport)
		if [[ "$cur" == - ]]; then
			subopts="-a -n -h -p -P -t -v -V"
		elif [[ "$cur" == -- ]]; then
			subopts="--all_clusters --help --noheader --parsable"
			subopts="$subopts --parsable2--quiet --verbose --version"
		else
			subopts="cluster job user reservation"
		fi
		;;
	*)
		return 0
		;;
	esac
	COMPREPLY=( $(compgen -W "${subopts}" -- $cur) )
}
complete -F _sreport sreport
