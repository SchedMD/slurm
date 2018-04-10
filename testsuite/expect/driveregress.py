#!/usr/bin/env python

############################################################################
# Copyright (C) 2011-2013 SchedMD LLC
# Written by David Bigagli <david@schedmd.com>
#
# This file is part of Slurm, a resource management program.
# For details, see <https://slurm.schedmd.com/>.
# Please also read the included file: DISCLAIMER.
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
import glob
import shutil

"""This program is a driver for the Slurm regression program."""

logger = logging.getLogger('driveregress.py')
logger.setLevel(logging.DEBUG)

def init_console():

    ch = logging.StreamHandler()
    formatter = logging.Formatter('%(asctime)s %(name)s %(levelname)s %(message)s',
                                  datefmt='%b %d %H:%M:%S')
    ch.setFormatter(formatter)
    logger.addHandler(ch)

def init_log(htab):

    testlogdir = '%s/log/%s' % (htab['root'], htab['section'])
    if not os.path.isdir(testlogdir):
        os.mkdir(testlogdir)

    htab['testlogdir'] = testlogdir
    testlogfile = '%s/log/%s/Log' % (htab['root'], htab['section'])

    fh = logging.FileHandler(testlogfile)
    formatter = logging.Formatter('%(asctime)s %(name)s %(levelname)s %(message)s',
                                  datefmt='%b %d %H:%M:%S')
    fh.setFormatter(formatter)
    logger.addHandler(fh)

    return fh

# Read the driver configuration which is in ini format
#
# NOTA BENE:
# ConfigParser craps out if there are leading spaces
# in the configuration file.
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
        logger.info('section -> %s', section)
        for option in conf.options(section):
            logger.info('%s = %s' % (option, conf.get(section, option)))

    return conf

# clean up the daemon logs from previous run
def cleanup_logs(htab):

    # pdb.set_trace()
    # hose slurmctld and slurmdbd logs
    logger.info('cd logdir -> %s' % (htab['logdir']))
    os.chdir(htab['logdir'])
    for f in glob.iglob('*'):
        try:
            os.unlink(f)
        except:
            pass

    # hose slurmd logs
    slogdir = '%s/log' %  (htab['logdir'])
    logger.info('cd logdir -> %s' % (slogdir))
    os.chdir(slogdir)
    for f in glob.iglob('*'):
        try:
            os.unlink(f)
        except:
            pass

    # hose the spool
    shutil.rmtree(htab['spooldir'])
    os.mkdir(htab['spooldir'])

# section is the test name
def configure_and_build(htab, conf, section):

    multi = 0
    multiname = None

    try:
        mailto = conf.get(section, 'mailto')
        htab['mailto'] = mailto
        logger.info( 'mailto -> %s', mailto)
    except ConfigParser.NoOptionError :
        pass

    try:
        version = conf.get(section, 'version')
        arch = conf.get(section, 'arch')
        multi = conf.get(section, 'multiple_slurmd')
        multiname = conf.get(section, 'multi_name')
    except:
        pass

    buildpath = '%s/clusters/%s/%s/build' % (htab['root'], version, arch)
    sbindir = '%s/clusters/%s/%s/sbin' % (htab['root'], version, arch)
    spooldir = '%s/clusters/%s/%s/spool' % (htab['root'], version, arch)
    bindir = '%s/clusters/%s/%s/bin' % (htab['root'], version, arch)
    prefix = '%s/clusters/%s/%s' % (htab['root'], version, arch)
    srcdir = '%s/distrib/%s/slurm' % (htab['root'], version)
    logdir = '%s/clusters/%s/%s/log' % (htab['root'], version, arch)

    slurmdbd = '%s/slurmdbd' % (sbindir)
    slurmctld = '%s/slurmctld' % (sbindir)
    slurmd = '%s/slurmd' % (sbindir)

    # Use hash table to communicate across
    # functions.
    htab['buildpath'] = buildpath
    htab['prefix'] = prefix
    htab['srcdir'] = srcdir
    htab['logdir'] = logdir
    htab['sbindir'] = sbindir
    htab['bindir'] = bindir
    htab['spooldir'] = spooldir
    htab['slurmdbd'] = slurmdbd
    htab['slurmctld']= slurmctld
    htab['slurmd'] = slurmd
    if multi != 0:
        htab['multi'] = multi
        htab['multiname'] = multiname
    htab['version'] = version
    htab['arch'] = arch
    htab['section'] = section

    logger.info('test: %s version: %s arch: %s multi: %s multiname: %s'
                % (section, htab['version'], htab['arch'],
                   htab['multi'], htab['multiname']))
    logger.info('buildpath -> %s', buildpath)
    logger.info('prefix -> %s', prefix)
    logger.info('srcdir -> %s', srcdir)
    logger.info('logdir -> %s', logdir)
    logger.info('spooldir -> %s', spooldir)
    logger.info('sbindir -> %s', sbindir)
    logger.info('bindir -> %s', bindir)
    logger.info('slurmdbd -> %s', slurmdbd)
    logger.info('slurmctld -> %s', slurmctld)
    logger.info('slurmd -> %s', slurmd)

    # clean up logdir
    cleanup_logs(htab)

    # before configuring let's make sure to pull
    # the github repository
    git_update(srcdir)

    # configure and build
    os.chdir(buildpath)
    logger.info('cd -> %s', os.getcwd())

    # this is the build file log
    buildlog = '%s/Build' % (htab['testlogdir'])
    lfile = open(buildlog, 'w')
    logger.info('build log file -> %s' % (lfile.name))

    logger.info('running -> make uninstall')
    make = 'make uninstall'
    try:
        proc = subprocess.Popen(make,
                                shell=True,
                                stdout = lfile,
                                stderr = lfile)
    except Exception :
        logger.error('Error make uninstall failed, make for the very first time?')

    rc = proc.wait()
    if rc != 0:
        logger.error('make uninstal exit with status %s,\
 make for the very first time?' % (rc))

    logger.info('running -> make clean')
    make = 'make distclean'
    try:
        proc = subprocess.Popen(make,
                                shell=True,
                                stdout = lfile,
                                stderr = lfile)
    except Exception :
        logger.error('Error make distclean failed, make for the very first time?')

    rc = proc.wait()
    if rc != 0:
        logger.error('make distclean exit with status %s,\
 make for the very first time?' % (rc))

    if 'multi' in htab:
        configure = ('%s/configure --prefix=%s --enable-debug\
 --enable-multiple-slurmd' %
                     (srcdir, prefix))
    else:
        configure = '%s/configure --prefix=%s --enable-debug' % (srcdir, prefix)

    logger.info('running -> %s', configure)
    try:
        proc = subprocess.Popen(configure,
                                shell=True,
                                stdout=lfile,
                                stderr=lfile)
    except OSError as e:
        logger.error('Error execution failed:' % (e))

    rc = proc.wait()
    if rc != 0:
        logger.critical('configure failed with status %s' % (rc))

    make = '/usr/bin/make -j 4 install'
    logger.info( 'cd -> %s', os.getcwd())
    logger.info('running -> %s', make)
    try:
        proc = subprocess.Popen(make,
                                shell=True,
                                stdout=lfile,
                                stderr=lfile)
    except OSError as e:
        logger.error('Error execution failed:' % (e))

    rc = proc.wait()
    if rc != 0:
        logger.critical('make -j 4 failed with status %s' % (rc))

    lfile.close()

    return True

def git_update(srcdir):

    logger.info('running git pull on -> %s', srcdir)
    gitpull = 'git pull'

    # dont forget to chdir back
    os.chdir(srcdir)
    try:
        proc = subprocess.check_call([gitpull], shell=True)
    except Exception as e:
        logger.error('Failed to run git pull on %s %s' % (srcdir, e))

def start_daemons(htab):

    logger.info('starting daemons...')
    try:
        proc = subprocess.Popen(htab['slurmdbd'], stdout = None,
                                stderr = subprocess.PIPE)
        rc = proc.wait()
        if rc != 0:
            logger.critic('Problems starting %s' % (htab['slurmdbd']))
            for line in proc.stderr:
                logger.critic('stderr: %s' % (line.strip()))
            return False
    except Exception as e:
        logger.error('Failed starting slurmdbd %s ' % (e))
        return -1
    logger.info('slurmdbd started')

    try:
        proc = subprocess.Popen(htab['slurmctld'], stdout = None,
                                stderr = subprocess.PIPE)
        rc = proc.wait()
        if rc != 0:
            logger.critic('Problems starting %s' % (htab['slurmctld']))
            for line in proc.stderr:
                logger.critic('stderr: %s' % (line.strip()))
            return False
    except Exception as e:
        logger.error('Failed starting slurmctld %s' % (e))
        return -1
    logger.info('slurmctld started')

    #pdb.set_trace()
    n = 1
    try:
        if 'multi' in htab:
            for n in range(1, int(htab['multi']) + 1):
                slurmd = '%s -N %s%d' % (htab['slurmd'], htab['multiname'], n)
                proc = subprocess.Popen(slurmd, shell = True, stdout = None,
                                        stderr = subprocess.PIPE)
                rc = proc.wait()
                if rc != 0:
                    logger.critic('Problems starting %s' % (htab['slurmd']))
                    for line in proc.stderr:
                        logger.critic('stderr: %s' % (line.strip()))
                    return False
                logger.info('%s started' % (slurmd))
        else:
            proc = subprocess.Popen(htab['slurmd'], stdout = None,
                                    stderr = subprocess.PIPE)
            rc = proc.wait()
            if rc != 0:
                logger.critic('Problems starting %s' % (htab['slurmd']))
                for line in proc.stderr:
                    logger.critic('stderr: %s' % (line.strip()))
                return False
            logger.info('slurmd started')
    except Exception as e:
        logger.error('Failed starting slurmd %s' % (e))
        return -1

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
        logger.error('sinfo failed to check cluster state')
    for line in proc.stdout:
        if line.strip() == 'idle':
            logger.info( 'Cluster state is ok -> %s' % line.strip())
        else:
            logger.error('Failed to get correct cluster status %s'
                         % line.strip())

def kill_daemons(htab):

    piddir = '%s/pid' % (htab['logdir'])

    ld = [htab['logdir'], piddir]
    for l in ld:
        os.chdir(l)
        for pf in glob.iglob('*.pid'):
            try:
                f = open(pf, 'r')
            except IOError as e:
                logger.error('No pidfile? -> %s %s' % (pf, e))
            else :
                pid = f.readline().strip()
                logger.info('Got %s pid -> %s' % (f.name, pid))
                try:
                    os.kill(int(pid), 15)
                except OSError as e:
                    logger.error('Cannot kill %s? %s' % (pid, e))
                f.close()

def run_regression(htab):

    testdir = '%s/testsuite/expect' % (htab['srcdir'])
    regress = '%s/regression.py' % (testdir)

    os.chdir(testdir)
    logger.info('cd to -> %s', testdir)

    # instal globals.local
    try:
        f = open('globals.local', 'w')
    except IOError as e:
        logger.error('Error failed opening globals.local %s' % (e))
        return -1

    # create entries in globals.local
    z = 'set slurm_dir %s' % (htab['prefix'])
    w = 'set  mpicc /usr/local/openmpi/bin/mpicc'
    u = 'set build_dir %s' % (htab['buildir'])
    print >> f, z
    print >> f, w
    print >> f, u
    f.close()

    # Write regression output into logfile
    regfile = '%s/Regression' % (htab['testlogdir'])
    htab['regfile'] = regfile

    try:
        rf = open(regfile, 'w')
    except IOError as e:
        logger.error('Error failed to open %s %s' % (regfile, e))
        return -1

#    pdb.set_trace()
    logger.info('running regression %s' % (regress))

    try:
        proc = subprocess.Popen(regress,
                                shell=True,
                                stdout=rf,
                                stderr=rf)
    except OSError as e:
        logger.error('Error execution failed %s' % (e))

    proc.wait()
    rf.close()

def send_result(htab):

    if not htab['mailto']:
        logger.info('No mail will be sent..')
        os.rename(htab['regfile'], 'regression-')
        return

    os.chdir(htab['testlogdir'])
    logger.info('Sending result from %s' % (htab['testlogdir']))

    mailmsg = '%s/mailmsg' % (htab['testlogdir'])

    try:
        f = open(htab['regfile'])
    except IOError as e:
        logger.error('Error failed to open regression output file %s'
                     % (e))
    try:
        fp = open(mailmsg, 'w')
    except IOError as e:
        logger.error('Error failed open mailmsg file %s' % (e))

    print >> fp, 'Finished test', htab['section'], htab['version'], htab['arch']

    # open the regression file and send the tail
    # of it starting at 'Ending'
    ended = False
    for line in f:
        lstr = line.strip('\n')
        if not ended and lstr.find('Ended') != -1:
            ended = True
        if ended:
            print >> fp, lstr

    try:
        f.close()
    except IOError as e:
        logger.error('Failed closing %s did the regression ran all right ?'
                     % (e))
    try:
        fp.close()
    except IOError as e:
        logger.error('Failed closing %s did the regression terminated all right ?'
                     % (e))

    # Open a plain text file for reading.  For this example, assume that
    # the text file contains only ASCII characters.
    fp = open(mailmsg, 'rb')
    # Create a text/plain message
    msg = MIMEText(fp.read())
    fp.close()

    me = 'david@schedmd.com'
    to = htab['mailto']
    # me == the sender's email address
    # to == the recipient's email address
    msg['Subject'] = 'Regression results %s@%s' % (htab['section'], htab['cas'])
    msg['From'] = me
    msg['To'] = to
#    msg['CC'] = cc

    # Send the message via our own SMTP server, but don't include the
    # envelope header.
    s = smtplib.SMTP('localhost')
    #    s.sendmail(me, [to] + [cc], msg.as_string())
    s.sendmail(me, [to], msg.as_string())
    s.quit()
    # ciao ... save latest copies...

    logger.info('email sent to %s' % (to))

    os.rename(mailmsg, 'mailsmsg-')
    os.rename(htab['regfile'], 'regression-')

def set_environ(htab):

    os.environ['PATH'] = '/bin:/usr/bin:%s' % (htab['bindir'])
    logger.info('PATH-> %s' % (os.environ['PATH']))
    os.environ['LD_LIBRARY_PATH'] = '/usr/local/openmpi/lib'
    logger.info('LD_LIBRARY_PATH-> %s' % (os.environ['LD_LIBRARY_PATH']))

# Da main of da driver
def main():

    init_console()
    # Define program argument list, create and invoke
    # the parser
    parser = argparse.ArgumentParser()
    parser.add_argument('-f', '--config_file',
                        help = 'specify the location of the config file')
    args = parser.parse_args()

    if not args.config_file:
        if os.path.isfile('driver.conf'):
            cfile = 'driver.conf'
        else :
            logger.critical('path to configuration file not specified')
            logger.critical('default driver.conf not found')
            return -1
    else:
        cfile = args.config_file

#    pdb.set_trace()

    dt = datetime.datetime.now()
    cas = '%s-%s:%s:%s' % (dt.month, dt.day, dt.hour, dt.minute)
    logger.info('Starting %s cas -> %s', os.getpid(), cas)

    # process the configuration file
    conf = read_config(cfile)

    for section in conf.sections():
        htab = {}
        htab['cas'] = cas
        try:
            root = conf.get(section, 'root')
            htab['root'] = root
            logger.info( 'root -> %s', root)
        except ConfigParser.NoOptionError as e:
            logger.fatal('Error root option missing from configuration %s' % (e))
        htab['section'] = section
        fh = init_log(htab)
        logger.info('Root %s of section %s' % (htab['root'], htab['section']))
        configure_and_build(htab, conf, section)
        set_environ(htab)
        start_daemons(htab)
        run_regression(htab)
        send_result(htab)
        kill_daemons(htab)
        logger.info('test %s done daemons killed' % (section))
        logger.removeHandler(fh)

    logger.info('all tests done...')

    return 0

if __name__ == '__main__':
    sys.exit(main())

