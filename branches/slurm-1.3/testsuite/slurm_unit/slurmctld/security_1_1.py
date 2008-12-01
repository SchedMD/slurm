#!/usr/bin/env python

from optparse import OptionParser
import pwd
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
    parser.add_option("-c", "--sysconfdir", type="string", dest="sysconfdir",
                      help="location of directory containing config files", 
                      metavar="DIR")
    parser.add_option("-p", "--prefix", type="string", dest="prefix",
                      help="slurm install directory prefix", metavar="DIR")
    (options, args) = parser.parse_args(args=argv)
    if options.prefix is None:
        options.prefix = '/usr'
        print 'Assuming installation prefix is "%s"' % (options.prefix)
    if options.sysconfdir is None:
        options.sysconfdir = '/etc/slurm'
	options.conf = options.sysconfdir + '/slurm.conf'
        print 'Assuming slurm conf file is "%s"' % (options.conf)
    else:
        options.conf = options.sysconfdir + '/slurm.conf'

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
    print "NOTE: slurm_epilog and slurm_prolog only exist on BlueGene systems"
    print "NOTE: federation.conf only exists on AIX systems"
    print "NOTE: sview, slurmdbd and slurmdbd.conf exists only on selected systems"
    print "NOTE: JobCredentialPrivateKey, SlurmctldLogFile, and StateSaveLocation only on control host"
    print "NOTE: SlurmdLogFile and SlurmdSpoolDir only exist on compute servers" 
    print
    print "Ensuring the following are not world writable:"
    files = []
    files.append(options.sysconfdir)
    files.append(options.conf)
    files.append(options.sysconfdir+'/bluegene.conf')
    files.append(options.sysconfdir+'/federation.conf')
    files.append(options.sysconfdir+'/slurm.conf')
    files.append(options.sysconfdir+'/slurmdbd.conf')
    files.append(options.sysconfdir+'/wiki.conf')
    files.append(options.prefix+'/bin/mpiexec')
    files.append(options.prefix+'/bin/sacct')
    files.append(options.prefix+'/bin/sacctmgr')
    files.append(options.prefix+'/bin/salloc')
    files.append(options.prefix+'/bin/sattach')
    files.append(options.prefix+'/bin/sbatch')
    files.append(options.prefix+'/bin/sbcast')
    files.append(options.prefix+'/bin/scancel')
    files.append(options.prefix+'/bin/scontrol')
    files.append(options.prefix+'/bin/sinfo')
    files.append(options.prefix+'/bin/smap')
    files.append(options.prefix+'/bin/squeue')
    files.append(options.prefix+'/bin/srun')
    files.append(options.prefix+'/bin/strigger')
    files.append(options.prefix+'/bin/sview')
    files.append(options.prefix+'/sbin/slurmctld')
    files.append(options.prefix+'/sbin/slurmd')
    files.append(options.prefix+'/sbin/slurmdbd')
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

    pwname = pwd.getpwnam(confpairs['SlurmUser'])
    for fname in files:
        rc = verify_perms(fname, S_IWOTH, pwname)
        if rc is False:
            error = True

    #
    # Make sure that these files are NOT world READABLE.
    #
    print
    print "Ensuring the following are not world readable:"
    files = []
    append_file(files, confpairs, 'JobCredentialPrivateKey')
    files.append(options.sysconfdir+'/slurmdbd.conf')
    files.append(options.sysconfdir+'/wiki.conf')

    for fname in files:
        rc = verify_perms(fname, S_IROTH, pwname)
        if rc is False:
            error = True

    print
    if error:
        print 'FAILURE. Some file permissions were incorrect.'
    else:
        print 'SUCCESS'

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

def verify_perms(filename, perm_bits, pwname):
    """Check file ownership and permission.
    
    Returns 'True' when the permission and ownership are verified, and 'False'
    otherwise.  The checks fail if the file's permissions contain the bits
    'perm_bits', of if the file's uid does not match the supplied entry from
    passwd."""
    try:
        s = os.stat(filename)
    except:
        print >>sys.stderr, 'WARNING: Unable to stat', filename
        return True

    perm = S_IMODE(s.st_mode)
    if perm & perm_bits:
        print >>sys.stderr, 'ERROR: %s: %o has bits %.3o set' % (filename, perm, perm_bits)
        return False
    elif s.st_uid != 0 and s.st_uid != pwname.pw_uid:
        print >>sys.stderr, 'ERROR: %s has incorrect uid %d' % (filename, s.st_uid)
        return False
    else:
        print 'OK: %o %s ' % (perm, filename)
        return True

if __name__ == "__main__":
    sys.exit(main())


