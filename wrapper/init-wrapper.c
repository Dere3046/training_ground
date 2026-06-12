#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>

int main(int argc, char *argv[], char *envp[])
{
    int fd = open("/smc_monitor.ko", O_RDONLY);
    if (fd >= 0) {
        syscall(__NR_finit_module, fd, "", 0);
        close(fd);
    }

    execve("/init.ksu", argv, envp);
    execve("/init.real", argv, envp);
    return 1;
}
