#!/usr/bin/python

############################################################################
# Copyright (C) 2011-2013 SchedMD LLC
# Written by David Bigagli <david@schedmd.com>
#
# This file is part of SLURM, a resource management program.
# For details, see <http://slurm.schedmd.com/>.
# Please also read the included file: DISCLAIMER.
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

import sys
import os
import errno
import time
import argparse
import ConfigParser
import pdb
import subprocess
import datetime
import smtplib
from email.mime.text import MIMEText
import logging

"""This program is a driver for the Slurm regression program."""
# Print the usage if asked for
def usage():
    print 'regress.py: [-h] slurm_version (e.g. 2.6.4)'

logger = logging.getLogger('log.py')
def init_log(htab, conf):

    for section in conf.sections():
        if section == 'params':
            continue

        logfile = '%s/log/%s/Log' % (htab['root'], section)
        logger.setLevel(logging.DEBUG)
        # create console handler and set level to debug
        ch = logging.StreamHandler()
        ch.setLevel(logging.DEBUG)

        fh = logging.FileHandler(logfile)
        # create formatter
        formatter \
            = logging.Formatter('%(asctime)s %(name)s %(levelname)s %(message)s',\
                                    datefmt='%b %d %H:%M:%S')

        # add formatter to ch
        ch.setFormatter(formatter)
        fh.setFormatter(formatter)

        # add ch to logger
        logger.addHandler(ch)
        logger.addHandler(fh)

# Read the driver configuration which is in ini format
#
# NOTA BENE:
# ConfigParser craps out if there are leading spaces
# in the configuration file.
#
#[params]
#root = /home/david/regression
#mailto = david@schedmd.com
#
#[test_1]
#version = 26
#arch = linux
#multiple_slurmd = 4
#
def read_config(confile):

    conf = ConfigParser.ConfigParser()
    try:
        conf.read(confile)
        logger.info( 'configuration read')
    except Exception as e:
        logger.info( 'Error reading configuration file')
        print e
        return -1

    for section in conf.sections():
        logger.info( 'section -> %s', section)
        for option in conf.options(section):
            logger.info( '%s = %s', option, conf.get(section, option))

    return conf

def read_params(htab, conf):

    try:
        root = conf.get('params', 'root')
        htab['root'] = root
        logger.info( 'root -> %s', root)
    except ConfigParser.NoOptionError as e:
        print >> sys.stderr, 'Error root option missing from configuration', e

    try:
        mailto = conf.get('params', 'mailto')
        htab['mailto'] = mailto
        logger.info( 'mailto -> %s', mailto)
    except ConfigParser.NoOptionError :
        pass

def configure_and_build(htab, section, conf):

        test = section
        try:
            version = conf.get(section, 'version')
            arch = conf.get(section, 'arch')
            multi = conf.get(section, 'multiple_slurmd')
        except:
            pass

        logger.info('%s %s %s %s', test, version, arch, multi)
        buildpath = '%s/clusters/%s/%s/build' % (htab['root'], version, arch)
        sbindir = '%s/clusters/%s/%s/sbin' % (htab['root'], version, arch)
        bindir = '%s/clusters/%s/%s/bin' % (htab['root'], version, arch)
        prefix = '%s/clusters/%s/%s' % (htab['root'], version, arch)
        srcdir = '%s/distrib/%s/slurm' % (htab['root'], version)
        logdir = '%s/log/%s' % (htab['root'], test)
        logfile = '%s/%s.build' % (logdir, htab['cas'])
        slurmdbd = '%s/slurmdbd' % (sbindir)
        slurmctld = '%s/slurmctld' % (sbindir)
        slurmd = '%s/slurmd' % (sbindir)
        arturo = '%s/arturo' % (sbindir)

        try:
            if not os.path.isdir(logdir):
                os.makedirs(logdir)
            lfile = open(logfile, 'w')
        except IOError as e:
            print >> sys.stderr, 'mkdir() or open() failed', e

        logger.info( 'buildpath -> %s', buildpath)
        logger.info( 'prefix -> %s', prefix)
        logger.info( 'srcdir -> %s', srcdir)
        logger.info( 'logdir -> %s', logdir)
        logger.info( 'logfile -> %s', logfile)
        logger.info( 'sbindir -> %s', sbindir)
        logger.info( 'bindir -> %s', bindir)
        logger.info( 'slurmdbd -> %s', slurmdbd)
        logger.info( 'slurmctld -> %s', slurmctld)
        logger.info( 'slurmd -> %s', slurmd)
        logger.info( 'arturo -> %s', arturo)

        # before configuring let's make sure to pull
        # the github repository
        git_update(srcdir)

        # configure and build
        os.chdir(buildpath)
        logger.info('cd -> %s', os.getcwd())

        if multi != 0:
            configure = \
                '%s/configure --prefix=%s --enable-debug --enable-multiple-slurmd' % \
                (srcdir, prefix)
        else:
            configure = '%s/configure --prefix=%s --enable-debug' % (srcdir, prefix, multi)

        logger.info('running -> %s', configure)
        try:
            proc = subprocess.Popen(configure,
                                    shell=True,
                                    stdout=lfile,
                                    stderr=subprocess.PIPE)
        except OSError as e:
            print >> sys.stderr, 'Error execution failed:', e

        rc = proc.wait()
        if rc != 0:
            for line in proc.stderr:
                print >> sys.stderr, 'Error configure failed'
                print('configure stderr: ' + line.rstrip())

        make = '/usr/bin/make -j 4 install'
        logger.info( 'cd -> %s', os.getcwd())
        logger.info('running -> %s', make)
        try:
            proc = subprocess.Popen(make,
                                    shell=True,
                                    stdout=lfile,
                                    stderr=subprocess.PIPE)
        except OSError as e:
            print >> sys.stderr, 'Error execution failed:', e

        rc = proc.wait()
        if rc != 0:
            for line in proc.stderr:
                print >> sys.stderr, 'Error make failed'
                print('configure stderr: ' + line.rstrip())

        lfile.close()
        # Use hash table to communicate across
        # functions.
        htab['buildpath'] = buildpath
        htab['prefix'] = prefix
        htab['srcdir'] = srcdir
        htab['logdir'] = logdir
        htab['logfile'] = logfile
        htab['sbindir'] = sbindir
        htab['bindir'] = bindir
        htab['slurmdbd'] = slurmdbd
        htab['slurmctld']= slurmctld
        htab['slurmd'] = slurmd
        htab['arturo'] = arturo
        htab['multi'] = multi


def git_update(srcdir):

    logger.info('running git pull on -> %s', srcdir)
    gitpull = 'git pull'

    # dont forget to chdir back
    os.chdir(srcdir)
    try:
        proc = subprocess.check_call([gitpull], shell=True)
    except Exception as e:
        print >> sys.stderr, 'Failed to run git pull on', srcdir, e

def start_daemons(htab):

    logger.info('starting daemons...')
    try:
        subprocess.check_call([htab['slurmdbd']])
    except Exception as e:
        print >> sys.stderr, 'Error failed starting slurmdbd', e
        return -1
    logger.info('slurmdbd started')

    try:
        subprocess.check_call([htab['slurmctld']])
    except Exception as e:
        print >> sys.stderr, 'Error failed starting slurmdbd', e
        return -1
    logger.info('slurmctld started')

    try:
        if htab['multi'] != 0:
            subprocess.check_call([htab['arturo'], htab['multi']])
        else:
            subprocess.check_call([htab['slurmd']])
    except Exception as e:
        print >> sys.stderr, 'Error failed starting slurmd/arturo', e
        return -1
    logger.info('slurmd/arturo started')

    logger.info('Wait 5 secs for all slurmd to come up...')
    time.sleep(5)

    # run sinfo to check if all sweet
    sinfo = '%s/sinfo --noheader --format=%%T' % (htab['bindir'])
    logger.info('sinfo -> %s', sinfo)

    proc = subprocess.Popen(sinfo,
                            shell=True,
                            stdout=subprocess.PIPE,
                            stderr=None)
    rc = proc.wait()
    if rc != 0:
        print >> sys.stderr, 'Error sinfo failed to check cluster state'
        return -1
    for line in proc.stdout:
        if line.strip() == 'idle':
            logger.info( 'Cluster state is ok -> %s', line.strip())
        else:
            print >> sys.stderr, 'Failed to get correct cluster status'
            return -1

def run_regression(htab):

    testdir = '%s/testsuite/expect' % (htab['srcdir'])
    regress = '%s/regression.py' % (testdir)

    os.chdir(testdir)
    logger.info('cd to -> %s', testdir)

    # instal globals.local
    try:
        f = open('globals.local', 'w')
    except IOError as e:
        logger.error('Error failed opening globals.local %s', e)
        return -1

    z = 'set slurm_dir %s' % (htab['prefix'])
    w = 'set  mpicc /usr/local/openmpi/bin/mpicc'
    print >> f, z
    print >> f, w
    f.close()

    # Write regression output into logfile
    regfile = '%s/%s.reg' % (htab['logdir'], htab['cas'])
    htab['regfile'] = regfile
    try:
        rf = open(regfile, 'w')
    except IOError as e:
        logger.error('Error failed to open %s %s', regfile, e)
        return -1

#    pdb.set_trace()
    logger.info('running regression %s', regress)

    try:
        proc = subprocess.Popen(regress,
                                shell=True,
                                stdout=rf,
                                stderr=rf)
    except OSError as e:
        logger.error('Error execution failed %s', e)

    proc.wait()
    rf.close()

def send_result(htab):

    mailmsg = '%s/mailmsg' % (htab['logdir'])
    ended = False
    try:
        f = open(htab['regfile'])
    except IOError as e:
        logger.error('Error failed to open regression output file %s %s', \
                         f.name, e)

    try:
        fp = open(mailmsg, 'w')
    except IOError as e:
        logger.error('Error failed open mailmsg file %s %s', mailmsg, e)

    # open the regression file and send the tail
    # of it starting at 'Ending'
    for line in f:
        lstr = line.strip('\n')
        if not ended and lstr.find('Ended') != -1:
            ended = True
        if ended:
            print >> f2, lstr

    try:
        f.close()
    except IOError as e:
        print >> sys.stderr, \
            'Failed closing did the regression ran all right ?', f.name, e
    try:
        fp.close()
    except IOError as e:
        print >> sys.stderr, \
            'Failed closing did the regression terminated all right ?', fp.name, e

    me = 'david@schedmd.com'
    to = 'david@schedmd.com'
#    cc = 'da@schedmd.com'

    # Open a plain text file for reading.  For this example, assume that
    # the text file contains only ASCII characters.
    fp = open(mailmsg, 'rb')
    # Create a text/plain message
    msg = MIMEText(fp.read())
    fp.close()

    # me == the sender's email address
    # you == the recipient's email address
    msg['Subject'] = 'Regression results %s@%s' % (htab['test'], htab['cas'])
    msg['From'] = me
    msg['To'] = me
#    msg['CC'] = cc

    # Send the message via our own SMTP server, but don't include the
    # envelope header.
    s = smtplib.SMTP('localhost')
    #    s.sendmail(me, [to] + [cc], msg.as_string())
    s.sendmail(me, [to], msg.as_string())
    s.quit()
    # ciao ...
    os.unlink(mailmsg)

def set_environ(htab):

    os.environ['PATH'] = '/bin:/usr/bin:%s' % (htab['bindir'])
    logger.info( 'PATH-> %s', os.environ['PATH'])


# Da main of da driver
def main():

    # Define program argument list, create and invoke
    # the parser
    parser = argparse.ArgumentParser()
    parser.add_argument('-f', '--config_file', \
                            help = 'specify the location of the config file')
    parser.add_argument('-L', '--log',\
                            help = 'generate log file under the section directory')

    args = parser.parse_args()
    htab = {}

    if not args.config_file:
        if os.path.isfile('driver.conf'):
            cfile = 'driver.conf'
        else :
            print >> sys.stderr, 'Error path to configuration file not specified'
            print >> sys.stderr, 'default driver.conf not found'
            return -1
    else:
        cfile = args.config_file

    if args.log:
        htab['log'] = 1

#    pdb.set_trace()

    # Process the configuration file
    conf = read_config(cfile)

    # process the params section of the ini file
    read_params(htab, conf)

    init_log(htab, conf)

    dt = datetime.datetime.now()
    cas = '%s-%s:%s:%s' % (dt.month, dt.day, dt.hour, dt.minute)
    logger.info('cas -> %s', cas)

    htab['cas'] = cas
    children = {}
    for section in conf.sections():

        if section == 'params':
            continue

        pid = os.fork()
        if pid == 0:
            logger.info('child running test %s pid %s', section, os.getpid())
            htab['test'] = section
            configure_and_build(htab, section, conf)
            set_environ(htab)
            start_daemons(htab)
            time.sleep(5)
            run_regression(htab)
            send_result(htab)
            exit(0)
        else:
            children[pid] = section

    logger.info('%s waiting for all tests to complete', os.getpid())
    while True:
        try:
            w = os.wait()
            for t in children:
                if t == w[0]:
                    logger.info( \
                        '%s test %s pid %d done with status %d',\
                            os.getpid(), children[pid], pid, w[1])
        except OSError:
            logger.info('%s: All tests done...', os.getpid())
            break
    return 0

if __name__ == '__main__':
    sys.exit(main())

