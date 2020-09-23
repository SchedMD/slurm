#!/usr/bin/env python
#
# Copyright 2015-2016 Hewlett Packard Enterprise Development LP
""" A script to generate slurm.conf and gres.conf for a
    Cray system on the smw """

from __future__ import print_function

import argparse
import os
import re
import subprocess
import sys
import time
import xml.etree.ElementTree
from jinja2 import Environment, FileSystemLoader

NAME = 'slurmconfgen_smw.py'


class Gres(object):
    """ A class for generic resources """
    def __init__(self, name, count):
        """ Initialize a gres with the given name and count """
        self.name = name
        self.count = count
        if name == 'gpu':
            if count == 1:
                self.file = '/dev/nvidia0'
            else:
                self.file = '/dev/nvidia[0-{0}]'.format(count - 1)
        elif name == 'mic':
            if count == 1:
                self.file = '/dev/mic0'
            else:
                self.file = '/dev/mic[0-{0}]'.format(count - 1)
        else:
            self.file = None

    def __eq__(self, other):
        """ Check if two gres are equal """
        return (self.name == other.name and self.count == other.count and
                self.file == other.file)

    def __str__(self):
        """ Return a gres string suitable for slurm.conf """
        if self.count == 1:
            return self.name
        else:
            return '{0}:{1}'.format(self.name, self.count)


def parse_args():
    """ Parse arguments """
    parser = argparse.ArgumentParser(
        description='Generate slurm.conf and gres.conf on a Cray smw')
    parser.add_argument('controlmachine',
                        help='Hostname of the node to run slurmctld')
    parser.add_argument('partition',
                        help='Partition to generate slurm.conf for')
    parser.add_argument('-t', '--templatedir',
                        help='Directory containing j2 templates',
                        default='.')
    parser.add_argument('-o', '--output',
                        help='Output directory for slurm.conf and gres.conf',
                        default='.')
    return parser.parse_args()


def get_service_nodes(partition):
    """ Gets a list of service nodes for the given partition. """
    print('Getting list of service nodes...')
    try:
        xtcliout = subprocess.check_output(['/opt/cray/hss/default/bin/xtcli',
                                            'status', partition],
                                           stderr=subprocess.STDOUT)
        service = []
        for line in xtcliout.splitlines():
            cname = re.match(
                r'\s*(c\d+-\d+c[0-2]s(?:\d|1[0-5])n[0-3]):\s+service',
                line)
            if cname:
                service.append(cname.group(1))

        print('Found {} service nodes.'.format(len(service)))
        return service
    except subprocess.CalledProcessError:
        return []


def get_node(nodexml):
    """
    Convert node XML into a node dictionary.
    Returns None for disabled nodes.
    """
    cname = nodexml.find('cname').text
    status = nodexml.find('status').text

    # Skip disabled nodes
    if status != 'enabled':
        print('Skipping {} node {}'.format(status, cname))
        return None

    cores = int(nodexml.find('cores').text)
    sockets = int(nodexml.find('sockets').text)
    memory = int(nodexml.find('memory/sizeGB').text) * 1024

    node = {'cname': cname,
            'nid': int(nodexml.find('nid').text),
            'CoresPerSocket': cores / sockets,
            'RealMemory': memory,
            'Sockets': sockets,
            'ThreadsPerCore': int(nodexml.find('hyper_threads').text)}

    # Determine the generic resources
    craynetwork = 4
    gpu = 0
    mic = 0
    for accelxml in nodexml.findall(
            'accelerator_list/accelerator/type'):
        if accelxml.text == 'GPU':
            gpu += 1
        elif accelxml.text == 'MIC':
            mic += 1
            craynetwork = 2
        else:
            print('WARNING: accelerator type {0} unknown'
                  .format(accelxml.text))

    node['Gres'] = [Gres('craynetwork', craynetwork)]
    if gpu > 0:
        node['Gres'].append(Gres('gpu', gpu))
    if mic > 0:
        node['Gres'].append(Gres('mic', mic))
    return node


def get_inventory(partition, service):
    """ Gets a hardware inventory for the given partition.
        Returns the node dictionary """
    print('Gathering hardware inventory...')
    nodes = {}

    # Get an inventory and parse the XML
    xthwinv = subprocess.Popen(['/opt/cray/hss/default/bin/xthwinv',
                                '-X', partition], stdout=subprocess.PIPE)
    inventory, _ = xthwinv.communicate()
    inventoryxml = xml.etree.ElementTree.fromstring(inventory)

    # Loop through all modules
    for modulexml in inventoryxml.findall('module_list/module'):
        # Loop through nodes in this module
        for nodexml in modulexml.findall('node_list/node'):
            node = get_node(nodexml)
            if node is None or node['cname'] in service:
                continue

            # Add to output data structures
            nodes[node['nid']] = node

    print('Found {} compute nodes.'.format(len(nodes)))
    return nodes


def compact_nodes(nodes):
    """ Compacts nodes when possible into single entries """
    basenode = None
    toremove = []

    print('Compacting node configuration...')
    for curnid in sorted(nodes):
        if basenode is None:
            basenode = nodes[curnid]
            nidlist = [int(curnid)]
            continue

        curnode = nodes[curnid]
        if (curnode['CoresPerSocket'] == basenode['CoresPerSocket'] and
                curnode['Gres'] == basenode['Gres'] and
                curnode['RealMemory'] == basenode['RealMemory'] and
                curnode['Sockets'] == basenode['Sockets'] and
                curnode['ThreadsPerCore'] == basenode['ThreadsPerCore']):
            # Append this nid to the nidlist
            nidlist.append(int(curnid))
            toremove.append(curnid)
        else:
            # We can't consolidate, move on
            basenode['NodeName'] = rli_compress(nidlist)
            basenode = curnode
            nidlist = [int(curnid)]

    basenode['NodeName'] = rli_compress(nidlist)

    # Remove nodes we've consolidated
    for nid in toremove:
        del nodes[nid]

    print('Compacted into {} group(s).'.format(len(nodes)))


def scale_mem(mem):
    """ Scale memory values back since available memory is
        lower than total memory """
    return mem * 98 / 100


def get_mem_per_cpu(nodes):
    """ Given the node configuration, determine the
        default memory per cpu (mem)/(cores)
        and max memory per cpu, returned as a tuple """
    defmem = 0
    maxmem = 0
    for node in nodes.values():
        if node['RealMemory'] > maxmem:
            maxmem = node['RealMemory']

        mem_per_thread = (node['RealMemory'] / node['Sockets'] /
                          node['CoresPerSocket'] / node['ThreadsPerCore'])
        if defmem == 0 or mem_per_thread < defmem:
            defmem = mem_per_thread

    return (scale_mem(defmem), scale_mem(maxmem))


def range_str(range_start, range_end, field_width):
    """ Returns a string representation of the given range
            using the given field width """
    if range_end < range_start:
        raise Exception('Range end before range start')
    elif range_start == range_end:
        return '{0:0{1}d}'.format(range_end, field_width)
    elif range_start + 1 == range_end:
        return '{0:0{2}d},{1:0{2}d}'.format(range_start, range_end,
                                            field_width)

    return '{0:0{2}d}-{1:0{2}d}'.format(range_start, range_end,
                                        field_width)


def rli_compress(nidlist):
    """ Given a list of node ids, rli compress them into a slurm hostlist
       (ex. list [1,2,3,5] becomes string nid0000[1-3,5]) """

    # Determine number of digits in the highest nid number
    numdigits = len(str(max(nidlist)))
    if numdigits > 5:
        raise Exception('Nid number too high')

    range_start = nidlist[0]
    range_end = nidlist[0]
    ranges = []
    for nid in nidlist:
        # If nid too large, append to rli and start fresh
        if nid > range_end + 1 or nid < range_end:
            ranges.append(range_str(range_start, range_end, numdigits))
            range_start = nid

        range_end = nid

    # Append the last range
    ranges.append(range_str(range_start, range_end, numdigits))

    return 'nid{0}[{1}]'.format('0' * (5 - numdigits), ','.join(ranges))


def get_gres_types(nodes):
    """ Get a set of gres types """
    grestypes = set()
    for node in nodes.values():
        grestypes.update([gres.name for gres in node['Gres']])
    return grestypes


def main():
    """ Get hardware info, format it, and write to slurm.conf and gres.conf """
    args = parse_args()

    # Get info from cnode and xthwinv
    service = get_service_nodes(args.partition)
    nodes = get_inventory(args.partition, service)
    nodelist = rli_compress([int(nid) for nid in nodes])
    compact_nodes(nodes)
    defmem, maxmem = get_mem_per_cpu(nodes)

    # Write files from templates
    jinjaenv = Environment(loader=FileSystemLoader(args.templatedir))
    conffile = os.path.join(args.output, 'slurm.conf')
    print('Writing Slurm configuration to {0}...'.format(conffile))
    with open(conffile, 'w') as outfile:
        outfile.write(jinjaenv.get_template('slurm.conf.j2').render(
            script=sys.argv[0],
            date=time.asctime(),
            controlmachine=args.controlmachine,
            grestypes=get_gres_types(nodes),
            defmem=defmem,
            maxmem=maxmem,
            nodes=nodes,
            nodelist=nodelist))

    gresfilename = os.path.join(args.output, 'gres.conf')
    print('Writing gres configuration to {0}...'.format(gresfilename))
    with open(gresfilename, 'w') as gresfile:
        gresfile.write(jinjaenv.get_template('gres.conf.j2').render(
            script=sys.argv[0],
            date=time.asctime(),
            nodes=nodes))

    print('Done.')


if __name__ == "__main__":
    main()
