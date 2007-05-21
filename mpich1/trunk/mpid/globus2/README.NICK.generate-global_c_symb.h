/**************************/
/* on shakey.mcs.anl.gov: */
/**************************/

/home/MPI/maint/mpicheckout

/******************************/
/* rest on lemon.mcs.anl.gov: */
/******************************/

setenv GLOBUS_LOCATION /my/packages/globus/globus-2.0
source $GLOBUS_LOCATION/etc/globus-user-env.csh
setenv X509_CERT_DIR $GLOBUS_LOCATION/setup/globus

cd mpich/mpid/globus2/
./rename_symb.sh -config --with-arch=IRIXN32

/* You may want to check what new symbols have been added. */

diff global_c_symb.h.old.* global_c_symb.h | grep "#define"

/*
 * If new global symbols were added (see the output of the previous
 * 'diff' command), you have to commit the #define file into the CVS
 * repository.
 */

cvs commit -m "updated #define file to rename global symbols" global_c_symb.h

/* Remove the backup copy of the old #define file. */

rm global_c_symb.h.old.*







--------------- instructions from sebastien 
vvvvvvvvvvvvvvv on how to generate mpid/globus2/global_c_symb.h
# To re-generate an up-to-date #define file to rename the global
# symbols of MPICH-G2 and avoid symbol clashing while building
# MPICH-G2 on top of an MPICH-based vendor-MPI implementation, find a
# machine with an MPI flavor of Globus installed.

hostname
 --> lemon.mcs.anl.gov

# Have an up-to-date MPICH working directory.

cd /sandbox/lacour/tmp/mpich
cvs -q update -d -P

# The script supports Globus-v1.1.x and Globus-v2.x.
# Check your GLOBUS_LOCATION / GLOBUS_INSTALL_PATH env. var.

echo $GLOBUS_LOCATION
 --> /my/packages/globus/globus-2.0

# Launch the script.  On Lemon, you'll need to pass the configure
# option "--with-arch=IRIXN32".

cd mpid/globus2/
./rename_symb.sh -config --with-arch=IRIXN32

# "-config" is an option which instructs the script to pass the option
# "--with-arch=IRIXN32" to the configure script of MPICH.
# The script "./rename_symb.sh" will:
#  - clean up your working MPICH directory,
#  - configure MPICH-G2 with the right flavor automatically (an MPI
#    flavor if available),
#  - compile MPICH-G2,
#  - find the global symbols in MPICH-G2's libraries (except those
#    coming from mpid/globus2/pr_mpi_g),
#  - create a new #define file augmented with the potentially new
#    global symbols added to MPICH,
#  - clean up your working MPICH directory.
# The script is moderately verbose and shows the command it executes
# to configure MPICH-G2.  On Lemon, it all takes about 25 minutes.  If
# you have an MPI flavor of Globus on Shakey, it takes less than 5
# minutes...

# You may want to check what new symbols have been added.

diff global_c_symb.h.old.* global_c_symb.h | grep "#define"

# A few lines will be modified in the header comment of the #define
# file: that does not mean some new symbols were added.

# If new global symbols were added (see the output of the previous
# 'diff' command), you have to commit the #define file into the CVS
# repository.

cvs commit -m "updated #define file to rename global symbols" global_c_symb.h

# Remove the backup copy of the old #define file.

rm global_c_symb.h.old.*

# To get more help on how to use this script, use option "-h".
^^^^^^^^^^^^^^^ instructions from sebastien 
--------------- on how to generate mpid/globus2/global_c_symb.h

