#!/usr/bin/env bash
################################################################################
# slurm_completion.sh - bash completion script for Slurm client commands.
#
# Setting environment variables can control additional behaviors:
# - SLURM_COMP_VALUE=[0-1]
#   - Description: Toggles value completion from slurmctld, slurmdbd.
#   - Accepted values:
#     - 0 = Disabled -- no call to slurm{ctl,db}d for value completion.
#     - 1 = Enabled -- will make call to slurm{ctl,db}d. (default)
# - SLURM_COMP_HOSTLIST=[0-1]
#   - Description: Alter slurm hostlist comprehension completion behavior.
#   - Accepted values:
#     - 0 = Disabled -- will not convert to hostlist notation.
#     - 1 = Enabled -- will convert to hostlist notation. (default)
# - SLURM_COMPLOG_FILE=</path/to/file.log>
#   - Description: File for script logging to write to.
# - SLURM_COMPLOG_LEVEL=[0-5]
#   - Description: Script will log at this level of detail to specified file.
#   - Accepted values:
#     - 0 = QUIET
#     - 1 = ERROR
#     - 2 = WARN
#     - 3 = INFO (default)
#     - 4 = DEBUG
#     - 5 = TRACE
################################################################################
#  Copyright (C) SchedMD LLC.
#  Written by Skyler Malinowski <malinowski@schedmd.com>
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
################################################################################

# Source guard
(return 0 2>/dev/null) && SOURCED=1 || SOURCED=0
if [ $SOURCED -eq 0 ]; then
	echo "FATAL: this script (slurm_completion.sh) is meant to be sourced." 1>&2
	exit 1
elif [[ -z $BASH_VERSION ]] && [[ -z ${BASH_SOURCE[-1]} ]]; then
	echo "FATAL: this script (slurm_completion.sh) only supports bash." 1>&2
	return
elif ! [[ -f "/usr/share/bash-completion/bash_completion" ]] ||
	! declare -F _completion_loader >/dev/null 2>&1; then
	cat <<-EOF 1>&2
		FATAL: Missing required source file.
		  (1) Install package 'bash-completion'
		  (2) $ source /usr/share/bash-completion/bash_completion
		  (3) $ source slurm_completion.sh
	EOF
	return
fi

# Enable shell options
shopt -s extglob

################################################################################
#			Slurm Completion Logger Functions
################################################################################

# Log at trace level
function __slurm_log_trace() {
	local message="$1"
	local level="5"

	__slurm_comp_logger "${level}" "${message}"
}

# Log at debug level
function __slurm_log_debug() {
	local message="$1"
	local level="4"

	__slurm_comp_logger "${level}" "${message}"
}

# Log at info level
function __slurm_log_info() {
	local message="$1"
	local level="3"

	__slurm_comp_logger "${level}" "${message}"
}

# Log at warn level
function __slurm_log_warn() {
	local message="$1"
	local level="2"

	__slurm_comp_logger "${level}" "${message}"
}

# Log at error level
function __slurm_log_error() {
	local message="$1"
	local level="1"

	__slurm_comp_logger "${level}" "${message}"
}

# Logger function for debugging script
#
# $1: log level
# $2: message
function __slurm_comp_logger() {
	local level="${1-1}"
	local message="$2"
	local loglevels=(
		"QUIET"
		"ERROR"
		" WARN"
		" INFO"
		"DEBUG"
		"TRACE"
	)
	local logfile="${SLURM_COMPLOG_FILE:-}"
	local loglevel="${SLURM_COMPLOG_LEVEL:-1}"

	if [[ -n $logfile ]] && ((level <= loglevel)); then
		local fmt
		fmt="[$(date)][${loglevels[level]}]"
		printf '%s %s\n' "${fmt}" "${message}" >>"${SLURM_COMPLOG_FILE}"
	fi
}

# Get the name of the current function
function __func__() {
	if [[ -n ${FUNCNAME[1]} ]]; then
		echo "${FUNCNAME[1]}"
	else
		echo "unknown"
	fi
}

################################################################################
#			Slurm Shell Completion Functions
################################################################################

# Generate completion reply. Appends a space to completion word if necessary.
# Based on __gitcomp().
#
# $1: List of completion words.
# $2: Prefix to be pre-appended to each completion word (optional).
# $3: Generate possible completion matches for this word (optional).
# $4: Suffix to be appended to each completion word (optional).
# $5: Separator for completion words (optional).
function __slurm_comp() {
	local options="$1"
	local prefix="$2"
	local _cur="${3-$cur}"
	local suffix="$4"
	local ifs="${5-" "}"
	local IFS=$' \t\n'
	COMPREPLY=() # reset

	__slurm_log_debug "$(__func__): prefix='$prefix' _cur='$_cur' suffix='$suffix' ifs='$ifs'"
	__slurm_log_debug "$(__func__): options='$options'"

	local c
	for c in $options; do
		c="$c${suffix-}"
		__slurm_log_trace "$(__func__): for loop: c='$c'"
		if [[ $c == "$_cur"* ]]; then
			case $c in
			*=) ;;     # parameter
			- | --) ;; # flag stub
			*,) ;;     # slurm list notation
			*) c="${c}${ifs}" ;;
			esac
			COMPREPLY+=("${prefix-}$c")
		fi
	done

	__ltrim_colon_completions "${_cur}"

	__slurm_log_info "$(__func__): #COMPREPLY[@]='${#COMPREPLY[@]}'"
	__slurm_log_info "$(__func__): COMPREPLY[*]='${COMPREPLY[*]}'"
}

# Slurm completion helper for count notation
# Example:
#     GRES    = name[[:type]:count][,name[[:type]:count]...]
#     LICENSE = license[@db][:count][,license[@db][:count]...]
#
# $1: list of items for completion
function __slurm_compreply_count() {
	local options="$1"
	local prefix=""
	local suffix=","
	local curlist="${cur%"$suffix"*}"
	local curitem="${cur##*"$suffix"}"
	local compreply=()

	if [[ $curitem == "$curlist" ]]; then
		curlist=""
	elif [[ -n $curlist ]]; then
		prefix="${curlist}${suffix}"
	fi

	__slurm_log_debug "$(__func__): cur='$cur' curitem='$curitem' curlist='$curlist'"

	case "${curitem}" in
	*:)
		__slurm_log_debug "$(__func__): expect count spec"
		;;
	*:+([0-9]))
		__slurm_log_debug "$(__func__): found count spec"
		__slurm_compreply_list "$cur"
		;;
	*)
		__slurm_log_debug "$(__func__): expect item spec"
		options="$(compgen -W "${options[*]}" -S ":" -- "${curitem}")"

		# build array without seen items
		for item in $options; do
			__slurm_log_trace "$(__func__): for loop: item='$item'"
			[[ $item =~ ^[[:space:]]*$ ]] && continue
			[[ $curlist =~ "${item}"[[:digit:]]+"${suffix}"? ]] && continue
			compreply+=("$item")
		done

		__slurm_comp "${compreply[*]}" "${prefix-}" "${curitem}" "" ""
		;;
	esac
}

# Completion function for parameters
function __slurm_compreply_param() {
	local options="$1"
	local compreply=()
	local IFS=$' \t\n'
	local p=""

	__slurm_log_debug "$(__func__): cur='$cur'"
	__slurm_log_debug "$(__func__): options='$options'"

	# build array without seen items
	for param in $options; do
		p="${param%%?(\\)=*}"
		__slurm_log_trace "$(__func__): for loop: param='$param' p*='$p'"
		[[ ${words[*]} =~ ${p}= ]] && continue
		[[ ${words[*]} =~ [[:space:]]+${p}[[:space:]]+ ]] && continue
		compreply+=("$param")
	done

	__slurm_log_trace "$(__func__): #words[@]='${#words[@]}'"
	__slurm_log_trace "$(__func__): words[*]='${words[*]}'"
	__slurm_log_trace "$(__func__): #compreply[@]='${#compreply[@]}'"
	__slurm_log_trace "$(__func__): compreply[*]='${compreply[*]}'"

	__slurm_comp "$(compgen -W "${compreply[*]}" -- "$cur")"
}

# Value completion function for (comma) delimeted items
#
# $1: word list for completions
# $2: reserved words, complete not in list (optional)
# $3: can use hostlist compression (optional)
function __slurm_compreply_list() {
	local options="$1"
	local reserved_words="$2"
	local hostlist_compression="${3-"false"}"
	local _mode="automatic"
	local prefix=""
	local suffix=","
	local curlist="${cur%"$suffix"*}"
	local curitem="${cur##*"$suffix"}"
	local compreply=()
	local IFS=$' \t\n'
	local ifs=" "
	local curlist_hostnames=""
	local curlist_hostlist=""

	# append $reserved_words to $options
	if [[ $cur == "$curitem" ]]; then
		options="${options} ${reserved_words}"
	fi

	if [[ $curitem == "$curlist" ]]; then
		curlist=""
	elif [[ -n $curlist ]]; then
		prefix="${curlist}${suffix}"
	fi

	__slurm_log_debug "$(__func__): cur='$cur' curitem='$curitem' curlist='$curlist'"
	__slurm_log_trace "$(__func__): reserved_words='$reserved_words'"
	__slurm_log_trace "$(__func__): options='$options'"

	if [[ $hostlist_compression == "true" ]]; then
		if ((SLURM_COMP_HOSTLIST == 1)); then
			__slurm_log_info "$(__func__): Slurm hostlist completion is enabled."
		fi
		curlist_hostnames="$(__slurm_hostnames "$curlist")"
		__slurm_log_trace "$(__func__): curlist_hostnames='$curlist_hostnames'"
	fi

	# build array without seen items
	for item in $options; do
		__slurm_log_trace "$(__func__): for loop: item='$item'"
		[[ $item =~ ^[[:space:]]*$ ]] && continue
		[[ $curlist =~ "${item}${suffix}"? ]] && continue
		[[ $curlist_hostnames =~ "${item}${suffix}"? ]] && continue
		compreply+=("$item")
	done

	# compress to hostlist
	if ((SLURM_COMP_HOSTLIST == 1)) && [[ $hostlist_compression == "true" ]]; then
		curlist_hostlist="$(__slurm_hostlist "$curlist_hostnames")"

		if [[ -n $curlist_hostlist ]]; then
			prefix="${curlist_hostlist}${suffix}"
		fi

		local found=0
		local filter=()
		filter=("$(compgen -W "${compreply[*]}" -- "${curitem}")")
		((${#filter[@]} == 1)) && [[ ${filter[0]} == "$curitem" ]] && found=1
		__slurm_log_trace "$(__func__): found='$found' #filter[@]='${#filter[@]}' filter[*]='${filter[*]}'"

		if ((found == 1)); then
			curlist_hostlist="$(__slurm_hostlist "${curlist_hostnames},${curitem}")"
			prefix=""
			curitem="$curlist_hostlist"
			compreply=("$curlist_hostlist")
		fi

		__slurm_log_trace "$(__func__): curlist_hostlist='$curlist_hostlist'"
	fi

	__slurm_log_info "$(__func__): Slurm list completion is in $_mode mode."
	__slurm_log_debug "$(__func__): prefix='$prefix' suffix='$suffix' ifs='$ifs'"
	__slurm_log_debug "$(__func__): #compreply[@]='${#compreply[@]}'"
	__slurm_log_debug "$(__func__): compreply[*]='${compreply[*]}'"

	if [[ $cur != "" ]] &&
		[[ " ${reserved_words} " =~ [[:space:]]+"${cur//$suffix/}"[[:space:]]+ ]]; then
		cur="${cur//$suffix/}"
		__slurm_log_info "$(__func__): reserved word '$cur' detected."
		__slurm_compreply "$reserved_words"
	else
		__slurm_comp "$(compgen -W "${compreply[*]}" -- "${curitem}")" "${prefix-}" "${curitem}" "${suffix-}" "${ifs-}"
	fi
}

# Completion function for an item
#
# $1: options for single completion
function __slurm_compreply() {
	local options="$1"

	__slurm_log_debug "$(__func__): cur='$cur'"
	__slurm_log_debug "$(__func__): #options[@]='${#options[@]}'"
	__slurm_log_debug "$(__func__): options[*]='${options[*]}'"

	__slurm_comp "$(compgen -W "${options[*]}" -- "$cur")"
}

# Generic flag completion function
#
# $1: client command context
# STDOUT: results
function __slurm_autocompletion() {
	local context="$1"
	local query="$cur"
	local output=""

	if [[ -z $cmd ]]; then
		__slurm_log_error "$(__func__): client command context is empty."
		echo ""
		return 1
	fi

	local cmd="$context --autocomplete=\"$query\""
	output="$(__slurm_func_wrapper "$cmd")"

	__slurm_log_trace "$(__func__): output='$output'"

	echo "${output}"
}

# Generic slurm command completion function
#
# $1: function name
# RET: 0 = success, 1 = failure
function __slurm_comp_command() {
	local completion_func="$1"

	__slurm_log_debug "$(__func__): completion_func='$completion_func'."

	if declare -f -- "$completion_func" >/dev/null 2>&1; then
		__slurm_log_trace "$(__func__): function '$completion_func' is defined."
		$completion_func
		return $?
	else
		__slurm_log_error "$(__func__): function '$completion_func' is not defined."
		return 1
	fi
}

# Search words[@] for an item from input array
#
# $1: array of strings
# RET: found subcommand
function __slurm_find_subcmd() {
	local items=("$1")
	local item=""
	local IFS=$' \t\n'

	local c=1 word=""
	while [[ $c -lt ${cword-1} ]]; do
		word="${words[c]}"
		__slurm_log_trace "$(__func__): while loop: c='$c' word='$word'"
		if [[ " ${items[*]} " =~ [[:space:]]+"${word}"[[:space:]]+ ]]; then
			item="$word"
			break
		fi
		((c++))
	done

	__slurm_log_debug "$(__func__): item='$item'"
	__slurm_log_trace "$(__func__): #words[@]='${#words[@]}'"
	__slurm_log_trace "$(__func__): words[*]='${words[*]}'"
	__slurm_log_trace "$(__func__): #items[@]='${#items[@]}'"
	__slurm_log_trace "$(__func__): items[*]='${items[*]}'"

	echo "${item}"
}

# Search words[@] for any parameters from given array
#
# $1: parameter array
# RET: found parameter
function __slurm_find_param() {
	local params="$1"
	local param=""
	local IFS=$' \t\n'

	for p in $params; do
		__slurm_log_trace "$(__func__): for loop: p='$p' p*='${p%%=*}'"
		if [[ ${words[*]} =~ ${p%%?(\\)=*} ]]; then
			param="$p"
			break
		fi
	done

	__slurm_log_debug "$(__func__): param='$param'"
	__slurm_log_trace "$(__func__): #words[@]='${#words[@]}'"
	__slurm_log_trace "$(__func__): words[*]='${words[*]}'"
	__slurm_log_trace "$(__func__): params='$params'"

	echo "${param}"
}

# Adapted from bash-completion _split_longopt()
#
# RET: 0 = made split, 1 = no split
function __slurm_split_opt() {
	__slurm_log_trace "$(__func__): prev='$prev' cur='$cur'"

	case "${cur}" in
	--?*=* | ?*=*)
		prev="${cur%%?(\\)=*}"
		cur="${cur#*?(\\)=}"
		;;
	*) return 1 ;;
	esac

	__slurm_log_trace "$(__func__): prev='$prev' cur='$cur'"

	return 0
}

# _init_completion() wrapper
#
# RET: 0 = success, 1 = failure
function __slurm_init_completion() {
	COMPREPLY=()
	cur="${COMP_WORDS[COMP_CWORD]}"
	prev="${COMP_WORDS[COMP_CWORD - 1]}"
	cword="${#COMP_CWORD[@]}"
	words=("${COMP_WORDS[@]}")
	split="false"

	_init_completion -s -n "=:" || return 1
	__slurm_split_opt && split="true"

	__slurm_log_debug "$(__func__): prev='$prev'"
	__slurm_log_debug "$(__func__): cur='$cur'"
	__slurm_log_debug "$(__func__): split='$split'"
	__slurm_log_debug "$(__func__): cword='$cword'"
	__slurm_log_debug "$(__func__): #words[@]='${#words[@]}'"
	__slurm_log_debug "$(__func__): words[*]='${words[*]}'"
	__slurm_log_trace "$(__func__): COMP_CWORD='$COMP_CWORD'"
	__slurm_log_trace "$(__func__): COMP_LINE='$COMP_LINE'"
	__slurm_log_trace "$(__func__): COMP_POINT='$COMP_POINT'"
	__slurm_log_trace "$(__func__): COMP_WORDBREAKS='${COMP_WORDBREAKS}'"
	__slurm_log_trace "$(__func__): COMP_WORDS[*]='${COMP_WORDS[*]}'"
	__slurm_log_trace "$(__func__): COMPREPLY[*]='${COMPREPLY[*]}'"

	return 0
}

# Initialize shell completion
#
# RET: 0 = success, 1 = failure
function __slurm_compinit() {
	# default environment variable values
	SLURM_COMPLOG_FILE="${SLURM_COMPLOG_FILE:-""}"
	SLURM_COMPLOG_LEVEL="${SLURM_COMPLOG_LEVEL:-3}"
	SLURM_COMP_VALUE="${SLURM_COMP_VALUE:-1}"
	SLURM_COMP_HOSTLIST="${SLURM_COMP_HOSTLIST:-1}"

	__slurm_log_info ""
	__slurm_log_info "$(__func__): ========== COMPLETION LOGIC START ==========="
	__slurm_log_info "$(__func__): SLURM_COMPLOG_LEVEL='${SLURM_COMPLOG_LEVEL}'"
	__slurm_log_info "$(__func__): SLURM_COMPLOG_FILE='$SLURM_COMPLOG_FILE'"
	__slurm_log_info "$(__func__): SLURM_COMP_VALUE='${SLURM_COMP_VALUE}'"
	__slurm_log_info "$(__func__): SLURM_COMP_HOSTLIST='${SLURM_COMP_HOSTLIST}'"

	__slurm_init_completion || return 1

	return 0
}

################################################################################
#			Slurm Helper Completion Functions
################################################################################

# Check if slurm value completions are enabled
#
# RET 0 = enabled, 1 = disabled
function __slurm_comp_slurm_value() {
	if ((SLURM_COMP_VALUE == 0)); then
		__slurm_log_warn "$(__func__): Slurm value completion is disabled."
		return 1
	fi

	__slurm_log_info "$(__func__): Slurm value completion is enabled."

	return 0
}

# Filter and normalize completion items
#
# $@: array of items
function __slurm_comp_filter() {
	local input=("$@")
	local output
	output="$(echo "${input[*]}" | awk NF | sort -u | paste -s -d' ')"

	__slurm_log_trace "$(__func__): #input[@]='${#input[@]}'"
	__slurm_log_trace "$(__func__): input[*]='${input[*]}'"
	__slurm_log_trace "$(__func__): output='$output'"

	echo "${output}"
}

# Determine if a slurmctld will respond
function __slurm_ctld_status() {
	local output exit_code
	output=$(scontrol ping)
	exit_code=$?

	if ((exit_code == 0)); then
		__slurm_log_debug "$(__func__): exit_code='$exit_code'"
	else
		__slurm_log_error "$(__func__): exit_code='$exit_code'"
	fi

	return $exit_code
}

# Determine if a slurmdbd will respond
function __slurm_dbd_status() {
	local output exit_code
	output=$(true) # TODO: need 'sacctmgr ping'
	exit_code=$?

	if ((exit_code == 0)); then
		__slurm_log_debug "$(__func__): exit_code='$exit_code'"
	else
		__slurm_log_error "$(__func__): exit_code='$exit_code'"
	fi

	return $exit_code
}

# Slurm helper function for running slurm functions
function __slurm_func_wrapper() {
	local cmd="$1"
	local output=""
	local exit_code=0

	if [[ -z $cmd ]]; then
		__slurm_log_error "$(__func__): no command given"
		echo ""
		return 1
	fi

	__slurm_log_debug "$(__func__): $cmd"

	output="$(eval "$cmd" 2>/dev/null)"
	exit_code=$?

	if ((exit_code == 0)); then
		__slurm_log_trace "$(__func__): output='$output'"
		output="$(__slurm_comp_filter "${output}")"
	else
		__slurm_log_error "$(__func__): command failed, ignoring output"
		output=""
	fi

	if [[ -n $output ]]; then
		__slurm_log_debug "$(__func__): output='$output'"
	else
		__slurm_log_warn "$(__func__): output is empty"
	fi

	echo "${output}"
}

# Slurm helper function to get accounts list
#
# RET: space delimited list
function __slurm_accounts() {
	__slurm_comp_slurm_value || return
	__slurm_dbd_status || return

	local cmd="sacctmgr -Pn list accounts format=account"
	__slurm_func_wrapper "$cmd"
}

# Slurm helper function to get associations list
#
# RET: space delimited list
function __slurm_associations() {
	__slurm_comp_slurm_value || return
	__slurm_dbd_status || return

	local cmd="sacctmgr -Pn list associations format=id"
	__slurm_func_wrapper "$cmd"
}

# Slurm helper function to return accepted boolean values
#
# RET: boolean list
function __slurm_boolean() {
	local boolean=(
		"no"
		"yes"
	)
	local output
	output="${boolean[*]}"

	__slurm_log_trace "$(__func__): output='$output'"
	echo "${output}"
}

# Slurm helper function to return accepted compress type values
#
# RET: compress_types list
function __slurm_compress_types() {
	local compress_types=(
		"lz4"
		"none"
	)
	local output="${compress_types[*]}"

	__slurm_log_trace "$(__func__): output='$output'"
	echo "${output}"
}

# Slurm helper function to return accepted cpu bind type values
#
# RET: cpubind_types list
function __slurm_cpubind_types() {
	local cpubind_types=(
		"board"
		"core"
		"ldom"
		"none"
		"off"
		"socket"
		"thread"
	)
	local output="${cpubind_types[*]}"

	__slurm_log_trace "$(__func__): output='$output'"
	echo "${output}"
}

# Slurm helper function to get clusters list
#
# RET: space delimited list
function __slurm_clusters() {
	__slurm_comp_slurm_value || return
	__slurm_dbd_status || return

	local cmd="sacctmgr -Pn list clusters format=clusters"
	__slurm_func_wrapper "$cmd"
}

# Slurm helper function to get cluster features list
#
# RET: space delimited list
function __slurm_clusters_feature() {
	__slurm_comp_slurm_value || return
	__slurm_dbd_status || return

	local cmd="sacctmgr -Pn list federation format=features"
	__slurm_func_wrapper "$cmd"
}

# Slurm helper function to get clusters RPC list
#
# RET: space delimited list
function __slurm_clusters_rpc() {
	__slurm_comp_slurm_value || return
	__slurm_dbd_status || return

	local cmd="sacctmgr -Pn list clusters format=rpc"
	__slurm_func_wrapper "$cmd"
}

# Slurm helper function to get features list
#
# RET: space delimited list
function __slurm_features() {
	__slurm_comp_slurm_value || return
	__slurm_ctld_status || return

	local cmd="scontrol -o show nodes | grep -E -o 'AvailableFeatures=\S+' | cut -d= -f2 | tr ',' '\n'"
	__slurm_func_wrapper "$cmd"
}

# Slurm helper function to get active features list
#
# RET: space delimited list
function __slurm_features_active() {
	__slurm_comp_slurm_value || return
	__slurm_ctld_status || return

	local cmd="scontrol -o show nodes | grep -E -o 'ActiveFeatures=\S+' | cut -d= -f2 | tr ',' '\n'"
	__slurm_func_wrapper "$cmd"
}

# Slurm helper function to get federations list
#
# RET: space delimited list
function __slurm_federations() {
	__slurm_comp_slurm_value || return
	__slurm_dbd_status || return

	local cmd="sacctmgr -Pn list federation format=federation"
	__slurm_func_wrapper "$cmd"
}

# Slurm helper function to get format list
#
# $1: slurm command to run helpformat against
# RET: space delimited list
function __slurm_helpformat() {
	local cmd="$1"
	local output=""

	if [[ -z $cmd ]]; then
		__slurm_log_error "$(__func__): command context is empty"
		return 1
	fi

	output="$(__slurm_func_wrapper "$cmd --helpformat")"
	output=${output,,} # force lowercase
	echo "${output}"
}

# Slurm helper function to get sorted hostlist list
#
# RET: space delimited list
function __slurm_hostlist() {
	local hostnames="$1"
	local cmd="scontrol -o show hostlistsorted \"$hostnames\""
	__slurm_func_wrapper "$cmd"
}

# Slurm helper function to get hostnames list
#
# RET: space delimited list
function __slurm_hostnames() {
	local hostlist="${1-}"
	local cmd="scontrol -o show hostnames \"$hostlist\""
	__slurm_func_wrapper "$cmd"
}

# Slurm helper function to get GRES list
#
# RET: space delimited list
function __slurm_gres() {
	__slurm_comp_slurm_value || return
	__slurm_ctld_status || return

	local cmd="scontrol -o show config | grep 'GresTypes' | tr -d '[:space:]' | cut -d= -f2 | tr ',' '\n'"
	__slurm_func_wrapper "$cmd"
}

# Slurm helper function to get recent jobs list
#
# RET: space delimited list
function __slurm_jobs() {
	__slurm_comp_slurm_value || return
	__slurm_ctld_status || return

	local cmd="scontrol -o show jobs | grep -Po 'JobId=\S+' | cut -d'=' -f2"
	__slurm_func_wrapper "$cmd"
}

# Slurm helper function to get recent jobstep tasks list
#
# RET: space delimited list
function __slurm_jobsteps_tasks() {
	__slurm_comp_slurm_value || return
	__slurm_ctld_status || return

	local cmd="scontrol -o show step | grep -Po 'StepId=\d+\.\d+' | cut -d'=' -f2"
	__slurm_func_wrapper "$cmd"
}

# Slurm helper function to get recent jobsteps list
#
# RET: space delimited list
function __slurm_jobsteps() {
	__slurm_comp_slurm_value || return
	__slurm_ctld_status || return

	local cmd="scontrol -o show step | grep -Po 'StepId=\d+\.\S+' | cut -d'=' -f2"
	__slurm_func_wrapper "$cmd"
}

# Slurm helper function to get recent job name list
#
# RET: space delimited list
function __slurm_jobnames() {
	__slurm_comp_slurm_value || return
	__slurm_ctld_status || return

	local cmd="scontrol -o show jobs | grep -Po 'JobName=\S+' | cut -d'=' -f2"
	__slurm_func_wrapper "$cmd"
}

# Slurm helper function to get license list
#
# RET: space delimited list
function __slurm_licenses() {
	__slurm_comp_slurm_value || return
	__slurm_ctld_status || return

	local cmd="scontrol -o show license | grep -Po 'LicenseName=\S+' | cut -d'=' -f2"
	__slurm_func_wrapper "$cmd"
}

# Slurm helper function to get node list
#
# RET: space delimited list
function __slurm_nodes() {
	__slurm_comp_slurm_value || return
	__slurm_ctld_status || return

	local cmd="scontrol -o show nodes | grep -Po 'NodeName=\S+' | cut -d'=' -f2"
	__slurm_func_wrapper "$cmd"
}

# Slurm helper function to get frontend node list
#
# RET: space delimited list
function __slurm_nodes_frontend() {
	__slurm_comp_slurm_value || return
	__slurm_ctld_status || return

	local cmd="scontrol -o show frontend | grep -Po 'FrontendName=\S+' | cut -d'=' -f2"
	__slurm_func_wrapper "$cmd"
}

# Slurm helper function to get organizations list
#
# RET: space delimited list
function __slurm_organizations() {
	__slurm_comp_slurm_value || return
	__slurm_dbd_status || return

	local cmd="sacctmgr -Pn list accounts format=organization"
	__slurm_func_wrapper "$cmd"
}

# Slurm helper function to get partition list
#
# RET: space delimited list
function __slurm_partitions() {
	__slurm_comp_slurm_value || return
	__slurm_ctld_status || return

	local cmd="scontrol -o show partitions | grep -Po 'PartitionName=\S+' | cut -d'=' -f2"
	__slurm_func_wrapper "$cmd"
}

# Slurm helper function to get QOS list
#
# RET: space delimited list
function __slurm_qos() {
	__slurm_comp_slurm_value || return
	__slurm_dbd_status || return

	local cmd="sacctmgr -Pn list qos format=name"
	__slurm_func_wrapper "$cmd"
}

# Slurm helper function to get QOS ID list
#
# RET: space delimited list
function __slurm_qosid() {
	__slurm_comp_slurm_value || return
	__slurm_dbd_status || return

	local cmd="sacctmgr -Pn list qos format=id"
	__slurm_func_wrapper "$cmd"
}

# Slurm helper function to get reservation list
#
# RET: space delimited list
function __slurm_reservations() {
	__slurm_comp_slurm_value || return
	__slurm_ctld_status || return

	local cmd="scontrol -o show reservations | grep -Po 'ReservationName=\S+' | cut -d= -f2"
	__slurm_func_wrapper "$cmd"
}

# Slurm helper function to get resources list
#
# RET: space delimited list
function __slurm_resources() {
	__slurm_comp_slurm_value || return
	__slurm_dbd_status || return

	local cmd="sacctmgr -Pn list resource format=name"
	__slurm_func_wrapper "$cmd"
}

# Slurm helper function to get resource servers list
#
# RET: space delimited list
function __slurm_resources_servers() {
	__slurm_comp_slurm_value || return
	__slurm_dbd_status || return

	local cmd="sacctmgr -Pn list resource format=server"
	__slurm_func_wrapper "$cmd"
}

# Slurm helper function to return accepted signals list
#
# RET: signals
function __slurm_signals() {
	local signals=(
		"SIGHUP"
		"SIGINT"
		"SIGQUIT"
		"SIGKILL"
		"SIGKILL"
		"SIGALRM"
		"SIGTERM"
		"SIGUSR1"
		"SIGUSR2"
		"SIGURG"
		"SIGCONT"
		"SIGSTOP"
		"SIGTSTP"
		"SIGTTIN"
		"SIGTTOU"
		"SIGXCPU"
	)
	local output="${signals[*]}"

	__slurm_log_trace "$(__func__): output='$output'"
	echo "${output}"
}

# Slurm helper function to get TRES list
#
# RET: space delimited list
function __slurm_tres() {
	__slurm_comp_slurm_value || return
	__slurm_dbd_status || return

	local cmd="scontrol -o show config | grep 'AccountingStorageTRES' | cut -d= -f2 | tr -d '[:space:]' | tr ',' '\n'"
	__slurm_func_wrapper "$cmd"
}

# Slurm helper function to get user list
#
# RET: space delimited list
function __slurm_users() {
	__slurm_comp_slurm_value || return
	__slurm_dbd_status || return

	local cmd="sacctmgr -Pn list users format=user"
	__slurm_func_wrapper "$cmd"
}

# Slurm helper function to get user group list
#
# RET: space delimited list
function __slurm_wckeys() {
	__slurm_comp_slurm_value || return
	__slurm_dbd_status || return

	local cmd="sacctmgr -Pn list wckeys format=wckey"
	__slurm_func_wrapper "$cmd"
}

# Slurm completion helper for flag completion
#
# $1: slurm command being completed
# RET: 0 = did completion; 1 = no completion
function __slurm_comp_flags() {
	local cmd="$1"

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_debug "$(__func__): cmd='$cmd'"

	case "${cur}" in
	-*) __slurm_compreply_param "$(__slurm_autocompletion "$cmd")" ;;
	*) return 1 ;;
	esac

	return 0
}

# Slurm completion helper for job dependency spec
function __slurm_comp_dependency() {
	local prefix=""
	local suffix=","
	if [[ $cur == *"?"* ]] && [[ $cur != *","* ]]; then
		# Dependency expresson can use either ',' or '?' as the
		# separator, but not both in the same expression.
		#   , = logical and (all must be satisfied)
		#   ? = logical or (any may be satisfied)
		suffix="?"
	fi
	local curlist="${cur%"$suffix"*}"
	local curitem="${cur##*"$suffix"}"
	local curitem2="${curitem##*:}"
	local curverb="${curitem%:*}:"
	local compreply=()

	local dependency_types=(
		"after:"
		"afterany:"
		"afterburstbuffer:"
		"aftercorr:"
		"afternotok:"
		"afterok:"
		"singleton"
	)

	if [[ $curitem == "$curlist" ]]; then
		curlist=""
	elif [[ -n $curlist ]]; then
		prefix="${curlist}${suffix}"
	fi

	__slurm_log_debug "$(__func__): cur='$cur' curitem='$curitem' curlist='$curlist'"
	__slurm_log_debug "$(__func__): curitem2='$curitem2' prefix='$prefix' suffix='$suffix'"

	case "${curitem}" in
	*:*) ;;
	*)
		__slurm_log_debug "$(__func__): expect verb spec"
		compreply=("$(compgen -W "${dependency_types[*]}" -- "${curitem}")")
		__slurm_comp "${compreply[*]}" "${prefix-}" "${curitem}" "" ""
		return
		;;
	esac

	case "${curitem2}" in
	*+)
		__slurm_log_debug "$(__func__): expect time spec"
		;;
	*++([0-9]))
		__slurm_log_debug "$(__func__): found time spec"

		compreply+=("$(compgen -W "${curitem}" -S "${suffix}" -- "${curitem}")")
		compreply+=("$(compgen -W "${curitem}" -S ":" -- "${curitem}")")
		if [[ $cur != *","* ]]; then
			compreply+=("$(compgen -W "${curitem}" -S "?" -- "${curitem}")")
		fi
		__slurm_comp "${compreply[*]}" "${prefix-}" "${curitem}" "" ""
		;;
	*)
		__slurm_log_debug "$(__func__): expect job spec"
		local jobs=()
		local options=""
		options="$(__slurm_jobs)"

		# build array without seen items
		for item in $options; do
			__slurm_log_trace "$(__func__): for loop: item='$item'"
			[[ $curitem =~ :"${item}". ]] && continue
			jobs+=("$item")
		done

		compreply+=("$(compgen -W "${jobs[*]}" -P "${curverb}" -S "${suffix}" -- "${curitem2}")")
		compreply+=("$(compgen -W "${jobs[*]}" -P "${curverb}" -S ":" -- "${curitem2}")")
		compreply+=("$(compgen -W "${jobs[*]}" -P "${curverb}" -S "+" -- "${curitem2}")")
		if [[ $cur != *","* ]]; then
			compreply+=("$(compgen -W "${jobs[*]}" -P "${curverb}" -S "?" -- "${curitem2}")")
		fi
		__slurm_comp "${compreply[*]}" "${prefix-}" "${curitem}" "" ""
		;;
	esac
}

# Slurm completion helper for salloc, sbatch, srun
#
# $1: slurm command being completed
# RET: 0 = did completion; 1 = no completion
function __slurm_comp_common() {
	local cmd="$1"
	local accelbind_types=(
		"g"
		"n"
		"v"
	)
	local acctgfreq_types=(
		"energy="
		"filesystem="
		"network="
		"task="
	)
	local binary=(
		"0"
		"1"
	)
	local cpubind_types=(
		"cores"
		"help"
		"ldoms"
		"map_cpu:"
		"map_ldom:"
		"mask_cpu:"
		"mask_ldom:"
		"none"
		"quiet"
		"rank"
		"rank_ldom"
		"sockets"
		"threads"
		"verbose"
	)
	local cpufreq_types=(
		"low"
		"medium"
		"high"
		"highm1"
		"conservative"
		"ondemand"
		"performance"
		"powersave"
	)
	local distribution_types=(
		"arbitrary"
		"block"
		"cyclic"
		"plane="
	)
	local env_modes=(
		"L"
		"S"
	)
	local exclusive_types=(
		"exclusive"
		"mcs"
		"oversubscribe"
		"user"
	)
	local export_types=(
		"all"
		"none"
	)
	local gpubind_types=(
		"closest"
		"map_gpu:"
		"mask_gpu:"
		"none"
		"per_task:"
		"single:"
	)
	local gpufreq_types=(
		"low"
		"medium"
		"high"
		"highm1"
	)
	local gres_flags=(
		"disable-binding"
		"enforce-binding"
	)
	local hints=(
		"compute_bound"
		"help"
		"memory_bound"
		"multithread"
		"nomultithread"
	)
	local mail_types=(
		"all"
		"array_tasks"
		"begin"
		"end"
		"fail"
		"invalid_depend"
		"none"
		"requeue"
		"stage_out"
		"time_limit"
		"time_limit_90"
		"time_limit_80"
		"time_limit_50"
	)
	local membind_types=(
		"help"
		"local"
		"map_mem:"
		"mask_mem:"
		"none"
		"prefer"
		"rank"
		"sort"
	)
	local mpi_types=(
		"list"
		"none"
		"pmi2"
		"pmix"
	)
	local network_types=(
		"blade"
		"system"
	)
	local open_modes=(
		"append"
		"truncate"
	)
	local power_flags=(
		"level"
	)
	local profile_types=(
		"all"
		"energy"
		"filesystem"
		"network"
		"none"
		"task"
	)
	local propagate_types=(
		"all"
		"as"
		"core"
		"cpu"
		"data"
		"fsize"
		"memlock"
		"nofile"
		"none"
		"nproc"
		"rss"
		"stack"
	)
	local slurmd_levels=(
		"quiet"
		"fatal"
		"error"
		"info"
		"verbose"
	)
	local x11_modes=(
		"all"
		"first"
		"last"
	)

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_debug "$(__func__): cmd='$cmd'"

	__slurm_comp_flags "$cmd" && return

	case "${prev}" in
	--accel-bind) __slurm_compreply "${accelbind_types[*]}" ;;
	-A | --account?(s)) __slurm_compreply "$(__slurm_accounts)" ;;
	--acctg-freq) __slurm_compreply_list "${acctgfreq_types[*]}" ;;
	--batch) __slurm_compreply_list "$(__slurm_features)" ;;
	--bcast) _filedir -d ;;
	--bcast-exclude) _filedir -d ;;
	-D | --chdir) _filedir -d ;;
	--cluster-constraint?(s)) __slurm_compreply_list "$(__slurm_clusters_feature)" ;;
	-M | --cluster?(s)) __slurm_compreply_list "$(__slurm_clusters)" ;;
	--compress) __slurm_compreply "$(__slurm_compress_types)" ;;
	-C | --constraint) __slurm_compreply_list "$(__slurm_features)" ;;
	--container) _filedir ;;
	--cpu-bind) __slurm_compreply "${cpubind_types[*]}" ;;
	--cpu-freq) __slurm_compreply "${cpufreq_types[*]}" ;;
	-d | --dependency) __slurm_comp_dependency ;;
	-m | --distribution) __slurm_compreply "${distribution_types[*]}" ;;
	--epilog) _filedir ;;
	-e | --error) _filedir ;;
	-x | --exclude) __slurm_compreply_list "$(__slurm_nodes)" "ALL" "true" ;;
	--exclusive) __slurm_compreply "${exclusive_types[*]}" ;;
	--export) __slurm_compreply "${export_types[*]}" ;;
	--export-file) _filedir ;;
	--get-user-env) __slurm_compreply "${env_modes[*]}" ;;
	--gid) __slurm_compreply "$(__slurm_linux_groups) $(__slurm_linux_gids)" ;;
	--gpu-bind) __slurm_compreply "${gpubind_types[*]}" ;;
	--gpu-freq) __slurm_compreply "${gpufreq_types[*]}" ;;
	--gres) __slurm_compreply_count "$(__slurm_gres)" ;;
	--gres-flag?(s)) __slurm_compreply "${gres_flags[*]}" ;;
	--hint) __slurm_compreply "${hints[*]}" ;;
	-i | --input) _filedir ;;
	--jobid) __slurm_compreply "$(__slurm_jobs)" ;;
	-K)
		# warning: salloc and srun overload -K
		case "${cmd}" in
		salloc) __slurm_compreply "$(__slurm_signals)" ;;
		srun) __slurm_compreply "${binary[*]}" ;;
		*) return 1 ;;
		esac
		;;
	--kill-command) __slurm_compreply "$(__slurm_signals)" ;;
	--kill-on-bad-exit) __slurm_compreply "${binary[*]}" ;;
	--kill-on-invalid-dep) __slurm_compreply "$(__slurm_boolean)" ;;
	-L | --license?(s)) __slurm_compreply_count "$(__slurm_licenses)" ;;
	--mail-type) __slurm_compreply_list "${mail_types[*]}" ;;
	--mail-user) __slurm_compreply "$(__slurm_users)" ;;
	--mem-bind) __slurm_compreply "${membind_types[*]}" ;;
	--mpi) __slurm_compreply "${mpi_types[*]}" ;;
	--network) __slurm_compreply "${network_types[*]}" ;;
	-F | --nodefile) _filedir ;;
	-w | --nodelist) __slurm_compreply_list "$(__slurm_nodes)" "ALL" "true" ;;
	--open-mode?(s)) __slurm_compreply "${open_modes[*]}" ;;
	-o | --output) _filedir ;;
	-p | --partition?(s)) __slurm_compreply_list "$(__slurm_partitions)" ;;
	--power) __slurm_compreply_list "${power_flags[*]}" ;;
	--prefer?(s)) __slurm_compreply_list "$(__slurm_features)" ;;
	--profile?(s)) __slurm_compreply_list "${profile_types[*]}" ;;
	--prolog) _filedir ;;
	--propagate?(s)) __slurm_compreply_list "${propagate_types[*]}" ;;
	-q | --qos?(s)) __slurm_compreply "$(__slurm_qos)" ;;
	--reservation?(s)) __slurm_compreply_list "$(__slurm_reservations)" ;;
	--send-lib?(s)) __slurm_compreply "$(__slurm_boolean)" ;;
	--signal) __slurm_compreply "$(__slurm_signals)" ;;
	--slurmd-debug) __slurm_compreply "${slurmd_levels[*]}" ;;
	--task-epilog) _filedir ;;
	--task-prolog) _filedir ;;
	--tres-per-task) __slurm_compreply_count "$(__slurm_tres | tr '/' ':')" ;;
	--uid?(s)) __slurm_compreply "$(__slurm_users) $(__slurm_linux_uids)" ;;
	--wait-all-node?(s)) __slurm_compreply "${binary[*]}" ;;
	--wckey?(s)) __slurm_compreply "$(__slurm_wckeys)" ;;
	--x11) __slurm_compreply "${x11_modes[*]}" ;;
	*) return 1 ;;
	esac

	return 0
}

################################################################################
#			Linux Helper Completion Functions
################################################################################

# Returns list of linux users from passwd
#
# RET: space delimited list
function __slurm_linux_users() {
	local output
	output="$(getent passwd | cut -d: -f1)"
	__slurm_log_trace "$(__func__): output='$output'"
	echo "${output}"
}

# Returns list of linux groups from group
#
# RET: space delimited list
function __slurm_linux_groups() {
	local output
	output="$(getent group | cut -d: -f1)"
	__slurm_log_trace "$(__func__): output='$output'"
	echo "${output}"
}

# Returns list of linux UIDs from passwd
#
# RET: space delimited list
function __slurm_linux_uids() {
	local output
	output="$(getent passwd | cut -d: -f3)"
	__slurm_log_trace "$(__func__): output='$output'"
	echo "${output}"
}

# Returns list of linux GIDs from passwd
#
# RET: space delimited list
function __slurm_linux_gids() {
	local output
	output="$(getent passwd | cut -d: -f4)"
	__slurm_log_trace "$(__func__): output='$output'"
	echo "${output}"
}

################################################################################
#			SACCT Completion Functions
################################################################################

# sacct completion handler
# https://slurm.schedmd.com/sacct.html
function _sacct() {
	local cur prev words cword split
	__slurm_compinit "$1" || return
	__slurm_log_info "$(__func__): prev='$prev' cur='$cur' split='$split'"

	local sched_flags=(
		"schedsubmit"
		"schedmain"
		"schedbackfill"
	)
	local reasons=(
		"none"
		"prolog"
		"priority"
		"dependency"
		"resources"
		"partitionnodelimit"
		"partitiontimelimit"
		"partitiondown"
		"partitioninactive"
		"jobheldadmin"
		"jobhelduser"
		"begintime"
		"licenses"
		"associationjoblimit"
		"associationresourcelimit"
		"associationtimelimit"
		"reservation"
		"reqnodenotavail"
		"frontenddown"
		"partitiondown"
		"nodedown"
		"badconstraints"
		"systemfailure"
		"joblaunchfailure"
		"nonzeroexitcode"
		"timelimit"
		"inactivelimit"
		"invalidaccount"
		"invalidqos"
		"qosusagethreshold"
		"qosjoblimit"
		"qosresourcelimit"
		"qostimelimit"
		"blockmaxerror"
		"blockfreeaction"
		"cleaning"
		"qosnotallowed"
		"accountnotallowed"
		"dependencyneversatisfied"
		"qosgrpcpulimit"
		"qosgrpcpuminuteslimit"
		"qosgrpcpurunminuteslimit"
		"qosgrpjobslimit"
		"qosgrpmemlimit"
		"qosgrpnodelimit"
		"qosgrpsubmitjobslimit"
		"qosgrpwalllimit"
		"qosmaxcpuperjoblimit"
		"qosmaxcpuminutesperjoblimit"
		"qosmaxnodeperjoblimit"
		"qosmaxwalldurationperjoblimit"
		"qosmaxcpuperuserlimit"
		"qosmaxjobsperuserlimit"
		"qosmaxnodeperuserlimit"
		"qosmaxsubmitjobperuserlimit"
		"qosmincpunotsatisfied"
		"assocgrpcpulimit"
		"assocgrpcpuminuteslimit"
		"assocgrpcpurunminuteslimit"
		"assocgrpjobslimit"
		"assocgrpmemlimit"
		"assocgrpnodelimit"
		"assocgrpsubmitjobslimit"
		"assocgrpwalllimit"
		"assocmaxjobslimit"
		"assocmaxcpuperjoblimit"
		"assocmaxcpuminutesperjoblimit"
		"assocmaxnodeperjoblimit"
		"assocmaxwalldurationperjoblimit"
		"assocmaxsubmitjoblimit"
		"jobholdmaxrequeue"
		"jobarraytasklimit"
		"burstbufferresources"
		"burstbufferstagein"
		"burstbufferoperation"
		"powernotavail"
		"powerreserved"
		"assocgrpunknown"
		"assocgrpunknownminutes"
		"assocgrpunknownrunminutes"
		"assocmaxunknownperjob"
		"assocmaxunknownpernode"
		"assocmaxunknownminutesperjob"
		"assocmaxcpupernode"
		"assocgrpmemminutes"
		"assocgrpmemrunminutes"
		"assocmaxmemperjob"
		"assocmaxmempernode"
		"assocmaxmemminutesperjob"
		"assocgrpnodeminutes"
		"assocgrpnoderunminutes"
		"assocmaxnodeminutesperjob"
		"assocgrpenergy"
		"assocgrpenergyminutes"
		"assocgrpenergyrunminutes"
		"assocmaxenergyperjob"
		"assocmaxenergypernode"
		"assocmaxenergyminutesperjob"
		"assocgrpgres"
		"assocgrpgresminutes"
		"assocgrpgresrunminutes"
		"assocmaxgresperjob"
		"assocmaxgrespernode"
		"assocmaxgresminutesperjob"
		"assocgrplicense"
		"assocgrplicenseminutes"
		"assocgrplicenserunminutes"
		"assocmaxlicenseperjob"
		"assocmaxlicenseminutesperjob"
		"assocgrpbb"
		"assocgrpbbminutes"
		"assocgrpbbrunminutes"
		"assocmaxbbperjob"
		"assocmaxbbpernode"
		"assocmaxbbminutesperjob"
		"qosgrpunknown"
		"qosgrpunknownminutes"
		"qosgrpunknownrunminutes"
		"qosmaxunknownperjob"
		"qosmaxunknownpernode"
		"qosmaxunknownperuser"
		"qosmaxunknownminutesperjob"
		"qosminunknown"
		"qosmaxcpupernode"
		"qosgrpmemoryminutes"
		"qosgrpmemoryrunminutes"
		"qosmaxmemoryperjob"
		"qosmaxmemorypernode"
		"qosmaxmemoryperuser"
		"qosmaxmemoryminutesperjob"
		"qosminmemory"
		"qosgrpnodeminutes"
		"qosgrpnoderunminutes"
		"qosmaxnodeminutesperjob"
		"qosminnode"
		"qosgrpenergy"
		"qosgrpenergyminutes"
		"qosgrpenergyrunminutes"
		"qosmaxenergyperjob"
		"qosmaxenergypernode"
		"qosmaxenergyperuser"
		"qosmaxenergyminutesperjob"
		"qosminenergy"
		"qosgrpgres"
		"qosgrpgresminutes"
		"qosgrpgresrunminutes"
		"qosmaxgresperjob"
		"qosmaxgrespernode"
		"qosmaxgresperuser"
		"qosmaxgresminutesperjob"
		"qosmingres"
		"qosgrplicense"
		"qosgrplicenseminutes"
		"qosgrplicenserunminutes"
		"qosmaxlicenseperjob"
		"qosmaxlicenseperuser"
		"qosmaxlicenseminutesperjob"
		"qosminlicense"
		"qosgrpbb"
		"qosgrpbbminutes"
		"qosgrpbbrunminutes"
		"qosmaxbbperjob"
		"qosmaxbbpernode"
		"qosmaxbbperuser"
		"assocmaxbbminutesperjob"
		"qosminbb"
		"deadline"
		"maxbbperaccount"
		"maxcpuperaccount"
		"maxenergyperaccount"
		"maxgresperaccount"
		"maxnodeperaccount"
		"maxlicenseperaccount"
		"maxmemoryperaccount"
		"maxunknownperaccount"
		"maxjobsperaccount"
		"maxsubmitjobsperaccount"
		"partitionconfig"
		"accountingpolicy"
		"fedjoblock"
		"outofmemory"
		"maxmemperlimit"
		"assocgrpbilling"
		"assocgrpbillingminutes"
		"assocgrpbillingrunminutes"
		"assocmaxbillingperjob"
		"assocmaxbillingpernode"
		"assocmaxbillingminutesperjob"
		"qosgrpbilling"
		"qosgrpbillingminutes"
		"qosgrpbillingrunminutes"
		"qosmaxbillingperjob"
		"qosmaxbillingpernode"
		"qosmaxbillingperuser"
		"qosmaxbillingminutesperjob"
		"maxbillingperaccount"
		"qosminbilling"
		"reservationdeleted"
	)
	local states=(
		"boot_fail"
		"cancelled"
		"completed"
		"deadline"
		"failed"
		"node_fail"
		"out_of_memory"
		"pending"
		"preempted"
		"running"
		"requeued"
		"resizing"
		"revoked"
		"suspended"
		"timeout"
	)
	local units=(
		"K"
		"M"
		"G"
		"T"
		"P"
	)

	__slurm_comp_flags "$1" && return

	case "${prev}" in
	-A | --account?(s)) __slurm_compreply_list "$(__slurm_accounts)" ;;
	-x | --association?(s)) __slurm_compreply_list "$(__slurm_associations)" ;;
	-M | --clusters) __slurm_compreply_list "$(__slurm_clusters)" ;;
	-C | --constraint?(s)) __slurm_compreply_list "$(__slurm_features)" ;;
	-f | --file) _filedir ;;
	-F | --flag?(s)) __slurm_compreply_list "${sched_flags[*]}" ;; # TODO: want --helpsched
	-o | --format | --field?(s)) __slurm_compreply_list "$(__slurm_helpformat "$1")" ;;
	-g | --gid?(s) | --group) __slurm_compreply_list "$(__slurm_linux_groups) $(__slurm_linux_gids)" ;;
	-j | --job?(s)) __slurm_compreply "$(__slurm_jobs) $(__slurm_jobsteps)" ;;
	--name?(s)) __slurm_compreply_list "$(__slurm_jobnames)" ;;
	-N | --nodelist) __slurm_compreply_list "$(__slurm_nodes)" "ALL" "true" ;;
	-r | --partition?(s)) __slurm_compreply_list "$(__slurm_partitions)" ;;
	-q | --qos?(s)) __slurm_compreply_list "$(__slurm_qos)" ;;
	-R | --reason) __slurm_compreply_list "${reasons[*]}" ;; # TODO: want --helpreason
	-s | --state) __slurm_compreply_list "${states[*]}" ;;   # TODO: want --helpstate
	-u | --uid?(s) | --user?(s)) __slurm_compreply_list "$(__slurm_linux_users) $(__slurm_linux_uids)" ;;
	--units) __slurm_compreply "${units[*]}" ;;
	-W | --wckey?(s)) __slurm_compreply_list "$(__slurm_wckeys)" ;;
	--whole-hetjob) __slurm_compreply "$(__slurm_boolean)" ;;
	esac

	[[ $split == "true" ]] && return

	if ((${#COMPREPLY[@]} == 0)) && [[ $cur == "" ]]; then
		__slurm_compreply "--"
	fi
}
complete -o nospace -F _sacct sacct

################################################################################
#			SACCTMGR Completion Functions
################################################################################

# helper function for __slurm_comp_sacctmgr_spec_*()
#
# $1 - mode selector
#      0 = where parameters (delete/remove)
#      1 = where+option parameters (list/show)
#      2 = where+set parameters (add/create)
#      3 = where, set parameters (modify/update)
# RET: 0 = parameters updated, 1 = nothing updated
function __slurm_comp_mode_select() {
	local mode="$1"
	local conditions=(
		"where"
		"set"
	)
	local condition_where
	condition_where="$(__slurm_find_subcmd "${conditions[0]}")"
	local condition_set
	condition_set="$(__slurm_find_subcmd "${conditions[1]}")"

	__slurm_log_debug "$(__func__): mode='$mode' condition_where='$condition_where' condition_set='$condition_set'"
	__slurm_log_trace "$(__func__): #parameters_where[@]='${#parameters_where[@]}'"
	__slurm_log_trace "$(__func__): parameters_where[*]='${parameters_where[*]}'"
	__slurm_log_trace "$(__func__): #parameters_set[@]='${#parameters_set[@]}'"
	__slurm_log_trace "$(__func__): parameters_set[*]='${parameters_set[*]}'"
	__slurm_log_trace "$(__func__): #conditions[@]='${#conditions[@]}'"
	__slurm_log_trace "$(__func__): conditions[*]='${conditions[*]}'"
	__slurm_log_trace "$(__func__): #options[@]='${#options[@]}'"
	__slurm_log_trace "$(__func__): options[*]='${options[*]}'"

	case "${mode}" in
	0) # delete/remove
		# only use 'where' parameters
		parameters+=("${conditions[0]}")
		parameters+=("${parameters_where[@]}")
		;;
	1) # list/show
		# use 'where' parameters and 'options'
		parameters+=("${conditions[0]}")
		if [[ -z $condition_where ]]; then
			parameters+=("${options[@]}")
			parameters+=("format=")
		fi
		parameters+=("${parameters_where[@]}")
		;;
	2) # add/create
		# use 'where' and 'set' parameters
		parameters+=("${conditions[0]}")
		parameters+=("${parameters_where[@]}")
		parameters+=("${parameters_set[@]}")
		;;
	3) # modify/update
		# use 'where' parameters, or 'set' parameters
		parameters+=("${conditions[@]}")
		if [[ -n $condition_where ]]; then
			parameters+=("${parameters_where[@]}")
		elif [[ -n $condition_set ]]; then
			parameters+=("${parameters_set[@]}")
		else
			parameters+=("${parameters_where[@]}")
		fi
		;;
	*) return 1 ;;
	esac

	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	return 0
}

# completion helper for sacctmgr association specifications
# https://slurm.schedmd.com/sacctmgr.html#SECTION_SPECIFICATIONS-FOR-ASSOCIATIONS
# https://slurm.schedmd.com/sacctmgr.html#SECTION_GENERAL-SPECIFICATIONS-FOR-ASSOCIATION-BASED-ENTITIES
# https://slurm.schedmd.com/sacctmgr.html#SECTION_LIST/SHOW-ASSOCIATION-FORMAT-OPTIONS
#
# RET: 0 = did completion, 1 = did not do completion
function __slurm_comp_sacctmgr_spec_associations() {
	local mode="$1"
	if [[ -z ${parameters[0]} ]]; then
		local parameters=()
	fi
	local parameters_where=(
		"account="
		"cluster="
		"partition="
		"parent="
		"qos="
		"user="
	)
	local parameters_set=(
		"defaultqos="
		"fairshare=" "share="
		"grpjobs="
		"grpjobsaccrue="
		"grpsubmit=" "grpsubmitjobs="
		"grptres="
		"grptresmins="
		"grptresrunmins="
		"grpwall="
		"maxjobs="
		"maxjobsaccrue="
		"maxsubmit=" "maxsubmitjobs="
		"maxtresmins=" "maxtresminsperjob="
		"maxtres=" "maxtresperjob="
		"maxwall=" "maxwalldurationperjob="
		"priority="
		"qoslevel="
		"qoslevel\+="
		"qoslevel\-="
	)
	local options=(
		"onlydefaults"
		"tree"
		"withdeleted"
		"withrawqoslevel"
		"withsubaccounts"
		"wolimits"
		"wopinfo"
		"woplimits"
	)

	__slurm_log_debug "$(__func__): mode='$mode'"

	__slurm_comp_mode_select "$mode"

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	case "${prev}" in
	account?(s)) __slurm_compreply_list "$(__slurm_accounts)" ;;
	cluster?(s)) __slurm_compreply_list "$(__slurm_clusters)" ;;
	defaultqos) __slurm_compreply "$(__slurm_qos)" ;;
	partition?(s)) __slurm_compreply_list "$(__slurm_partitions)" ;;
	parent?(s)) __slurm_compreply_list "$(__slurm_accounts)" ;;
	qos?(s)) __slurm_compreply_list "$(__slurm_qos)" ;;
	qoslevel?(s)?(+|-)) __slurm_compreply_list "$(__slurm_qos)" ;;
	user?(s)) __slurm_compreply_list "$(__slurm_users)" ;;
	*)
		[[ $split == "true" ]] && return 1
		__slurm_compreply_param "${parameters[*]}"
		return 1
		;;
	esac

	return 0
}

# completion helper for sacctmgr account specifications
# https://slurm.schedmd.com/sacctmgr.html#SECTION_SPECIFICATIONS-FOR-ACCOUNTS
# https://slurm.schedmd.com/sacctmgr.html#SECTION_LIST/SHOW-ACCOUNT-FORMAT-OPTIONS
function __slurm_comp_sacctmgr_spec_accounts() {
	local mode="$1"
	local parameters=()
	local parameters_where=(
		"cluster="
		"name="
		"parent="
	)
	local parameters_set=(
		"description="
		"organization="
		"rawusage="
	)
	local options=(
		"withassoc"
		"withcoord"
		"withdeleted"
	)

	__slurm_log_debug "$(__func__): mode='$mode'"

	__slurm_comp_mode_select "$mode"
	__slurm_comp_sacctmgr_spec_associations "$mode" && return

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	case "${prev}" in
	cluster?(s)) __slurm_compreply_list "$(__slurm_clusters)" ;;
	name?(s)) __slurm_compreply_list "$(__slurm_accounts)" ;;
	organization?(s)) __slurm_compreply "$(__slurm_organizations)" ;;
	parent?(s)) __slurm_compreply "$(__slurm_accounts)" ;;
	rawusage) __slurm_compreply "0" ;;
	*)
		[[ $split == "true" ]] && return
		__slurm_compreply_param "${parameters[*]}"
		;;
	esac
}

# completion helper for sacctmgr cluster specifications
# https://slurm.schedmd.com/sacctmgr.html#SECTION_SPECIFICATIONS-FOR-CLUSTERS
# https://slurm.schedmd.com/sacctmgr.html#SECTION_LIST/SHOW-CLUSTER-FORMAT-OPTIONS
function __slurm_comp_sacctmgr_spec_clusters() {
	local mode="$1"
	local parameters=()
	local parameters_where=(
		"cluster="
		"flags="
		"name="
		"rpc="
	)
	local parameters_set=(
		"classification="
		"features="
		"federation="
		"fedstate="
	)
	local options=(
		"withdeleted"
		"withfed"
		"wolimits"
	)
	local fed_states=(
		"active"
		"inactive"
		"drain"
		"drain+remove"
	)
	local flags=(
		"cray"
		"external"
		"frontend"
		"multipleslurmd"
		"none"
	)

	__slurm_log_debug "$(__func__): mode='$mode'"

	__slurm_comp_mode_select "$mode"
	__slurm_comp_sacctmgr_spec_associations "$mode" && return

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	case "${prev}" in
	cluster?(s)) __slurm_compreply_list "$(__slurm_clusters)" ;;
	feature?(s)) __slurm_compreply_list "$(__slurm_features)" ;;
	federation?(s)) __slurm_compreply_list "$(__slurm_federations)" ;;
	fedstate) __slurm_compreply "${fed_states[*]}" ;;
	flag?(s)) __slurm_compreply_list "${flags[*]}" ;;
	name?(s)) __slurm_compreply_list "$(__slurm_clusters)" ;;
	rpc) __slurm_compreply_list "$(__slurm_clusters_rpc)" ;;
	*)
		[[ $split == "true" ]] && return
		__slurm_compreply_param "${parameters[*]}"
		;;
	esac
}

# completion helper for sacctmgr coordinator specifications
# https://slurm.schedmd.com/sacctmgr.html#SECTION_SPECIFICATIONS-FOR-COORDINATOR
function __slurm_comp_sacctmgr_spec_coordinators() {
	local mode="$1"
	local parameters=()
	local parameters_where=(
		"account="
		"name="
	)
	local parameters_set=(
	)
	local options=(
	)

	__slurm_log_debug "$(__func__): mode='$mode'"

	__slurm_comp_mode_select "$mode"

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	case "${prev}" in
	account?(s)) __slurm_compreply_list "$(__slurm_accounts)" ;;
	coordinator?(s)) __slurm_compreply_list "$(__slurm_users)" ;;
	name?(s)) __slurm_compreply_list "$(__slurm_users)" ;;
	*)
		[[ $split == "true" ]] && return
		__slurm_compreply_param "${parameters[*]}"
		;;
	esac
}

# completion helper for sacctmgr event specifications
# https://slurm.schedmd.com/sacctmgr.html#SECTION_SPECIFICATIONS-FOR-EVENTS
# https://slurm.schedmd.com/sacctmgr.html#SECTION_LIST/SHOW-EVENT-FORMAT-OPTIONS
function __slurm_comp_sacctmgr_spec_events() {
	local mode="$1"
	local parameters=()
	local parameters_where=(
		"clusters="
		"condflags="
		"end="
		"events="
		"maxcpus="
		"mincpus="
		"nodes="
		"reason="
		"start="
		"states="
		"users="
	)
	local parameters_set=(
	)
	local options=(
		"all_clusters"
		"all_time"
	)
	local cond_flags=(
		"open"
	)
	local event_types=(
		"cluster"
		"node"
	)
	local states=(
		"down"
		"drain"
		"fail"
		"future"
		"idle"
		"mixed"
		"unknown"
	)

	__slurm_log_debug "$(__func__): mode='$mode'"

	__slurm_comp_mode_select "$mode"

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	case "${prev}" in
	cluster?(s)) __slurm_compreply_list "$(__slurm_clusters)" ;;
	condflag?(s)) __slurm_compreply_list "${cond_flags[*]}" ;;
	event?(s)) __slurm_compreply_list "${event_types[*]}" ;;
	node?(s)) __slurm_compreply_list "$(__slurm_nodes)" "ALL" "true" ;;
	state?(s)) __slurm_compreply_list "${states[*]}" ;;
	user?(s)) __slurm_compreply_list "$(__slurm_users)" ;;
	*)
		[[ $split == "true" ]] && return
		__slurm_compreply_param "${parameters[*]}"
		;;
	esac
}

# completion helper for sacctmgr federation specifications
# https://slurm.schedmd.com/sacctmgr.html#SECTION_SPECIFICATIONS-FOR-FEDERATION
function __slurm_comp_sacctmgr_spec_federations() {
	local mode="$1"
	local parameters=()
	local parameters_where=(
		"name="
	)
	local parameters_set=(
		"clusters="
		"clusters\+="
		"clusters\-="
	)
	local options=(
		"tree"
		"withdeleted"
	)

	__slurm_log_debug "$(__func__): mode='$mode'"

	__slurm_comp_mode_select "$mode"

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	case "${prev}" in
	cluster?(s)?(+|-)) __slurm_compreply_list "$(__slurm_clusters)" ;;
	feature) __slurm_compreply_list "$(__slurm_features)" ;;
	federation?(s)) __slurm_compreply_list "$(__slurm_federations)" ;;
	name?(s)) __slurm_compreply_list "$(__slurm_federations)" ;;
	*)
		[[ $split == "true" ]] && return
		__slurm_compreply_param "${parameters[*]}"
		;;
	esac
}

# completion helper for sacctmgr job specifications
# https://slurm.schedmd.com/sacctmgr.html#SECTION_SPECIFICATIONS-FOR-JOB
function __slurm_comp_sacctmgr_spec_jobs() {
	local mode="$1"
	local parameters=()
	local parameters_where=(
		"cluster="
		"jobid="
	)
	local parameters_set=(
		"admincomment="
		"comment="
		"derivedexitcode="
		"endtime="
		"extra="
		"newwckey="
		"starttime="
		"systemcomment="
		"wckey="
		"user="
	)
	local options=(
	)

	__slurm_log_debug "$(__func__): mode='$mode'"

	__slurm_comp_mode_select "$mode"

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	case "${prev}" in
	cluster?(s)) __slurm_compreply_list "$(__slurm_clusters)" ;;
	jobid?(s)) __slurm_compreply_list "$(__slurm_jobs)" ;;
	newwckey?(s)) __slurm_compreply "$(__slurm_wckeys)" ;;
	user?(s)) __slurm_compreply "$(__slurm_users)" ;;
	wckey?(s)) __slurm_compreply_list "$(__slurm_wckeys)" ;;
	*)
		[[ $split == "true" ]] && return
		__slurm_compreply_param "${parameters[*]}"
		;;
	esac
}

# completion helper for sacctmgr QOS specifications
# https://slurm.schedmd.com/sacctmgr.html#SECTION_SPECIFICATIONS-FOR-QOS
# https://slurm.schedmd.com/sacctmgr.html#SECTION_LIST/SHOW-QOS-FORMAT-OPTIONS
function __slurm_comp_sacctmgr_spec_qos() {
	local mode="$1"
	local parameters=()
	local parameters_where=(
		"id="
		"name="
	)
	local parameters_set=(
		"flags="
		"gracetime="
		"grpjobs="
		"grpjobsaccrue="
		"grpsubmit=" "grpsubmitjobs="
		"grptres="
		"grptresmins="
		"grptresrunmins="
		"grpwall="
		"limitfactor="
		"maxjobsaccruepa=" "maxjobsaccrueperaccount="
		"maxjobsaccruepu=" "maxjobsaccrueperuser="
		"maxjobspa=" "maxjobsperaccount="
		"maxjobspu=" "maxjobsperuser="
		"maxsubmitjobspa=" "maxsubmitjobsperaccount="
		"maxsubmitjobspu=" "maxsubmitjobsperuser="
		"maxtres=" "maxtresperjob="
		"maxtresmins=" "maxtresminsperjob="
		"maxtrespa=" "maxtresperaccount="
		"maxtrespernode="
		"maxtrespu=" "maxtresperuser="
		"maxwall=" "maxwalldurationperjob="
		"minpriothreshold="
		"mintres=" "mintresperjob="
		"preempt="
		"preemptexempttime="
		"preemptmode="
		"priority="
		"rawusage="
		"usagefactor="
		"usagethreshold="
	)
	local options=(
		"withdeleted"
	)
	local flags=(
		"denyonlimit"
		"enforceusagethreshold"
		"nodecay"
		"noreserve"
		"overpartqos"
		"partitionmaxnodes"
		"partitionminnodes"
		"partitiontimelimit"
		"requiresreservation"
		"usagefactorsafe"
	)
	local preempt_modes=(
		"cancel"
		"gang"
		"off"
		"requeue"
		"suspend"
		"within"
	)

	__slurm_log_debug "$(__func__): mode='$mode'"

	__slurm_comp_mode_select "$mode"

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	case "${prev}" in
	flag?(s)) __slurm_compreply_list "${flags[*]}" ;;
	id?(s)) __slurm_compreply_list "$(__slurm_qosid)" ;;
	name?(s)) __slurm_compreply_list "$(__slurm_qos)" ;;
	preempt?(s)) __slurm_compreply_list "$(__slurm_qos)" ;;
	preemptmode) __slurm_compreply "${preempt_modes[*]}" ;;
	rawusage) __slurm_compreply "0" ;;
	qos?(s)) __slurm_compreply_list "$(__slurm_qos)" ;;
	*)
		[[ $split == "true" ]] && return
		__slurm_compreply_param "${parameters[*]}"
		;;
	esac
}

# completion helper for sacctmgr reservation specifications
# https://slurm.schedmd.com/sacctmgr.html#SECTION_SPECIFICATIONS-FOR-RESERVATIONS
# https://slurm.schedmd.com/sacctmgr.html#SECTION_LIST/SHOW-RESERVATION-FORMAT-OPTIONS
function __slurm_comp_sacctmgr_spec_reservations() {
	local mode="$1"
	local parameters=()
	local parameters_where=(
		"clusters="
		"end="
		"id="
		"name="
		"nodes="
		"start="
	)
	local parameters_set=(
	)
	local options=(
	)

	__slurm_log_debug "$(__func__): mode='$mode'"

	__slurm_comp_mode_select "$mode"

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	case "${prev}" in
	cluster?(s)) __slurm_compreply_list "$(__slurm_clusters)" ;;
	name?(s)) __slurm_compreply_list "$(__slurm_reservations)" ;;
	node?(s)) __slurm_compreply_list "$(__slurm_nodes)" "ALL" "true" ;;
	*)
		[[ $split == "true" ]] && return
		__slurm_compreply_param "${parameters[*]}"
		;;
	esac
}

# completion helper for sacctmgr resource specifications
# https://slurm.schedmd.com/sacctmgr.html#SECTION_SPECIFICATIONS-FOR-RESOURCE
# https://slurm.schedmd.com/sacctmgr.html#SECTION_LIST/SHOW-RESOURCE-FORMAT-OPTIONS
function __slurm_comp_sacctmgr_spec_resources() {
	local mode="$1"
	local parameters=()
	local parameters_where=(
		"name="
		"server="
	)
	local parameters_set=(
		"allowed="
		"cluster="
		"count="
		"description="
		"flags="
		"servertype="
		"type="
	)
	local options=(
		"withclusters"
		"withdeleted"
	)
	local condition_set
	condition_set="$(__slurm_find_subcmd "${conditions[1]}")"
	local flags=(
	)
	local types=(
		"license"
	)

	__slurm_log_debug "$(__func__): mode='$mode'"

	__slurm_comp_mode_select "$mode"

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	case "${prev}" in
	cluster?(s)) __slurm_compreply_list "$(__slurm_clusters)" ;;
	flag?(s)) __slurm_compreply_list "${flags[*]}" ;;
	name?(s)) __slurm_compreply_list "$(__slurm_resources)" ;;
	server?(s)) __slurm_compreply_list "$(__slurm_resources_servers)" ;;
	type?(s)) __slurm_compreply "${types[*]}" ;;
	*)
		[[ $split == "true" ]] && return
		__slurm_compreply_param "${parameters[*]}"
		;;
	esac
}

# completion helper for sacctmgr transaction specifications
# https://slurm.schedmd.com/sacctmgr.html#SECTION_SPECIFICATIONS-FOR-TRANSACTIONS
# https://slurm.schedmd.com/sacctmgr.html#SECTION_LIST/SHOW-TRANSACTIONS-FORMAT-OPTIONS
function __slurm_comp_sacctmgr_spec_transactions() {
	local mode="$1"
	local parameters=()
	local parameters_where=(
		"account="
		"action="
		"actor="
		"cluster="
		"end="
		"start="
		"user="
	)
	local parameters_set=(
	)
	local options=(
		"withassoc"
	)

	__slurm_log_debug "$(__func__): mode='$mode'"

	__slurm_comp_mode_select "$mode"

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	case "${prev}" in
	account?(s)) __slurm_compreply_list "$(__slurm_accounts)" ;;
	actor?(s)) __slurm_compreply_list "$(__slurm_users)" ;;
	cluster?(s)) __slurm_compreply_list "$(__slurm_clusters)" ;;
	user?(s)) __slurm_compreply_list "$(__slurm_users)" ;;
	*)
		[[ $split == "true" ]] && return
		__slurm_compreply_param "${parameters[*]}"
		;;
	esac
}

# completion helper for sacctmgr tres specifications
# https://slurm.schedmd.com/sacctmgr.html#SECTION_LIST/SHOW-TRES
function __slurm_comp_sacctmgr_spec_tres() {
	local mode="$1"
	local parameters=()
	local parameters_where=(
		"id="
		"name="
		"type="
	)
	local parameters_set=(
	)
	local options=(
	)
	local types=(
		"bb"
		"cpu"
		"energy"
		"gres"
		"license"
		"memory"
		"node"
	)

	__slurm_log_debug "$(__func__): mode='$mode'"

	__slurm_comp_mode_select "$mode"

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	case "${prev}" in
	type?(s)) __slurm_compreply "${types[*]}" ;;
	*)
		[[ $split == "true" ]] && return
		__slurm_compreply_param "${parameters[*]}"
		;;
	esac
}

# completion helper for sacctmgr user specifications
# https://slurm.schedmd.com/sacctmgr.html#SECTION_SPECIFICATIONS-FOR-USERS
# https://slurm.schedmd.com/sacctmgr.html#SECTION_LIST/SHOW-USER-FORMAT-OPTIONS
function __slurm_comp_sacctmgr_spec_users() {
	local mode="$1"
	local parameters=()
	local parameters_where=(
		"account="
		"cluster="
		"name="
		"partition="
	)
	local parameters_set=(
		"account="
		"adminlevel="
		"clusters="
		"defaultaccount="
		"defaultwckey="
		"newname="
		"partition="
		"rawusage="
		"wckey="
	)
	local options=(
		"withassoc"
		"withcoord"
		"withdeleted"
	)
	local admin_levels=(
		"admin"
		"none"
		"operator"
	)

	__slurm_log_debug "$(__func__): mode='$mode'"

	__slurm_comp_mode_select "$mode"
	__slurm_comp_sacctmgr_spec_associations "$mode" && return

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	case "${prev}" in
	account?(s)) __slurm_compreply "$(__slurm_accounts)" ;;
	adminlevel?(s)) __slurm_compreply "${admin_levels[*]}" ;;
	cluster?(s)) __slurm_compreply_list "$(__slurm_clusters)" ;;
	defaultaccount) __slurm_compreply "$(__slurm_accounts)" ;;
	defaultwckey?(s)) __slurm_compreply "$(__slurm_wckeys)" ;;
	name?(s)) __slurm_compreply_list "$(__slurm_users)" ;;
	newname?(s)) __slurm_compreply_list "$(__slurm_users)" ;;
	partition?(s)) __slurm_compreply_list "$(__slurm_partitions)" ;;
	rawusage) __slurm_compreply "0" ;;
	wckey?(s)) __slurm_compreply "$(__slurm_wckeys)" ;;
	*)
		[[ $split == "true" ]] && return
		__slurm_compreply_param "${parameters[*]}"
		;;
	esac
}

# completion helper for sacctmgr wckeys specifications
# https://slurm.schedmd.com/sacctmgr.html#SECTION_LIST/SHOW-WCKey
function __slurm_comp_sacctmgr_spec_wckeys() {
	local mode="$1"
	local parameters=()
	local parameters_where=(
		"cluster="
		"id="
		"name="
		"user="
	)
	local parameters_set=(
	)
	local options=(
		"withdeleted"
	)

	__slurm_log_debug "$(__func__): mode='$mode'"

	__slurm_comp_mode_select "$mode"

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	case "${prev}" in
	cluster?(s)) __slurm_compreply_list "$(__slurm_clusters)" ;;
	name?(s)) __slurm_compreply_list "$(__slurm_wckeys)" ;;
	user?(s)) __slurm_compreply_list "$(__slurm_users)" ;;
	*)
		[[ $split == "true" ]] && return
		__slurm_compreply_param "${parameters[*]}"
		;;
	esac
}

# completion handler for: sacctmgr add account [key=val]...
function __sacctmgr_add_account() {
	__slurm_comp_sacctmgr_spec_accounts 2
}

# completion handler for: sacctmgr add cluster [key=val]...
function __sacctmgr_add_cluster() {
	__slurm_comp_sacctmgr_spec_clusters 2
}

# completion handler for: sacctmgr add coordinator [key=val]...
function __sacctmgr_add_coordinator() {
	__slurm_comp_sacctmgr_spec_coordinators 2
}

# completion handler for: sacctmgr add federation [key=val]...
function __sacctmgr_add_federation() {
	__slurm_comp_sacctmgr_spec_federations 2
}

# completion handler for: sacctmgr add qos [key=val]...
function __sacctmgr_add_qos() {
	__slurm_comp_sacctmgr_spec_qos 2
}

# completion handler for: sacctmgr add resource [key=val]...
function __sacctmgr_add_resource() {
	__slurm_comp_sacctmgr_spec_resources 2
}

# completion handler for: sacctmgr add user [key=val]...
function __sacctmgr_add_user() {
	__slurm_comp_sacctmgr_spec_users 2
}

# completion handler for: sacctmgr add *
function __sacctmgr_add() {
	local subcmds=(
		"account"
		"cluster"
		"coordinator"
		"federation"
		"qos"
		"resource"
		"user"
	)
	local subcmd
	subcmd="$(__slurm_find_subcmd "${subcmds[*]}")"

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_debug "$(__func__): subcmd='$subcmd'"
	__slurm_log_trace "$(__func__): #subcmds[@]='${#subcmds[@]}'"
	__slurm_log_trace "$(__func__): subcmds[*]='${subcmds[*]}'"

	if [[ -z ${subcmd-} ]]; then
		__slurm_compreply "${subcmds[*]}"
	else
		comp_cmd="${comp_cmd}_${subcmd//[^[:alnum:]]/}"
		__slurm_comp_command "${comp_cmd}"
	fi
}

# completion handler for: sacctmgr archive dump *
function __sacctmgr_archive_dump() {
	local parameters=(
		"directory="
		"events"
		"purgeeventafter="
		"purgejobafter="
		"purgestepafter="
		"purgesuspendafter="
		"script="
		"steps"
		"suspend"
	)

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	case "${prev}" in
	directory) _filedir ;;
	script) _filedir ;;
	*)
		[[ $split == "true" ]] && return
		__slurm_compreply_param "${parameters[*]}"
		;;
	esac
}

# completion handler for: sacctmgr archive load *
function __sacctmgr_archive_load() {
	local parameters=(
		"file="
		"insert="
	)

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	case "${prev}" in
	file) _filedir ;;
	*)
		[[ $split == "true" ]] && return
		__slurm_compreply_param "${parameters[*]}"
		;;
	esac
}

# completion handler for: sacctmgr archive *
function __sacctmgr_archive() {
	local subcmds=(
		"dump"
		"load"
	)
	local subcmd
	subcmd="$(__slurm_find_subcmd "${subcmds[*]}")"

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_debug "$(__func__): subcmd='$subcmd'"
	__slurm_log_trace "$(__func__): #subcmds[@]='${#subcmds[@]}'"
	__slurm_log_trace "$(__func__): subcmds[*]='${subcmds[*]}'"

	if [[ -z ${subcmd-} ]]; then
		__slurm_compreply "${subcmds[*]}"
	else
		comp_cmd="${comp_cmd}_${subcmd//[^[:alnum:]]/}"
		__slurm_comp_command "${comp_cmd}"
	fi
}

# completion handler for: sacctmgr create *
function __sacctmgr_create() {
	comp_cmd="${comp_cmd//_create/_add}"
	__sacctmgr_add
}

# completion handler for: sacctmgr delete *
function __sacctmgr_delete() {
	comp_cmd="${comp_cmd//_delete/_remove}"
	__sacctmgr_remove
}

# completion handler for: sacctmgr dump [key=val]...
function __sacctmgr_dump() {
	local subcmds=(
		"account"
		"association"
		"cluster"
		"configuration"
		"coordinator"
		"event"
		"federation"
		"job"
		"problem"
		"qos"
		"resource"
		"reservation"
		"runawayjobs"
		"stats"
		"transaction"
		"tres"
		"user"
		"wckey"
	)
	local parameters=(
		"file="
	)
	local subcmd
	subcmd="$(__slurm_find_subcmd "${subcmds[*]}")"
	local param
	param="$(__slurm_find_param "${parameters[*]}")"

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_debug "$(__func__): subcmd='$subcmd' param='$param'"
	__slurm_log_trace "$(__func__): #subcmds[@]='${#subcmds[@]}'"
	__slurm_log_trace "$(__func__): subcmds[*]='${subcmds[*]}'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	if [[ -z ${subcmd-} ]]; then
		__slurm_compreply "${subcmds[*]}"
	elif [[ -z ${param-} ]]; then
		__slurm_compreply "${parameters[*]}"
	else
		case "${prev}" in
		file) _filedir ;;
		esac
	fi
}

# completion handler for: sacctmgr list account [key=val]...
function __sacctmgr_list_account() {
	__slurm_comp_sacctmgr_spec_accounts 1
}

# completion handler for: sacctmgr list association [key=val]...
function __sacctmgr_list_association() {
	__slurm_comp_sacctmgr_spec_associations 1
}

# completion handler for: sacctmgr list cluster [key=val]...
function __sacctmgr_list_cluster() {
	__slurm_comp_sacctmgr_spec_clusters 1
}

# completion handler for: sacctmgr list event [key=val]...
function __sacctmgr_list_event() {
	__slurm_comp_sacctmgr_spec_events 1
}

# completion handler for: sacctmgr list federation [key=val]...
function __sacctmgr_list_federation() {
	__slurm_comp_sacctmgr_spec_federations 1
}

# completion handler for: sacctmgr list qos [key=val]...
function __sacctmgr_list_qos() {
	__slurm_comp_sacctmgr_spec_qos 1
}

# completion handler for: sacctmgr list resource [key=val]...
function __sacctmgr_list_resource() {
	__slurm_comp_sacctmgr_spec_resources 1
}

# completion handler for: sacctmgr list reservation [key=val]...
function __sacctmgr_list_reservation() {
	__slurm_comp_sacctmgr_spec_reservations 1
}

# completion handler for: sacctmgr list transaction [key=val]...
function __sacctmgr_list_transaction() {
	__slurm_comp_sacctmgr_spec_transactions 1
}

# completion handler for: sacctmgr list tres [key=val]...
function __sacctmgr_list_tres() {
	__slurm_comp_sacctmgr_spec_tres 1
}

# completion handler for: sacctmgr list user [key=val]...
function __sacctmgr_list_user() {
	__slurm_comp_sacctmgr_spec_users 1
}

# completion handler for: sacctmgr list wckey [key=val]...
function __sacctmgr_list_wckey() {
	__slurm_comp_sacctmgr_spec_wckeys 1
}

# completion handler for: sacctmgr list [key=val]...
function __sacctmgr_list() {
	local subcmds=(
		"account"
		"association"
		"cluster"
		"configuration"
		"event"
		"federation"
		"problem"
		"qos"
		"resource"
		"reservation"
		"runawayjobs"
		"stat"
		"transaction"
		"tres"
		"user"
		"wckey"
	)
	local subcmd
	subcmd="$(__slurm_find_subcmd "${subcmds[*]}")"

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_debug "$(__func__): subcmd='$subcmd'"
	__slurm_log_trace "$(__func__): #subcmds[@]='${#subcmds[@]}'"
	__slurm_log_trace "$(__func__): subcmds[*]='${subcmds[*]}'"

	if [[ -z ${subcmd-} ]]; then
		__slurm_compreply "${subcmds[*]}"
	else
		comp_cmd="${comp_cmd}_${subcmd//[^[:alnum:]]/}"
		__slurm_comp_command "${comp_cmd}"
	fi
}

# completion handler for: sacctmgr load [key=val]...
function __sacctmgr_load() {
	_filedir
}

# completion handler for: sacctmgr modify account [key=val]...
function __sacctmgr_modify_account() {
	__slurm_comp_sacctmgr_spec_accounts 3
}

# completion handler for: sacctmgr modify cluster [key=val]...
function __sacctmgr_modify_cluster() {
	__slurm_comp_sacctmgr_spec_clusters 3
}

# completion handler for: sacctmgr modify federation [key=val]...
function __sacctmgr_modify_federation() {
	__slurm_comp_sacctmgr_spec_federations 3
}

# completion handler for: sacctmgr modify job [key=val]...
function __sacctmgr_modify_job() {
	__slurm_comp_sacctmgr_spec_jobs 3
}

# completion handler for: sacctmgr modify qos [key=val]...
function __sacctmgr_modify_qos() {
	__slurm_comp_sacctmgr_spec_qos 3
}

# completion handler for: sacctmgr modify resource [key=val]...
function __sacctmgr_modify_resource() {
	__slurm_comp_sacctmgr_spec_resources 3
}

# completion handler for: sacctmgr modify user [key=val]...
function __sacctmgr_modify_user() {
	__slurm_comp_sacctmgr_spec_users 3
}

# completion handler for: sacctmgr modify [key=val]...
function __sacctmgr_modify() {
	local subcmds=(
		"account"
		"cluster"
		"federation"
		"job"
		"qos"
		"resource"
		"user"
	)
	local subcmd
	subcmd="$(__slurm_find_subcmd "${subcmds[*]}")"

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_debug "$(__func__): subcmd='$subcmd'"
	__slurm_log_trace "$(__func__): #subcmds[@]='${#subcmds[@]}'"
	__slurm_log_trace "$(__func__): subcmds[*]='${subcmds[*]}'"

	if [[ -z ${subcmd-} ]]; then
		__slurm_compreply "${subcmds[*]}"
	else
		comp_cmd="${comp_cmd}_${subcmd//[^[:alnum:]]/}"
		__slurm_comp_command "${comp_cmd}"
	fi
}

# completion handler for: sacctmgr remove account [key=val]...
function __sacctmgr_remove_account() {
	__slurm_comp_sacctmgr_spec_accounts 0
}

# completion handler for: sacctmgr remove cluster [key=val]...
function __sacctmgr_remove_cluster() {
	__slurm_comp_sacctmgr_spec_clusters 0
}

# completion handler for: sacctmgr remove coordinator [key=val]...
function __sacctmgr_remove_coordinator() {
	__slurm_comp_sacctmgr_spec_coordinators 0
}

# completion handler for: sacctmgr remove federation [key=val]...
function __sacctmgr_remove_federation() {
	__slurm_comp_sacctmgr_spec_federations 0
}

# completion handler for: sacctmgr remove qos [key=val]...
function __sacctmgr_remove_qos() {
	__slurm_comp_sacctmgr_spec_qos 0
}

# completion handler for: sacctmgr remove resource [key=val]...
function __sacctmgr_remove_resource() {
	__slurm_comp_sacctmgr_spec_resources 0
}

# completion handler for: sacctmgr remove user [key=val]...
function __sacctmgr_remove_user() {
	__slurm_comp_sacctmgr_spec_users 0
}

# completion handler for: sacctmgr remove *
function __sacctmgr_remove() {
	local subcmds=(
		"account"
		"cluster"
		"coordinator"
		"federation"
		"qos"
		"resource"
		"user"
	)
	local subcmd
	subcmd="$(__slurm_find_subcmd "${subcmds[*]}")"

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_debug "$(__func__): subcmd='$subcmd'"
	__slurm_log_trace "$(__func__): #subcmds[@]='${#subcmds[@]}'"
	__slurm_log_trace "$(__func__): subcmds[*]='${subcmds[*]}'"

	if [[ -z ${subcmd-} ]]; then
		__slurm_compreply "${subcmds[*]}"
	else
		comp_cmd="${comp_cmd}_${subcmd//[^[:alnum:]]/}"
		__slurm_comp_command "${comp_cmd}"
	fi
}

# completion handler for: sacctmgr list *
function __sacctmgr_show() {
	comp_cmd="${comp_cmd//_show/_list}"
	__sacctmgr_list
}

# completion handler for: sacctmgr update *
function __sacctmgr_update() {
	comp_cmd="${comp_cmd//_update/_modify}"
	__sacctmgr_modify
}

# Slurm completion helper for sacctmgr flag completion
#
# RET: 0 = did completion; 1 = no completion
function __slurm_comp_sacctmgr_flags() {
	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"

	case "${prev}" in
	*) return 1 ;;
	esac

	return 0
}

# sacctmgr completion handler
function _sacctmgr() {
	local cur prev words cword split
	__slurm_compinit "$1" || return
	__slurm_log_info "$(__func__): prev='$prev' cur='$cur'"

	local comp_cmd="$1"
	local subcmds=(
		"add" "create"
		"archive"
		"clear"
		"dump"
		"help"
		"list" "show"
		"load"
		"modify" "update"
		"reconfigure"
		"remove" "delete"
		"shutdown"
		"version"
	)
	local subcmd
	subcmd="$(__slurm_find_subcmd "${subcmds[*]}")"

	__slurm_log_debug "$(__func__): subcmd='$subcmd'"
	__slurm_log_trace "$(__func__): #subcmds[@]='${#subcmds[@]}'"
	__slurm_log_trace "$(__func__): subcmds[*]='${subcmds[*]}'"

	__slurm_comp_flags "$comp_cmd" && return
	__slurm_comp_sacctmgr_flags && return

	if [[ -z ${subcmd-} ]]; then
		__slurm_compreply "${subcmds[*]}"
	else
		comp_cmd="__${comp_cmd}_${subcmd//[^[:alnum:]]/}"
		__slurm_comp_command "${comp_cmd}"
	fi
}
complete -o nospace -F _sacctmgr sacctmgr

################################################################################
#			SALLOC Completion Functions
################################################################################

# salloc completion handler
# https://slurm.schedmd.com/salloc.html
function _salloc() {
	local cur prev words cword split
	__slurm_compinit "$1" || return
	__slurm_log_info "$(__func__): prev='$prev' cur='$cur'"

	__slurm_comp_common "$1" && return
	[[ $split == "true" ]] && return

	_filedir
}
complete -o nospace -F _salloc salloc

################################################################################
#			SATTACH Completion Functions
################################################################################

# sattach completion handler
# https://slurm.schedmd.com/sattach.html
function _sattach() {
	local cur prev words cword split
	__slurm_compinit "$1" || return
	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"

	__slurm_comp_flags "$1" && return
	[[ $split == "true" ]] && return

	__slurm_compreply "$(__slurm_jobsteps_tasks)"

}
complete -o nospace -F _sattach sattach

################################################################################
#			SBATCH Completion Functions
################################################################################

# sbatch completion handler
# https://slurm.schedmd.com/sbatch.html
function _sbatch() {
	local cur prev words cword split
	__slurm_compinit "$1" || return
	__slurm_log_info "$(__func__): prev='$prev' cur='$cur'"

	__slurm_comp_common "$1" && return
	[[ $split == "true" ]] && return

	_filedir
}
complete -o nospace -F _sbatch sbatch

################################################################################
#			SBCAST Completion Functions
################################################################################

# sbcast completion handler
# https://slurm.schedmd.com/sbcast.html
function _sbcast() {
	local cur prev words cword split
	__slurm_compinit "$1" || return
	__slurm_log_info "$(__func__): prev='$prev' cur='$cur'"

	__slurm_comp_flags "$1" && return

	case "${prev}" in
	-C | --compress) __slurm_compreply "$(__slurm_compress_types)" ;;
	--exclude?(s)) _filedir -d ;;
	-j | --jobid?(s)) __slurm_compreply "$(__slurm_jobs) $(__slurm_jobsteps)" ;;
	--send-lib?(s)) __slurm_compreply "$(__slurm_boolean)" ;;
	*)
		[[ $split == "true" ]] && return
		_filedir
		;;
	esac
}
complete -o nospace -F _sbcast sbcast

################################################################################
#			SCANCEL Completion Functions
################################################################################

# scancel completion handler
# https://slurm.schedmd.com/scancel.html
function _scancel() {
	local cur prev words cword split
	__slurm_compinit "$1" || return
	__slurm_log_info "$(__func__): prev='$prev' cur='$cur'"

	local states=(
		"pending"
		"running"
		"suspended"
	)

	__slurm_comp_flags "$1" && return

	case "${prev}" in
	-A | --account?(s)) __slurm_compreply_list "$(__slurm_accounts)" ;;
	-M | --cluster?(s)) __slurm_compreply_list "$(__slurm_clusters)" ;;
	-n | --jobname?(s)) __slurm_compreply_list "$(__slurm_jobnames)" ;;
	-w | --nodelist) __slurm_compreply_list "$(__slurm_nodes)" "ALL" "true" ;;
	-p | --partition?(s)) __slurm_compreply_list "$(__slurm_partitions)" ;;
	-q | --qos?(s)) __slurm_compreply_list "$(__slurm_qos)" ;;
	-R | --reservation?(s)) __slurm_compreply_list "$(__slurm_nodes)" "ALL" "true" ;;
	--sibling?(s)) __slurm_compreply_list "$(__slurm_clusters)" ;;
	-s | --signal) __slurm_compreply "$(__slurm_signals)" ;;
	-t | --state?(s)) __slurm_compreply "${states[*]}" ;;
	-u | --user?(s)) __slurm_compreply_list "$(__slurm_users)" ;;
	--wckey?(s)) __slurm_compreply_list "$(__slurm_wckeys)" ;;
	*) __slurm_compreply "$(__slurm_jobs) $(__slurm_jobsteps)" ;;
	esac
}
complete -o nospace -F _scancel scancel

################################################################################
#			SCONTROL Completion Functions
################################################################################

# completion handler for: scontrol cancel_reboot *
function __scontrol_cancel_reboot() {
	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"

	case "${prev}" in
	cancel_reboot) __slurm_compreply_list "$(__slurm_nodes)" "ALL" "true" ;;
	esac
}

# completion handler for: scontrol cluster *
function __scontrol_cluster() {
	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"

	case "${prev}" in
	cluster) __slurm_compreply "$(__slurm_clusters)" ;;
	esac
}

# completion handler for: scontrol create nodename=* *
function __scontrol_create_nodename() {
	local parameters=(
		"bcastaddr="
		"boards="
		"corespeccount="
		"corespersocket="
		"cpubind="
		"cpus="
		"cpuspeclist="
		"features="
		"gres="
		"memspeclimit="
		"nodeaddr="
		"nodehostname="
		"nodename=" # meta
		"port="
		"procs="
		"realmemory="
		"reason="
		"sockets="
		"socketsperboard="
		"state="
		"threadspercore="
		"tmpdisk="
		"weight="
	)
	local states=(
		"cloud"
		"future"
	)

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	case "${prev}" in
	cpubind) __slurm_compreply "$(__slurm_cpubind_types)" ;;
	feature?(s)) __slurm_compreply "$(__slurm_features)" ;;
	gres) __slurm_compreply "$(__slurm_gres)" ;;
	nodename?(s)) __slurm_compreply_list "$(__slurm_nodes)" "" "true" ;;
	state) __slurm_compreply "${states[*]}" ;;
	*)
		[[ $split == "true" ]] && return
		__slurm_compreply_param "${parameters[*]}"
		;;
	esac
}

# completion handler for: scontrol create partitionname=* [key=val]...
function __scontrol_create_partitionname() {
	__scontrol_update_partitionname
}

# completion handler for: scontrol create reservation [key=val]...
function __scontrol_create_reservation() {
	__scontrol_update_reservationname
}

# completion handler for: scontrol create reservationname=* [key=val]...
function __scontrol_create_reservationname() {
	__scontrol_update_reservationname
}

# completion handler for: scontrol create *
function __scontrol_create() {
	local parameters=(
		"nodename="
		"partitionname="
		"reservation "
		"reservationname="
	)
	local param
	param="$(__slurm_find_param "${parameters[*]}")"

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_debug "$(__func__): param='$param'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	if [[ -z ${param-} ]]; then
		__slurm_compreply "${parameters[*]}"
	else
		comp_cmd="${comp_cmd}_${param//[^[:alnum:]]/}"
		__slurm_comp_command "${comp_cmd}"
	fi
}

# completion handler for: scontrol delete *
function __scontrol_delete() {
	local parameters=(
		"nodename="
		"partitionname="
		"reservationname="
	)
	local param
	param="$(__slurm_find_param "${parameters[*]}")"

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_debug "$(__func__): param='$param'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	if [[ -z ${param-} ]]; then
		__slurm_compreply "${parameters[*]}"
	else
		case "${prev}" in
		nodename?(s)) __slurm_compreply_list "$(__slurm_nodes)" "ALL" "true" ;;
		partitionname?(s)) __slurm_compreply "$(__slurm_partitions)" ;;
		reservationname?(s)) __slurm_compreply "$(__slurm_reservations)" ;;
		esac
	fi
}

# completion handler for: scontrol hold *
function __scontrol_hold() {
	local parameters=(
		"jobid="
		"jobname="
	)

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	case "${prev}" in
	jobid) __slurm_compreply_list "$(__slurm_jobs)" ;;
	jobname) __slurm_compreply_list "$(__slurm_jobnames)" ;;
	*)
		[[ $split == "true" ]] && return
		__slurm_compreply_param "${parameters[*]}"
		;;
	esac
}

# completion handler for: scontrol notify *
function __scontrol_notify() {
	case "${prev}" in
	notify) __slurm_compreply "$(__slurm_jobs)" ;;
	esac
}

# completion handler for: scontrol pidinfo *
function __scontrol_pidinfo() {
	_pids # TODO: return only slurm pids
}

# completion handler for: scontrol listpids *
function __scontrol_listpids() {
	__slurm_compreply_list "$(__slurm_jobs)"
}

# completion handler for: scontrol reboot *
function __scontrol_reboot() {
	local parameters=(
		"asap"
		"nextstate="
		"reason="
	)
	local states=(
		"down"
		"resume"
	)

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	case "${prev}" in
	nextstate) __slurm_compreply "${states[*]}" ;;
	reboot) __slurm_compreply_list "$(__slurm_nodes)" "ALL" "true" ;;
	*)
		[[ $split == "true" ]] && return
		__slurm_compreply_param "${parameters[*]}"
		;;
	esac
}

# completion handler for: scontrol release *
function __scontrol_release() {
	local parameters=(
		"jobid="
		"jobname="
	)

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	case "${prev}" in
	jobid) __slurm_compreply_list "$(__slurm_jobs)" ;;
	jobname) __slurm_compreply_list "$(__slurm_jobnames)" ;;
	*)
		[[ $split == "true" ]] && return
		__slurm_compreply_param "${parameters[*]}"
		;;
	esac
}

# completion handler for: scontrol requeue *
function __scontrol_requeue() {
	local parameters=(
		"incomplete"
		"jobid="
	)

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	case "${prev}" in
	jobid) __slurm_compreply_list "$(__slurm_jobs)" ;;
	*)
		[[ $split == "true" ]] && return
		__slurm_compreply_param "${parameters[*]}"
		;;
	esac
}

# completion handler for: scontrol requeuehold *
function __scontrol_requeuehold() {
	local parameters=(
		"incomplete"
		"jobid="
		"state="
	)
	local states=(
		"specialexit"
	)

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	case "${prev}" in
	jobid) __slurm_compreply_list "$(__slurm_jobs)" ;;
	state) __slurm_compreply "${states[*]}" ;;
	*)
		[[ $split == "true" ]] && return
		__slurm_compreply_param "${parameters[*]}"
		;;
	esac
}

# completion handler for: scontrol resume *
function __scontrol_resume() {
	__slurm_compreply_list "$(__slurm_jobs)"
}

# completion handler for: scontrol schedloglevel *
function __scontrol_schedloglevel() {
	local schedlog_levels=(
		"disable"
		"enable"
	)

	__slurm_compreply "${schedlog_levels[*]}"
}

# completion handler for: scontrol setdebug *
function __scontrol_setdebug() {
	local parameters=(
		"nodes="
	)
	local debug_levels=(
		"quiet"
		"fatal"
		"error"
		"info"
		"verbose"
		"debug"
		"debug2"
		"debug3"
		"debug4"
		"debug5"
	)

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	case "${prev}" in
	setdebug) __slurm_compreply "${debug_levels[*]}" ;;
	node?(s)) __slurm_compreply_list "$(__slurm_nodes)" "ALL" "true" ;;
	*)
		[[ $split == "true" ]] && return
		__slurm_compreply_param "${parameters[*]}"
		;;
	esac
}

# completion handler for: scontrol setdebugflags *
function __scontrol_setdebugflags() {
	local debug_flags=(
		"accrue"
		"agent"
		"backfill"
		"backfillmap"
		"burstbuffer"
		"cgroup"
		"cpu_bind"
		"cpufrequency"
		"data"
		"dependency"
		"elasticsearch"
		"energy"
		"extsensors"
		"federation"
		"frontend"
		"gang"
		"gres"
		"hetjob"
		"jobaccountgather"
		"jobcontainer"
		"license"
		"network"
		"networkraw"
		"nodefeatures"
		"no_conf_hash"
		"power"
		"priority"
		"profile"
		"protocol"
		"reservation"
		"route"
		"script"
		"selecttype"
		"steps"
		"switch"
		"timecray"
		"tracejobs"
		"triggers"
		"workqueue"
	)
	local _debug_flags=()
	_debug_flags+=("$(compgen -P "+" -W "${debug_flags[*]}")")
	_debug_flags+=("$(compgen -P "-" -W "${debug_flags[*]}")")

	__slurm_compreply "${_debug_flags[*]}"
}

# completion handler for: scontrol show assoc_mgr *
function __scontrol_show_assoc_mgr() {
	local parameters=(
		"accounts="
		"flags="
		"qos="
		"users="
	)
	local flags=(
		"assoc"
		"users"
		"qos"
	)

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	case "${prev}" in
	account?(s)) __slurm_compreply_list "$(__slurm_accounts)" ;;
	flag?(s)) __slurm_compreply_list "${flags[*]}" ;;
	qos?(s)) __slurm_compreply_list "$(__slurm_qos)" ;;
	user?(s)) __slurm_compreply_list "$(__slurm_users)" ;;
	*)
		[[ $split == "true" ]] && return
		__slurm_compreply_param "${parameters[*]}"
		;;
	esac
}

# completion handler for: scontrol show hostlist *
function __scontrol_show_hostlist() {
	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"

	if [[ $cur =~ ^/ ]]; then
		_filedir
	else
		__slurm_compreply_list "$(__slurm_nodes)" "" "true"
	fi
}

# completion handler for: scontrol show hostlistsorted *
function __scontrol_show_hostlistsorted() {
	__scontrol_show_hostlist
}

# completion handler for: scontrol show hostnames *
function __scontrol_show_hostnames() {
	__slurm_compreply_list "$(__slurm_nodes)" "" "true"
}

# completion handler for: scontrol show jobs *
function __scontrol_show_jobs() {
	__slurm_compreply "$(__slurm_jobs)"
}

# completion handler for: scontrol show licenses *
function __scontrol_show_licenses() {
	__slurm_compreply "$(__slurm_licenses)"
}

# completion handler for: scontrol show nodes *
function __scontrol_show_nodes() {
	__slurm_compreply_list "$(__slurm_nodes)" "" "true"
}

# completion handler for: scontrol show partitions *
function __scontrol_show_partitions() {
	__slurm_compreply "$(__slurm_partitions)"
}

# completion handler for: scontrol show reservations *
function __scontrol_show_reservations() {
	__slurm_compreply "$(__slurm_reservations)"
}

# completion handler for: scontrol show step *
function __scontrol_show_steps() {
	__slurm_compreply "$(__slurm_jobs) $(__slurm_jobsteps)"
}

# completion handler for: scontrol show *
function __scontrol_show() {
	local subcmds=(
		"aliases"
		"assoc_mgr"
		"bbstat"
		"burstbuffer"
		"config"
		"daemons"
		"dwstat"
		"federations"
		"frontend"
		"hostlist"
		"hostlistsorted"
		"hostnames"
		"jobs"
		"licenses"
		"nodes"
		"partitions"
		"reservations"
		"slurmd"
		"steps"
		"topology"
	)
	local subcmd
	subcmd="$(__slurm_find_subcmd "${subcmds[*]}")"

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_debug "$(__func__): subcmd='$subcmd'"
	__slurm_log_trace "$(__func__): #subcmds[@]='${#subcmds[@]}'"
	__slurm_log_trace "$(__func__): subcmds[*]='${subcmds[*]}'"

	if [[ -z ${subcmd-} ]]; then
		__slurm_compreply "${subcmds[*]}"
	else
		comp_cmd="${comp_cmd}_${subcmd//[^[:alnum:]]/}"
		__slurm_comp_command "${comp_cmd}"
	fi
}

# completion handler for: scontrol shutdown *
function __scontrol_shutdown() {
	local parameters=(
		"slurmctld" "controller"
	)
	param="$(__slurm_find_param "${parameters[*]}")"

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_debug "$(__func__): param='$param'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	if [[ -z $param ]]; then
		__slurm_compreply "${parameters[*]}"
	fi
}

# completion handler for: scontrol suspend *
function __scontrol_suspend() {
	__slurm_compreply_list "$(__slurm_jobs)"
}

# completion handler for: scontrol top *
function __scontrol_top() {
	__slurm_compreply_list "$(__slurm_jobs)"
}

# completion handler for: scontrol token *
function __scontrol_token() {
	local parameters=(
		"lifespan="
		"username="
	)

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	case "${prev}" in
	lifespan) ;;
	username?(s)) __slurm_compreply "$(__slurm_users)" ;;
	*)
		[[ $split == "true" ]] && return
		__slurm_compreply_param "${parameters[*]}"
		;;
	esac
}

# completion handler for: scontrol uhold *
function __scontrol_uhold() {
	__scontrol_release
}

# completion handler for: scontrol update frontendname=* [key=val]...
function __scontrol_update_frontendname() {
	local parameters=(
		"frontendname=" # meta
		"reason="
		"state="
	)
	local states=(
		"down"
		"drain"
		"resume"
	)

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	case "${prev}" in
	frontendname) __slurm_compreply "$(__slurm_nodes_frontend)" ;;
	state?(s)) __slurm_compreply "${states[*]}" ;;
	*)
		[[ $split == "true" ]] && return
		__slurm_compreply_param "${parameters[*]}"
		;;
	esac
}

# completion handler for: scontrol update jobid=* [key=val]...
function __scontrol_update_jobid() {
	local parameters=(
		"account="
		"admincomment="
		"arraytaskthrottle="
		"burstbuffer="
		"clusters="
		"clusterfeatures="
		"comment="
		"contiguous="
		"corespec="
		"cpuspertask="
		"deadline="
		"delayboot="
		"dependency="
		"eligibletime="
		"endtime="
		"excnodelist="
		"extra="
		"features="
		"gres="
		"jobid=" # meta
		"jobname="
		"licenses="
		"mailtype="
		"mailuser="
		"mincpusnode="
		"minmemorycpu="
		"minmemorynode="
		"mintmpdisknode="
		"name="
		"nice="
		"nodelist="
		"numcpus="
		"numnodes="
		"numtasks="
		"oversubscribe="
		"partition="
		"prefer="
		"priority="
		"qos="
		"reboot="
		"reqcores="
		"reqnodelist="
		"reqnodes="
		"reqprocs="
		"reqsockets="
		"reqthreads="
		"irequeue="
		"reservationname="
		"resetaccruetime="
		"sitefactor="
		"stdout="
		"shared="
		"starttime="
		"switches="
		"taskspernode="
		"threadspec="
		"timelimit="
		"timemin="
		"userid="
		"wait-for-switch="
		"wckey="
		"workdir="
	)
	local binary=(
		"0"
		"1"
	)
	local mail_types=(
		"all"
		"array_tasks"
		"begin"
		"end"
		"fail"
		"none"
		"requeue"
		"stage_out"
		"time_limit"
		"time_limit_90"
		"time_limit_80"
		"time_limit_50"
	)

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	case "${prev}" in
	account?(s)) __slurm_compreply "$(__slurm_accounts)" ;;
	burstbuffer?(s)) __slurm_compreply_list "$(__slurm_burstbuffers)" ;;
	cluster?(s)) __slurm_compreply_list "$(__slurm_clusters)" ;;
	clusterfeature?(s)) __slurm_compreply_list "$(__slurm_features)" ;;
	contiguous) __slurm_compreply "$(__slurm_boolean)" ;;
	dependency) __slurm_comp_dependency ;;
	excnodelist) __slurm_compreply_list "$(__slurm_nodes)" "ALL" "true" ;;
	feature?(s)) __slurm_compreply_list "$(__slurm_features)" ;;
	gres) __slurm_compreply_list "$(__slurm_gres)" ;;
	jobid?(s)) __slurm_compreply "$(__slurm_jobs)" ;;
	jobname?(s)) __slurm_compreply_list "$(__slurm_jobnames)" ;;
	license?(s)) __slurm_compreply_list "$(__slurm_licenses)" ;;
	mailtype?(s)) __slurm_compreply_list "${mail_types[*]}" ;;
	mailuser?(s)) __slurm_compreply "$(__slurm_users)" ;;
	nodelist) __slurm_compreply_list "$(__slurm_nodes)" "ALL" "true" ;;
	oversubscribe) __slurm_compreply "$(__slurm_boolean)" ;;
	partition) __slurm_compreply_list "$(__slurm_partitions)" ;;
	prefer) __slurm_compreply "$(__slurm_features)" ;;
	qos?(s)) __slurm_compreply "$(__slurm_qos)" ;;
	reboot) __slurm_compreply "$(__slurm_boolean)" ;;
	reqnodelist) __slurm_compreply_list "$(__slurm_nodes)" "ALL" "true" ;;
	irequeue) __slurm_compreply "${binary[*]}" ;;
	reservationname?(s)) __slurm_compreply_list "$(__slurm_reservations)" ;;
	stdout) _filedir ;;
	shared) __slurm_compreply "$(__slurm_boolean)" ;;
	userid?(s)) __slurm_compreply "$(__slurm_users)" ;;
	wckey?(s)) __slurm_compreply "$(__slurm_wckey)" ;;
	workdir) _filedir -d ;;
	*)
		[[ $split == "true" ]] && return
		__slurm_compreply_param "${parameters[*]}"
		;;
	esac
}

# completion handler for: scontrol update nodename=* [key=val]...
function __scontrol_update_nodename() {
	local parameters=(
		"activefeatures="
		"availablefeatures="
		"comment="
		"cpubind="
		"extra="
		"gres="
		"nodeaddr="
		"nodehostname="
		"nodename=" # meta
		"reason="
		"resumeafter="
		"state="
		"weight="
	)
	local states=(
		"cancel_reboot"
		"down"
		"drain"
		"fail"
		"future"
		"noresp"
		"power_down"
		"power_down_asap"
		"power_down_force"
		"power_up"
		"resume"
		"undrain"
	)

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	case "${prev}" in
	activefeature?(s)) __slurm_compreply_list "$(__slurm_features_active)" ;;
	availablefeature?(s)) __slurm_compreply_list "$(__slurm_features)" ;;
	cpubind) __slurm_compreply "$(__slurm_cpubind_types)" ;;
	gres) __slurm_compreply "$(__slurm_gres)" ;;
	nodename?(s)) __slurm_compreply_list "$(__slurm_nodes)" "ALL" "true" ;;
	state?(s)) __slurm_compreply "${states[*]}" ;;
	*)
		[[ $split == "true" ]] && return
		__slurm_compreply_param "${parameters[*]}"
		;;
	esac
}

# completion handler for: scontrol update partitionname=* [key=val]...
function __scontrol_update_partitionname() {
	local parameters=(
		"allowaccounts="
		"allowgroups="
		"allowqos="
		"allocnodes="
		"alternate="
		"cpubind="
		"default="
		"defaulttime="
		"defmempercpu="
		"defmempernode="
		"denyaccounts="
		"denyqos="
		"disablerootjobs="
		"exclusiveuser="
		"gracetime="
		"hidden="
		"jobdefaults="
		"lln="
		"maxcpuspernode="
		"maxcpuspersocket="
		"maxmempercpu="
		"maxmempernode="
		"maxnodes="
		"maxtime="
		"minnodes="
		"nodes="
		"overtimelimit="
		"oversubscribe="
		"partitionname=" # meta
		"powerdownonidle="
		"preemptmode="
		"priority="
		"priorityjobfactor="
		"prioritytier="
		"qos="
		"rootonly="
		"reqresv="
		"shared="
		"state="
		"tresbillingweights="
	)
	local job_defaults=(
		"defcpupergpu="
		"defmempergpu="
	)
	local oversubscribe_types=(
		"exclusive"
		"force"
		"no"
		"yes"
	)
	local states=(
		"down"
		"drain"
		"inactive"
		"up"
	)
	local preempt_modes=(
		"cancel"
		"off"
		"requeue"
		"suspend"
	)

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	case "${prev}" in
	allowaccount?(s)) __slurm_compreply_list "$(__slurm_accounts)" "ALL" ;;
	allowgroup?(s)) __slurm_compreply_list "$(__slurm_linux_groups)" "ALL" ;;
	allowqos) __slurm_compreply_list "$(__slurm_qos)" "ALL" ;;
	allocnode?(s)) __slurm_compreply_list "$(__slurm_nodes)" "ALL" "true" ;;
	alternate) __slurm_compreply "$(__slurm_partitions) NONE" ;;
	cpubind) __slurm_compreply "$(__slurm_cpubind_types)" ;;
	default) __slurm_compreply "$(__slurm_boolean)" ;;
	denyaccount?(s)) __slurm_compreply_list "$(__slurm_accounts)" ;;
	denyqos) __slurm_compreply_list "$(__slurm_qos)" ;;
	disablerootjob?(s)) __slurm_compreply "$(__slurm_boolean)" ;;
	exclusiveuser) __slurm_compreply "$(__slurm_boolean)" ;;
	hidden) __slurm_compreply "$(__slurm_boolean)" ;;
	jobdefault?(s)) __slurm_compreply "${job_defaults[*]}" ;;
	lln) __slurm_compreply "$(__slurm_boolean)" ;;
	node?(s)) __slurm_compreply_list "$(__slurm_nodes)" "ALL" "true" ;;
	oversubscribe) __slurm_compreply "${oversubscribe_types[*]}" ;;
	partitionname) __slurm_compreply "$(__slurm_partitions)" ;;
	powerdownonidle) __slurm_compreply "$(__slurm_boolean)" ;;
	preemptmode) __slurm_compreply "${preempt_modes[*]}" ;;
	qos?(s)) __slurm_compreply "$(__slurm_qos)" ;;
	rootonly) __slurm_compreply "$(__slurm_boolean)" ;;
	reqresv) __slurm_compreply "$(__slurm_boolean)" ;;
	shared) __slurm_compreply "${oversubscribe_types[*]}" ;;
	state) __slurm_compreply "${states[*]}" ;;
	*)
		[[ $split == "true" ]] && return
		__slurm_compreply_param "${parameters[*]}"
		;;
	esac
}

# completion handler for: scontrol update reservationname=* [key=val]...
function __scontrol_update_reservationname() {
	local parameters=(
		"accounts="
		"burstbuffer="
		"corecnt="
		"duration="
		"endtime="
		"features="
		"flags="
		"groups="
		"licenses="
		"maxstartdelay="
		"nodecnt="
		"nodes="
		"partition="
		"reservationname=" # meta
		"skip"
		"starttime="
		"tres="
		"users="
	)
	local flags=(
		"any_nodes"
		"daily"
		"flex"
		"first_cores"
		"ignore_jobs"
		"license_only"
		"hourly"
		"maint"
		"magnetic"
		"no_hold_jobs_after"
		"overlap"
		"part_nodes"
		"purge_comp" "purge_comp\\\="
		"replace"
		"replace_down"
		"spec_nodes"
		"static_alloc"
		"time_float"
		"weekday"
		"weekend"
		"weekly"
	)

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	case "${prev}" in
	account?(s)) __slurm_compreply "$(__slurm_accounts)" ;;
	burstbuffer?(s)) __slurm_compreply_list "$(__slurm_burstbuffers)" ;;
	feature?(s)) __slurm_compreply_list "$(__slurm_features)" ;;
	flag?(s)) __slurm_compreply_list "${flags[*]}" ;;
	group?(s)) __slurm_compreply_list "$(__slurm_linux_groups)" ;;
	license?(s)) __slurm_compreply_list "$(__slurm_licenses)" ;;
	node?(s)) __slurm_compreply_list "$(__slurm_nodes)" "ALL" "true" ;;
	partition?(s)) __slurm_compreply "$(__slurm_partitions)" ;;
	reservationname?(s)) __slurm_compreply "$(__slurm_reservations)" ;;
	user?(s)) __slurm_compreply "$(__slurm_users)" ;;
	*)
		[[ $split == "true" ]] && return
		__slurm_compreply_param "${parameters[*]}"
		;;
	esac
}

# completion handler for: scontrol update stepid=* [key=val]...
function __scontrol_update_stepid() {
	local parameters=(
		"stepid=" # meta
		"timelimit="
	)

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	case "${prev}" in
	stepid) __slurm_compreply "$(__slurm_jobsteps)" ;;
	*)
		[[ $split == "true" ]] && return
		__slurm_compreply_param "${parameters[*]}"
		;;
	esac
}

# completion handler for: scontrol update *
function __scontrol_update() {
	local parameters=(
		"frontendname="
		"jobid="
		"nodename="
		"partitionname="
		"reservationname="
		"stepid="
	)
	local param
	param="$(__slurm_find_param "${parameters[*]}")"

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_debug "$(__func__): param='$param'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	if [[ -z ${param-} ]]; then
		__slurm_compreply "${parameters[*]}"
	else
		comp_cmd="${comp_cmd}_${param//[^[:alnum:]]/}"
		__slurm_comp_command "${comp_cmd}"
	fi
}

# completion handler for: scontrol wait_job *
function __scontrol_wait_job() {
	__slurm_compreply "$(__slurm_jobs)"
}

# completion handler for: scontrol write *
function __scontrol_write() {
	local subcmds=(
		"batch_script"
		"config"
	)
	local subcmd
	subcmd="$(__slurm_find_subcmd "${subcmds[*]}")"

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_debug "$(__func__): subcmd='$subcmd'"
	__slurm_log_trace "$(__func__): #subcmds[@]='${#subcmds[@]}'"
	__slurm_log_trace "$(__func__): subcmds[*]='${subcmds[*]}'"

	case "${subcmd}" in
	batch_script) __slurm_compreply "$(__slurm_jobs)" ;;
	config) _filedir ;;
	*) __slurm_compreply "${subcmds[*]}" ;;
	esac
}

# Slurm completion helper for scontrol flag completion
#
# RET: 0 = did completion; 1 = no completion
function __slurm_comp_scontrol_flags() {
	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"

	case "${prev}" in
	-M | --cluster?(s)) __slurm_compreply "$(__slurm_clusters)" ;;
	-u | --uid?(s)) __slurm_compreply "$(__slurm_users)" ;;
	*) return 1 ;;
	esac

	return 0
}

# scontrol completion handler
# https://slurm.schedmd.com/scontrol.html
function _scontrol() {
	local cur prev words cword split
	__slurm_compinit "$1" || return
	__slurm_log_info "$(__func__): prev='$prev' cur='$cur'"

	local comp_cmd="$1"
	local subcmds=(
		"abort"
		"cancel_reboot"
		"cluster"
		"create"
		"completing"
		"delete"
		"errnumstr"
		"fsdampeningfactor"
		"help"
		"hold"
		"notify"
		"pidinfo"
		"listpids"
		"ping"
		"reboot"
		"reconfigure"
		"release"
		"requeue"
		"requeuehold"
		"resume"
		"schedloglevel"
		"setdebug"
		"setdebugflags"
		"show"
		"shutdown"
		"suspend"
		"takeover"
		"top"
		"token"
		"uhold"
		"update"
		"version"
		"wait_job"
		"write"
	)
	local subcmd
	subcmd="$(__slurm_find_subcmd "${subcmds[*]}")"

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_debug "$(__func__): subcmd='$subcmd'"
	__slurm_log_trace "$(__func__): #subcmds[@]='${#subcmds[@]}'"
	__slurm_log_trace "$(__func__): subcmds[*]='${subcmds[*]}'"

	__slurm_comp_flags "$comp_cmd" && return
	__slurm_comp_scontrol_flags && return

	if [[ -z ${subcmd-} ]]; then
		__slurm_compreply "${subcmds[*]}"
	else
		comp_cmd="__${comp_cmd}_${subcmd//[^[:alnum:]]/}"
		__slurm_comp_command "${comp_cmd}"
	fi
}
complete -o nospace -F _scontrol scontrol

################################################################################
#			SCRONTAB Completion Functions
################################################################################

# scrontab completion handler
# https://slurm.schedmd.com/scrontab.html
function _scrontab() {
	local cur prev words cword split
	__slurm_compinit "$1" || return
	__slurm_log_info "$(__func__): prev='$prev' cur='$cur'"

	__slurm_comp_flags "$1" && return

	case "${prev}" in
	-u) __slurm_compreply "$(__slurm_users)" ;;
	esac

	[[ $split == "true" ]] && return

	if ((${#COMPREPLY[@]} == 0)) && [[ $cur == "" ]]; then
		__slurm_compreply "-"
	fi
}
complete -o nospace -F _scrontab scrontab

################################################################################
#			SDIAG Completion Functions
################################################################################

# sdiag completion handler
# https://slurm.schedmd.com/sdiag.html
function _sdiag() {
	local cur prev words cword split
	__slurm_compinit "$1" || return
	__slurm_log_info "$(__func__): prev='$prev' cur='$cur'"

	__slurm_comp_flags "$1" && return

	case "${prev}" in
	-M | --cluster?(s)) __slurm_compreply_list "$(__slurm_clusters)" ;;
	esac

	[[ $split == "true" ]] && return

	if ((${#COMPREPLY[@]} == 0)) && [[ $cur == "" ]]; then
		__slurm_compreply "--"
	fi
}
complete -o nospace -F _sdiag sdiag

################################################################################
#			SINFO Completion Functions
################################################################################

# sinfo completion handler
# https://slurm.schedmd.com/sinfo.html
function _sinfo() {
	local cur prev words cword split
	__slurm_compinit "$1" || return
	__slurm_log_info "$(__func__): prev='$prev' cur='$cur'"

	local fields=(
		"%ALL"
		"%a" "%A"
		"%b" "%B"
		"%c" "%C"
		"%d" "%D"
		"%e" "%E"
		"%f" "%F"
		"%g" "%G"
		"%h" "%H"
		"%I"
		"%l" "%L"
		"%m" "%M"
		"%n" "%N"
		"%o" "%O"
		"%p" "%P"
		"%r" "%R"
		"%s" "%S"
		"%t" "%T"
		"%u" "%U"
		"%v" "%V"
		"%w"
		"%X"
		"%Y"
		"%z" "%Z"
	)
	local fields_long=(
		"ALL"
		"allocmem"
		"allocnodes"
		"available"
		"cluster"
		"comment"
		"cores"
		"cpus"
		"cpusload"
		"cpusstate"
		"defaulttime"
		"disk"
		"extra"
		"features"
		"features_act"
		"freemem"
		"gres"
		"gresused"
		"groups"
		"maxcpuspernode"
		"memory"
		"nodeaddr"
		"nodeai"
		"nodeaiot"
		"nodehost"
		"nodelist"
		"nodes"
		"oversubscribe"
		"partition"
		"partitionname"
		"port"
		"preemptmode"
		"priorityjobfactor"
		"prioritytier"
		"reason"
		"root"
		"size"
		"socketcorethread"
		"sockets"
		"statecompact"
		"statelong"
		"statecomplete"
		"threads"
		"time"
		"timestamp"
		"user"
		"userlong"
		"version"
		"weight"
	)
	local states=(
		"allocated"
		"cloud"
		"completing"
		"down"
		"drain"
		"drained"
		"draining"
		"fail"
		"future"
		"idle"
		"maint"
		"mixed"
		"no_respond"
		"perfctrs"
		"planned"
		"power_down"
		"powering_down"
		"powered_down"
		"powering_up"
		"reboot"
		"reboot^"
		"reserved"
		"unknown"
		"?"
	)

	__slurm_comp_flags "$1" && return

	case "${prev}" in
	-M | --cluster?(s)) __slurm_compreply_list "$(__slurm_clusters)" ;;
	-o | --format) __slurm_compreply_list "${fields[*]}" ;;      # TODO: want --helpformat
	-O | --Format) __slurm_compreply_list "${fields_long[*]}" ;; # TODO: want --helpformat2
	-n | --node?(s)) __slurm_compreply_list "$(__slurm_nodes)" "ALL" "true" ;;
	-p | --partition?(s)) __slurm_compreply_list "$(__slurm_partitions)" ;;
	-S | --sort) __slurm_compreply_list "${fields[*]//%/}" ;;
	-t | --state?(s)) __slurm_compreply_list "${states[*]}" ;; # TODO: want --helpstate
	esac

	[[ $split == "true" ]] && return

	if ((${#COMPREPLY[@]} == 0)) && [[ $cur == "" ]]; then
		__slurm_compreply "--"
	fi
}
complete -o nospace -F _sinfo sinfo

################################################################################
#			SPRIO Completion Functions
################################################################################

# sprio completion handler
# https://slurm.schedmd.com/sprio.html
function _sprio() {
	local cur prev words cword split
	__slurm_compinit "$1" || return
	__slurm_log_info "$(__func__): prev='$prev' cur='$cur'"

	local fields=(
		"%a" "%A"
		"%b" "%B"
		"%c"
		"%f" "%F"
		"%i"
		"%j" "%J"
		"%N"
		"%p" "%P"
		"%q" "%Q"
		"%r"
		"%S"
		"%t" "%T"
		"%u"
		"%y" "%Y"
	)

	__slurm_comp_flags "$1" && return

	case "${prev}" in
	-M | --cluster?(s)) __slurm_compreply_list "$(__slurm_clusters)" ;;
	-o | --format) __slurm_compreply_list "${fields[*]}" ;; # TODO: want --helpformat
	-j | --job?(s)) __slurm_compreply_list "$(__slurm_jobs)" ;;
	-S | --sort) __slurm_compreply_list "${fields[*]//%/}" ;;
	-u) __slurm_compreply_list "$(__slurm_users)" ;;
	esac

	[[ $split == "true" ]] && return

	if ((${#COMPREPLY[@]} == 0)) && [[ $cur == "" ]]; then
		__slurm_compreply "--"
	fi
}
complete -o nospace -F _sprio sprio

################################################################################
#			SQUEUE Completion Functions
################################################################################

# squeue completion handler
# https://slurm.schedmd.com/squeue.html
function _squeue() {
	local cur prev words cword split
	__slurm_compinit "$1" || return
	__slurm_log_info "$(__func__): prev='$prev' cur='$cur'"

	local fields=(
		"%ALL"
		"%a" "%A"
		"%b" "%B"
		"%c" "%C"
		"%d" "%D"
		"%e" "%E"
		"%f" "%F"
		"%g" "%G"
		"%h" "%H"
		"%i" "%I"
		"%j" "%J"
		"%k" "%K"
		"%l" "%L"
		"%m" "%M"
		"%n" "%N"
		"%o" "%O"
		"%p" "%P"
		"%q" "%Q"
		"%r" "%R"
		"%s" "%S"
		"%t" "%T"
		"%u" "%U"
		"%v" "%V"
		"%w" "%W"
		"%x" "%X"
		"%y" "%Y"
		"%z" "%Z"
	)
	local fields_long=(
		"ALL"
		"account"
		"accruetime"
		"admin_comment"
		"allocnodes"
		"allocsid"
		"arrayjobid"
		"arraytaskid"
		"associd"
		"batchflag"
		"batchhost"
		"boardspernode"
		"burstbuffer"
		"burstbufferstate"
		"cluster"
		"clusterfeature"
		"command"
		"comment"
		"contiguous"
		"container"
		"cores"
		"corespec"
		"cpufreq"
		"cpus-per-task"
		"cpus-per-tres"
		"deadline"
		"delayboot"
		"dependency"
		"derivedec"
		"eligibletime"
		"endtime"
		"exit_code"
		"feature"
		"groupid"
		"groupname"
		"hetjobid"
		"hetjobidset"
		"hetjoboffset"
		"jobarrayid"
		"jobid"
		"lastschedeval"
		"licenses"
		"maxcpus"
		"maxnodes"
		"mcslabel"
		"mem-per-tres"
		"mincpus"
		"minmemory"
		"mintime"
		"mintmpdisk"
		"name"
		"network"
		"nice"
		"nodelist"
		"nodes"
		"ntperboard"
		"ntpercore"
		"ntpernode"
		"ntpersocket"
		"numcpus"
		"numnodes"
		"numtasks"
		"origin"
		"originraw"
		"oversubscribe"
		"partition"
		"preempttime"
		"pendingtime"
		"priority"
		"prioritylong"
		"profile"
		"qos"
		"reason"
		"reasonlist"
		"reboot"
		"reqnodes"
		"reqswitch"
		"requeue"
		"reservation"
		"resizetime"
		"restartcnt"
		"resvport"
		"schednodes"
		"sct"
		"selectjobinfo"
		"siblingsactive"
		"siblingsactiveraw"
		"siblingsviable"
		"siblingsviableraw"
		"sockets"
		"sperboard"
		"starttime"
		"state"
		"statecompact"
		"stderr"
		"stdin"
		"stdout"
		"stepid"
		"stepname"
		"stepstate"
		"submittime"
		"system_comment"
		"threads"
		"timeleft"
		"timelimit"
		"timeused"
		"tres-alloc"
		"tres-bind"
		"tres-freq"
		"tres-per-job"
		"tres-per-node"
		"tres-per-socket"
		"tres-per-step"
		"tres-per-task"
		"userid"
		"username"
		"wait4switch"
		"wckey"
		"workdir"
	)
	local states=(
		"boot_fail"
		"cancelled"
		"completed"
		"deadline"
		"failed"
		"node_fail"
		"out_of_memory"
		"pending"
		"preempted"
		"running"
		"requeued"
		"resizing"
		"revoked"
		"suspended"
		"timeout"
	)

	__slurm_comp_flags "$1" && return

	case "${prev}" in
	-A | --account?(s)) __slurm_compreply_list "$(__slurm_accounts)" ;;
	-o | --format) __slurm_compreply_list "${fields[*]}" ;;      # TODO: want --helpformat
	-O | --Format) __slurm_compreply_list "${fields_long[*]}" ;; # TODO: want --helpformat2
	-j | --job?(s)) __slurm_compreply_list "$(__slurm_jobs)" ;;
	-L | --license?(s)) __slurm_compreply_list "$(__slurm_licenses)" ;;
	-w | --nodelist) __slurm_compreply_list "$(__slurm_nodes)" "ALL" "true" ;;
	-p | --partition?(s)) __slurm_compreply_list "$(__slurm_partitions)" ;;
	-q | --qos?(s)) __slurm_compreply_list "$(__slurm_qos)" ;;
	-R | --reservation?(s)) __slurm_compreply_list "$(__slurm_reservations)" ;;
	-S | --sort) __slurm_compreply_list "${fields[*]//%/}" ;;
	-t | --state?(s)) __slurm_compreply_list "${states[*]}" ;;
	-u | --user?(s)) __slurm_compreply_list "$(__slurm_users)" ;;
	esac

	[[ $split == "true" ]] && return

	if ((${#COMPREPLY[@]} == 0)) && [[ $cur == "" ]]; then
		__slurm_compreply "--"
	fi
}
complete -o nospace -F _squeue squeue

################################################################################
#			SREPORT Completion Functions
################################################################################

# completion helper for sreport global specifications
#
# RET: 0 = did completion, 1 = did not do completion
function __slurm_comp_sreport_spec_all() {
	if [[ -z ${parameters[0]} ]]; then
		local parameters=()
	fi
	parameters+=(
		"clusters="
		"end="
		"format="
		"start="
	)

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_trace "$(__func__): #parameters[@]='${#parameters[@]}'"
	__slurm_log_trace "$(__func__): parameters[*]='${parameters[*]}'"

	case "${prev}" in
	cluster?(s)) __slurm_compreply_list "$(__slurm_clusters)" ;;
	*)
		[[ $split == "true" ]] && return 1
		__slurm_compreply_param "${parameters[*]}"
		return 1
		;;
	esac

	return 0
}

# completion handler for: sreport cluster *
function __sreport_cluster() {
	local subcmds=(
		"accountutilizationbyuser"
		"userutilizationbyaccount"
		"userutilizationbywckey"
		"utilization"
		"wckeyutilizationbyuser"
	)
	local subcmd
	subcmd="$(__slurm_find_subcmd "${subcmds[*]}")"
	parameters=(
		"accounts="
		"tree"
		"users="
		"wckeys="
	)

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_debug "$(__func__): subcmd='$subcmd'"
	__slurm_log_trace "$(__func__): #subcmds[@]='${#subcmds[@]}'"
	__slurm_log_trace "$(__func__): subcmds[*]='${subcmds[*]}'"

	if [[ -z ${subcmd-} ]]; then
		__slurm_compreply "${subcmds[*]}"
		return
	fi

	__slurm_comp_sreport_spec_all && return

	case "${prev}" in
	account?(s)) __slurm_compreply_list "$(__slurm_accounts)" ;;
	user?(s)) __slurm_compreply_list "$(__slurm_user)" ;;
	wckey?(s)) __slurm_compreply_list "$(__slurm_wckeys)" ;;
	*)
		[[ $split == "true" ]] && return 1
		__slurm_compreply_param "${parameters[*]}"
		return 1
		;;
	esac
}

# completion handler for: sreport job *
function __sreport_job() {
	local subcmds=(
		"sizesbyaccount"
		"sizesbyaccountandwckey"
		"sizesbywckey"
	)
	local subcmd
	subcmd="$(__slurm_find_subcmd "${subcmds[*]}")"
	parameters=(
		"accounts="
		"acctasparent"
		"flatview"
		"gid="
		"grouping="
		"jobs="
		"nodes="
		"partitions="
		"printjobcount="
		"users="
		"wckeys="
	)

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_debug "$(__func__): subcmd='$subcmd'"
	__slurm_log_trace "$(__func__): #subcmds[@]='${#subcmds[@]}'"
	__slurm_log_trace "$(__func__): subcmds[*]='${subcmds[*]}'"

	if [[ -z ${subcmd-} ]]; then
		__slurm_compreply "${subcmds[*]}"
		return
	fi

	__slurm_comp_sreport_spec_all && return

	case "${prev}" in
	account?(s)) __slurm_compreply_list "$(__slurm_accounts)" ;;
	gid?(s)) __slurm_compreply_list "$(__slurm_linux_gids)" ;;
	job?(s)) __slurm_compreply_list "$(__slurm_jobs) $(__slurm_jobsteps)" ;;
	node?(s)) __slurm_compreply_list "$(__slurm_nodes)" ;;
	partition?(s)) __slurm_compreply_list "$(__slurm_partitions)" ;;
	user?(s)) __slurm_compreply_list "$(__slurm_user)" ;;
	wckey?(s)) __slurm_compreply_list "$(__slurm_wckeys)" ;;
	*)
		[[ $split == "true" ]] && return 1
		__slurm_compreply_param "${parameters[*]}"
		return 1
		;;
	esac
}

# completion handler for: sreport reservation *
function __sreport_reservation() {
	local subcmds=(
		"utilization"
	)
	local subcmd
	subcmd="$(__slurm_find_subcmd "${subcmds[*]}")"
	parameters=(
		"names="
		"nodes="
	)

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_debug "$(__func__): subcmd='$subcmd'"
	__slurm_log_trace "$(__func__): #subcmds[@]='${#subcmds[@]}'"
	__slurm_log_trace "$(__func__): subcmds[*]='${subcmds[*]}'"

	if [[ -z ${subcmd-} ]]; then
		__slurm_compreply "${subcmds[*]}"
		return
	fi

	__slurm_comp_sreport_spec_all && return

	case "${prev}" in
	name?(s)) __slurm_compreply_list "$(__slurm_reservations)" ;;
	node?(s)) __slurm_compreply_list "$(__slurm_nodes)" ;;
	*)
		[[ $split == "true" ]] && return 1
		__slurm_compreply_param "${parameters[*]}"
		return 1
		;;
	esac
}

# completion handler for: sreport user *
function __sreport_user() {
	local subcmds=(
		"topusage"
	)
	local subcmd
	subcmd="$(__slurm_find_subcmd "${subcmds[*]}")"
	parameters=(
		"accounts="
		"group"
		"topcount="
		"users="
	)

	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"
	__slurm_log_debug "$(__func__): subcmd='$subcmd'"
	__slurm_log_trace "$(__func__): #subcmds[@]='${#subcmds[@]}'"
	__slurm_log_trace "$(__func__): subcmds[*]='${subcmds[*]}'"

	if [[ -z ${subcmd-} ]]; then
		__slurm_compreply "${subcmds[*]}"
		return
	fi

	__slurm_comp_sreport_spec_all && return

	case "${prev}" in
	account?(s)) __slurm_compreply_list "$(__slurm_accounts)" ;;
	user?(s)) __slurm_compreply_list "$(__slurm_user)" ;;
	*)
		[[ $split == "true" ]] && return 1
		__slurm_compreply_param "${parameters[*]}"
		return 1
		;;
	esac
}

# Slurm completion helper for sreport flag completion
#
# RET: 0 = did completion; 1 = no completion
function __slurm_comp_sreport_flags() {
	__slurm_log_debug "$(__func__): prev='$prev' cur='$cur'"

	local time_format=(
		"secper"
		"minper"
		"hourper"
		"seconds"
		"minutes"
		"hours"
		"percent"
	)

	case "${prev}" in
	-M | --cluster?(s)) __slurm_compreply_list "$(__slurm_clusters)" ;;
	-t) __slurm_compreply "${time_format[*]}" ;;
	-T | --tres) __slurm_compreply_list "$(__slurm_tres)" ;;
	*) return 1 ;;
	esac

	return 0
}

# sreport completion handler
# https://slurm.schedmd.com/sreport.html
function _sreport() {
	local cur prev words cword split
	__slurm_compinit "$1" || return
	__slurm_log_info "$(__func__): prev='$prev' cur='$cur'"

	local comp_cmd="$1"
	local subcmds=(
		"cluster"
		"job"
		"reservation"
		"user"
	)
	local subcmd
	subcmd="$(__slurm_find_subcmd "${subcmds[*]}")"

	__slurm_log_debug "$(__func__): subcmd='$subcmd'"
	__slurm_log_trace "$(__func__): #subcmds[@]='${#subcmds[@]}'"
	__slurm_log_trace "$(__func__): subcmds[*]='${subcmds[*]}'"

	__slurm_comp_flags "$comp_cmd" && return
	__slurm_comp_sreport_flags && return

	if [[ -z ${subcmd-} ]]; then
		__slurm_compreply "${subcmds[*]}"
	else
		comp_cmd="__${comp_cmd}_${subcmd//[^[:alnum:]]/}"
		__slurm_comp_command "${comp_cmd}"
	fi

	[[ $split == "true" ]] && return

	if ((${#COMPREPLY[@]} == 0)) && [[ $cur == "" ]]; then
		__slurm_compreply "--"
	fi
}
complete -o nospace -F _sreport sreport

################################################################################
#			SRUN Completion Functions
################################################################################

# srun completion handler
# https://slurm.schedmd.com/srun.html
function _srun() {
	local cur prev words cword split
	__slurm_compinit "$1" || return
	__slurm_log_info "$(__func__): prev='$prev' cur='$cur'"

	__slurm_comp_common "$1" && return
	[[ $split == "true" ]] && return

	_filedir
}
complete -o nospace -F _srun srun

################################################################################
#			SSHARE Completion Functions
################################################################################

# sshare completion handler
# https://slurm.schedmd.com/sshare.html
function _sshare() {
	local cur prev words cword split
	__slurm_compinit "$1" || return
	__slurm_log_info "$(__func__): prev='$prev' cur='$cur'"

	__slurm_comp_flags "$1" && return

	case "${prev}" in
	-A | --account?(s)) __slurm_compreply_list "$(__slurm_accounts)" ;;
	-M | --cluster?(s)) __slurm_compreply_list "$(__slurm_clusters)" ;;
	-o | --format) __slurm_compreply_list "$(__slurm_helpformat "$1")" ;;
	-u | --user?(s)) __slurm_compreply_list "$(__slurm_users)" ;;
	esac

	[[ $split == "true" ]] && return

	if ((${#COMPREPLY[@]} == 0)) && [[ $cur == "" ]]; then
		__slurm_compreply "--"
	fi
}
complete -o nospace -F _sshare sshare

################################################################################
#			SSTAT Completion Functions
################################################################################

# sstat completion handler
# https://slurm.schedmd.com/sstat.html
function _sstat() {
	local cur prev words cword split
	__slurm_compinit "$1" || return
	__slurm_log_info "$(__func__): prev='$prev' cur='$cur'"

	__slurm_comp_flags "$1" && return

	case "${prev}" in
	-o | --format | --field?(s)) __slurm_compreply_list "$(__slurm_helpformat "$1")" ;;
	-j | --job?(s)) __slurm_compreply_list "$(__slurm_jobs) $(__slurm_jobsteps)" ;;
	esac

	[[ $split == "true" ]] && return

	if ((${#COMPREPLY[@]} == 0)) && [[ $cur == "" ]]; then
		__slurm_compreply "--"
	fi
}
complete -o nospace -F _sstat sstat

################################################################################
#			STRIGGER Completion Functions
################################################################################

# strigger completion handler
# https://slurm.schedmd.com/strigger.html
function _strigger() {
	local cur prev words cword split
	__slurm_compinit "$1" || return
	__slurm_log_info "$(__func__): prev='$prev' cur='$cur'"

	local subcmds=(
		"--clear"
		"--get"
		"--set"
	)
	local subcmd
	subcmd="$(__slurm_find_subcmd "${subcmds[*]}")"
	local flags=(
		"perm"
	)

	__slurm_log_debug "$(__func__): subcmd='$subcmd'"
	__slurm_log_trace "$(__func__): #subcmds[@]='${#subcmds[@]}'"
	__slurm_log_trace "$(__func__): subcmds[*]='${subcmds[*]}'"

	if [[ -z ${subcmd-} ]]; then
		__slurm_compreply "${subcmds[*]}"
		return
	fi

	__slurm_comp_flags "$1" && return

	case "${prev}" in
	-M | --cluster?(s)) __slurm_compreply_list "$(__slurm_clusters)" ;;
	--flag?(s)) __slurm_compreply_list "${flags[*]}" ;;
	-j | --jobid?(s)) __slurm_compreply_list "$(__slurm_jobs)" ;;
	-n | --node?(s)) __slurm_compreply_list "$(__slurm_nodes)" "ALL" "true" ;;
	-p | --program) _filedir ;;
	--user?(s)) __slurm_compreply_list "$(__slurm_users)" ;;
	esac

	[[ $split == "true" ]] && return

	if ((${#COMPREPLY[@]} == 0)) && [[ $cur == "" ]]; then
		__slurm_compreply "--"
	fi
}
complete -o nospace -F _strigger strigger
