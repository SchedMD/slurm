#!/usr/bin/env bash
############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
# Auxiliary file to test interactive commands.
# See proc reset_bash_prompt in globals.
############################################################################

# Issue 50963: TODO: Save diagnostic info
#   pid           — this bash's PID (== salloc child if -i bash, or srun-step bash)
#   ppid          — parent (helps identify whether we're under salloc, srun, sshd, …)
#   tty           — controlling terminal (confirms PTY setup vs. pipe)
#   stdin_isatty  — yes/no, mirrors `bash -i` detection
#   job/step      — SLURM env, confirms which allocation/step bash is in
if [ -w /var/slurm/log ]; then
	_reset_bash_prompt_log=/var/slurm/log/reset_bash_prompt.log
else
	_reset_bash_prompt_log=/tmp/reset_bash_prompt.log
fi
{
	printf '[%s] reset_bash_prompt sourced: pid=%s ppid=%s tty=%s stdin_isatty=%s job=%s step=%s\n' \
		"$(date '+%Y-%m-%dT%H:%M:%S.%3N')" \
		"$$" "$PPID" \
		"$(tty 2>/dev/null || echo not-a-tty)" \
		"$([ -t 0 ] && echo yes || echo no)" \
		"${SLURM_JOB_ID:-unset}" "${SLURM_STEP_ID:-unset}"
} >>"$_reset_bash_prompt_log" 2>/dev/null
unset _reset_bash_prompt_log

unset PROMPT_COMMAND
unset PS0
export PS1="TEST_PROMPT: "
