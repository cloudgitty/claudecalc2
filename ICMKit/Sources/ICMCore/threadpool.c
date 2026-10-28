#include "threadpool.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>

struct tp_pool {
    int nthreads;
    pthread_t *threads;
    pthread_mutex_t lock;
    pthread_cond_t wake;
    pthread_cond_t done;
    unsigned generation;
    int busy;              /* helper threads still working on the current job */
    int quit;
    tp_task fn;
    void *ctx;
    int n;
    atomic_int next;
};

typedef struct { tp_pool *pool; int worker; } tp_arg;

static void run_items(tp_pool *p, int worker)
{
    for (;;) {
        int i = atomic_fetch_add(&p->next, 1);
        if (i >= p->n) break;
        p->fn(p->ctx, i, worker);
    }
}

static void *worker_main(void *arg)
{
    tp_arg a = *(tp_arg *)arg;
    free(arg);
    tp_pool *p = a.pool;
    unsigned seen = 0;
    pthread_mutex_lock(&p->lock);
    for (;;) {
        while (!p->quit && p->generation == seen) pthread_cond_wait(&p->wake, &p->lock);
        if (p->quit) break;
        seen = p->generation;
        pthread_mutex_unlock(&p->lock);
        run_items(p, a.worker);
        pthread_mutex_lock(&p->lock);
        if (--p->busy == 0) pthread_cond_signal(&p->done);
    }
    pthread_mutex_unlock(&p->lock);
    return NULL;
}

tp_pool *tp_create(int nthreads)
{
    if (nthreads < 1) nthreads = 1;
    tp_pool *p = calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->nthreads = nthreads;
    pthread_mutex_init(&p->lock, NULL);
    pthread_cond_init(&p->wake, NULL);
    pthread_cond_init(&p->done, NULL);
    atomic_init(&p->next, 0);
    if (nthreads > 1) {
        p->threads = calloc((size_t)nthreads - 1, sizeof(pthread_t));
        if (!p->threads) { tp_destroy(p); return NULL; }
        for (int i = 1; i < nthreads; i++) {
            tp_arg *a = malloc(sizeof(*a));
            if (!a) { p->nthreads = i; tp_destroy(p); return NULL; }
            a->pool = p;
            a->worker = i;
            if (pthread_create(&p->threads[i - 1], NULL, worker_main, a) != 0) {
                free(a);
                p->nthreads = i;
                tp_destroy(p);
                return NULL;
            }
        }
    }
    return p;
}

void tp_destroy(tp_pool *p)
{
    if (!p) return;
    pthread_mutex_lock(&p->lock);
    p->quit = 1;
    pthread_cond_broadcast(&p->wake);
    pthread_mutex_unlock(&p->lock);
    for (int i = 1; i < p->nthreads; i++) pthread_join(p->threads[i - 1], NULL);
    free(p->threads);
    pthread_mutex_destroy(&p->lock);
    pthread_cond_destroy(&p->wake);
    pthread_cond_destroy(&p->done);
    free(p);
}

int tp_threads(const tp_pool *p) { return p ? p->nthreads : 1; }

void tp_parallel_for(tp_pool *p, int n, tp_task fn, void *ctx)
{
    if (n <= 0) return;
    if (!p || p->nthreads == 1 || n == 1) {
        for (int i = 0; i < n; i++) fn(ctx, i, 0);
        return;
    }
    pthread_mutex_lock(&p->lock);
    p->fn = fn;
    p->ctx = ctx;
    p->n = n;
    atomic_store(&p->next, 0);
    p->busy = p->nthreads - 1;
    p->generation++;
    pthread_cond_broadcast(&p->wake);
    pthread_mutex_unlock(&p->lock);

    run_items(p, 0);

    pthread_mutex_lock(&p->lock);
    while (p->busy > 0) pthread_cond_wait(&p->done, &p->lock);
    pthread_mutex_unlock(&p->lock);
}
