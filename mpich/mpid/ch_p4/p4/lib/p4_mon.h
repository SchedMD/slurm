
typedef MD_lock_t p4_lock_t;

#define p4_lock_init(l)  MD_lock_init(l)
#define p4_lock(l)       MD_lock(l)
#define p4_unlock(l)     MD_unlock(l)

struct p4_mon_queue;  /* for c++ folks */
struct p4_monitor {
    p4_lock_t mon_lock;
    struct p4_mon_queue *qs;
};

typedef struct p4_monitor p4_monitor_t;


struct p4_mon_queue {
    int count;
    p4_lock_t delay_lock;
};


struct p4_getsub_monitor {
    struct p4_monitor m;
    int sub;
};

typedef struct p4_getsub_monitor p4_getsub_monitor_t;

#define p4_getsub(gs,s,max,nprocs) p4_getsubs(gs,s,max,nprocs,1)

struct p4_barrier_monitor {
    struct p4_monitor m;
};

typedef struct p4_barrier_monitor p4_barrier_monitor_t;

struct p4_askfor_monitor {
    struct p4_monitor m;
    int pgdone;
    int pbdone;
};

typedef struct p4_askfor_monitor p4_askfor_monitor_t;

