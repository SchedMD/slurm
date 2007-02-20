#!/usr/bin/env python

import pwd
import sys
import time

import slurm
import hostlist

def elapsed_time_str(start_time):
    now = time.time()
    elapsed = now - start_time
    minutes = elapsed / 60
    seconds = elapsed % 60

    return '%d:%02d' % (minutes, seconds)

def get_nodes(update_time=0, show_flags=0):
    l = []

    tmp = slurm.slurm_load_node(update_time, show_flags)
    if tmp is None:
        return l

    for i in range(0, tmp.record_count):
        l.append(tmp.get_node(i))
    return l

def get_partitions(update_time=0, show_flags=0):
    l = []

    tmp = slurm.slurm_load_partitions(update_time, show_flags)
    if tmp is None:
        return l

    for i in range(0, tmp.record_count):
        l.append(tmp.get_partition(i))
    return l

def find_part(partitions, nodename):
    part_str = ""
    for p in partitions:
        if nodename in hostlist.string_to_list(p.nodes):
            part_str = p.name
            if p.default_part == 1:
                part_str = part_str + '*'
                return p, part_str

node_states = {	slurm.NODE_STATE_UNKNOWN:	"unk",
                slurm.NODE_STATE_DOWN:		"down",
                slurm.NODE_STATE_IDLE:		"idle",
                slurm.NODE_STATE_ALLOCATED:	"alloc",
                slurm.NODE_STATE_END:		"BAD" }

def print_nodes():
    nodes = get_nodes()
    parts = get_partitions()

    print 'PARTITION AVAIL  TIMELIMIT NODES  STATE NODELIST'
    #      debug*       up      30:00     2  alloc foo[1-2]
    for n in nodes:
        part, part_str = find_part(parts, n.name)
        if part.state_up == 1:
            avail = 'up'
        else:
            avail = 'down'
        print '%-9s %5s %10s %5d %6s %s' % (
            part_str,
            avail,
            part.max_time,
            1,
            node_states[n.node_state],
            n.name)

def main(argv=None):
    if argv is None:
        argv = sys.argv

    print_nodes()

if __name__ == "__main__":
        sys.exit(main())
