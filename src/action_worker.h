#ifndef ACTION_WORKER_H
#define ACTION_WORKER_H
struct action_worker;
struct action_worker *aw_new(void);
/* Start rejects pending completion or an unreaped child, including after timeout.
   The monitor exclusively owns waitpid for its child; callers must not reap it. */
int aw_start(struct action_worker *w, const char *const argv[], int timeout_s);
/* False after timeout even while an unreaped child still prevents starting. */
int aw_busy(const struct action_worker *w);
/* Ends a running child now, as the timeout would: the next poll reports it
   as stopped (-1). Nothing happens when no child runs. */
void aw_cancel(struct action_worker *w);
int aw_poll(struct action_worker *w, int *exit_status);
/* Invalidates w immediately; cleanup is deferred until any child can be reaped.
   As with other destructors, callers must stop using w before calling this. */
void aw_free(struct action_worker *w);
#endif
