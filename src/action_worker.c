#include "action_worker.h"
#include <errno.h>
#include <dirent.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

struct action_worker {
    pthread_mutex_t lock;
    pthread_cond_t wake;
    pid_t pid;
    int busy, pending, result, stopped, released;
    int64_t deadline;
};

static int64_t monotonic_ms(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return -1;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

/* The lock serializes signals with waitpid: a reaped PID is never signalled. */
static void stop_child(struct action_worker *w) {
    if (!w->pid || w->stopped) return;
    kill(-w->pid, SIGKILL);
    kill(w->pid, SIGKILL);
    w->stopped = 1;
    w->busy = 0;
    w->result = -1;
    w->pending = 1;
}

static void *monitor(void *arg) {
    struct action_worker *w = arg;
    for (;;) {
        pthread_mutex_lock(&w->lock);
        while (!w->pid && !w->released) pthread_cond_wait(&w->wake, &w->lock);
        if (!w->pid && w->released) {
            pthread_mutex_unlock(&w->lock);
            pthread_cond_destroy(&w->wake);
            pthread_mutex_destroy(&w->lock);
            free(w);
            return NULL;
        }
        int64_t now = monotonic_ms();
        if (w->released || now < 0 || now >= w->deadline) stop_child(w);
        int status = 0;
        pid_t got = waitpid(w->pid, &status, WNOHANG);
        if (got == w->pid || (got < 0 && errno == ECHILD)) {
            if (!w->stopped) {
                w->result = got < 0 ? -1 : WIFEXITED(status) ? WEXITSTATUS(status) :
                    WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1;
                w->pending = 1;
                w->busy = 0;
            }
            w->pid = 0;
        }
        pthread_mutex_unlock(&w->lock);
        /* SIGKILL may remain pending in uninterruptible driver work. Retain
           ownership, retry nonblocking reap, and leave the UI free to proceed. */
        struct timespec delay = {0, 10000000};
        nanosleep(&delay, NULL);
    }
}

struct action_worker *aw_new(void) {
    struct action_worker *w = calloc(1, sizeof *w);
    if (!w) return NULL;
    if (pthread_mutex_init(&w->lock, NULL) != 0) { free(w); return NULL; }
    if (pthread_cond_init(&w->wake, NULL) != 0) {
        pthread_mutex_destroy(&w->lock); free(w); return NULL;
    }
    pthread_attr_t attr;
    int initialized = pthread_attr_init(&attr) == 0;
    pthread_t thread;
    int error = !initialized || pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0;
    if (!error) error = pthread_create(&thread, &attr, monitor, w);
    if (initialized) pthread_attr_destroy(&attr);
    if (error) {
        pthread_cond_destroy(&w->wake); pthread_mutex_destroy(&w->lock); free(w); return NULL;
    }
    /* Create the monitor before any child: thread failure cannot orphan a PID. */
    return w;
}

int aw_start(struct action_worker *w, const char *const argv[], int timeout_s) {
    if (!w || !argv || !argv[0] || !argv[0][0] || timeout_s <= 0) return -1;
    pthread_mutex_lock(&w->lock);
    if (w->pid || w->pending) {
        pthread_mutex_unlock(&w->lock); return -1;
    }
    struct rlimit limit;
    int64_t now = monotonic_ms();
    if (getrlimit(RLIMIT_NOFILE, &limit) != 0 || now < 0) {
        pthread_mutex_unlock(&w->lock); return -1;
    }
    /* macOS reports an infinite hard limit. Include live descriptors above the
       soft limit too, since the limit can be lowered after opening a file. */
    rlim_t bound = limit.rlim_max;
    if (bound > INT_MAX) bound = limit.rlim_cur;
    if (bound > INT_MAX) {
        long configured = sysconf(_SC_OPEN_MAX);
        if (configured <= 0 || configured > INT_MAX) {
            pthread_mutex_unlock(&w->lock); return -1;
        }
        bound = (rlim_t)configured;
    }
    int maxfd = (int)bound;
#ifdef __linux__
    DIR *fds = opendir("/proc/self/fd");
#else
    DIR *fds = opendir("/dev/fd");
#endif
    if (!fds) { pthread_mutex_unlock(&w->lock); return -1; }
    struct dirent *entry;
    while ((entry = readdir(fds)) != NULL) {
        char *end;
        long fd = strtol(entry->d_name, &end, 10);
        if (*entry->d_name && !*end && fd >= maxfd && fd < INT_MAX) maxfd = (int)fd + 1;
    }
    closedir(fds);
    w->deadline = now + (int64_t)timeout_s * 1000;
    pid_t pid = fork();
    if (pid == 0) {
        if (setpgid(0, 0) != 0) _exit(126);
        for (int fd = 3; fd < maxfd; ++fd) close(fd);
        execv(argv[0], (char *const *)argv);
        _exit(127);
    }
    if (pid < 0) { pthread_mutex_unlock(&w->lock); return -1; }
    /* Both sides set the group to avoid a scheduling race at the deadline. */
    (void)setpgid(pid, pid);
    w->pid = pid; w->busy = 1; w->stopped = 0;
    pthread_cond_signal(&w->wake);
    pthread_mutex_unlock(&w->lock);
    return 0;
}

int aw_busy(const struct action_worker *worker) {
    struct action_worker *w = (struct action_worker *)worker;
    if (!w) return 0;
    pthread_mutex_lock(&w->lock);
    int busy = w->busy;
    pthread_mutex_unlock(&w->lock);
    return busy;
}

int aw_poll(struct action_worker *w, int *exit_status) {
    if (!w || !exit_status) return 0;
    pthread_mutex_lock(&w->lock);
    if (!w->pending) { pthread_mutex_unlock(&w->lock); return 0; }
    *exit_status = w->result;
    w->pending = 0;
    pthread_mutex_unlock(&w->lock);
    return 1;
}

void aw_free(struct action_worker *w) {
    if (!w) return;
    pthread_mutex_lock(&w->lock);
    stop_child(w);
    w->released = 1;
    pthread_cond_signal(&w->wake);
    pthread_mutex_unlock(&w->lock);
    /* The detached monitor frees this object after the last child is reaped. */
}
