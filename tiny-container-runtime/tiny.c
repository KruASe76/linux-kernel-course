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


#define CONTAINER_ROOT "/container_rootfs"
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

    printf("inside container\n");
    printf("pid: %d\n", getpid());

    CHECK(sethostname("tiny-box", 8), "sethostname");
    setup_fs();

    char *envp[] = { "PATH=/bin", NULL };
    CHECK(execvpe(config->argv[0], config->argv, envp), "execvpe");

    return 0;
}


int command_run(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s run <command>\n", argv[0]);
        return EXIT_FAILURE;
    }

    printf("booting up a container...\n");
    struct container_config config = { .argv = &argv[2] };

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

int command_exec(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s exec <host_pid> <command>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *pid_str = argv[2];
    char path[256];

    const char *namespaces[] = {"cgroup", "ipc", "uts", "net", "pid", "mnt", NULL};

    printf("jumping into container %s...\n", pid_str);

    for (int i = 0; namespaces[i] != NULL; i++) {
        snprintf(path, sizeof(path), "/proc/%s/ns/%s", pid_str, namespaces[i]);
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            CHECK(setns(fd, 0), "setns");
            close(fd);
        }
    }

    pid_t pid = fork();
    CHECK(pid, "fork");

    if (pid == 0) {
        char root_path[256];
        snprintf(root_path, sizeof(root_path), "/proc/%s/root", pid_str);

        CHECK(chroot(root_path), "chroot into container root");
        CHECK(chdir("/"), "chdir into /");

        char *envp[] = { "PATH=/bin", NULL };
        CHECK(execvpe(argv[3], &argv[3], envp), "execvpe");
    }

    waitpid(pid, NULL, 0);
    return EXIT_SUCCESS;
}

int command_pod(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s pod <host_pid> <command>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *pid_str = argv[2];
    char path[256];

    const char *shared_namespaces[] = {"uts", "ipc", "net", NULL};

    printf("joining pod environment of %s...\n", pid_str);

    for (int i = 0; shared_namespaces[i] != NULL; i++) {
        snprintf(path, sizeof(path), "/proc/%s/ns/%s", pid_str, shared_namespaces[i]);
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            CHECK(setns(fd, 0), "setns pod");
            close(fd);
        }
    }

    printf("booting up a new container inside the pod...\n");
    struct container_config config = { .argv = &argv[3] };

    char *stack = malloc(STACK_SIZE);
    if (!stack) {
        perror("malloc");
        return EXIT_FAILURE;
    }

    int clone_flags = CLONE_NEWPID | CLONE_NEWNS | SIGCHLD;
    pid_t child_pid = clone(container_entrypoint, stack + STACK_SIZE, clone_flags, &config);
    CHECK(child_pid, "clone");

    printf("pod container created, host pid: %d\n", child_pid);

    setup_cgroups(child_pid);

    waitpid(child_pid, NULL, 0);
    printf("pod container stopped\n");

    free(stack);
    return EXIT_SUCCESS;
}


int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <run|exec|pod> <args...>\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "run") == 0) {
        return command_run(argc, argv);
    } else if (strcmp(argv[1], "exec") == 0) {
        return command_exec(argc, argv);
    } else if (strcmp(argv[1], "pod") == 0) {
        return command_pod(argc, argv);
    } else {
        fprintf(stderr, "Unknown command: %s\n", argv[1]);
        return EXIT_FAILURE;
    }
}
