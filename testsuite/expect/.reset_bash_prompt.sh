#!/usr/bin/env bash
############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
# Auxiliary file to test interactive commands.
# See proc reset_bash_prompt in globals.
############################################################################

unset PROMPT_COMMAND
unset PS0
export PS1="TEST_PROMPT: "
