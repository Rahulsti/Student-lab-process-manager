#ifndef COMMON_H
#define COMMON_H

#include <sys/types.h>
#include <signal.h>

#define NRES 3
#ifndef MAXS
#define MAXS 32
#endif
#define MAXQ 64
#define NAMELEN 32

#define ST_IDLE 0
#define ST_RUNNING 1
#define ST_DONE 2

#define POLICY_FCFS 0
#define POLICY_RR 1

#define REQ_GRANTED 0
#define REQ_EXCEEDS 1
#define REQ_UNAVAIL 2
#define REQ_UNSAFE 3
#define REQ_INVALID 4

typedef struct {
    char name[NAMELEN];
    int max[NRES];
    int alloc[NRES];
    int need[NRES];
    int duration;
    int state;
    pid_t pid;
} Student;

typedef struct {
    int sid;
    int req[NRES];
    int ticket;
    int attempts;
    int avail_after[NRES];
    int seq[MAXS];
    int seqlen;
} Request;

typedef struct {
    int total[NRES];
    int avail[NRES];
    Student st[MAXS];
    int nst;
    Request q[MAXQ];
    int qlen;
    int policy;
    int next_ticket;
    int granted;
    int queued;
    int denied;
    int completed;
    int safety_checks;
    int unsafe_blocked;
    int max_qlen;
} System;

extern const char *RES_NAMES[NRES];
extern volatile sig_atomic_t child_flag;

void sys_init(System *s, int a, int b, int c);
int sys_add_student(System *s, const char *name, int max[], int duration);
int sys_valid(System *s, int sid);
int sys_is_safe(System *s, int seq[], int *len);
int sys_request(System *s, int sid, int req[]);
int sys_release(System *s, int sid, int rel[]);
void sys_finish(System *s, int sid, int freed[]);
int sys_consistent(System *s);
const char *req_code_name(int code);

int q_push(System *s, int sid, int req[]);
void q_remove(System *s, int idx);
int q_drop_student(System *s, int sid);
int q_process(System *s, Request out[]);

void proc_setup(void);
pid_t proc_spawn(const char *exe, const char *name, int sid, int duration);
int proc_reap(pid_t pids[], int status[], int max);
int proc_stop(pid_t pid);
char proc_state(pid_t pid);
pid_t proc_ppid(pid_t pid);
int proc_count_zombies(void);
void proc_msleep(int ms);
int proc_read_all(int fd, void *buf, int n);
void proc_demo_zombie(void);
void proc_demo_orphan(void);

void log_open(const char *path);
void log_close(void);
void log_set_quiet(int q);
void log_msg(const char *fmt, ...);

int run_tests(const char *exe);

#endif
