#!/bin/sh

date
echo "******************************************************************"
echo "running allred"
mpi-testscripts/allred
echo "******************************************************************"
echo "running alltoall"
mpi-testscripts/alltoall
echo "******************************************************************"
echo "running barrier_timed"
mpi-testscripts/barrier_timed
echo "******************************************************************"
echo "running allred_timed"
mpi-testscripts/allred_timed
echo "******************************************************************"
echo "running alltoall_timed"
mpi-testscripts/alltoall_timed
echo "******************************************************************"
date
