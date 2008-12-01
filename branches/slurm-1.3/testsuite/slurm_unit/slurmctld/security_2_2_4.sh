#!/bin/sh
# Define location of slurm executables (if not in default search path)
#slurm_bin="/home/jette/slurm.mdev/bin/"

# Build the script
wd=`pwd`
tst_in=$wd/security_2_3_in
tst_out=$wd/security_2_3_out
rm -f $tst_in $tst_out
echo '#!/bin/sh'     >$tst_in
echo "id >$tst_out" >>$tst_in
chmod 700 $tst_in

# Set the trigger
echo "Executing:"
echo "${slurm_bin}strigger --set --idle --offset=0 --program=$tst_in"
echo ""
${slurm_bin}strigger --set --idle --offset=0 --program=$tst_in

# Wait for trigger event and test the results
if [ "$?" -eq 0 ]
then
    echo "Waiting for trigger event, this take 20 seconds"
    sleep 20
    if [ -f $tst_out ]
    then 
        echo "Trigger ran as this user:"
        cat $tst_out
        echo "If that's not your user and group id, this is a failure"
    else
        echo "FAILURE: No output file generated for trigger event"
    fi
else
    echo "If this failure is a security violation, that's fine"
fi

# Clean up
rm -f $tst_in $tst_out
