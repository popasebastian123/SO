#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>

static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t got_usr1 = 0;

static void handle_sigint(int sig) {
    (void)sig;
    running = 0;
}

static void handle_sigusr1(int sig) {
    (void)sig;
    got_usr1 = 1;
}

static void write_pid_file(void) {
    int fd;
    char buf[64];
    int len;

    fd = open(".monitor_pid", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd == -1) {
        perror("open .monitor_pid");
        exit(1);
    }

    len = snprintf(buf, sizeof(buf), "%ld\n", (long)getpid());

    if (write(fd, buf, len) != len) {
        perror("write .monitor_pid");
        close(fd);
        exit(1);
    }

    close(fd);
}

int main(void) {
#ifndef _WIN32
    struct sigaction sa_int;
    struct sigaction sa_usr1;

    write_pid_file();

    memset(&sa_int, 0, sizeof(sa_int));
    sa_int.sa_handler = handle_sigint;
    sigemptyset(&sa_int.sa_mask);

    if (sigaction(SIGINT, &sa_int, NULL) == -1) {
        perror("sigaction SIGINT");
        unlink(".monitor_pid");
        return 1;
    }

    memset(&sa_usr1, 0, sizeof(sa_usr1));
    sa_usr1.sa_handler = handle_sigusr1;
    sigemptyset(&sa_usr1.sa_mask);

    if (sigaction(SIGUSR1, &sa_usr1, NULL) == -1) {
        perror("sigaction SIGUSR1");
        unlink(".monitor_pid");
        return 1;
    }

    printf("monitor_reports started. pid=%ld\n", (long)getpid());
    fflush(stdout);

    while (running) {
        pause();

        if (got_usr1) {
            got_usr1 = 0;
            printf("monitor_reports: new report notification received through SIGUSR1\n");
            fflush(stdout);
        }
    }

    printf("monitor_reports: received SIGINT, shutting down\n");
    fflush(stdout);

    unlink(".monitor_pid");
    return 0;
#else
    printf("monitor_reports needs linux/wsl for sigaction and SIGUSR1\n");
    return 1;
#endif
}