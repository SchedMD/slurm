#!/usr/bin/env python

import pwd
import sys
import time

import slurm

def elapsed_time_str(start_time):
    now = time.time()
    elapsed = now - start_time
    minutes = elapsed / 60
    seconds = elapsed % 60

    return '%d:%02d' % (minutes, seconds)

def get_job_steps(update_time=0, job_id=0, step_id=0, show_flags=0):
    l = []

    tmp = slurm.slurm_get_job_steps(update_time, job_id, step_id, show_flags)
    if tmp is None:
        return l

    for i in range(0, tmp.job_step_count):
        l.append(tmp.get_step(i))
    return l

def print_job_steps():
    steps = get_job_steps()

    print 'STEPID         NAME PARTITION     USER      TIME NODELIST'
    for s in steps:
        print '%-10s %8s %9s %8s %9s %s' % (
            str(s.job_id)+'.'+str(s.step_id),
            s.name,
            s.partition,
            pwd.getpwuid(s.user_id)[0],
            elapsed_time_str(s.start_time),
            s.nodes)

def get_jobs(update_time=0, show_flags=0):
    l = []

    tmp = slurm.slurm_load_jobs(update_time, show_flags)
    if tmp is None:
        return l

    for i in range(0, tmp.record_count):
        l.append(tmp.get_job(i))
    return l

def print_jobs():
    jobs = get_jobs()

    print '  JOBID PARTITION     NAME     USER  ST       TIME  NODES NODELIST'
    for j in jobs:
        if j.job_state <= slurm.JOB_COMPLETE:
            print '%7d %9s %8s %8s %3s %10s %6d %s' % (
                j.job_id,
                j.partition,
                j.name,
                pwd.getpwuid(j.user_id)[0],
                'R',
                elapsed_time_str(j.start_time),
                j.num_procs,
                j.nodes)

def main(argv=None):
    if argv is None:
        argv = sys.argv

    if '-s' in argv:
        print_job_steps()
    else:
        print_jobs()

if __name__ == "__main__":
        sys.exit(main())
