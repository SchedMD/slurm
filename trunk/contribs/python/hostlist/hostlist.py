#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# Hostlist library
#
# Version 1.2
#
# Copyright (C) 2008 Kent Engström <kent@nsc.liu.se> and
#                    Thomas Bellman <bellman@nsc.liu.se>,
#                    National Supercomputer Centre
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301, USA.

# WARNING: The behaviour in corner cases have not been compared for
# compatibility with pdsh/dshbak/SLURM et al.

import re
import itertools

# Exception used for error reporting to the caller
class BadHostlist(Exception): pass

# Configuration to guard against ridiculously long expanded lists
MAX_SIZE = 100000

# Hostlist expansion

def expand_hostlist(hostlist, allow_duplicates=False, sort=False):
    """Expand a Livermore hostlist string to a Python list.

    Exemple: expand_hostlist("n[9-11],d[01-02]") ==> 
             ['n9', 'n10', 'n11', 'd01', 'd02']

    Unless allow_duplicates is true, duplicates will be purged
    from the results. If sort is true, the output will be sorted.
    """

    results = []
    bracket_level = 0
    part = ""
    
    for c in hostlist + ",":
        if c == "," and bracket_level == 0:
            # Comma at top level, split!
            if part: results.extend(expand_part(part))
            part = ""
            bad_part = False
        else:
            part += c

        if c == "[": bracket_level += 1
        elif c == "]": bracket_level -= 1

        if bracket_level > 1:
            raise BadHostlist, "nested brackets"
        elif bracket_level < 0:
            raise BadHostlist, "unbalanced brackets"

    if bracket_level > 0:
        raise BadHostlist, "unbalanced brackets"

    if not allow_duplicates:
        results = remove_duplicates(results)
    if sort:
        results = numerically_sorted(results)
    return results

def expand_part(s):
    """Expand a part (e.g. "x[1-2]y[1-3][1-3]") (no outer level commas)."""

    # Base case: the empty part expand to the singleton list of ""
    if s == "":
        return [""]

    # Split into:
    # 1) prefix string (may be empty)
    # 2) rangelist in brackets (may be missing)
    # 3) the rest

    m = re.match(r'([^,\[]*)(\[[^\]]*\])?(.*)', s)
    (prefix, rangelist, rest) = m.group(1,2,3)

    # Expand the rest first (here is where we recurse!)
    rest_expanded = expand_part(rest)

    # Expand our own part
    if not rangelist:
        # If there is no rangelist, our own contribution is the prefix only
        us_expanded = [prefix]
    else:
        # Otherwise expand the rangelist (adding the prefix before)
        us_expanded = expand_rangelist(prefix, rangelist[1:-1])

    # Combine our list with the list from the expansion of the rest
    # (but guard against too large results first)
    if len(us_expanded) * len(rest_expanded) > MAX_SIZE:
        raise BadHostlist, "results too large"

    return [us_part + rest_part
            for us_part in us_expanded
            for rest_part in rest_expanded]

def expand_rangelist(prefix, rangelist):
    """ Expand a rangelist (e.g. "1-10,14"), putting a prefix before."""
    
    # Split at commas and expand each range separately
    results = []
    for range_ in rangelist.split(","):
        results.extend(expand_range(prefix, range_))
    return results

def expand_range(prefix, range_):
    """ Expand a range (e.g. 1-10 or 14), putting a prefix before."""

    # Check for a single number first
    m = re.match(r'^[0-9]+$', range_)
    if m:
        return ["%s%s" % (prefix, range_)]

    # Otherwise split low-high
    m = re.match(r'^([0-9]+)-([0-9]+)$', range_)
    if not m:
        raise BadHostlist, "bad range"

    (s_low, s_high) = m.group(1,2)
    low = int(s_low)
    high = int(s_high)
    width = len(s_low)

    if high < low:
        raise BadHostlist, "start > stop"
    elif high - low > MAX_SIZE:
        raise BadHostlist, "range too large"

    results = []
    for i in xrange(low, high+1):
        results.append("%s%0*d" % (prefix, width, i))
    return results

def remove_duplicates(l):
    """Remove duplicates from a list (but keep the order)."""
    seen = set()
    results = []
    for e in l:
        if e not in seen:
            results.append(e)
            seen.add(e)
    return results

# Hostlist collection

def collect_hostlist(hosts, silently_discard_bad = False):
    """Collect a hostlist string from a Python list of hosts.

    We start grouping from the rightmost numerical part.
    Duplicates are removed.

    A bad hostname raises an exception (unless silently_discard_bad
    is true causing the bad hostname to be silently discarded instead).
    """

    # Split hostlist into a list of (host, "") for the iterative part.
    # (Also check for bad node names now)
    # The idea is to move already collected numerical parts from the
    # left side (seen by each loop) to the right side (just copied).

    left_right = []
    for host in hosts:
        # We remove leading and trailing whitespace first, and skip empty lines
        host = host.strip()
        if host == "": continue

        # We cannot accept a host containing any of the three special
        # characters in the hostlist syntax (comma and flat brackets)
        if re.search(r'[][,]', host):
            if silently_discard_bad:
                continue
            else:
                raise BadHostlist, "forbidden character"

        left_right.append((host, ""))

    # Call the iterative function until it says it's done
    looping = True
    while looping:
        left_right, looping = collect_hostlist_1(left_right)
    return ",".join([left + right for left, right in left_right])

def collect_hostlist_1(left_right):
    """Collect a hostlist string from a list of hosts (left+right).

    The input is a list of tuples (left, right). The left part
    is analyzed, while the right part is just passed along
    (it can contain already collected range expressions).
    """

    # Scan the list of hosts (left+right) and build two things:
    # *) a set of all hosts seen (used later)
    # *) a list where each host entry is preprocessed for correct sorting

    sortlist = []
    remaining = set()
    for left, right in left_right:
        host = left + right
        remaining.add(host)

        # Match the left part into parts
        m = re.match(r'^(.*?)([0-9]+)?([^0-9]*)$', left)
        (prefix, num_str, suffix) = m.group(1,2,3)

        # Add the right part unprocessed to the suffix.
        # This ensures than an already computed range expression
        # in the right part is not analyzed again.
        suffix = suffix + right 

        if num_str is None:
            # A left part with no numeric part at all gets special treatment!
            # The regexp matches with the whole string as the suffix,
            # with nothing in the prefix or numeric parts.
            # We do not want that, so we move it to the prefix and put
            # None as a special marker where the suffix should be.
            assert prefix == ""
            sortlist.append(((host, None), None, None, host))
        else:
            # A left part with at least an numeric part
            # (we care about the rightmost numeric part)
            num_int = int(num_str)
            num_width = len(num_str) # This width includes leading zeroes
            sortlist.append(((prefix, suffix), num_int, num_width, host))

    # Sort lexicographically, first on prefix, then on suffix, then on
    # num_int (numerically), then...
    # This determines the order of the final result.

    sortlist.sort()

    # We are ready to collect the result parts as a list of new (left,
    # right) tuples.

    results = []
    needs_another_loop = False 

    # Now group entries with the same prefix+suffix combination (the
    # key is the first element in the sortlist) to loop over them and
    # then to loop over the list of hosts sharing the same
    # prefix+suffix combination.

    for ((prefix, suffix), group) in itertools.groupby(sortlist,
                                                       key=lambda x:x[0]):

        if suffix is None:
            # Special case: a host with no numeric part
            results.append(("", prefix)) # Move everything to the right part
            remaining.remove(prefix)
        else:
            # The general case. We prepare to collect a list of
            # ranges expressed as (low, high, width) for later
            # formatting.
            range_list = []
    
            for ((prefix2, suffix2), num_int, num_width, host) in group:
                if host not in remaining:
                    # Below, we will loop internally to enumate a whole range
                    # at a time. We then remove the covered hosts from the set.
                    # Therefore, skip the host here if it is gone from the set.
                    continue
                assert num_int is not None

                # Scan for a range starting at the current host
                low = num_int
                while True:
                    host = "%s%0*d%s" % (prefix, num_width, num_int, suffix)
                    if host in remaining:
                        remaining.remove(host)
                        num_int += 1
                    else:
                        break
                high = num_int - 1
                assert high >= low
                range_list.append((low, high, num_width))

            # We have a list of ranges to format. We make sure
            # we move our handled numerical part to the right to
            # stop it from being processed again.
            needs_another_loop = True
            if len(range_list) == 1 and range_list[0][0] == range_list[0][1]:
                # Special case to make sure that n1 is not shown as n[1] etc
                results.append((prefix,
                                "%0*d%s" % 
                               (range_list[0][2], range_list[0][0], suffix)))
            else:
                # General case where high > low
                results.append((prefix, "[" + \
                                   ",".join([format_range(l, h, w)
                                             for l, h, w in range_list]) + \
                                   "]" + suffix))

    # At this point, the set of remaining hosts should be empty and we
    # are ready to return the result, together with the flag that says
    # if we need to loop again (we do if we have added something to a
    # left part).
    assert not remaining
    return results, needs_another_loop

def format_range(low, high, width):
    """Format a range from low to high inclusively, with a certain width."""

    if low == high:
        return "%0*d" % (width, low)
    else:
        return "%0*d-%0*d" % (width, low, width, high)

# Sort a list of hosts numerically

def numerically_sorted(l):
    """Sort a list of hosts numerically.

    E.g. sorted order should be n1, n2, n10; not n1, n10, n2.
    """

    return sorted(l, key=numeric_sort_key)

def int_if_possible(x):
    try:
        return int(x)
    except:
        return x

def numeric_sort_key(x):
    return [int_if_possible(n) for n in re.findall("([0-9]+|[^0-9]+)", x)]

    
#
# Keep this part to tell users where the command line interface went
#

if __name__ == '__main__':
    import os, sys
    sys.stderr.write("The command line utility has been moved to a separate 'hostlist' program.\n")
    sys.exit(os.EX_USAGE)
