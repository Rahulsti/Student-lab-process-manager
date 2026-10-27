#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include "common.h"

volatile sig_atomic_t child_flag = 0;

static void on_sigchld(int sig)
{
    (void)sig;
    child_flag = 1;
}

void proc_setup(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);
    prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0);
}

void proc_msleep(int ms)
{
    struct timespec req, rem;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&req, &rem) == -1 && errno == EINTR)
        req = rem;
}

pid_t proc_spawn(const char *exe, const char *name, int sid, int duration)
{
    char a1[16], a2[16];
    pid_t parent = getpid();
    pid_t pid;
    snprintf(a1, sizeof(a1), "%d", sid);
    snprintf(a2, sizeof(a2), "%d", duration);
    fflush(stdout);
    pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (getppid() != parent)
            _exit(1);
        execl(exe, "lab_session", name, a1, a2, (char *)NULL);
        perror("execl");
        _exit(127);
    }
    return pid;
}

int proc_reap(pid_t pids[], int status[], int max)
{
    int n = 0, st;
    pid_t pid;
    child_flag = 0;
    while (n < max) {
        pid = waitpid(-1, &st, WNOHANG);
        if (pid <= 0)
            break;
        pids[n] = pid;
        status[n] = st;
        n++;
    }
    return n;
}

int proc_stop(pid_t pid)
{
    int st = 0;
    kill(pid, SIGTERM);
    while (waitpid(pid, &st, 0) == -1 && errno == EINTR)
        ;
    return st;
}

static int read_stat(pid_t pid, char *state, pid_t *ppid)
{
    char path[64], buf[512];
    char *p;
    FILE *f;
    int pp = 0;
    snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);
    f = fopen(path, "r");
    if (f == NULL)
        return 0;
    if (fgets(buf, sizeof(buf), f) == NULL) {
        fclose(f);
        return 0;
    }
    fclose(f);
    p = strrchr(buf, ')');
    if (p == NULL)
        return 0;
    if (sscanf(p + 2, "%c %d", state, &pp) != 2)
        return 0;
    *ppid = pp;
    return 1;
}

char proc_state(pid_t pid)
{
    char c;
    pid_t pp;
    if (!read_stat(pid, &c, &pp))
        return 0;
    return c;
}

pid_t proc_ppid(pid_t pid)
{
    char c;
    pid_t pp;
    if (!read_stat(pid, &c, &pp))
        return -1;
    return pp;
}

int proc_count_zombies(void)
{
    DIR *d;
    struct dirent *e;
    pid_t me = getpid(), pp;
    char c;
    int n = 0, pid;
    d = opendir("/proc");
    if (d == NULL)
        return -1;
    while ((e = readdir(d)) != NULL) {
        pid = atoi(e->d_name);
        if (pid <= 0)
            continue;
        if (read_stat(pid, &c, &pp) && pp == me && c == 'Z')
            n++;
    }
    closedir(d);
    return n;
}

int proc_read_all(int fd, void *buf, int n)
{
    int got = 0, r;
    char *p = (char *)buf;
    while (got < n) {
        r = read(fd, p + got, n - got);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return got;
        got += r;
    }
    return got;
}

void proc_demo_zombie(void)
{
    pid_t pid;
    int st;
    char c;
    fflush(stdout);
    pid = fork();
    if (pid == 0)
        _exit(7);
    log_msg("ZOMBIE DEMO  forked child pid %d, child calls _exit(7) immediately", pid);
    log_msg("ZOMBIE DEMO  parent deliberately delays wait() for 500 ms");
    proc_msleep(500);
    c = proc_state(pid);
    log_msg("ZOMBIE DEMO  /proc/%d/stat state = '%c'  (zombie children of manager: %d)", pid, c ? c : '-', proc_count_zombies());
    waitpid(pid, &st, 0);
    c = proc_state(pid);
    log_msg("ZOMBIE DEMO  waitpid() collected exit status %d", WIFEXITED(st) ? WEXITSTATUS(st) : -1);
    log_msg("ZOMBIE DEMO  after reaping: /proc/%d %s  (zombie children of manager: %d)", pid, c ? "still exists" : "is gone", proc_count_zombies());
}

void proc_demo_orphan(void)
{
    int fd[2];
    int info[3];
    pid_t mid, g;
    int st;
    if (pipe(fd) < 0) {
        log_msg("ORPHAN DEMO  pipe failed");
        return;
    }
    fflush(stdout);
    mid = fork();
    if (mid == 0) {
        close(fd[0]);
        g = fork();
        if (g == 0) {
            info[0] = getpid();
            info[1] = getppid();
            if (write(fd[1], info, 2 * sizeof(int)) < 0)
                _exit(1);
            proc_msleep(800);
            info[2] = getppid();
            if (write(fd[1], &info[2], sizeof(int)) < 0)
                _exit(1);
            close(fd[1]);
            _exit(0);
        }
        proc_msleep(300);
        _exit(0);
    }
    close(fd[1]);
    proc_read_all(fd[0], info, 2 * sizeof(int));
    log_msg("ORPHAN DEMO  manager %d -> intermediate %d -> grandchild %d", getpid(), mid, info[0]);
    log_msg("ORPHAN DEMO  grandchild's parent before: %d", info[1]);
    waitpid(mid, &st, 0);
    log_msg("ORPHAN DEMO  intermediate %d exited, grandchild is now an orphan", mid);
    proc_read_all(fd[0], &info[2], sizeof(int));
    close(fd[0]);
    log_msg("ORPHAN DEMO  grandchild's parent after : %d %s", info[2], info[2] == getpid() ? "(adopted by manager: PR_SET_CHILD_SUBREAPER)" : "(adopted by init)");
    waitpid(info[0], &st, 0);
    log_msg("ORPHAN DEMO  manager reaped orphan %d, zombie children of manager: %d", info[0], proc_count_zombies());
}
