# Student Lab Process Manager

A deadlock-free resource manager for shared computer labs, written in C for Linux.

Students request lab **systems**, **software licences** and **peripherals**. Before anything is granted, the **Banker's Algorithm** checks that the lab stays in a safe state. Requests that can't be granted safely wait in a queue (FCFS or Round Robin), and every granted session runs as a real Linux process that is always cleaned up properly.

## Features

- **Deadlock avoidance:** Banker's safety and resource-request algorithms over Max / Allocation / Need / Available matrices
- **Waiting queue:** FCFS or Round Robin, re-evaluated every time resources are released
- **Real processes:** each session is started with `fork()` + `exec()` and reaped with `waitpid()` on `SIGCHLD`
- **Zombie and orphan handling:** child subreaper (`PR_SET_CHILD_SUBREAPER`) and parent-death signal (`PR_SET_PDEATHSIG`)
- **Interactive CLI and script mode**, with a timestamped event log
- **20 automated tests** and a **performance benchmark**

## Build

Requires Linux and GCC.

```
make
```

This builds `labmgr` (the manager) and `lab_session` (the program run for each granted session).

## Run

```
./labmgr                                   interactive mode
./labmgr -f scenarios/01_basic_lab.txt     run a scenario script
./labmgr --test                            automated test suite
make bench && ./bench                      performance benchmark
```

Every event is also appended to `labmgr.log`.

## Commands

```
init <S> <L> <P>                          reset the lab with total Systems / Licences / Peripherals
add <name> <maxS> <maxL> <maxP> <secs>    register a student with a max claim and session length
req <id> <s> <l> <p>                      request resources (Banker's safety check)
rel <id> <s> <l> <p>                      release part of an allocation
end <id>                                  end a session now (SIGTERM + waitpid)
show | safe | queue | ps | stats          matrices / safe sequence / queue / processes / counters
policy fcfs|rr                            waiting-queue scheduling policy
sleep <secs> | waitall                    let time pass / wait for all sessions
demo zombie | demo orphan                 process lifecycle demonstrations
help | quit
```

## Example

```
labmgr> init 4 2 3
labmgr> add Arun 2 1 1 3
labmgr> add Divya 2 1 2 4
labmgr> add Karthik 3 1 1 2
labmgr> req 0 1 1 0
GRANT  S0 Arun     <1,1,0>  Available=<3,1,3>  safe sequence: S0 -> S1 -> S2
SPAWN  S0 Arun     fork()+exec(lab_session) -> pid 247
labmgr> req 1 1 1 1
GRANT  S1 Divya    <1,1,1>  Available=<2,0,2>  safe sequence: S0 -> S1 -> S2
SPAWN  S1 Divya    fork()+exec(lab_session) -> pid 248
labmgr> req 2 2 0 1
WAIT   S2 Karthik  <2,0,1> would leave system UNSAFE -> denied for now, queued at position 1
```

Karthik's request is free to give, but granting it would leave `<0,0,1>`, which isn't enough for any student to finish, so it waits until a session ends.

## How a request is decided

1. **Request ≤ Need?** If not, it's denied: the student asked for more than their declared maximum.
2. **Request ≤ Available?** If not, it waits in the queue.
3. **Pretend to allocate**, then run the safety algorithm.
4. **Safe** → grant it and start the session process. **Unsafe** → roll back and queue it.
5. When a session ends, its resources are released and the queue is checked again.

## Scenarios

| Script | Shows |
|---|---|
| `01_basic_lab.txt` | Safe grants, an unsafe request, an unavailable request, the queue served on release |
| `02_textbook_bankers.txt` | The classic 5-process Banker's example, matching the textbook's answers |
| `03_fcfs_vs_rr.txt` | The same workload under FCFS and Round Robin |
| `04_process_lifecycle.txt` | fork/exec, early SIGTERM, zombie and orphan demos, clean shutdown |

## Performance

| Measure | Result |
|---|---|
| Safety check, 32 students (worst case) | ~1 µs |
| Average request decision | ~0.09 µs |
| Session start + reap (fork, exec, waitpid) | ~1.6 ms |
| Manager CPU usage | < 0.5% |
| Manager memory | ~2.4 MB |

## Project structure

| File | Purpose |
|---|---|
| `resource.c` | Resource matrices and the Banker's Algorithm |
| `queue.c` | Waiting queue with FCFS and Round Robin |
| `process.c` | fork/exec, SIGCHLD, waitpid, subreaper, PDEATHSIG, demos |
| `logger.c` | Timestamped console and file logging |
| `main.c` | CLI and event loop |
| `tests.c` | Automated test suite |
| `bench.c` | Performance benchmark |
| `lab_session.c` | Program executed for each granted session |
| `scenarios/` | Demo scripts |
