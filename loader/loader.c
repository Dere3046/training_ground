#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/stat.h>

#define MODULE_INIT_IGNORE_MODVERSIONS 1
#define MODULE_INIT_IGNORE_VERMAGIC    2
#define MODULE_INIT_COMPRESSED_FILE    4

static void die(const char *s)
{
    perror(s);
    _exit(1);
}

static void run(const char *cmd)
{
    char buf[1024];
    FILE *f = popen(cmd, "r");
    if (!f) return;
    while (fgets(buf, sizeof(buf), f))
        fputs(buf, stdout);
    pclose(f);
}

static void cmd_insmod(const char *path, int flags)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) die("open");

    run("dmesg -c > /dev/null 2>&1");

    long rc = syscall(__NR_finit_module, fd, "", flags);
    close(fd);
    if (rc != 0) die("finit_module");

    printf("loaded: %s\n", path);
    printf("--- dmesg ---\n");
    run("dmesg | grep smc_monitor 2>/dev/null");
    printf("-------------\n");
}

static void cmd_rmmod(const char *name)
{
    run("dmesg -c > /dev/null 2>&1");

    long rc = syscall(__NR_delete_module, name, 0);
    if (rc != 0) die("delete_module");

    printf("unloaded: %s\n", name);
    printf("--- dmesg ---\n");
    run("dmesg | grep smc_monitor 2>/dev/null");
    printf("-------------\n");
}

static void cmd_status(void)
{
    printf("--- lsmod ---\n");
    run("lsmod 2>/dev/null | grep smc || echo '(not loaded)'");
    printf("--- dmesg (last 20) ---\n");
    run("dmesg 2>/dev/null | grep smc_monitor | tail -20");
}

#ifndef VERSION
#define VERSION 0
#endif

#define STR_(x) #x
#define STR(x)  STR_(x)

static void usage(void)
{
    fprintf(stderr,
        "loader r" STR(VERSION) "\n"
        "usage: loader <command> [args...]\n"
        "  insmod <path> [--force]   load kernel module\n"
        "  rmmod  <name>             unload kernel module\n"
        "  status                    show module info\n");
}

int main(int argc, char *argv[])
{
    if (argc < 2) { usage(); return 1; }

    const char *cmd = argv[1];

    if (!strcmp(cmd, "insmod")) {
        if (argc < 3) { usage(); return 1; }
        int flags = 0;
        for (int i = 3; i < argc; i++)
            if (!strcmp(argv[i], "--force"))
                flags |= MODULE_INIT_IGNORE_MODVERSIONS
                       | MODULE_INIT_IGNORE_VERMAGIC;
        cmd_insmod(argv[2], flags);
    } else if (!strcmp(cmd, "rmmod")) {
        if (argc < 3) { usage(); return 1; }
        cmd_rmmod(argv[2]);
    } else if (!strcmp(cmd, "status")) {
        cmd_status();
    } else {
        usage();
        return 1;
    }

    return 0;
}
