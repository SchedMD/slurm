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

# $Id$

# Source function library.
[ -f /etc/rc.d/init.d/functions ] || exit 0
. /etc/rc.d/init.d/functions

# Source slurm specific configuration
if [ -f /etc/sysconfig/slurm ] ; then
    . /etc/sysconfig/slurm
else
    SLURMCTLD_OPTIONS="-c"
    SLURMD_OPTIONS="-c"
fi

[ -f /etc/slurm/slurm.conf ] || exit 1

RETVAL=0

start() {
    echo -n "starting $1: " 
    unset HOME MAIL USER USERNAME 
    daemon /usr/sbin/$1 $2
    RETVAL=$?
    echo
    touch /var/lock/subsys/$prog 
    return $RETVAL
}

stop() {
    echo -n "stopping $1: "
    killproc $1 -TERM
    RETVAL=$?
    echo
    rm -f /var/lock/subsys/$prog
    return $RETVAL
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
        for prog in `scontrol show daemons`; do
            optvar=`echo ${prog}_OPTIONS | tr "a-z" "A-Z"`
            start $prog ${!optvar} 
        done 
        ;;
    stop)
	for prog in `scontrol show daemons`; do
	    stop $prog
	done
        ;;
    status)
	for prog in `scontrol show daemons`; do
	   status $prog
	done
        ;;
    restart)
        $0 stop
        $0 start
        ;;
    condrestart)
        for prog in `scontrol show daemons`; do
            if [ -f /var/lock/subsys/$prog ]; then
                 stop $prog
                 start $prog
            fi
        done
        ;;
    reconfig)
	for prog in `scontrol show daemons`; do
	    killproc $prog -HUP
	done
	;;
    test)
	for prog in `scontrol show daemons`; do   
	    echo "$prog runs here"
	done
	;;
    *)
        echo "Usage: $0 {start|stop|status|restart|reconfig|condrestart|test}"
        exit 1
        ;;
esac

exit $RETVAL
