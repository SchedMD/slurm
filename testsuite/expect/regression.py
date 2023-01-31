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
import csv
import configparser
from optparse import OptionParser
from optparse import OptionValueError
from optparse import OptionGroup
from subprocess import Popen


LINE = '=' * 70
CONF_DIR = 'config/'

def main(argv=None):

    # "tests" is a list containing tuples of length 3 of the form
    # (test major number, test minor number, test filename)
    tests = []
    failed_tests = []
    passed_tests = []
    skipped_tests = []
    begin = (1,1)
    abort = False

    # Args for new features (dev-mode) - TJ
    real_fails = 0
    test_list = os.listdir('.')
    use_dir_list = True
    local_file = CONF_DIR + 'local-order.csv'
    fails_file = CONF_DIR + 'last_run_fails.csv'
    last_failed = []
    failed_tests_out = []

    # Handle config file
    conf_path = os.getcwd() + '/' + CONF_DIR
    if not os.path.exists(conf_path):
        os.makedirs(conf_path)

    config_file = CONF_DIR + 'config.ini'
    config = configparser.ConfigParser()
    imported_conf_dict = {
        'recursions': None,
        'max_fails': None,
        'order_file': None,
        'jenkins_file': None
    }

    if os.path.isfile(config_file):
        config.read(config_file)
        imported_conf_dict = config['DEFAULT']

    # Handle command line parameters
    if argv is None:
        argv = sys.argv

    # Create OptionsParser()
    parser = create_parser()
    (options, args) = parser.parse_args(args=argv)

    # Apply config / SET user input settings
    dev_mode, user_file = True, None
    if options.user_mode:
        dev_mode, user_file  = False, ' '

    # SAVED options
    recursions = 3
    max_fails = int(options.set_max_fails or imported_conf_dict['max_fails'] or 1)
    order_file = options.set_order_file or imported_conf_dict['order_file'] or ''
    jenkins_file =  options.set_jenkins_file or imported_conf_dict['jenkins_file'] or ''
    start_idx = 0

    config['DEFAULT'] = {
        'recursions': recursions,
        'max_fails': max_fails,
        'order_file': order_file,
        'jenkins_file': jenkins_file
    }

    # Write config updated with options we keep
    try:
        with open(config_file, 'w') as cfile:
            config.write(cfile)
    except IOError as e:
        print('*** Error writing %s ***' % config_file)

    # TEMP options (this run only)
    max_fails = int(options.max_fails or max_fails)
    order_file = user_file or options.order_file or order_file
    jenkins_file = options.jenkins_file or jenkins_file

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

    # Write test information
    print(LINE)
    if options.gen_order_csv:
        print("\nGenerating fast order file local-order.csv from this dir")
        print("\n** Let the suite finish a complete run for accuracy **")
        print("** It will only complete up to the last finished test **\n")
        dev_mode = False
        options.time_individual = True

    # Print out config if in dev_mode
    if dev_mode: 
        print("Dev mode is ON")
        print("** Current config **")
        for key, val in config['DEFAULT'].items():
            print('  * %s: %s' % (key, val))

        options.time_individual = True
        if os.path.isfile(fails_file):
            last_failed = read_csv_to_list(fails_file)
            if len(last_failed) > 0:
                print("Previous fails file FOUND!")
                print("Found %d fails" % len(last_failed))

    # TODO need to get list of dir tests always to add to the order file
    if os.path.isfile(order_file):
        use_dir_list = False
        print("Order file found! Using: %s" % order_file)

        if options.exclude_fails:
            test_list = read_csv_to_list(order_file)
        else:
            # Append last run fails to the front and remove duplicates
            combined_list = last_failed + read_csv_to_list(order_file)
            for item in combined_list:
                if item not in test_list:
                    test_list.append(item)

    else:
        options.gen_csv = True
        print("\n** No local order file found, generating one this run **")

    # Read the current working directory and build a sorted list
    # of the available tests.
    test_re = re.compile('test(\d+)\.(\d+)$')
    for filename in test_list:

        # Account for test-order.csv naming conventions if using it
        if not use_dir_list:
            filename = filename[0].split(' ')[0]

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

    # Sort by major, minor if using the directory to build an ordered test list
    if use_dir_list:
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
    gen_file_list = []
    fails_list = []

    cur_idx = start_idx - 1

    for test in tests[start_idx:]:
        cur_idx += 1
        test_start_time = time.time()
        test_id = f"{test[0]}.{test[1]}"
        sys.stdout.write(f"\nRunning test {test_id} ")
        sys.stdout.flush()
        test_dict = {}
        test_dict['id'] = test_id
        testlog_name = f"test{test_id}.log"
        try:
            os.remove(testlog_name+'.failed')
        except:
            pass
        testlog = open(testlog_name, 'w+')

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

        t2 = time.time()
        minutes = int(int(t2-t1)/60)
        seconds = (int(t2-t1))%60
        test_dict['duration'] = float("%.03f" % (t2 - t1))

        if dev_mode:
            sys.stdout.write('%.2f ' % test_dict['duration'])

        if options.time_individual:
            if minutes > 0:
                sys.stdout.write('%d min '%(minutes))
            sys.stdout.write('%.2f sec '%(seconds))

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

            fatals = re.findall(r'(?ms)\[[^\]]+\][ \[]+Fatal[ \]:]+(.*?) \(fail[^\)]+\)$', body)
            errors = re.findall(r'(?ms)\[[^\]]+\][ \[]+Error[ \]:]+(.*?) \(subfail[^\)]+\)$', body)
            warnings = re.findall(r'(?ms)\[[^\]]+\][ \[]+Warning[ \]:]+((?:(?!Warning).)*) \((?:sub)?skip[^\)]+\)$', body)
            if fatals:
                test_dict['reason'] = fatals[0]
            elif errors:
                test_dict['reason'] = errors[0]
            elif warnings:
                test_dict['reason'] = warnings[0]

        results_list.append(test_dict)
        gen_file_list.append(
            ['test%s' % test_dict['id'], test_dict['duration'], 'local_run'])

        testlog.close()

        if status == 'pass':
            passed_tests.append(test)
            sys.stdout.write(' ')
            if not options.keep_logs:
                try:
                    os.remove(testlog_name)
                except IOError as e:
                    print('ERROR failed to close %s %s' % (testlog_name, e),
                            file=sys.stederr);
        elif status == 'skip':
            skipped_tests.append(test)
            sys.stdout.write('SKIPPED ')
            if not options.keep_logs:
                try:
                    os.remove(testlog_name)
                except IOError as e:
                    print('ERROR failed to close %s %s' % (testlog_name, e),
                            file=sys.stederr);
        else:
            if dev_mode:
                if not abort:
                    fails = 0
                    for i in range(recursions):
                        try:
                            child = Popen(('expect', test[2]), shell=False,
                                    env=test_env)
                            retcode = child.wait()
                        except KeyboardInterrupt:
                            child.send_signal(signal.SIGINT)
                            retcode = child.wait()
                            abort = True

                        if retcode != 0 and retcode != 127:
                            fails += 1

                    passes = recursions - fails
                    if passes > 0:
                        print("This was an intermittent failure %s / %s passed" % (passes, recursions))
                    else:
                        print("This is a real non-intermittent failure %s / %s failed" % (fails, recursions))
                        real_fails += 1
                        failed_tests.append(test)
                        failed_tests_out.append(['test%s' % test_dict['id']])
                        os.rename(testlog_name, testlog_name+'.failed')
                        sys.stdout.write('FAILED! ')
                        if real_fails >= max_fails:
                            abort = 1
                            break
            else:
                failed_tests.append(test)
                os.rename(testlog_name, testlog_name+'.failed')
                sys.stdout.write('FAILED! ')
                if options.stop_on_first_fail:
                    break

        sys.stdout.flush()

        if abort:
            sys.stdout.write('\nRegression interrupted!\n')

            print('current index %d' % cur_idx)

            # TODO
            print("\nPrompt to save fails file here")
            if dev_mode and len(failed_tests_out) > 0:
                if ('test%s' % test_dict['id']) == failed_tests_out[-1]:
                    failed_tests_out.pop()
                    print('Last interrupted test not included in the fails file')
            break

    # End tests loop
    end_time = time.time()

    # Prompt to gen local file if use_dir
    if not abort:
        print("\n" +LINE)
        gen_prompt = str(
            input("\nComplete. Create a new default order based on this run?  [n]/y : ")).lower() or "n"

        if gen_prompt == "y":
    #        if abort:
    #            gen_file_list.pop()

            # Sort by duration ascending
            gen_file_list.sort(key=lambda x: x[1])
            data = []
            for item in gen_file_list:
                data.append([item[0]])

            try:
                write_list_to_csv(data, local_file)
            except IOError as e:
                print('Error writing %s' % local_file)
            finally:
                print('\n>> %d tests in order written to %s' % (len(data), local_file))
                print('If interrupted the last test will not be included.')
                print("This file will now run as default\n")

    if dev_mode:
        try:
            write_list_to_csv(failed_tests_out, fails_file)
        except IOError as e:
            print('Error writing to fails file %s' % fails_file)
        finally:
            print('\nFails will be saved to %s and will run first next time' % fails_file)
            print("To not run them first next run use the '-x' flag")
            print("You could also increase your max fails with more '-n' or exclude with '-e'\n")

        #TODO put last run test name and index here:
        print(f'Last ran test index for {order_file}: {cur_idx}')
        print(f'Last ran test: {tests[cur_idx][2]}')

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


def read_csv_to_list(file_name):
    try:
        with open(file_name, newline='') as f:
            reader = csv.reader(f)
            return list(reader)
    except IOError as e:
            print('Error reading %s' % file_name)


def write_list_to_csv(src_list, file_name):
    try:
        with open(file_name, 'w+', newline='') as local_fileout:
            write = csv.writer(local_fileout)
            write.writerows(src_list)
    except IOError as e:
            print('Error writing %s' % file_name)


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


def create_parser():
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

    # Dev mode options
    group0 = OptionGroup(parser, "Development Mode Options",
                        "Runs as default, bypass with '-u'. "
                        "These options apply to 'dev mode'. Dev mode will run the "
                        "tests from quickest to slowest using an order csv file in "
                        "hopes of finding failures as fast as possible. "

                        "Once a failure is found it will rercursively check intermittency "
                        "and then report the failures as determined by the options below.")
    # Temp options
    group1 = OptionGroup(parser, "Single Run Options",
                        "lowercase = Just run like this once")
    group1.add_option('-u', '--user-mode', action='store_true', dest='user_mode',
                     help='bypass dev mode and run numerically in order')
    group1.add_option('-n', '--max-fails', type='int', dest='max_fails',
                     help='when in dev mode, the max fails allowed before ending the test. (default = 1)')
    group1.add_option('-o', '--order-file', type='string', dest='order_file',
                     help="run once using this csv file instead. row format must be 'test(\d+).(\d+)$\\n'")
    group1.add_option('-j', '--jenkins-file', type='string', dest='jenkins_file',
                     help="use this jenkins run comparison file")
    group1.add_option('-x', '--exclude-fails', action='store_true', dest='exclude_fails',
                    help="skip requiring running the previous fails (if any) first this run")

    # Setters
    group2 = OptionGroup(parser, "Permanent Options",
                        "UPPERCASE = Always run like this")
    group2.add_option('-N', '--set-max-fails', type='int', dest='set_max_fails',
                     help='when in dev mode, the max fails allowed before ending the test. (default = 1)')
    group2.add_option('-O', '--set-order-file', type='string', dest='set_order_file',
                     help="set default order csv file to this one. row format must be 'test(\d+).(\d+)$\\n'")
    group2.add_option('-J', '--set-jenkins-file', type='string', dest='set_jenkins_file',
                     help="set jenkins run comparison file")
    group2.add_option('-G', '--gen-order-csv', action='store_true', dest='gen_order_csv',
                    help= 'generate and save a new order file using this directory')

    parser.add_option_group(group0)
    parser.add_option_group(group1)
    parser.add_option_group(group2)
    return parser


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
