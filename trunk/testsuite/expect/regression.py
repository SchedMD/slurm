#!/usr/bin/env python
############################################################################
# Copyright (C) 2006 The Regents of the University of California.
# Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
# Written by Christopher J. Morrone <morrone2@llnl.gov>
# LLNL-CODE-402394.
# 
# This file is part of SLURM, a resource management program.
# For details, see <http://www.llnl.gov/linux/slurm/>.
#  
# SLURM is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version.
# 
# SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
# 
# You should have received a copy of the GNU General Public License along
# with SLURM; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
############################################################################

"""This script makes it easier to run the SLURM expect test scripts."""

import os
import re
import sys
import time
from optparse import OptionParser
from optparse import OptionValueError

def main(argv=None):
    try:
        from subprocess import Popen
    except:
        Popen = poor_Popen_substitute

    # "tests" is a list containing tuples of length 3 of the form
    # (test major number, test minor number, test filename)
    tests = []
    failed_tests = []
    passed_tests = []

    # Handle command line parameters
    if argv is None:
        argv = sys.argv

    parser = OptionParser()
    parser.add_option('-t', '--time-individual', action='store_true',
                      dest='time_individual', default=False)
    parser.add_option('-e', '--exclude', type='string', dest='exclude_tests',
                      action='callback', callback=test_parser,
                      help='comma or space seperated string of tests to skip')
    parser.add_option('-i', '--include', type='string', dest='include_tests',
                      action='callback', callback=test_parser,
                      help='comma or space seperated string of tests to include')
    parser.add_option('-k', '--keep-logs', action='store_true', default=False)
    (options, args) = parser.parse_args(args=argv)

    # Sanity check
    if not os.path.isfile('globals'):
        print >>sys.stderr, 'ERROR: "globals" not here as needed'
        return -1

    # Read the current working directory and build a sorted list
    # of the available tests.
    test_re = re.compile('test(\d+)\.(\d+)$')
    for filename in os.listdir('.'):
        match = test_re.match(filename)
        if match:
            major = int(match.group(1))
            minor = int(match.group(2))
            if not test_in_list(major, minor, options.exclude_tests) \
                   and (not options.include_tests
                        or test_in_list(major, minor, options.include_tests)):
                tests.append((major, minor, filename))
    if not tests:
        print >>sys.stderr, 'ERROR: no test files found in current working directory'
        return -1
    tests.sort(test_cmp)

    # Now run the tests
    start_time = time.time()
    print >>sys.stdout, 'Started:', time.asctime(time.localtime(start_time))
    sys.stdout.flush()
    for test in tests:
        sys.stdout.write('Running test %d.%d ' % (test[0],test[1]))
        sys.stdout.flush()
        testlog_name = 'test%d.%d.log' % (test[0],test[1])
        try:
            os.remove(testlog_name+'.failed')
        except:
            pass
        testlog = file(testlog_name, 'w+')

        if options.time_individual:
            t1 = time.time()
        retcode = Popen(('expect', test[2]), shell=False,
                        stdout=testlog, stderr=testlog).wait()
        if options.time_individual:
            t2 = time.time()
            minutes = int(t2-t1)/60
            seconds = (t2-t1)%60
            if minutes > 0:
                sys.stdout.write('%d min '%(minutes))
            sys.stdout.write('%.2f sec '%(seconds))

        testlog.close()
        if retcode == 0:
            passed_tests.append(test)
            sys.stdout.write('\n')
            if not options.keep_logs:
                os.remove(testlog_name)
        else:
            failed_tests.append(test)
            os.rename(testlog_name, testlog_name+'.failed')
            sys.stdout.write('FAILED!\n')
        sys.stdout.flush()

    end_time = time.time()
    print >>sys.stdout, 'Ended:', time.asctime(time.localtime(end_time))
    print >>sys.stdout, '\nTestsuite ran for %d minutes %d seconds'\
          %((end_time-start_time)/60,(end_time-start_time)%60)
    print >>sys.stdout
    print >>sys.stdout, 'Completions  :', len(passed_tests)
    print >>sys.stdout, 'Failures     :', len(failed_tests)
    if len(failed_tests) > 0:
        print >>sys.stdout, 'Failed tests : ',
        first = True
        for test in failed_tests:
            if first:
                first = False
            else:
                sys.stdout.write(', ')
            sys.stdout.write('%d.%d'%(test[0], test[1]))
        sys.stdout.write('\n')
        sys.stdout.flush()

def test_cmp(testA, testB):
    rc = cmp(testA[0], testB[0])
    if rc != 0:
        return rc
    return cmp(testA[1], testB[1])

def test_in_list(major, minor, test_list):
    '''Test for whether a test numbered major.minor is in test_list.

    "major" and "minor" must be integers.  "test_list" is a list of
    tuples, each tuple representing one test.  The tuples are of the
    form:

       (major, minor, filename)

    Returns True if the test is in the list, and False otherwise.
    '''

    if not test_list:
        return False
    for test in test_list:
        if ((test[0] == '*' or test[0] == major)
            and (test[1] == '*' or test[1] == minor)):
            return True
    return False

def test_parser(option, opt_str, value, parser):
    '''Option callback function for the optparse.OptionParser class.

    Will take a string representing one or more test names and append
    a tuple representing the test into a list in the options's destination
    variable.

    A string representing test names must patch the regular expression
    named "test_re" below.  Some examples of exceptable options are:

        '1.5'
        'test9.8'
        '2.6 test3.1 14.2'
        '3.4,6.7,8.3'
        '1.*'
        '*.2'
        '1.*,3.8,9.2'

    Raises OptionValueError on error.
    '''

    # Initialize the option's destination array, if is does not already exist.
    if not hasattr(parser.values, option.dest):
        setattr(parser.values, option.dest, [])
    if getattr(parser.values, option.dest) is None:
        setattr(parser.values, option.dest, [])

    # Get a pointer to the option's destination array.
    l = getattr(parser.values, option.dest)

    # Split the user's option string into a series of tuples that represent
    # each test, and add each tuple to the destination array.
    splitter = re.compile('[,\s]+')
    val = splitter.split(value)
    test_re = re.compile('(test)?((\d+)|\*)\.((\d+)|\*)$')
    for v in val:
        m = test_re.match(v)
        if not m:
            raise OptionValueError
        major = m.group(2)
        if major != '*':
            major = int(major)
        minor = m.group(4)
        if minor != '*':
            minor = int(minor)
        l.append((major, minor))

class poor_Popen_substitute:
    '''subprocess.Popen work-alike function.

    The subprocess module and its subprocess.Popen class were
    added in Python 2.4.  This function is provided to supply the
    subset of Popen functionality need by this program if run under
    older python interpreters.
    '''
    def __init__(self, args, shell=False, stdout=None, stderr=None):
        if shell is not False:
            raise Exception("This substitute Popen only supports shell=True")
        self.stdin = None
        self.stdout = None
        self.stderr = None
        self.pid = None
        self.returncode = None

        pid = os.fork()
        if pid > 0:
            self.pid = pid
            return
        elif pid == 0:
            if sys.stdout is not None:
                os.dup2(stdout.fileno(), sys.stdout.fileno())
                if sys.stdout == 'STDOUT':
                    os.dup2(stdout.fileno(), sys.stderr.fileno())
            if sys.stderr is not None:
                os.dup2(stderr.fileno(), sys.stderr.fileno())

            os.execvp(args[0], args)

    def wait(self):
        (pid, rc) = os.waitpid(self.pid, 0)
        return rc

if __name__ == "__main__":
    sys.exit(main())
