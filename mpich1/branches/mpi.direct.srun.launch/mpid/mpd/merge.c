#include "mpdconf.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "merge.h"
#include "set.h"

static int intcmp(const void *a, const void *b)
{
	int x = *(int *) a, y = *(int *) b;

	if (x < y)
		return -1;
	if (x > y)
		return 1;

	return 0;
}

#define WHITESPACE      " \t\v\r\n\f"
static int nexttoken(char **token)
{
    *token += strspn(*token, WHITESPACE);

    return strcspn(*token, WHITESPACE);
}

static int token_eq(char *a, char *b)
{
    char *tokena = a, *tokenb = b;
    int len;

    do { 
        len = nexttoken(&tokena);
        if (len != nexttoken(&tokenb))
            return 0;

        if (strncmp(tokena, tokenb, len) != 0)
            return 0;

        tokena += len;
        tokenb += len;
    } while (len > 0);

    return 1;
}

static
int next_stripped_line(char *msg, int start, char **newmsg, int *len, int *who)
{
    int pos;
	char *tmp;

	while (sscanf(msg + start, "%d %n", who, &pos) != 1) {
        tmp = strchr(msg + start, '\n');
        if (tmp != NULL)
            start = (tmp - msg) + 1;
        else 
		    return -1;
    }

    msg += start + pos + 1;

    tmp = strchr(msg, '\n');
    if (tmp == NULL){
        *newmsg = malloc(strlen(msg) + 1);
        if (*newmsg == NULL)
            return -1;
        strcpy(*newmsg, msg);
        *len = strlen(msg);
        return *len + pos + 1 + start;
    } else {
        *len = 1 + tmp - msg;
        *newmsg = malloc(*len + 1);
        if (*newmsg == NULL)
            return -1;

        strncpy(*newmsg, msg, *len);
        (*newmsg)[*len] = '\0';

        return *len + pos + 1 + start;
    }
}

static void line_print(struct line *l, FILE * outstream)
{
	int *tmp;
	int start, end;
	int printed = 0;  /* Indicates if _any_ printing has been done so that */
	                  /* commas only go in between ranges and numbers */

	set_reset(l->nodes);
	/* s is necessarily non-empty */
	start = end = *(int *) set_next(l->nodes);
	do  {
        tmp = set_next(l->nodes); 

		if ((tmp != NULL) && (*tmp == end + 1)) {
			end++;
		} else {
			if (printed)
				fprintf(outstream, ",");

			if (end > start)
				fprintf(outstream, "%d-%d", start, end);
			else
				fprintf(outstream, "%d", start);

            if (tmp != NULL){
                start = end = *tmp;
                printed = 1;
            }
		}
	} while (tmp != NULL);

    if (outstream == stderr)
        fprintf(outstream, ".");
	fprintf(outstream, ":%s", l->text);
}

static struct nodestream *nodestream_create(int maxlines)
{
	struct nodestream *ns;

	ns = malloc(sizeof(struct nodestream));
	if (ns == NULL)
		return NULL;

	ns->lines = malloc(maxlines * sizeof(struct line));
	if (ns->lines == NULL) {
		free(ns);
		return NULL;
	}

	ns->maxlines = maxlines;
	ns->start = ns->end = 0;
    ns->ready = 0;

	return ns;
}

static void nodestream_destroy(struct nodestream *ns)
{
	int i;

	for (i = ns->start; i < ns->end; i++) {
		free(ns->lines[i].text);
		set_destroy(ns->lines[i].nodes);
	}

	free(ns->lines);
	free(ns);
}


static int nodestream_insert_set(struct nodestream *ns, char *text, 
                                 struct set *s)
{
	if (ns->end >= ns->maxlines) {
		if (ns->start > 0) {
			memmove(ns->lines, ns->lines + ns->start,
							(ns->end - ns->start) * sizeof(struct line));
			ns->end -= ns->start;
			ns->start = 0;
		} else {
			/* no space */
			return -1;
		}
	}

	ns->lines[ns->end].nodes = s;
	ns->lines[ns->end].text = text;

	ns->end++;

	return 0;
}

static 
int nodestream_insert(struct nodestream *ns, char *text, int who, int maxnodes)
{
    struct set *s;

	s = set_create(maxnodes, sizeof(int), intcmp);

	if (s == NULL)
		return -1;

	if (set_insert(s, &who) < 0)
		return -1;

	return nodestream_insert_set(ns, text, s);
}

static int nodestream_tryinsert(struct nodestream *ns, char *line, int who)
{
	int i;

	for (i = ns->start; i < ns->end; i++){
		if (token_eq(ns->lines[i].text, line) && 
            (set_exists(ns->lines[i].nodes, &who) == NULL)){
            if (set_insert(ns->lines[i].nodes, &who) < 0)
                continue;
            return 0;
        }
	}

	return -1;
}

static int nodestream_full(struct nodestream *ns)
{
	return (ns->start == 0) && (ns->end == ns->maxlines);
}

static void nodestream_flush(struct nodestream *ns, FILE *outstream)
{
    int i;
    for (i = ns->start; i < ns->end; i++) {
        line_print(&(ns->lines[i]), outstream);
        free(ns->lines[i].text);
        set_destroy(ns->lines[i].nodes);
    }

    ns->start = ns->end = 0;
}

#ifdef FOO
static void nodestream_print(struct nodestream *ns, FILE *outstream, int owner)
{
    int i;

    for (i = ns->start; i < ns->end; i++) {
        fprintf(outstream, "%d -> ", owner);
        line_print(&(ns->lines[i]), outstream);
    }
}
#endif

static int nodestreams_reown(struct nodestream *nss[], int which)
{
	int i;
    int *min;
    struct nodestream *ns = nss[which];

	for (i = ns->start; i < ns->end; i++){
		if ((min = set_min(ns->lines[i].nodes)) == NULL)
            return 0;
        if (*min != which) {
            if (nodestream_insert_set(nss[*min], ns->lines[i].text, ns->lines[i].nodes) == 0) {
                ns->end--;
                memmove(ns->lines+i, ns->lines+i+1, (ns->end-i)*sizeof(struct line));
            }
        }
	}

	return 0;
}

struct merged *merged_create(int nonodes, int maxlines, FILE * outstream)
{
    int i;
	struct merged *m;

	m = malloc(sizeof(struct merged));
	if (m == NULL)
		return NULL;

    m->submitted = malloc(nonodes * sizeof(int));
    if (m->submitted == NULL) {
        free(m);
        return NULL;
    }

	m->nstreams = malloc(nonodes * sizeof(struct nodestream));
	if (m->nstreams == NULL) {
        free(m->submitted);
		free(m);
		return NULL;
	}

	for (i = 0; i < nonodes; i++) {
		m->nstreams[i] = nodestream_create(maxlines);
		if (m->nstreams[i] == NULL) {
			for (; i >= 0; i--)
				nodestream_destroy(m->nstreams[i]);
            free(m->submitted);
			free(m->nstreams);
			free(m);
			return NULL;
		}
	}

    memset(m->submitted, 0, nonodes * sizeof(int));
	m->nonodes = nonodes;
	m->outstream = outstream;
    m->buf[0] = '\0';
    m->buflen = 0;

	return m;
}

void merged_destroy(struct merged *m)
{
	int i;

	if (m == NULL)
		return;

	for (i = 0; i < m->nonodes; i++)
		nodestream_destroy(m->nstreams[i]);

	free(m->nstreams);
	free(m);
}

int merged_num_ready(struct merged *m)
{
    int i;
    int count = 0;

    for (i = 0; i < m->nonodes; i++)
        count += m->nstreams[i]->ready ? 1 : 0;

    return count;
}

void merged_flush(struct merged *m)
{
    int i;

    for (i = 0; i < m->nonodes; i++) {
        nodestream_flush(m->nstreams[i], m->outstream);
        m->submitted[i] = 0;
        m->nstreams[i]->ready = 0;
    }
    fflush(m->outstream);
}

static void break_gdb_lines(char *text)
{
    char *tmp = text;

    while ((tmp = strstr(tmp, "(gdb)")) != NULL) {
        tmp += sizeof("(gdb)") - 1;
        *tmp = '\n';
    }
}

int merged_submit(struct merged *m, char *text)
{
    int i;
    int len;
	int who;
    int pos = 0;
	char *newmsg;

    if (m->buflen > 0) { 
        len = strlen(text);

        if (len + m->buflen < BUF_SIZE) {
            strcat(m->buf, text);
            text = m->buf;
        } else {
            char *st;

            st = malloc(len + m->buflen + 1);
            if (st == NULL)
                return -1;

            strcpy(st, m->buf);
            strcat(st, text);

            text = st;
        }
    }

    break_gdb_lines(text);

    m->buflen = 0;

	while ((pos = next_stripped_line(text, pos, &newmsg, &len, &who)) > 0) {

        /* Bogus source? */
		if (who < 0 || who >= m->nonodes) {
			free(newmsg);
			continue;
		}

        /* Did we get a stream-breaking line? */
        if (token_eq(newmsg, "(gdb)")) {
            free(newmsg);
            m->nstreams[who]->ready = 1;
            continue;
        }

        /* In case we get a program without debugging symbols */
        if (token_eq(newmsg, "(no debugging symbols found)...(gdb)")) {
            char *end;

            m->nstreams[who]->ready = 1;
            end = strrchr(newmsg, '(');
            *end = '\n';
            *(end+1) = '\0';
        }

        /* Do we need to buffer for later? */
        if ((newmsg[len-1] != '\n') && (BUF_SIZE - len > 0)){
            strcpy(m->buf, newmsg);
            m->buflen = len;
            free(newmsg);
            break;
        }

        m->submitted[who]++;

        /* See if anyone else said it */
		for (i = 0; i < m->nonodes; i++) {
			if (i != who) {
				if (nodestream_tryinsert(m->nstreams[i], newmsg, who) == 0) {
                    free(newmsg);
                    nodestreams_reown(m->nstreams, i);
					break;
				}
			}
		}

        /* Didn't find the line anywhere else */
        if (i == m->nonodes) {
            /* If the stream is full, flush it first */
            if (nodestream_full(m->nstreams[who])) {
                nodestream_flush(m->nstreams[who], m->outstream);
            }

            /* Put the line in who's cache */
            (void) nodestream_insert(m->nstreams[who], newmsg, who, m->nonodes);
        }
	}

	return 0;
}

void merged_print_status(struct merged *m)
{
    int i;
    int printed = 0;
    int start = -1, end = -1;

    fprintf(m->outstream, "Status:\n");
    fprintf(m->outstream, "Ready nodes: ");
    for (i = 0; i < m->nonodes; i++) {
        if (m->nstreams[i]->ready) { 
            if (start < 0) {
                start = end = i;
                continue;
            }

            if (end + 1 == i) {
                end = i;
            } else {
                if (printed) 
                    fprintf(m->outstream, ",");
                else
                    printed = 1;

                if (end > start)
                    fprintf(m->outstream, "%d-%d", start, end);
                else
                    fprintf(m->outstream, "%d", start);

                start = end = i;
            }
        }
    }

    if (printed) 
        fprintf(m->outstream, ",");
    else
        printed = 1;

    if (end > start)
        fprintf(m->outstream, "%d-%d", start, end);
    else
        fprintf(m->outstream, "%d", start);


    printed = 0;
    start = -1;

    fprintf(m->outstream, "\nWaiting nodes: ");
    for (i = 0; i < m->nonodes; i++) {
        if (!m->nstreams[i]->ready) { 
            if (start < 0) {
                start = end = i;
                continue;
            }

            if (end + 1 == i) {
                end = i;
            } else {
                if (printed) 
                    fprintf(m->outstream, ",");
                else
                    printed = 1;

                if (end > start)
                    fprintf(m->outstream, "%d-%d", start, end);
                else
                    fprintf(m->outstream, "%d", start);

                start = end = i;
            }
        }
    }

    if (printed) 
        fprintf(m->outstream, ",");
    else
        printed = 1;

    if (end > start)
        fprintf(m->outstream, "%d-%d", start, end);
    else
        fprintf(m->outstream, "%d", start);

    fprintf(m->outstream, "\n");
}

void merged_reset_next_ready(struct merged *m)
{  
    int i;
    
    for (i = 0; i < m->nonodes; i++) {
        if (m->nstreams[i]->ready) {
            m->next_ready = i;
            return;
        }
    }

    m->next_ready = m->nonodes;
}

int merged_next_ready(struct merged *m)
{
    int i, last_ready = m->next_ready;

    m->next_ready = m->nonodes;

    for (i = last_ready + 1; i < m->nonodes; i++) {
        if (m->nstreams[i]->ready) {
            m->next_ready = i;
        }
    }

    return last_ready < m->nonodes ? last_ready : -1;
}

#ifdef DEBUG_MERGE_C

void print_set(struct set *s)
{
    int *tmp;

    set_reset(s);
    while ((tmp = set_next(s)) != NULL){
        printf("%d, ", *tmp);
    }
    printf("\n");
}

int main(int argc, char *argv[])
{
	char line[1024];
	char line2[1024];
	struct merged *m;

	if (argc < 2) {
		fprintf(stderr, "usage: merge <size>\n");
		return 1;
	}

	m = merged_create(atoi(argv[1]), 2, stdout);
	if (m == NULL) {
		perror("Unable to create a merged");
		return 1;
	}

    strcpy(line, "0: hi\nhi\n1: hi\n");
		merged_submit(m, line);
	while (fgets(line, 1024, stdin) != NULL) {
		merged_submit(m, line);
	}

	return 0;
}

#endif /* DEBUG_MERGE_C */
