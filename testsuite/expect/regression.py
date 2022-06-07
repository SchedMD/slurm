#!/usr/bin/env python3
############################################################################
# Copyright (C) 2006 The Regents of the University of California.
# Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
# Written by Christopher J. Morrone <morrone2@llnl.gov>
# CODE-OCEC-09-009. All rights reserved.
#
# This file is part of Slurm, a resource management program.
# For details, see <https://slurm.schedmd.com/>.
# Please also read the supplied file: DISCLAIMER.
#
# Slurm is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version.
#
# Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along
# with Slurm; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
############################################################################

"""This script makes it easier to run the Slurm expect test scripts."""

from __future__ import print_function
import json
import os
import re
import sys
import time
import signal
from optparse import OptionParser
from optparse import OptionValueError
from subprocess import Popen

def main(argv=None):
    # "tests" is a list containing tuples of length 3 of the form
    # (test major number, test minor number, test filename)
    tests = []
    failed_tests = []
    passed_tests = []
    skipped_tests = []
    begin = (1,1)
    abort = False

    # Handle command line parameters
    if argv is None:
        argv = sys.argv

    parser = OptionParser()
    parser.add_option('-t', '--time-individual', action='store_true',
                      dest='time_individual', default=False)
    parser.add_option('-e', '--exclude', type='string', dest='exclude_tests',
                      action='callback', callback=test_parser,
                      help='comma or space separated string of tests to skip')
    parser.add_option('-i', '--include', type='string', dest='include_tests',
                      action='callback', callback=test_parser,
                      help='comma or space separated string of tests to include')
    parser.add_option('-k', '--keep-logs', action='store_true', default=False)
    parser.add_option('-s', '--stop-on-first-fail', action='store_true', default=False)
    parser.add_option('-b', '--begin-from-test', type='string',
                      dest='begin_from_test', action='callback',
                      callback=test_parser)
    parser.add_option('-f', '--results-file', type='string',
                      help='write json result to specified file name')

    (options, args) = parser.parse_args(args=argv)

    # Sanity check
    if not os.path.isfile('globals'):
        print('ERROR: "globals" not here as needed', file=sys.stderr)
        return -1

        # Clear any environment variables that could break the tests.
        # Cray sets some squeue format options that break tests
        del os.environ['SQUEUE_ALL']
        del os.environ['SQUEUE_SORT']
        del os.environ['SQUEUE_FORMAT']
        del os.environ['SQUEUE_FORMAT2']

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
        print('ERROR: no test files found in current working directory', file=sys.stderr)
        return -1
    # sory by major, minor
    tests.sort(key=lambda t: (t[0],t[1]))

    # Set begin value
    if options.begin_from_test is not None:
        begin  = options.begin_from_test[0]

    # Now run the tests
    start_time = time.time()
    test_env = os.environ.copy()
    if options.stop_on_first_fail:
        test_env["SLURM_TESTSUITE_CLEANUP_ON_FAILURE"] = "false"
    else:
        test_env["SLURM_TESTSUITE_CLEANUP_ON_FAILURE"] = "true"
    print('Started:', time.asctime(time.localtime(start_time)), file=sys.stdout)
    sys.stdout.flush()
    results_list = []
    for test in tests:
        if begin[0] > test[0] or (begin[0] == test[0] and begin[1] > test[1]):
            continue
        test_id = f"{test[0]}.{test[1]}"
        sys.stdout.write(f"Running test {test_id} ")
        sys.stdout.flush()
        test_dict = {}
        test_dict['id'] = test_id
        testlog_name = f"test{test_id}.log"
        try:
            os.remove(testlog_name+'.failed')
        except:
            pass
        testlog = open(testlog_name, 'w+')

        if options.time_individual:
            t1 = time.time()
            test_dict['start_time'] = float("%.03f" % t1)

        try:
            child = Popen(('expect', test[2]), shell=False,
                            env=test_env, stdout=testlog, stderr=testlog)
            retcode = child.wait()
        except KeyboardInterrupt:
            child.send_signal(signal.SIGINT)
            retcode = child.wait()
            abort = True

        if options.time_individual:
            t2 = time.time()
            minutes = int(int(t2-t1)/60)
            seconds = (int(t2-t1))%60
            if minutes > 0:
                sys.stdout.write('%d min '%(minutes))
            sys.stdout.write('%.2f sec '%(seconds))
            test_dict['duration'] = float("%.03f" % (t2 - t1))

        if retcode == 0:
            status = 'pass'
        elif retcode > 127:
            status = 'skip'
        else:
            status = 'fail'

        test_dict['status'] = status

        # Determine the reason if requesting a json results file
        if status != 'pass' and options.results_file:
            testlog.flush()
            testlog.seek(0)
            test_output = testlog.read()

            sections = [s for s in test_output.split('=' * 78 + "\n")]
            header = sections[1]
            body = sections[2]
            footer = ''.join(sections[3:])

            fatals = re.findall(r'(?ms)^\[[^\]]+\][ \[]+Fatal[ \]:]+(.*?) \(fail[^\)]+\)$', body)
            errors = re.findall(r'(?ms)^\[[^\]]+\][ \[]+Error[ \]:]+(.*?) \(subfail[^\)]+\)$', body)
            warnings = re.findall(r'(?ms)^\[[^\]]+\][ \[]+Warning[ \]:]+((?:(?!Warning).)*) \((?:sub)?skip[^\)]+\)$', body)
            if fatals:
                test_dict['reason'] = fatals[0]
            elif errors:
                test_dict['reason'] = errors[0]
            elif warnings:
                test_dict['reason'] = warnings[0]

        results_list.append(test_dict)

        testlog.close()

        if status == 'pass':
            passed_tests.append(test)
            sys.stdout.write('\n')
            if not options.keep_logs:
                try:
                    os.remove(testlog_name)
                except IOError as e:
                    print('ERROR failed to close %s %s' % (testlog_name, e),
                            file=sys.stederr);
        elif status == 'skip':
            skipped_tests.append(test)
            sys.stdout.write('SKIPPED\n')
            if not options.keep_logs:
                try:
                    os.remove(testlog_name)
                except IOError as e:
                    print('ERROR failed to close %s %s' % (testlog_name, e),
                            file=sys.stederr);
        else:
            failed_tests.append(test)
            os.rename(testlog_name, testlog_name+'.failed')
            sys.stdout.write('FAILED!\n')
            if options.stop_on_first_fail:
                break
        sys.stdout.flush()

        if abort:
            sys.stdout.write('\nRegression interrupted!\n')
            break

    end_time = time.time()
    print('Ended:', time.asctime(time.localtime(end_time)), file=sys.stdout)
    print('\nTestsuite ran for %d minutes %d seconds'\
          %((end_time-start_time)/60,(end_time-start_time)%60), file=sys.stdout)

    if options.results_file:
        with open(options.results_file, 'w') as results_file:
            json.dump(results_list, results_file)

    print('Completions  :', len(passed_tests), file=sys.stdout)
    print('Failures     :', len(failed_tests), file=sys.stdout)
    print('Skipped      :', len(skipped_tests), file=sys.stdout)
    if len(failed_tests) > 0:
        print('Failed tests : ', file=sys.stdout)
        first = True
        for test in failed_tests:
            if first:
                first = False
            else:
                sys.stdout.write(',')
            sys.stdout.write('%d.%d'%(test[0], test[1]))
        sys.stdout.write('\n')
        sys.stdout.flush()

    if abort:
        print('INCOMPLETE', file=sys.stdout)

    if len(failed_tests) > 0:
        return 1

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

if __name__ == "__main__":
    sys.exit(main())
