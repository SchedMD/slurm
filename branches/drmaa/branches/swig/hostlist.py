#!/usr/bin/env python2
#
# $Id: hostlist.py,v 1.5 2004/04/16 23:50:01 morrone Exp $
#
# Copyright (C) 2003 The Regents of the University of California.
# Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
# Written by Christopher J. Morrone
# UCRL-CODE-155801.
#
# This file is part of Lustre Administrative Tools.
#
# Lustre Admininstrative Tools is free software; you can redistribute it
# and/or modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 2 of the License,
# or (at your option)  any later version.
#
# Lustre Admininstrative Tools is distributed in the hope that it will be
# useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
# Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with Lustre Admininstrative Tools; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
#############################################################################

import re
import string

def replace_range(match):
    '''Takes a match object for the pattern '(\d+)-(\d+)' and returns
    a string of comma-seperated numbers of the range.

    If the lower bound of the range is a zero-padded number, the numbers
    in the resulting string will also be zero-padded.'''
    zeropad = 0
    low = int(match.group(1))
    high = int(match.group(2))+1

    if low >= high:
        raise Exception('invalid host range')

    if len(match.group(1)) == len(match.group(2)) and match.group(1)[0] == '0':
        zeropad = 1
        padlen = len(match.group(1))

    range_list = range(low, high)
    if zeropad:
        val = string.zfill(`range_list[0]`, padlen)
    else:
        val = `range_list[0]`
    replacement = '%s' % val
    for val in range_list[1:]:
        val = `val`
        if zeropad:
            val = string.zfill(val, padlen)
        replacement = replacement + ', %s' % (val)

    return replacement

def string_to_list(hoststr):
    '''Create a new hostlist from a string representation.

    The string representation may contain one or more hostnames or
    bracketed hostlists separated by either ',' or whitespace. A bracketed
    hostlist is denoted by a common prefix followed by a list of numeric
    ranges contained within brackets: e.g. "tux[0-5,12,20-25]"
    '''

    bracket = 0
    start = 0
    hosts = []
    for i in range(len(hoststr)):
        if hoststr[i] == '[':
            bracket = bracket + 1
            continue
        if hoststr[i] == ']':
            bracket = bracket - 1
            continue
        if hoststr[i] == ',' and bracket == 0:
            substring = hoststr[start:i]
            hosts = hosts + [substring]
            start = i + 1
    hosts = hosts + [hoststr[start:i+1]]

    hostlist = []
    for host in hosts:
        host = re.sub('(\d+)-(\d+)', replace_range, host)
        m = re.match('(.*)\[(.*)]', host)
        if m:
            prefix = m.group(1)
            for number in m.group(2).split(','):
                number = number.strip()
                hostlist = hostlist + [prefix + number]
        else:
            hostlist = hostlist + [host]
    return hostlist

def list_to_string(hostlist):
    '''Returns the string representation of a list of hostnames.

    hostlist_ranged_string() will write a bracketed hostlist representation
    where possible.'''
    hostdict = {}
    # build a dictionary of hostname bases with dict values that are
    # arrays of the host numbers
    for host in hostlist:
        m = re.match('^(\D+)(\d+)$', host)
        if m:
            if not hostdict.has_key(m.group(1)):
                hostdict[m.group(1)] = []
            hostdict[m.group(1)].append(int(m.group(2)))
        else:
            hostdict[host] = None

    # sort the number arrays and remove duplicates
    for host in hostdict.keys():
        if hostdict[host] == None:
            continue
        hostdict[host].sort()
        i = 1
        while 1:
            if i >= len(hostdict[host]):
                break
            if hostdict[host][i] == hostdict[host][i-1]:
                del hostdict[host][i]
            else:
                i += 1

    # Make ANOTHER dictionary with arrays of ranges in string form
    newdict = {}
    for key in hostdict.keys():
        nums = hostdict[key]
        if nums == None:
            newdict[key] = None
            continue
        start = -1
        newdict[key] = []
        for num in nums:
            if start == -1:
                start = num
                end = num
            elif num == end + 1:
                end = num
            else:
                if start == end:
                    newdict[key].append(`start`)
                else:
                    newdict[key].append(`start`+'-'+`end`)
                start = num
                end = num
        if start == end:
            newdict[key].append(`start`)
        else:
            newdict[key].append(`start`+'-'+`end`)

    # Create the bracketed hostlist string
    hoststr = ''
    for base in newdict.keys():
        nums = newdict[base]
        hoststr += base
        if nums == None:
            hoststr += ','
        elif len(nums) == 1:
            if '-' in nums[0]:
                hoststr += '[' + nums[0] + '],'
            else:
                hoststr += nums[0] + ','
        else:
            hoststr += '['
            for num in nums:
                hoststr += num + ','
            hoststr = hoststr[:-1]
            hoststr += '],'
    hoststr = hoststr[:-1]
    
    return hoststr

def intersection(hl1, hl2):
    l1 = string_to_list(hl1)
    l2 = string_to_list(hl2)

    inter = [x for x in l1 for y in l2 if x == y]
    return list_to_string(inter)


if __name__ == "__main__":
    import getopt
    import sys
    num_hosts = 0
    delim = ','

    try:
        opts, leftover = getopt.getopt(sys.argv[1:], "in:d:", [])
    except getopt.GetoptError:
        raise

    inter = False
    for opt, val in opts:
        if opt == '-n':
            num_hosts = int(val)
        if opt == '-d':
            delim = val
        if opt == '-i':
            inter = True

    if leftover == []:
        sys.exit('Usage: hostlist.py [-n numhosts] '
                 '[-d delimiter] <hoststr>')

    if inter:
        x, y = leftover[0].split(':')
        res = intersection(x, y)
        if res:
            print res
            sys.exit(0)
        else:
            sys.exit(1)
    else:
        hostlist = string_to_list(leftover[0])
        if num_hosts == 0:
            num_hosts = len(hostlist)
        output = ''
        for i in range(num_hosts):
            output = output + hostlist[i]
            if i != num_hosts-1:
                output = output + delim
        print output
