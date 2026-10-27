#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include "common.h"

static int passed = 0;
static int failed = 0;

static void report(const char *id, const char *desc, const char *expected, const char *actual)
{
    int ok = strcmp(expected, actual) == 0;
    if (ok)
        passed++;
    else
        failed++;
    printf("| %-4s | %-46s | %-28s | %-28s | %-4s |\n", id, desc, expected, actual, ok ? "PASS" : "FAIL");
}

static void vec(char *buf, int v[])
{
    sprintf(buf, "<%d,%d,%d>", v[0], v[1], v[2]);
}

static void seqstr(char *buf, int seq[], int len, int safe)
{
    int i;
    char t[16];
    strcpy(buf, safe ? "SAFE " : "UNSAFE ");
    for (i = 0; i < len; i++) {
        sprintf(t, "%sP%d", i ? "," : "", seq[i]);
        strcat(buf, t);
    }
}

static void classic(System *s)
{
    int max[5][3] = {{7, 5, 3}, {3, 2, 2}, {9, 0, 2}, {2, 2, 2}, {4, 3, 3}};
    int al[5][3] = {{0, 1, 0}, {2, 0, 0}, {3, 0, 2}, {2, 1, 1}, {0, 0, 2}};
    int i, j;
    char nm[8];
    sys_init(s, 10, 5, 7);
    for (i = 0; i < 5; i++) {
        sprintf(nm, "P%d", i);
        sys_add_student(s, nm, max[i], 0);
        for (j = 0; j < NRES; j++) {
            s->st[i].alloc[j] = al[i][j];
            s->st[i].need[j] = max[i][j] - al[i][j];
            s->avail[j] -= al[i][j];
        }
    }
}

static void line(void)
{
    printf("+------+------------------------------------------------+------------------------------+------------------------------+------+\n");
}

int run_tests(const char *exe)
{
    System s;
    int seq[MAXS], len, ok, code, i, j, st, k;
    int r[NRES], m[NRES];
    char a[512], b[256], sig[32];
    Request out[MAXQ];
    pid_t pid, mid;
    int fd[2], info[3];

    log_set_quiet(1);
    setenv("LAB_QUIET", "1", 1);
    printf("Student Lab Process Manager - automated test suite\n");
    line();
    printf("| %-4s | %-46s | %-28s | %-28s | %-4s |\n", "ID", "Test case", "Expected", "Actual", "Res.");
    line();

    classic(&s);
    ok = sys_is_safe(&s, seq, &len);
    seqstr(a, seq, len, ok);
    report("T01", "Safety check on textbook state (Avail 3,3,2)", "SAFE P1,P3,P4,P0,P2", a);

    r[0] = 1; r[1] = 0; r[2] = 2;
    code = sys_request(&s, 1, r);
    vec(b, s.avail);
    sprintf(a, "%s %s", req_code_name(code), b);
    report("T02", "P1 requests <1,0,2> (safe request)", "GRANTED <2,3,0>", a);

    r[0] = 3; r[1] = 3; r[2] = 0;
    code = sys_request(&s, 4, r);
    report("T03", "P4 requests <3,3,0> (more than Available)", "WAIT_UNAVAILABLE", req_code_name(code));

    r[0] = 0; r[1] = 2; r[2] = 0;
    code = sys_request(&s, 0, r);
    vec(b, s.avail);
    sprintf(a, "%s %s", req_code_name(code), b);
    report("T04", "P0 requests <0,2,0> (unsafe) + rollback", "WAIT_UNSAFE <2,3,0>", a);

    r[0] = 1; r[1] = 0; r[2] = 0;
    code = sys_request(&s, 3, r);
    report("T05", "P3 requests <1,0,0> beyond its max claim", "EXCEEDS_MAX", req_code_name(code));

    r[0] = 0; r[1] = 0; r[2] = 0;
    code = sys_request(&s, 2, r);
    k = sys_request(&s, 9, m);
    sprintf(b, "%s/%s", req_code_name(code), req_code_name(k));
    report("T06", "Empty request / unknown student id", "INVALID/INVALID", b);

    sys_finish(&s, 1, r);
    vec(a, r);
    vec(b, s.avail);
    strcat(a, " ");
    strcat(a, b);
    report("T07", "P1 finishes, all its resources released", "<3,0,2> <5,3,2>", a);

    sys_init(&s, 2, 1, 1);
    m[0] = 2; m[1] = 0; m[2] = 0;
    sys_add_student(&s, "A", m, 0);
    sys_add_student(&s, "B", m, 0);
    for (i = 0; i < 2; i++) {
        s.st[i].alloc[0] = 1;
        s.st[i].need[0] = 1;
    }
    s.avail[0] = 0;
    ok = sys_is_safe(&s, seq, &len);
    seqstr(a, seq, len, ok);
    report("T08", "Circular-wait state detected as unsafe", "UNSAFE ", a);

    sys_init(&s, 4, 2, 3);
    m[0] = 5; m[1] = 1; m[2] = 1;
    k = sys_add_student(&s, "Big", m, 0);
    sprintf(a, "%d", k);
    report("T09", "Register student with max > lab total", "-2", a);

    sys_init(&s, 3, 1, 1);
    m[0] = 3; m[1] = 0; m[2] = 0;
    sys_add_student(&s, "A", m, 0);
    m[0] = 1;
    sys_add_student(&s, "B", m, 0);
    m[0] = 2;
    sys_add_student(&s, "C", m, 0);
    r[0] = 2; r[1] = 0; r[2] = 0;
    sys_request(&s, 2, r);
    r[0] = 3;
    q_push(&s, 0, r);
    r[0] = 1;
    q_push(&s, 1, r);
    s.policy = POLICY_FCFS;
    k = q_process(&s, out);
    sprintf(a, "granted=%d qlen=%d", k, s.qlen);
    report("T10", "FCFS: blocked head stops queue (no overtake)", "granted=0 qlen=2", a);

    sys_finish(&s, 2, r);
    k = q_process(&s, out);
    sprintf(a, "granted=%d S%d qlen=%d", k, k > 0 ? out[0].sid : -1, s.qlen);
    report("T11", "FCFS: release triggers re-check, head served", "granted=1 S0 qlen=1", a);

    sys_init(&s, 3, 1, 1);
    m[0] = 3; m[1] = 0; m[2] = 0;
    sys_add_student(&s, "A", m, 0);
    m[0] = 1;
    sys_add_student(&s, "B", m, 0);
    m[0] = 2;
    sys_add_student(&s, "C", m, 0);
    r[0] = 2; r[1] = 0; r[2] = 0;
    sys_request(&s, 2, r);
    r[0] = 3;
    q_push(&s, 0, r);
    r[0] = 1;
    q_push(&s, 1, r);
    s.policy = POLICY_RR;
    k = q_process(&s, out);
    sprintf(a, "granted=%d S%d qlen=%d", k, k > 0 ? out[0].sid : -1, s.qlen);
    report("T12", "RR: safe later request served past blocked one", "granted=1 S1 qlen=1", a);

    sys_init(&s, 1, 1, 1);
    m[0] = 1; m[1] = 1; m[2] = 1;
    sys_add_student(&s, "H", m, 0);
    sys_add_student(&s, "X", m, 0);
    sys_add_student(&s, "Y", m, 0);
    sys_add_student(&s, "Z", m, 0);
    sys_request(&s, 0, m);
    q_push(&s, 1, m);
    q_push(&s, 2, m);
    q_push(&s, 3, m);
    s.policy = POLICY_RR;
    q_process(&s, out);
    sprintf(a, "S%d,S%d,S%d", s.q[0].sid, s.q[1].sid, s.q[2].sid);
    q_process(&s, out);
    sprintf(b, " S%d,S%d,S%d", s.q[0].sid, s.q[1].sid, s.q[2].sid);
    strcat(a, b);
    report("T13", "RR: queue rotates each scheduling pass", "S2,S3,S1 S3,S1,S2", a);

    srand(2026);
    sys_init(&s, 6, 4, 5);
    for (i = 0; i < 6; i++) {
        for (j = 0; j < NRES; j++)
            m[j] = rand() % (s.total[j] + 1);
        sys_add_student(&s, "R", m, 0);
    }
    ok = 1;
    k = 0;
    for (i = 0; i < 5000; i++) {
        int sid = rand() % s.nst;
        if (rand() % 3 == 0) {
            for (j = 0; j < NRES; j++)
                r[j] = s.st[sid].alloc[j] ? rand() % (s.st[sid].alloc[j] + 1) : 0;
            sys_release(&s, sid, r);
        } else {
            for (j = 0; j < NRES; j++)
                r[j] = s.st[sid].need[j] ? rand() % (s.st[sid].need[j] + 1) : 0;
            if (sys_request(&s, sid, r) == REQ_GRANTED)
                k++;
        }
        if (!sys_consistent(&s) || !sys_is_safe(&s, seq, &len))
            ok = 0;
    }
    sprintf(a, "%s", ok ? "always safe" : "violation");
    sprintf(b, "%d random ops: safe+consistent", i);
    report("T14", b, "always safe", a);

    pid = proc_spawn(exe, "Test", 0, 1);
    st = -1;
    waitpid(pid, &st, 0);
    sprintf(a, "pid>0 exit=%d", WIFEXITED(st) ? WEXITSTATUS(st) : -1);
    if (pid <= 0)
        strcpy(a, "spawn failed");
    report("T15", "fork()+exec() session runs and exits cleanly", "pid>0 exit=0", a);

    pid = proc_spawn(exe, "Kill", 0, 30);
    proc_msleep(200);
    st = proc_stop(pid);
    sprintf(a, "signal=%d", WIFSIGNALED(st) ? WTERMSIG(st) : 0);
    sprintf(sig, "signal=%d", SIGTERM);
    report("T16", "Admin ends session early (SIGTERM + waitpid)", sig, a);

    fflush(stdout);
    pid = fork();
    if (pid == 0)
        _exit(0);
    proc_msleep(300);
    a[0] = proc_state(pid);
    a[1] = 0;
    k = proc_count_zombies();
    waitpid(pid, &st, 0);
    sprintf(b, "%s z=%d after=%d", a, k, proc_count_zombies());
    report("T17", "Unreaped child is zombie; wait() clears it", "Z z=1 after=0", b);

    fflush(stdout);
    if (pipe(fd) < 0)
        return 1;
    mid = fork();
    if (mid == 0) {
        close(fd[0]);
        pid = fork();
        if (pid == 0) {
            info[0] = getpid();
            if (write(fd[1], info, sizeof(int)) < 0)
                _exit(1);
            proc_msleep(600);
            info[1] = getppid();
            if (write(fd[1], &info[1], sizeof(int)) < 0)
                _exit(1);
            _exit(0);
        }
        proc_msleep(200);
        _exit(0);
    }
    close(fd[1]);
    proc_read_all(fd[0], info, sizeof(int));
    waitpid(mid, &st, 0);
    proc_read_all(fd[0], &info[1], sizeof(int));
    close(fd[0]);
    waitpid(info[0], &st, 0);
    sprintf(a, "%s", info[1] == getpid() ? "adopted by manager" : "adopted by init");
    report("T18", "Orphaned grandchild re-parented to manager", "adopted by manager", a);

    fflush(stdout);
    if (pipe(fd) < 0)
        return 1;
    mid = fork();
    if (mid == 0) {
        close(fd[0]);
        pid = proc_spawn(exe, "Orphan", 0, 30);
        if (write(fd[1], &pid, sizeof(pid)) < 0)
            _exit(1);
        proc_msleep(300);
        _exit(0);
    }
    close(fd[1]);
    proc_read_all(fd[0], &pid, sizeof(pid));
    close(fd[0]);
    waitpid(mid, &st, 0);
    st = 0;
    waitpid(pid, &st, 0);
    sprintf(a, "signal=%d", WIFSIGNALED(st) ? WTERMSIG(st) : 0);
    report("T19", "Session dies if its parent dies (PDEATHSIG)", sig, a);

    sprintf(a, "%d", proc_count_zombies());
    report("T20", "No zombie children after whole test run", "0", a);

    line();
    printf("Total: %d   Passed: %d   Failed: %d\n", passed + failed, passed, failed);
    return failed;
}
