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
# processname: /usr/sbin/slurm_tv_clean
# pidfile: /var/run/slurm_tv_clean.pid
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
    SLURMCTLD_OPTIONS=""
    SLURMD_OPTIONS=""
    SLURM_TV_CLEAN_OPTIONS=""
fi

[ -f /etc/slurm/slurm.conf ] || exit 1

RETVAL=0

start() {
    echo -n "starting $1: " 
    unset HOME MAIL USER USERNAME 
    daemon /usr/sbin/$1 $2
    RETVAL=$?
    echo
    touch /var/lock/subsys/slurm
    return $RETVAL
}

stop() { 
    echo -n "stopping $1: "
    killproc $1 -TERM
    RETVAL=$?
    echo
    rm -f /var/lock/subsys/slurm
    return $RETVAL
}

startall() {
    for prog in `scontrol show daemons`; do 
        optvar=`echo ${prog}_OPTIONS | tr "a-z" "A-Z"`
        start $prog ${!optvar} 
    done 

    prog="slurm_tv_clean"
    optvar=`echo ${prog}_OPTIONS | tr "a-z" "A-Z"`
    start $prog ${!optvar} 
}

# status() with slight modifications to take into account
# instantiations of job manager slurmd's, which should not be
# counted as "running"
#
slurmstatus() {
    local base=${1##*/}
    local pid
    local rpid
    local pidfile

    pidfile=`grep -i ${base}pid /etc/slurm/slurm.conf | grep -v '^ *#'`
    if [ $? = 0 ]; then
        pidfile=${pidfile##*=}
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
            echo $"${base} dead but pid file exists"
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
	for prog in `scontrol show daemons`; do
	    stop $prog
	done
	stop slurm_tv_clean
        ;;
    status)
	for prog in `scontrol show daemons`; do
	   slurmstatus $prog
	done
	status slurm_tv_clean
        ;;
    restart)
        $0 stop && $0 start
        ;;
    condrestart)
        if [ -f /var/lock/subsys/slurm ]; then
            for prog in `scontrol show daemons`; do
                 stop $prog
                 start $prog
            done
	    stop  slurm_tv_clean
	    start slurm_tv_clean
        fi
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
	echo "slurm_tv_clean runs here"
	;;
    *)
        echo "Usage: $0 {start|startclean|stop|status|restart|reconfig|condrestart|test}"
        exit 1
        ;;
esac

exit $RETVAL
