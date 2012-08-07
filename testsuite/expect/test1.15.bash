#!/bin/bash

    if [[ -z "$SLURM_PROCID" ]]
        then exit
    fi
    if [[ $SLURM_PROCID == 1 ]]
        then exit
    fi
    sleep 300

