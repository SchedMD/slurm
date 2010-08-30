srcs = LCRM_date2epoch.c datetoi.c
objs = ${srcs:.c=.o}

all: LCRM_date2epoch

LCRM_date2epoch: ${objs} lrm_install.h 
	${CC} -o LCRM_date2epoch ${objs}

clean:
	rm -f LCRM_date2epoch ${objs}
