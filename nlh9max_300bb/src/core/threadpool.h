#ifndef ICMCORE_THREADPOOL_H
#define ICMCORE_THREADPOOL_H

/* Internal: a small fork-join pool on pthreads. Not part of the public API. */

typedef void (*tp_task)(void *ctx, int index, int worker);

typedef struct tp_pool tp_pool;

/* Creates a pool with nthreads workers in total (the calling thread counts as worker 0). */
tp_pool *tp_create(int nthreads);
void tp_destroy(tp_pool *pool);
int tp_threads(const tp_pool *pool);
/* Runs fn(ctx, i, worker) for i in [0, n) and returns when all calls have finished. */
void tp_parallel_for(tp_pool *pool, int n, tp_task fn, void *ctx);

#endif
