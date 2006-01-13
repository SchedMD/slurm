#!/usr/bin/env python

from optparse import OptionParser
import sys
import os
import re
from stat import S_IRUSR, S_IWUSR, S_IXUSR
from stat import S_IRGRP, S_IWGRP, S_IXGRP
from stat import S_IROTH, S_IWOTH, S_IXOTH
from stat import S_IMODE, S_IFMT

def main(argv=None):
    confpairs = {}
    error = False

    # Handle command line parameters
    if argv is None:
        argv = sys.argv

    parser = OptionParser()
    parser.add_option("-c", "--config", type="string", dest="conf",
                      help="specify location of slurm.conf", metavar="FILE")
    parser.add_option("-p", "--prefix", type="string", dest="prefix",
                      help="slurm install directory prefix", metavar="DIR")
    (options, args) = parser.parse_args(args=argv)
    if options.prefix is None:
        options.prefix = '/usr/local'
        print 'Assuming installation prefix is "%s"' % (options.prefix)
    if options.conf is None:
        options.conf = options.prefix + '/etc/slurm.conf'
        print 'Assuming slurm conf file is "%s"' % (options.conf)

    # Parse the slurm.conf file
    try:
        conf = open(options.conf, 'r')
    except:
        print >>sys.stderr, "Unable to open slurm configuration file", options.conf
        return -3
    for line in conf.readlines():
        line = line.rstrip()
        line = line.split('#')[0] # eliminate comments
        m = re.compile('\s*([^=]+)\s*=\s*([^\s]+)').search(line)
        if m:
            confpairs[m.group(1)] = m.group(2)

    rc = 0
    #
    # Make sure that these files are NOT world writable.
    #
    print
    print "Ensuring the following are not world writable:"
    files = []
    files.append(options.conf)
    files.append(options.prefix+'/bin/srun')
    files.append(options.prefix+'/bin/sacct')
    files.append(options.prefix+'/bin/sinfo')
    files.append(options.prefix+'/bin/squeue')
    files.append(options.prefix+'/bin/scontrol')
    files.append(options.prefix+'/bin/scancel')
    files.append(options.prefix+'/bin/smap')
    files.append(options.prefix+'/sbin/slurmctld')
    files.append(options.prefix+'/sbin/slurmd')
    files.append(options.prefix+'/sbin/slurmstepd')
    files.append(options.prefix+'/sbin/slurm_epilog')
    files.append(options.prefix+'/sbin/slurm_prolog')
    append_file(files, confpairs, 'Prolog')
    append_file(files, confpairs, 'Epilog')
    append_file(files, confpairs, 'JobCredentialPrivateKey')
    append_file(files, confpairs, 'JobCredentialPublicCertificate')
    append_file(files, confpairs, 'SlurmdSpoolDir')
    append_file(files, confpairs, 'StateSaveLocation')
    append_file(files, confpairs, 'SlurmctldLogFile')
    append_file(files, confpairs, 'SlurmdLogFile')
    append_file(files, confpairs, 'JobCompLog')
    append_file(files, confpairs, 'PluginDir')
    append_dir(files, confpairs, 'PluginDir')

    for fname in files:
        rc = check_perms(fname, S_IWOTH)
        if rc is True:
            error = True

    #
    # Make sure that these files are NOT world READABLE.
    #
    print
    print "Ensuring the following are not world readble:"
    files = []
    append_file(files, confpairs, 'JobCredentialPrivateKey')

    for fname in files:
        rc = check_perms(fname, S_IROTH)
        if rc is True:
            error = True

    print
    if error:
        print 'FAILURE! Some file permissions were incorrect.'
    else:
        print 'SUCCESS.'

    return error

def append_file(l, d, key):
    """If 'key' exists in dictionary 'd', then append its value to list 'l'"""
    if d.has_key(key):
        l.append(d[key])
        return True
    else:
        return False

def append_dir(l, d, key):
    """If 'key' exists in dictionary 'd', then the value in 'd' is a directory
    name.  Append all of the entries in the directory to list 'l'."""
    if d.has_key(key):
        for fname in os.listdir(d[key]):
            l.append(d[key] + '/' + fname)

def check_perms(filename, perm_bits):
    """Returns 'True' if the file's permissions contain the bits 'perm_bits'"""
    try:
        perm = S_IMODE(os.stat(filename).st_mode)
    except:
        print >>sys.stderr, 'ERROR: Unable to stat', filename
        return True
    
    if perm & perm_bits:
        print >>sys.stderr, 'ERROR: %s: %o has bits %.3o set' % (filename, perm, perm_bits)
        return True
    else:
        print 'OK: %o %s ' % (perm, filename)
        return False

if __name__ == "__main__":
    sys.exit(main())


