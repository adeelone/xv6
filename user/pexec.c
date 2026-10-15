#include "kernel/param.h"
#include "kernel/types.h"
#include "user/user.h"
#define MAXARGS 16

// pexec <priority> <command> [args...]
// Runs command at the given priority. pexec sets its own priority
// first, and the forked child inherits it.
int
main(int argc, char *argv[])
{
    int i, rc;
    char *newargv[MAXARGS];
    int priority;

    if(argc < 3){
        printf("Usage: pexec <priority> <command> [args...]\n");
        exit(-1);
    }

    // atoi() reads "-1" or "abc" as 0, so check the text first.
    int ok = (argv[1][0] != 0), k;
    for(k=0; argv[1][k] != 0; k++)
        if(argv[1][k] < '0' || argv[1][k] > '9')
            ok = 0;
    if(k > 2)
        ok = 0;
    priority = atoi(argv[1]);
    if(!ok || setpriority(priority) < 0){
        printf("pexec: priority must be 0..%d\n", MAXPRIORITY);
        exit(-1);
    }
    for(i=2; i<argc && i-2 < MAXARGS-1; i++)
        newargv[i-2] = argv[i];
    newargv[i-2] = 0;

    rc = fork();
    if (rc == 0) {
        exec(newargv[0], newargv);
        printf("pexec: exec %s failed\n", newargv[0]);
        exit(-1);
    }
    wait(0);
    exit(0);
}
