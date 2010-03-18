########################################################################
#
# Makefile for NEWS Tests  (96-12-11)
#
########################################################################

CODES = barrier_timed allred allred_timed alltoall alltoall_timed

OPT4 = -qarch=440d

CC = mpxlc
CFLAGS = $(OPT4)


FC = mpxlf
FFLAGS =    ${OPT4}

# ====================

all : ${CODES}

barrier : barrier.o
	echo "Building code barrier...";\
	${CC} -o barrier barrier.o

barrier_timed : barrier_timed.o
	echo "Building code barrier_timed...";\
	${CC} -o barrier_timed barrier_timed.o

allred : allred.o
	echo "Building code allred...";\
	${CC} -o allred allred.o

allred_timed : allred_timed.o
	echo "Building code allred_timed...";\
	${CC} -o allred_timed allred_timed.o

alltoall : alltoall.o
	echo "Building code alltoall...";\
	${CC} -o alltoall alltoall.o

alltoall_timed : alltoall_timed.o
	echo "Building code alltoall_timed...";\
	${CC} -o alltoall_timed alltoall_timed.o

clean :
	rm -f *.o ${CODES}

#**********************#
#     SUFFIX RULES     #
#**********************#

#.SILENT :

.SUFFIXES :
.SUFFIXES : .o .c .f

.c.o :
	echo ${CC} $<
	${CC} -c ${CFLAGS} ${INCL} $<

.f.o :
	echo ${FC} $<
	${FC} -c ${FFLAGS} ${INCL} $<

########################################################################
#       END OF MAKE
########################################################################
