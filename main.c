#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <libgen.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include "common.h"

static System S;
static char session_exe[512];
static int script_mode = 0;
static int running_flag = 1;

static void fmt_vec(char *buf, int v[])
{
    sprintf(buf, "<%d,%d,%d>", v[0], v[1], v[2]);
}

static void fmt_seq(char *buf, int seq[], int len)
{
    int i;
    char t[16];
    buf[0] = 0;
    for (i = 0; i < len; i++) {
        sprintf(t, "%sS%d", i ? " -> " : "", seq[i]);
        strcat(buf, t);
    }
    if (len == 0)
        strcpy(buf, "(no active students)");
}

static const char *state_name(int st)
{
    if (st == ST_IDLE)
        return "WAITING";
    if (st == ST_RUNNING)
        return "RUNNING";
    return "DONE";
}

static int count_running(void)
{
    int i, n = 0;
    for (i = 0; i < S.nst; i++)
        if (S.st[i].state == ST_RUNNING)
            n++;
    return n;
}

static void handle_grant(int sid, int req[], Request *qr)
{
    int seq[MAXS], len;
    char v[32], a[32], q[512];
    pid_t pid;
    Student *p = &S.st[sid];
    int from_queue = qr != NULL;
    fmt_vec(v, req);
    if (qr != NULL) {
        fmt_vec(a, qr->avail_after);
        fmt_seq(q, qr->seq, qr->seqlen);
    } else {
        sys_is_safe(&S, seq, &len);
        fmt_vec(a, S.avail);
        fmt_seq(q, seq, len);
    }
    log_msg("GRANT  S%d %-8s %s%s  Available=%s  safe sequence: %s", sid, p->name, v, from_queue ? " (from queue)" : "", a, q);
    if (p->state != ST_RUNNING) {
        pid = proc_spawn(session_exe, p->name, sid, p->duration);
        if (pid > 0) {
            p->state = ST_RUNNING;
            p->pid = pid;
            log_msg("SPAWN  S%d %-8s fork()+exec(lab_session) -> pid %d", sid, p->name, pid);
        } else {
            log_msg("ERROR  fork failed for S%d: %s", sid, strerror(errno));
        }
    }
}

static void run_scheduler(void)
{
    Request out[MAXQ];
    int n, i;
    n = q_process(&S, out);
    for (i = 0; i < n; i++)
        handle_grant(out[i].sid, out[i].req, &out[i]);
}

static void reap_children(void)
{
    pid_t pids[64];
    int sts[64], n, i, k, found;
    int freed[NRES];
    char f[32], a[32], how[64];
    n = proc_reap(pids, sts, 64);
    for (i = 0; i < n; i++) {
        found = -1;
        for (k = 0; k < S.nst; k++)
            if (S.st[k].state == ST_RUNNING && S.st[k].pid == pids[i])
                found = k;
        if (WIFEXITED(sts[i]))
            sprintf(how, "exit status %d", WEXITSTATUS(sts[i]));
        else if (WIFSIGNALED(sts[i]))
            sprintf(how, "killed by signal %d", WTERMSIG(sts[i]));
        else
            strcpy(how, "stopped");
        if (found < 0) {
            log_msg("REAP   stray child pid %d reaped (%s)", pids[i], how);
            continue;
        }
        sys_finish(&S, found, freed);
        k = q_drop_student(&S, found);
        S.completed++;
        fmt_vec(f, freed);
        fmt_vec(a, S.avail);
        log_msg("EXIT   S%d %-8s pid %d reaped by waitpid() (%s), released %s  Available=%s", found, S.st[found].name, pids[i], how, f, a);
        if (k > 0)
            log_msg("QUEUE  dropped %d pending request(s) of finished S%d", k, found);
    }
    if (n > 0)
        run_scheduler();
}

static void pump(int ms)
{
    struct timeval tv;
    int waited = 0, step;
    reap_children();
    while (waited < ms) {
        step = ms - waited;
        if (step > 100)
            step = 100;
        tv.tv_sec = 0;
        tv.tv_usec = step * 1000;
        select(0, NULL, NULL, NULL, &tv);
        waited += step;
        reap_children();
    }
}

static void cmd_show(void)
{
    int i;
    Student *p;
    printf("    Total      : Systems=%d  Licenses=%d  Peripherals=%d\n", S.total[0], S.total[1], S.total[2]);
    printf("    Available  : Systems=%d  Licenses=%d  Peripherals=%d\n", S.avail[0], S.avail[1], S.avail[2]);
    printf("    Policy     : %s     Queue length: %d\n", S.policy == POLICY_RR ? "Round Robin" : "FCFS", S.qlen);
    printf("    +-----+----------+---------+-------+-----------+-----------+-----------+\n");
    printf("    | ID  | Student  | State   | PID   |    Max    |   Alloc   |   Need    |\n");
    printf("    |     |          |         |       |   S  L  P |   S  L  P |   S  L  P |\n");
    printf("    +-----+----------+---------+-------+-----------+-----------+-----------+\n");
    for (i = 0; i < S.nst; i++) {
        p = &S.st[i];
        printf("    | S%-2d | %-8s | %-7s | %5d |  %2d %2d %2d |  %2d %2d %2d |  %2d %2d %2d |\n", i, p->name, state_name(p->state), (int)p->pid,
               p->max[0], p->max[1], p->max[2], p->alloc[0], p->alloc[1], p->alloc[2],
               p->state == ST_DONE ? 0 : p->need[0], p->state == ST_DONE ? 0 : p->need[1], p->state == ST_DONE ? 0 : p->need[2]);
    }
    printf("    +-----+----------+---------+-------+-----------+-----------+-----------+\n");
}

static void cmd_safe(void)
{
    int seq[MAXS], len, ok;
    char q[512];
    ok = sys_is_safe(&S, seq, &len);
    fmt_seq(q, seq, len);
    if (ok)
        log_msg("SAFE   state is SAFE, safe sequence: %s", q);
    else
        log_msg("UNSAFE state is UNSAFE, only %d student(s) can finish: %s", len, q);
}

static void cmd_queue(void)
{
    int i;
    Request *r;
    printf("    Waiting queue (%s), %d request(s)\n", S.policy == POLICY_RR ? "Round Robin" : "FCFS", S.qlen);
    for (i = 0; i < S.qlen; i++) {
        r = &S.q[i];
        printf("    %2d. ticket #%-3d S%-2d %-8s request <%d,%d,%d>  checks so far: %d\n", i + 1, r->ticket, r->sid, S.st[r->sid].name, r->req[0], r->req[1], r->req[2], r->attempts);
    }
}

static void cmd_ps(void)
{
    int i;
    char c;
    printf("    Manager pid %d, running sessions: %d\n", getpid(), count_running());
    for (i = 0; i < S.nst; i++) {
        if (S.st[i].state != ST_RUNNING)
            continue;
        c = proc_state(S.st[i].pid);
        printf("    S%-2d %-8s pid %-6d ppid %-6d /proc state '%c'\n", i, S.st[i].name, S.st[i].pid, (int)proc_ppid(S.st[i].pid), c ? c : '-');
    }
    printf("    Zombie children of manager right now: %d\n", proc_count_zombies());
}

static void cmd_stats(void)
{
    struct rusage ru;
    printf("    Requests granted        : %d\n", S.granted);
    printf("    Requests queued         : %d\n", S.queued);
    printf("    Requests denied         : %d\n", S.denied);
    printf("    Unsafe grants prevented : %d\n", S.unsafe_blocked);
    printf("    Safety checks executed  : %d\n", S.safety_checks);
    printf("    Sessions completed      : %d\n", S.completed);
    printf("    Peak queue length       : %d\n", S.max_qlen);
    printf("    State consistent        : %s\n", sys_consistent(&S) ? "yes" : "NO");
    getrusage(RUSAGE_SELF, &ru);
    printf("    Manager CPU time        : user %.1f ms, system %.1f ms\n", ru.ru_utime.tv_sec * 1000.0 + ru.ru_utime.tv_usec / 1000.0, ru.ru_stime.tv_sec * 1000.0 + ru.ru_stime.tv_usec / 1000.0);
    printf("    Manager peak memory     : %ld KB resident, lab state %zu bytes\n", ru.ru_maxrss, sizeof(System));
}

static void stop_all(void)
{
    int i, st, freed[NRES];
    for (i = 0; i < S.nst; i++) {
        if (S.st[i].state != ST_RUNNING)
            continue;
        st = proc_stop(S.st[i].pid);
        log_msg("STOP   S%d %-8s pid %d terminated (%s) and reaped", i, S.st[i].name, S.st[i].pid, WIFSIGNALED(st) ? "SIGTERM" : "exited");
        sys_finish(&S, i, freed);
    }
}

static void cmd_help(void)
{
    printf("    init <S> <L> <P>                 reset lab with total Systems/Licenses/Peripherals\n");
    printf("    add <name> <maxS> <maxL> <maxP> <secs>  register student with max claim and session length\n");
    printf("    req <id> <s> <l> <p>             request resources (Banker's safety check)\n");
    printf("    rel <id> <s> <l> <p>             release part of a student's allocation\n");
    printf("    end <id>                         end a student's session now (SIGTERM + waitpid)\n");
    printf("    show | safe | queue | ps | stats display matrices / safety / queue / processes / counters\n");
    printf("    policy fcfs|rr                   choose waiting-queue scheduling policy\n");
    printf("    sleep <secs> | waitall           let time pass / wait until every session finishes\n");
    printf("    demo zombie | demo orphan        process lifecycle demonstrations\n");
    printf("    help | quit\n");
}

static int read_ints(char *args, int v[], int n)
{
    int i;
    char *t;
    for (i = 0; i < n; i++) {
        t = strtok(i == 0 ? args : NULL, " \t");
        if (t == NULL)
            return 0;
        v[i] = atoi(t);
    }
    return 1;
}

static void execute(char *line)
{
    char cmd[32], rest[256], name[NAMELEN];
    int v[6], code, id, j, pos;
    char vb[32], ab[32];
    cmd[0] = 0;
    rest[0] = 0;
    if (sscanf(line, "%31s %255[^\n]", cmd, rest) < 1)
        return;
    if (cmd[0] == '#')
        return;
    if (strcmp(cmd, "init") == 0) {
        if (!read_ints(rest, v, 3) || v[0] < 0 || v[1] < 0 || v[2] < 0) {
            printf("    usage: init <S> <L> <P>\n");
            return;
        }
        stop_all();
        j = S.policy;
        sys_init(&S, v[0], v[1], v[2]);
        S.policy = j;
        log_msg("INIT   lab resources: Systems=%d Licenses=%d Peripherals=%d", v[0], v[1], v[2]);
    } else if (strcmp(cmd, "add") == 0) {
        if (sscanf(rest, "%31s %d %d %d %d", name, &v[0], &v[1], &v[2], &v[3]) != 5) {
            printf("    usage: add <name> <maxS> <maxL> <maxP> <secs>\n");
            return;
        }
        id = sys_add_student(&S, name, v, v[3]);
        if (id == -2)
            log_msg("REJECT %s: max claim <%d,%d,%d> exceeds lab total", name, v[0], v[1], v[2]);
        else if (id < 0)
            log_msg("REJECT %s: cannot register", name);
        else
            log_msg("ADD    S%d %-8s max claim <%d,%d,%d>, session %ds", id, name, v[0], v[1], v[2], v[3]);
    } else if (strcmp(cmd, "req") == 0) {
        if (!read_ints(rest, v, 4)) {
            printf("    usage: req <id> <s> <l> <p>\n");
            return;
        }
        id = v[0];
        code = sys_request(&S, id, &v[1]);
        fmt_vec(vb, &v[1]);
        if (code == REQ_GRANTED) {
            handle_grant(id, &v[1], NULL);
        } else if (code == REQ_UNAVAIL || code == REQ_UNSAFE) {
            pos = q_push(&S, id, &v[1]);
            fmt_vec(ab, S.avail);
            if (code == REQ_UNAVAIL)
                log_msg("WAIT   S%d %-8s %s exceeds Available=%s -> queued at position %d", id, S.st[id].name, vb, ab, pos + 1);
            else
                log_msg("WAIT   S%d %-8s %s would leave system UNSAFE -> denied for now, queued at position %d", id, S.st[id].name, vb, pos + 1);
        } else if (code == REQ_EXCEEDS) {
            S.denied++;
            log_msg("DENY   S%d %-8s %s exceeds remaining need <%d,%d,%d> (max claim violated)", id, S.st[id].name, vb, S.st[id].need[0], S.st[id].need[1], S.st[id].need[2]);
        } else {
            log_msg("ERROR  invalid request (unknown/finished student or empty request)");
        }
    } else if (strcmp(cmd, "rel") == 0) {
        if (!read_ints(rest, v, 4)) {
            printf("    usage: rel <id> <s> <l> <p>\n");
            return;
        }
        code = sys_release(&S, v[0], &v[1]);
        fmt_vec(vb, &v[1]);
        if (code == 0) {
            fmt_vec(ab, S.avail);
            log_msg("REL    S%d %-8s released %s  Available=%s", v[0], S.st[v[0]].name, vb, ab);
            run_scheduler();
        } else {
            log_msg("ERROR  release %s rejected (more than allocated or invalid id)", vb);
        }
    } else if (strcmp(cmd, "end") == 0) {
        if (!read_ints(rest, v, 1) || !sys_valid(&S, v[0])) {
            log_msg("ERROR  usage: end <active id>");
            return;
        }
        id = v[0];
        if (S.st[id].state == ST_RUNNING) {
            j = proc_stop(S.st[id].pid);
            log_msg("STOP   S%d %-8s pid %d sent SIGTERM, reaped (%s)", id, S.st[id].name, S.st[id].pid, WIFSIGNALED(j) ? "terminated by signal" : "exited");
        }
        sys_finish(&S, id, &v[1]);
        q_drop_student(&S, id);
        S.completed++;
        fmt_vec(vb, &v[1]);
        fmt_vec(ab, S.avail);
        log_msg("EXIT   S%d %-8s session ended by admin, released %s  Available=%s", id, S.st[id].name, vb, ab);
        run_scheduler();
    } else if (strcmp(cmd, "show") == 0) {
        cmd_show();
    } else if (strcmp(cmd, "safe") == 0) {
        cmd_safe();
    } else if (strcmp(cmd, "queue") == 0) {
        cmd_queue();
    } else if (strcmp(cmd, "ps") == 0) {
        cmd_ps();
    } else if (strcmp(cmd, "stats") == 0) {
        cmd_stats();
    } else if (strcmp(cmd, "policy") == 0) {
        if (strncmp(rest, "rr", 2) == 0)
            S.policy = POLICY_RR;
        else if (strncmp(rest, "fcfs", 4) == 0)
            S.policy = POLICY_FCFS;
        else {
            printf("    usage: policy fcfs|rr\n");
            return;
        }
        log_msg("POLICY waiting queue now scheduled by %s", S.policy == POLICY_RR ? "Round Robin" : "FCFS");
        run_scheduler();
    } else if (strcmp(cmd, "sleep") == 0) {
        if (!read_ints(rest, v, 1))
            v[0] = 1;
        pump(v[0] * 1000);
    } else if (strcmp(cmd, "waitall") == 0) {
        log_msg("WAIT   waiting for all sessions and queued requests to complete");
        while (count_running() > 0 || S.qlen > 0) {
            pump(100);
            if (count_running() == 0 && S.qlen > 0) {
                run_scheduler();
                if (count_running() == 0) {
                    log_msg("WARN   queue cannot progress");
                    break;
                }
            }
        }
        log_msg("DONE   all sessions finished, Available=<%d,%d,%d>, zombies=%d", S.avail[0], S.avail[1], S.avail[2], proc_count_zombies());
    } else if (strcmp(cmd, "demo") == 0) {
        if (strncmp(rest, "zombie", 6) == 0)
            proc_demo_zombie();
        else if (strncmp(rest, "orphan", 6) == 0)
            proc_demo_orphan();
        else
            printf("    usage: demo zombie|orphan\n");
    } else if (strcmp(cmd, "help") == 0) {
        cmd_help();
    } else if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
        running_flag = 0;
    } else {
        printf("    unknown command '%s' (type help)\n", cmd);
    }
}

static void find_session_exe(const char *argv0)
{
    char self[512], dir[512];
    ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n > 0) {
        self[n] = 0;
        strcpy(dir, self);
    } else {
        strncpy(dir, argv0, sizeof(dir) - 1);
        dir[sizeof(dir) - 1] = 0;
    }
    snprintf(session_exe, sizeof(session_exe), "%s/lab_session", dirname(dir));
}

static void shutdown_manager(void)
{
    stop_all();
    pump(0);
    log_msg("SHUTDOWN manager exiting, zombie children left: %d", proc_count_zombies());
    log_close();
}

static void run_script(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[512];
    int n;
    if (f == NULL) {
        perror(path);
        return;
    }
    script_mode = 1;
    while (running_flag && fgets(line, sizeof(line), f) != NULL) {
        n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = 0;
        if (n == 0)
            continue;
        if (line[0] == '#') {
            printf("\n%s\n", line);
            continue;
        }
        printf("labmgr> %s\n", line);
        execute(line);
        pump(50);
    }
    fclose(f);
}

static void run_interactive(void)
{
    char line[512];
    fd_set fds;
    struct timeval tv;
    int r, prompt = 1;
    while (running_flag) {
        if (prompt) {
            printf("labmgr> ");
            fflush(stdout);
            prompt = 0;
        }
        FD_ZERO(&fds);
        FD_SET(0, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 200000;
        r = select(1, &fds, NULL, NULL, &tv);
        if (child_flag)
            reap_children();
        if (r > 0 && FD_ISSET(0, &fds)) {
            if (fgets(line, sizeof(line), stdin) == NULL)
                break;
            execute(line);
            prompt = 1;
        }
    }
}

int main(int argc, char *argv[])
{
    find_session_exe(argv[0]);
    proc_setup();
    if (argc > 1 && strcmp(argv[1], "--test") == 0)
        return run_tests(session_exe) == 0 ? 0 : 1;
    log_open("labmgr.log");
    sys_init(&S, 4, 2, 3);
    log_msg("START  Student Lab Process Manager (pid %d), default lab Systems=4 Licenses=2 Peripherals=3", getpid());
    if (argc > 2 && strcmp(argv[1], "-f") == 0) {
        run_script(argv[2]);
    } else {
        printf("    type 'help' for commands\n");
        run_interactive();
    }
    shutdown_manager();
    return 0;
}
