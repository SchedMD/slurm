#!/bin/bash
#
# chkconfig: 345 90 10
# description: slurm startup script
#
# 1. This file should be installed as /etc/init.d/slurm
# 
# 2. Start slurm with:
#
#      /etc/init.d/slurm start
#
# Source function library.
[ -f /etc/rc.d/init.d/functions ] || exit 0
. /etc/rc.d/init.d/functions

# Source slurm specific configuration
if [ -f /etc/sysconfig/slurm ] ; then
    . /etc/sysconfig/slurm
else
    SLURMCTLD_OPTIONS="-c"
    SLURMD_OPTIONS="-c"
    DAEMONS=`scontrol show daemons`
    echo $DAEMONS | grep -q slurmctld
    CONTROLLER=$?
    echo $DAEMONS | grep -q slurmd
    SERVER=$?
fi

RETVAL=0

#
# The pathname substitution in daemon command assumes prefix and
# exec_prefix are same.  This is the default, unless the user requests
# otherwise.
#
# Any node can be a slurm controller and/or server.
#
case "$1" in
    start)
        if [ $CONTROLLER -eq 0 ]; then
            echo -n "Starting slurmctld: "
            daemon /usr/local/slurm/sbin/slurmctld $SLURMCTLD_OPTIONS
            RETVAL=$?
            [ $RETVAL -eq 0 ] && touch /var/lock/subsys/slurmctld
            echo
	fi
        if [ $SERVER -eq 0 ]; then
            echo -n "Starting slurmd: "
            daemon /usr/local/slurm/sbin/slurmd $SLURMD_OPTIONS
            RETVALS=$?
            [ $RETVALS -eq 0 ] && touch /var/lock/subsys/slurmd
            RETVAL=$(($RETVAL + $RETVALS))
            echo
	fi
        ;;
    stop)
        if [ $CONTROLLER -eq 0 ]; then
            echo -n "Shutting down slurmctld: "
            killproc slurmctld
            RETVAL=$?
            [ $RETVAL -eq 0 ] && rm -f /var/lock/subsys/slurmctld
            echo
	fi
        if [ $SERVER -eq 0 ]; then
            echo -n "Shutting down slurmd: "
            killproc slurmd
            RETVALS=$?
            [ $RETVALS -eq 0 ] && rm -f /var/lock/subsys/slurmd
            RETVAL=$(($RETVAL + $RETVALS))
            echo
	fi
        ;;
    status)
        if [ $CONTROLLER -eq 0 ]; then
            status slurmctld
	fi
        if [ $SERVER -eq 0 ]; then
            status slurmd
	fi
        ;;
    restart)
        $0 stop
        $0 start
        ;;
    test)
        if [ $CONTROLLER -eq 0 ]; then
            echo "slurmctld runs here"
	fi
        if [ $SERVER -eq 0 ]; then
            echo "slurmd runs here"
	fi
        ;;
    *)
        echo "Usage: $0 {start|stop|status|restart|test}"
        exit 1
        ;;
esac
