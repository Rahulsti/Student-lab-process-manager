#include <stdio.h>
#include <string.h>
#include "common.h"

int q_push(System *s, int sid, int req[])
{
    int j;
    Request *r;
    if (s->qlen >= MAXQ)
        return -1;
    r = &s->q[s->qlen];
    r->sid = sid;
    for (j = 0; j < NRES; j++)
        r->req[j] = req[j];
    r->ticket = s->next_ticket;
    s->next_ticket++;
    r->attempts = 1;
    s->qlen++;
    s->queued++;
    if (s->qlen > s->max_qlen)
        s->max_qlen = s->qlen;
    return s->qlen - 1;
}

void q_remove(System *s, int idx)
{
    int i;
    if (idx < 0 || idx >= s->qlen)
        return;
    for (i = idx; i < s->qlen - 1; i++)
        s->q[i] = s->q[i + 1];
    s->qlen--;
}

int q_drop_student(System *s, int sid)
{
    int i = 0, n = 0;
    while (i < s->qlen) {
        if (s->q[i].sid == sid) {
            q_remove(s, i);
            n++;
        } else {
            i++;
        }
    }
    return n;
}

static void snapshot(System *s, Request *r)
{
    int j;
    for (j = 0; j < NRES; j++)
        r->avail_after[j] = s->avail[j];
    sys_is_safe(s, r->seq, &r->seqlen);
}

static int q_fcfs(System *s, Request out[])
{
    int n = 0, code;
    Request r;
    while (s->qlen > 0) {
        r = s->q[0];
        code = sys_request(s, r.sid, r.req);
        if (code == REQ_GRANTED) {
            snapshot(s, &r);
            out[n] = r;
            n++;
            q_remove(s, 0);
        } else if (code == REQ_UNAVAIL || code == REQ_UNSAFE) {
            s->q[0].attempts++;
            break;
        } else {
            q_remove(s, 0);
        }
    }
    return n;
}

static int q_rr(System *s, Request out[])
{
    Request tmp[MAXQ];
    Request first;
    int m = s->qlen, i, n = 0, code;
    for (i = 0; i < m; i++)
        tmp[i] = s->q[i];
    s->qlen = 0;
    for (i = 0; i < m; i++) {
        code = sys_request(s, tmp[i].sid, tmp[i].req);
        if (code == REQ_GRANTED) {
            snapshot(s, &tmp[i]);
            out[n] = tmp[i];
            n++;
        } else if (code == REQ_UNAVAIL || code == REQ_UNSAFE) {
            tmp[i].attempts++;
            s->q[s->qlen] = tmp[i];
            s->qlen++;
        }
    }
    if (s->qlen > 1) {
        first = s->q[0];
        for (i = 0; i < s->qlen - 1; i++)
            s->q[i] = s->q[i + 1];
        s->q[s->qlen - 1] = first;
    }
    return n;
}

int q_process(System *s, Request out[])
{
    if (s->qlen == 0)
        return 0;
    if (s->policy == POLICY_RR)
        return q_rr(s, out);
    return q_fcfs(s, out);
}
