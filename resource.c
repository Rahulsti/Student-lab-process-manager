#include <stdio.h>
#include <string.h>
#include "common.h"

const char *RES_NAMES[NRES] = {"Systems", "Licenses", "Peripherals"};

void sys_init(System *s, int a, int b, int c)
{
    int j;
    memset(s, 0, sizeof(System));
    s->total[0] = a;
    s->total[1] = b;
    s->total[2] = c;
    for (j = 0; j < NRES; j++)
        s->avail[j] = s->total[j];
    s->policy = POLICY_FCFS;
    s->next_ticket = 1;
}

int sys_add_student(System *s, const char *name, int max[], int duration)
{
    int j, id;
    Student *p;
    if (s->nst >= MAXS)
        return -1;
    for (j = 0; j < NRES; j++)
        if (max[j] < 0 || max[j] > s->total[j])
            return -2;
    if (duration < 0)
        return -3;
    id = s->nst;
    s->nst++;
    p = &s->st[id];
    memset(p, 0, sizeof(Student));
    strncpy(p->name, name, NAMELEN - 1);
    for (j = 0; j < NRES; j++) {
        p->max[j] = max[j];
        p->alloc[j] = 0;
        p->need[j] = max[j];
    }
    p->duration = duration;
    p->state = ST_IDLE;
    p->pid = 0;
    return id;
}

int sys_valid(System *s, int sid)
{
    if (sid < 0 || sid >= s->nst)
        return 0;
    if (s->st[sid].state == ST_DONE)
        return 0;
    return 1;
}

int sys_is_safe(System *s, int seq[], int *len)
{
    int work[NRES];
    int finish[MAXS];
    int i, j, ok, count = 0, active = 0, progress = 1;
    for (j = 0; j < NRES; j++)
        work[j] = s->avail[j];
    for (i = 0; i < s->nst; i++) {
        finish[i] = (s->st[i].state == ST_DONE);
        if (!finish[i])
            active++;
    }
    while (progress) {
        progress = 0;
        for (i = 0; i < s->nst; i++) {
            if (finish[i])
                continue;
            ok = 1;
            for (j = 0; j < NRES; j++)
                if (s->st[i].need[j] > work[j])
                    ok = 0;
            if (ok) {
                for (j = 0; j < NRES; j++)
                    work[j] += s->st[i].alloc[j];
                finish[i] = 1;
                seq[count] = i;
                count++;
                progress = 1;
            }
        }
    }
    *len = count;
    return count == active;
}

int sys_request(System *s, int sid, int req[])
{
    int j, sum = 0, len;
    int seq[MAXS];
    Student *p;
    if (!sys_valid(s, sid))
        return REQ_INVALID;
    for (j = 0; j < NRES; j++) {
        if (req[j] < 0)
            return REQ_INVALID;
        sum += req[j];
    }
    if (sum == 0)
        return REQ_INVALID;
    p = &s->st[sid];
    for (j = 0; j < NRES; j++)
        if (req[j] > p->need[j])
            return REQ_EXCEEDS;
    for (j = 0; j < NRES; j++)
        if (req[j] > s->avail[j])
            return REQ_UNAVAIL;
    for (j = 0; j < NRES; j++) {
        s->avail[j] -= req[j];
        p->alloc[j] += req[j];
        p->need[j] -= req[j];
    }
    s->safety_checks++;
    if (sys_is_safe(s, seq, &len)) {
        s->granted++;
        return REQ_GRANTED;
    }
    for (j = 0; j < NRES; j++) {
        s->avail[j] += req[j];
        p->alloc[j] -= req[j];
        p->need[j] += req[j];
    }
    s->unsafe_blocked++;
    return REQ_UNSAFE;
}

int sys_release(System *s, int sid, int rel[])
{
    int j, sum = 0;
    Student *p;
    if (!sys_valid(s, sid))
        return -1;
    p = &s->st[sid];
    for (j = 0; j < NRES; j++) {
        if (rel[j] < 0 || rel[j] > p->alloc[j])
            return -2;
        sum += rel[j];
    }
    if (sum == 0)
        return -1;
    for (j = 0; j < NRES; j++) {
        p->alloc[j] -= rel[j];
        p->need[j] += rel[j];
        s->avail[j] += rel[j];
    }
    return 0;
}

void sys_finish(System *s, int sid, int freed[])
{
    int j;
    Student *p = &s->st[sid];
    for (j = 0; j < NRES; j++) {
        freed[j] = p->alloc[j];
        s->avail[j] += p->alloc[j];
        p->alloc[j] = 0;
        p->need[j] = p->max[j];
    }
    p->state = ST_DONE;
    p->pid = 0;
}

int sys_consistent(System *s)
{
    int i, j, sum;
    for (j = 0; j < NRES; j++) {
        sum = s->avail[j];
        if (s->avail[j] < 0)
            return 0;
        for (i = 0; i < s->nst; i++) {
            Student *p = &s->st[i];
            if (p->alloc[j] < 0 || p->alloc[j] > p->max[j])
                return 0;
            if (p->need[j] != p->max[j] - p->alloc[j])
                return 0;
            sum += p->alloc[j];
        }
        if (sum != s->total[j])
            return 0;
    }
    return 1;
}

const char *req_code_name(int code)
{
    if (code == REQ_GRANTED)
        return "GRANTED";
    if (code == REQ_EXCEEDS)
        return "EXCEEDS_MAX";
    if (code == REQ_UNAVAIL)
        return "WAIT_UNAVAILABLE";
    if (code == REQ_UNSAFE)
        return "WAIT_UNSAFE";
    return "INVALID";
}
