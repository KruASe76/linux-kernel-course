#define _GNU_SOURCE
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>


#define CONTAINER_ROOT "./container_rootfs"
#define STACK_SIZE (1024 * 1024) // i guess 1mb would be enough

// a handy macros
#define CHECK(cmd, msg) if ((cmd) < 0) { perror(msg); exit(EXIT_FAILURE); }


struct container_config {
    char **argv;
};


void setup_cgroups(pid_t child_pid) {
    const char *cgroup_dir = "/sys/fs/cgroup/tiny_container";

    mkdir(cgroup_dir, 0755); // ignoring if exists

    char path[256];
    char value[64];
    int fd;

    // 50 mb RAM
    snprintf(path, sizeof(path), "%s/memory.max", cgroup_dir);
    fd = open(path, O_WRONLY);
    if (fd >= 0) {
        write(fd, "50000000", 8);
        close(fd);
    }

    // cpu: 50% of one core
    snprintf(path, sizeof(path), "%s/cpu.max", cgroup_dir);
    fd = open(path, O_WRONLY);
    if (fd >= 0) {
        write(fd, "50000 100000", 12);
        close(fd);
    }

    // pid into cgroup
    snprintf(path, sizeof(path), "%s/cgroup.procs", cgroup_dir);
    fd = open(path, O_WRONLY);
    if (fd >= 0) {
        snprintf(value, sizeof(value), "%d", child_pid);
        write(fd, value, strlen(value));
        close(fd);
    }
}

void setup_fs() {
    CHECK(mount(NULL, "/", NULL, MS_PRIVATE | MS_REC, NULL), "mount private");

    CHECK(mount(CONTAINER_ROOT, CONTAINER_ROOT, "bind", MS_BIND | MS_REC, NULL), "mount bind rootfs");

    CHECK(chroot(CONTAINER_ROOT), "chroot");
    CHECK(chdir("/"), "chdir");

    mkdir("/proc", 0755);
    CHECK(mount("proc", "/proc", "proc", 0, NULL), "mount proc");
}


int container_entrypoint(void *arg) {
    struct container_config *config = arg;

    printf("inside container");
    printf("pid: %d\n", getpid());

    CHECK(sethostname("tiny-box", 8), "sethostname");
    setup_fs();

    char *envp[] = { "PATH=/bin", NULL };
    CHECK(execvpe(config->argv[0], config->argv, envp), "execvpe");

    return 0; // never landing here if execvpe succeeds
}


int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <command>\n", argv[0]);
        return EXIT_FAILURE;
    }

    printf("booting up a container...\n");

    struct container_config config = { .argv = &argv[1] };

    char *stack = malloc(STACK_SIZE);
    if (!stack) {
        perror("malloc");
        return EXIT_FAILURE;
    }

    int clone_flags = CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNS | SIGCHLD;

    pid_t child_pid = clone(container_entrypoint, stack + STACK_SIZE, clone_flags, &config);
    CHECK(child_pid, "clone");

    printf("container created, host pid: %d\n", child_pid);

    setup_cgroups(child_pid);

    waitpid(child_pid, NULL, 0);
    printf("container stopped\n");

    free(stack);
    return EXIT_SUCCESS;
}
