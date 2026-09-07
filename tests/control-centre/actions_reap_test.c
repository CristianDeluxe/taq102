// SOURCES:
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t gate_lock = PTHREAD_MUTEX_INITIALIZER;
static int hold_reap = 1, reaped, freed;

static pid_t controlled_waitpid(pid_t pid, int *status, int options) {
    assert(options == WNOHANG); /* A wedged child must never trigger blocking reap. */
    pthread_mutex_lock(&gate_lock);
    int held = hold_reap;
    pthread_mutex_unlock(&gate_lock);
    if (held) return 0;
    pid_t result = waitpid(pid, status, options);
    if (result == pid) {
        pthread_mutex_lock(&gate_lock); ++reaped; pthread_mutex_unlock(&gate_lock);
    }
    return result;
}

static void tracked_free(void *ptr) {
    free(ptr);
    pthread_mutex_lock(&gate_lock); ++freed; pthread_mutex_unlock(&gate_lock);
}

/* Interpose only the reaping/resource boundary; run real fork, signals and timing. */
#define waitpid controlled_waitpid
#define free tracked_free
#include "../../src/action_worker.c"
#undef free
#undef waitpid

static void set_hold(int value) {
    pthread_mutex_lock(&gate_lock); hold_reap = value; pthread_mutex_unlock(&gate_lock);
}

static void await_count(int *value, int expected) {
    int seen = 0;
    for (int i = 0; i < 300; ++i) {
        pthread_mutex_lock(&gate_lock); seen = *value; pthread_mutex_unlock(&gate_lock);
        if (seen >= expected) break;
        usleep(10000);
    }
    assert(seen == expected);
}

static void await_result(struct action_worker *w, int expected) {
    int done = 0, status = 999;
    for (int i = 0; i < 300 && !done; ++i) {
        done = aw_poll(w, &status);
        if (!done) usleep(10000);
    }
    assert(done && status == expected && !aw_busy(w));
    assert(aw_poll(w, &status) == 0);
}

int main(void) {
    const char *ok[] = { "/bin/sh", "-c", "exit 0", NULL };
    struct action_worker *w = aw_new(); assert(w);
    assert(aw_start(w, ok, 1) == 0);
    await_result(w, -1); /* Publication must not depend on reapability. */
    assert(aw_start(w, ok, 5) == -1); /* No overlap with the unreaped child. */
    set_hold(0); await_count(&reaped, 1);
    int started = -1;
    for (int i = 0; i < 300 && started; ++i) {
        started = aw_start(w, ok, 5);
        if (started) usleep(10000);
    }
    assert(started == 0); await_result(w, 0);
    aw_free(w); await_count(&freed, 1);

    set_hold(1);
    w = aw_new(); assert(w);
    assert(aw_start(w, ok, 1) == 0); await_result(w, -1);
    int64_t before = monotonic_ms();
    aw_free(w); /* The detached monitor retains ownership until eventual reap. */
    assert(monotonic_ms() - before < 500);
    pthread_mutex_lock(&gate_lock); assert(freed == 1); pthread_mutex_unlock(&gate_lock);
    set_hold(0); await_count(&reaped, 3); await_count(&freed, 2);
    puts("actions deferred reap ok");
    return 0;
}
