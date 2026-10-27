#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    int left, quiet;
    if (argc < 4) {
        fprintf(stderr, "usage: lab_session <name> <id> <seconds>\n");
        return 2;
    }
    quiet = getenv("LAB_QUIET") != NULL;
    left = atoi(argv[3]);
    if (!quiet) {
        printf("          [session pid %d] S%s %s started, parent %d, runs %ds\n", getpid(), argv[2], argv[1], getppid(), left);
        fflush(stdout);
    }
    while (left > 0)
        left = sleep(left);
    if (!quiet) {
        printf("          [session pid %d] S%s %s finished work, exiting\n", getpid(), argv[2], argv[1]);
        fflush(stdout);
    }
    return 0;
}
