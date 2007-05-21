#ifndef MERGE_H
#define MERGE_H

struct line {
    char *text;
    struct set *nodes;
};

#define DFLT_NO_LINES   10

struct nodestream {
    struct line *lines;
    int start, end;
    int maxlines;
    int ready;
};

#define MAX_AGE         6
#define BUF_SIZE        1024

struct merged {
    int *submitted;
    struct nodestream **nstreams;
    int nonodes;
    FILE *outstream;

    int next_ready;
    int buflen;
    char buf[BUF_SIZE];
};

extern struct merged *merged_create(int nonodes, int maxlines, FILE * outstream);
extern void merged_destroy(struct merged *m);
extern int merged_submit(struct merged *m, char *text);
extern char *merged_next(struct merged *m, char *buf, int len);
extern void merged_flush(struct merged *m);
extern int merged_num_ready(struct merged *m);
extern void merged_print_status(struct merged *m);

extern void merged_reset_next_ready(struct merged *m);
extern int merged_next_ready(struct merged *m);

#endif /* MERGE_H */
