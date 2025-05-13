# RELEASE NOTES FOR SLURM VERSION 25.05

## IMPORTANT NOTES
* If using the slurmdbd (Slurm DataBase Daemon) you must update this first. If
using a backup DBD you must start the primary first to do any database
conversion, the backup will not start until this has happened.

* The 25.05 slurmdbd will work with Slurm daemons of version 23.11 and above.
You will not need to update all clusters at the same time, but it is very
important to update slurmdbd first and having it running before updating
any other clusters making use of it.

* Slurm can be upgraded from version 23.11, 24.05 or 24.11 to version
25.05 without loss of jobs or other state information. Upgrading directly from
an earlier version of Slurm will result in loss of state information.

* All SPANK plugins must be recompiled when upgrading from any Slurm version
prior to 25.05.

## HIGHLIGHTS

## CONFIGURATION FILE CHANGES (see appropriate man page for details)

## COMMAND CHANGES (see man pages for details)

## API CHANGES

## SLURMRESTD CHANGES
