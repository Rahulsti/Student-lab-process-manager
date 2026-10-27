#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include "common.h"

static System S;
static char exe[512];

static double now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

static void bench_safety(void)
{
    int sizes[8] = {8, 16, 32, 64, 128, 256, 512, 1024};
    int k, i, j, n, reps, r, seq[MAXS], len, ok = 1;
    int m[NRES];
    double t0, worst, best;
    FILE *f = fopen("bench_safety.csv", "w");
    printf("\n[1] Safety algorithm cost vs number of students (m = 3 resource types)\n");
    printf("    %6s %14s %14s %10s\n", "n", "best (us)", "worst (us)", "worst/n^2 (ns)");
    fprintf(f, "n,best_us,worst_us\n");
    for (k = 0; k < 8; k++) {
        n = sizes[k];
        sys_init(&S, n + 1, n + 1, n + 1);
        for (i = 0; i < n; i++) {
            for (j = 0; j < NRES; j++)
                m[j] = 1 + n - i;
            sys_add_student(&S, "w", m, 0);
            for (j = 0; j < NRES; j++) {
                S.st[i].alloc[j] = 1;
                S.st[i].need[j] = n - i;
            }
        }
        for (j = 0; j < NRES; j++)
            S.avail[j] = 1;
        reps = 20000000 / (n * n);
        if (reps < 20)
            reps = 20;
        t0 = now_us();
        for (r = 0; r < reps; r++)
            ok &= sys_is_safe(&S, seq, &len);
        worst = (now_us() - t0) / reps;
        for (i = 0; i < n; i++)
            for (j = 0; j < NRES; j++)
                S.st[i].need[j] = 0;
        reps = reps * 4;
        t0 = now_us();
        for (r = 0; r < reps; r++)
            ok &= sys_is_safe(&S, seq, &len);
        best = (now_us() - t0) / reps;
        printf("    %6d %14.3f %14.3f %10.3f\n", n, best, worst, worst * 1000.0 / ((double)n * n));
        fprintf(f, "%d,%.4f,%.4f\n", n, best, worst);
    }
    fclose(f);
    printf("    all states verified safe: %s\n", ok ? "yes" : "NO");
}

static void bench_request(void)
{
    int i, j, sid, code, r[NRES], m[NRES], seq[MAXS], len, k;
    long calls = 0, granted = 0, unsafe = 0, unavail = 0;
    double total = 0.0, t0, t;
    double maxlat = 0.0;
    printf("\n[2] Request decision latency (n = 32 students, 1,000,000 random operations)\n");
    srand(7);
    sys_init(&S, 12, 8, 10);
    for (i = 0; i < 32; i++) {
        for (j = 0; j < NRES; j++)
            m[j] = rand() % (S.total[j] / 2 + 1);
        sys_add_student(&S, "r", m, 0);
    }
    for (k = 0; k < 1000000; k++) {
        sid = rand() % S.nst;
        if (rand() % 3 == 0) {
            for (j = 0; j < NRES; j++)
                r[j] = S.st[sid].alloc[j] ? rand() % (S.st[sid].alloc[j] + 1) : 0;
            sys_release(&S, sid, r);
            continue;
        }
        for (j = 0; j < NRES; j++)
            r[j] = S.st[sid].need[j] ? rand() % (S.st[sid].need[j] + 1) : 0;
        t0 = now_us();
        code = sys_request(&S, sid, r);
        t = now_us() - t0;
        if (code == REQ_INVALID)
            continue;
        total += t;
        if (t > maxlat)
            maxlat = t;
        calls++;
        if (code == REQ_GRANTED)
            granted++;
        if (code == REQ_UNSAFE)
            unsafe++;
        if (code == REQ_UNAVAIL)
            unavail++;
    }
    printf("    decisions timed      : %ld\n", calls);
    printf("    average latency      : %.3f us\n", total / calls);
    printf("    worst observed       : %.3f us\n", maxlat);
    printf("    throughput           : %.0f decisions/s\n", calls / (total / 1e6));
    printf("    granted / unavailable / unsafe-blocked : %ld / %ld / %ld\n", granted, unavail, unsafe);
    printf("    final state safe and consistent        : %s\n", (sys_is_safe(&S, seq, &len) && sys_consistent(&S)) ? "yes" : "NO");
}

static void bench_process(void)
{
    int i, st_, n = 300;
    pid_t pid;
    double t0, fe, fo;
    printf("\n[3] Process lifecycle cost (%d sessions each)\n", n);
    setenv("LAB_QUIET", "1", 1);
    t0 = now_us();
    for (i = 0; i < n; i++) {
        pid = proc_spawn(exe, "B", 0, 0);
        waitpid(pid, &st_, 0);
    }
    fe = (now_us() - t0) / n;
    t0 = now_us();
    for (i = 0; i < n; i++) {
        pid = fork();
        if (pid == 0)
            _exit(0);
        waitpid(pid, &st_, 0);
    }
    fo = (now_us() - t0) / n;
    printf("    fork + exec(lab_session) + exit + waitpid : %8.1f us  (%.0f sessions/s)\n", fe, 1e6 / fe);
    printf("    fork + exit + waitpid (no exec)           : %8.1f us\n", fo);
    printf("    zombie children after benchmark           : %d\n", proc_count_zombies());
}

static void bench_memory(void)
{
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    printf("\n[4] Memory\n");
    printf("    sizeof(System) in this build (MAXS=%d)  : %zu bytes\n", MAXS, sizeof(System));
    printf("    peak resident memory of bench          : %ld KB\n", ru.ru_maxrss);
}

int main(int argc, char *argv[])
{
    char self[512];
    ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    (void)argc;
    if (n > 0) {
        self[n] = 0;
        snprintf(exe, sizeof(exe), "%s/lab_session", dirname(self));
    } else {
        snprintf(exe, sizeof(exe), "%s", argv[0]);
    }
    log_set_quiet(1);
    proc_setup();
    printf("Student Lab Process Manager - performance benchmark\n");
    bench_safety();
    bench_request();
    bench_process();
    bench_memory();
    return 0;
}
