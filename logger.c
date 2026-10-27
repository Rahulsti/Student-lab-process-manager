#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include "common.h"

static FILE *logfile = NULL;
static struct timespec t0;
static int quiet = 0;
static int started = 0;

void log_open(const char *path)
{
    clock_gettime(CLOCK_MONOTONIC, &t0);
    started = 1;
    logfile = fopen(path, "a");
    if (logfile != NULL)
        fprintf(logfile, "---- session log start ----\n");
}

void log_close(void)
{
    if (logfile != NULL) {
        fprintf(logfile, "---- session log end ----\n");
        fclose(logfile);
    }
    logfile = NULL;
}

void log_set_quiet(int q)
{
    quiet = q;
}

void log_msg(const char *fmt, ...)
{
    struct timespec now;
    double t = 0.0;
    char buf[1024];
    va_list ap;
    if (quiet)
        return;
    if (!started) {
        clock_gettime(CLOCK_MONOTONIC, &t0);
        started = 1;
    }
    clock_gettime(CLOCK_MONOTONIC, &now);
    t = (now.tv_sec - t0.tv_sec) + (now.tv_nsec - t0.tv_nsec) / 1e9;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("[%7.3f] %s\n", t, buf);
    fflush(stdout);
    if (logfile != NULL) {
        fprintf(logfile, "[%7.3f] %s\n", t, buf);
        fflush(logfile);
    }
}
