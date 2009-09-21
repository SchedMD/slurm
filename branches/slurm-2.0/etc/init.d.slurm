#!/bin/bash
#
# chkconfig: 345 90 10
# description: SLURM is a simple resource management system which \
#              manages exclusive access to a set of compute \
#              resources and distributes work to those resources.
#
# processname: /usr/sbin/slurmd 
# pidfile: /var/run/slurmd.pid
#
# processname: /usr/sbin/slurmctld
# pidfile: /var/run/slurmctld.pid
#
# config: /etc/sysconfig/slurm
#
### BEGIN INIT INFO
# Provides:          slurm
# Required-Start:    $remote_fs $syslog $network munge
# Required-Stop:     $remote_fs $syslog $network munge
# Should-Start:      $named
# Should-Stop:       $named
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Short-Description: slurm daemon management
# Description:       Start slurm to provide resource management
### END INIT INFO

BINDIR=/usr/bin
CONFDIR=/etc/slurm
LIBDIR=/usr/lib
SBINDIR=/usr/sbin

# Source function library.
if [ -f /etc/rc.status ]; then
   . /etc/rc.status
   SUSE=1
   STARTPROC=startproc

   rc_reset
else
   [ -f /etc/rc.d/init.d/functions ] || exit 0
   . /etc/rc.d/init.d/functions
   SUSE=0
   STARTPROC=daemon

   function rc_status() {
      RETVAL=$?
   }
   function rc_exit () {
      exit $RETVAL
   }
   RETVAL=0
fi

# We can not use a starter program without losing environment 
# variables that are critical on Blue Gene systems
if [ -d /bgl/BlueLight/ppcfloor ]; then
   STARTPROC=""
fi

# Source slurm specific configuration
if [ -f /etc/sysconfig/slurm ] ; then
    . /etc/sysconfig/slurm
else
    SLURMCTLD_OPTIONS=""
    SLURMD_OPTIONS=""
fi

[ -f $CONFDIR/slurm.conf ] || exit 1

# setup library paths for slurm and munge support
export LD_LIBRARY_PATH="$LIBDIR:$LD_LIBRARY_PATH"

start() {
    prog=$1
    shift
    echo -n "starting $prog: " 
    unset HOME MAIL USER USERNAME 
    $STARTPROC $SBINDIR/$prog $*
    rc_status -v
    echo
    touch /var/lock/subsys/slurm
}

stop() { 
    echo -n "stopping $1: "
    killproc $1 -TERM
    rc_status -v
    echo
    rm -f /var/lock/subsys/slurm
}

startall() {
    for prog in `$BINDIR/scontrol show daemons`; do 
        optvar=`echo ${prog}_OPTIONS | tr "a-z" "A-Z"`
        start $prog ${!optvar} 
    done
}

#
# status() with slight modifications to take into account
# instantiations of job manager slurmd's, which should not be
# counted as "running"
#
slurmstatus() {
    local base=${1##*/}
    local pid
    local rpid
    local pidfile

    pidfile=`grep -i ${base}pid $CONFDIR/slurm.conf | grep -v '^ *#'`
    if [ $? = 0 ]; then
        pidfile=${pidfile##*=}
        pidfile=${pidfile%#*}
    else
        pidfile=/var/run/${base}.pid
    fi

    pid=`pidof -o $$ -o $$PPID -o %PPID -x $1 || \
         pidof -o $$ -o $$PPID -o %PPID -x ${base}`

    if [ -f $pidfile ]; then
        read rpid < $pidfile
        if [ "$rpid" != "" -a "$pid" != "" ]; then
            for i in $pid ; do
                if [ "$i" = "$rpid" ]; then 
                    echo $"${base} (pid $pid) is running..."
                    return 0
                fi     
            done
        elif [ "$rpid" != "" -a "$pid" = "" ]; then
#           Due to change in user id, pid file may persist 
#           after slurmctld terminates
            if [ "$base" != "slurmctld" ] ; then
               echo $"${base} dead but pid file exists"
            else
               echo $"${base} is stopped"
            fi
            return 1
        fi 

    fi

    if [ "$base" = "slurmctld" -a "$pid" != "" ] ; then
        echo $"${base} (pid $pid) is running..."
        return 0
    fi
     
    echo $"${base} is stopped"
    
    return 3
}

#
# stop slurm daemons, 
# wait for termination to complete (up to 10 seconds) before returning
#
slurmstop() {
    for prog in `$BINDIR/scontrol show daemons`; do
       stop $prog

       for i in 1 2 3 4
       do
          sleep $i
          slurmstatus $prog
          if [ $? != 0 ]; then
             break
          fi
       done
    done
}

#
# The pathname substitution in daemon command assumes prefix and
# exec_prefix are same.  This is the default, unless the user requests
# otherwise.
#
# Any node can be a slurm controller and/or server.
#
case "$1" in
    start)
	startall
        ;;
    startclean)
        SLURMCTLD_OPTIONS="-c $SLURMCTLD_OPTIONS"
        SLURMD_OPTIONS="-c $SLURMD_OPTIONS"
        startall
        ;;
    stop)
	slurmstop
        ;;
    status)
	for prog in `$BINDIR/scontrol show daemons`; do
	   slurmstatus $prog
	done
        ;;
    restart)
        $0 stop
        $0 start
        ;;
    condrestart)
        if [ -f /var/lock/subsys/slurm ]; then
            for prog in `$BINDIR/scontrol show daemons`; do
                 stop $prog
                 start $prog
            done
        fi
        ;;
    reconfig)
	for prog in `$BINDIR/scontrol show daemons`; do
	    killproc $prog -HUP
	done
	;;
    test)
	for prog in `$BINDIR/scontrol show daemons`; do   
	    echo "$prog runs here"
	done
	;;
    *)
        echo "Usage: $0 {start|startclean|stop|status|restart|reconfig|condrestart|test}"
        exit 1
        ;;
esac

rc_exit
