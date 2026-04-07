#include "worker_pool.h"
#include "common.h"
#include "fs_utils.h"
#include "sapi_tts.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

typedef struct WorkerContext {
    unsigned int worker_id;
    const AppConfig *cfg;
    const SegmentList *segments;
    volatile LONG *next_index;
    volatile LONG *stop_flag;
    volatile LONG *succeeded;
    volatile LONG *failed;
    CRITICAL_SECTION *log_lock;
    HANDLE sapi_mutex;
} WorkerContext;

static unsigned int resolve_thread_count(const AppConfig *cfg, size_t task_count)
{
    unsigned int count = cfg->threads;

    if (count == 0U) {
        DWORD cpu = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        if (cpu == 0U) {
            cpu = 1U;
        }
        count = (unsigned int)cpu;
    }

    if (count == 0U) {
        count = 1U;
    }

    if (task_count > 0U && (size_t)count > task_count) {
        count = (unsigned int)task_count;
    }

    if (count == 0U) {
        count = 1U;
    }

    return count;
}

static void locked_log(CRITICAL_SECTION *lock, const char *fmt, ...)
{
    va_list args;

    EnterCriticalSection(lock);
    va_start(args, fmt);
    (void)vfprintf(stderr, fmt, args);
    va_end(args);
    LeaveCriticalSection(lock);
}

static DWORD WINAPI worker_thread_proc(LPVOID param)
{
    WorkerContext *ctx = (WorkerContext *)param;
    HRESULT co_hr = S_OK;
    bool com_initialized = false;
    SapiEngine engine;
    bool engine_ok = false;

    co_hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(co_hr)) {
        com_initialized = true;
    } else {
        locked_log(ctx->log_lock, "[worker %u] CoInitializeEx failed, hr=0x%08lX\n", ctx->worker_id, (unsigned long)co_hr);
        InterlockedIncrement(ctx->failed);
        if (!ctx->cfg->continue_on_error) {
            InterlockedExchange(ctx->stop_flag, 1L);
        }
        return 1U;
    }

    if (!ctx->cfg->dry_run) {
        char init_err[256] = {0};
        if (!sapi_engine_init(&engine, ctx->cfg, ctx->sapi_mutex, init_err, sizeof(init_err))) {
            locked_log(ctx->log_lock, "[worker %u] SAPI init failed: %s\n", ctx->worker_id, init_err);
            InterlockedIncrement(ctx->failed);
            if (!ctx->cfg->continue_on_error) {
                InterlockedExchange(ctx->stop_flag, 1L);
            }
            if (com_initialized) {
                CoUninitialize();
            }
            return 1U;
        }
        engine_ok = true;
    }

    while (1) {
        LONG index_l;
        size_t index;
        const SegmentTask *task;
        wchar_t output_path[2048];
        char err[256] = {0};

        if (!ctx->cfg->continue_on_error && InterlockedCompareExchange(ctx->stop_flag, 0L, 0L) != 0L) {
            break;
        }

        index_l = InterlockedIncrement(ctx->next_index) - 1L;
        if (index_l < 0L) {
            continue;
        }

        index = (size_t)index_l;
        if (index >= ctx->segments->count) {
            break;
        }

        task = &ctx->segments->items[index];

        if (!fs_build_output_path_wide(
                ctx->cfg->output_dir,
                ctx->cfg->filename_template,
                task->segment_number,
                output_path,
                sizeof(output_path) / sizeof(output_path[0]),
                err,
                sizeof(err))) {
            locked_log(
                ctx->log_lock,
                "[worker %u] segment #%u path build failed: %s\n",
                ctx->worker_id,
                task->segment_number,
                err);
            InterlockedIncrement(ctx->failed);
            if (!ctx->cfg->continue_on_error) {
                InterlockedExchange(ctx->stop_flag, 1L);
                break;
            }
            continue;
        }

        if (ctx->cfg->dry_run) {
            locked_log(ctx->log_lock, "[dry-run][worker %u] segment #%u planned\n", ctx->worker_id, task->segment_number);
            InterlockedIncrement(ctx->succeeded);
            continue;
        }

        if (!sapi_engine_speak_to_wav(&engine, task->text, output_path, err, sizeof(err))) {
            locked_log(
                ctx->log_lock,
                "[worker %u] segment #%u synth failed: %s\n",
                ctx->worker_id,
                task->segment_number,
                err);
            InterlockedIncrement(ctx->failed);
            if (!ctx->cfg->continue_on_error) {
                InterlockedExchange(ctx->stop_flag, 1L);
                break;
            }
            continue;
        }

        InterlockedIncrement(ctx->succeeded);
    }

    if (engine_ok) {
        sapi_engine_cleanup(&engine);
    }

    if (com_initialized) {
        CoUninitialize();
    }

    return 0U;
}

bool worker_pool_run(
    const AppConfig *cfg,
    const SegmentList *segments,
    WorkerStats *stats,
    char *err,
    size_t err_size)
{
    unsigned int thread_count;
    HANDLE *thread_handles = NULL;
    WorkerContext *contexts = NULL;
    HANDLE sapi_mutex = NULL;
    CRITICAL_SECTION log_lock;
    volatile LONG next_index = 0L;
    volatile LONG stop_flag = 0L;
    volatile LONG succeeded = 0L;
    volatile LONG failed = 0L;
    unsigned int i;
    bool ok = false;
    bool lock_initialized = false;
    bool spawn_failed = false;

    stats->succeeded = 0U;
    stats->failed = 0U;

    if (segments->count == 0U) {
        return true;
    }

    thread_count = resolve_thread_count(cfg, segments->count);

    thread_handles = (HANDLE *)calloc(thread_count, sizeof(HANDLE));
    contexts = (WorkerContext *)calloc(thread_count, sizeof(WorkerContext));
    if (thread_handles == NULL || contexts == NULL) {
        set_error(err, err_size, "memory allocation failed for worker pool");
        goto cleanup;
    }

    if (cfg->use_sapi_mutex) {
        sapi_mutex = CreateMutexW(NULL, FALSE, NULL);
        if (sapi_mutex == NULL) {
            set_error(err, err_size, "CreateMutexW for SAPI lock failed");
            goto cleanup;
        }
    }

    InitializeCriticalSection(&log_lock);
    lock_initialized = true;

    for (i = 0U; i < thread_count; ++i) {
        contexts[i].worker_id = i + 1U;
        contexts[i].cfg = cfg;
        contexts[i].segments = segments;
        contexts[i].next_index = &next_index;
        contexts[i].stop_flag = &stop_flag;
        contexts[i].succeeded = &succeeded;
        contexts[i].failed = &failed;
        contexts[i].log_lock = &log_lock;
        contexts[i].sapi_mutex = sapi_mutex;

        thread_handles[i] = CreateThread(NULL, 0U, worker_thread_proc, &contexts[i], 0U, NULL);
        if (thread_handles[i] == NULL) {
            set_error(err, err_size, "CreateThread failed at worker #%u", i + 1U);
            InterlockedExchange(&stop_flag, 1L);
            spawn_failed = true;
            goto cleanup_threads;
        }
    }

cleanup_threads:
    for (i = 0U; i < thread_count; ++i) {
        if (thread_handles[i] != NULL) {
            (void)WaitForSingleObject(thread_handles[i], INFINITE);
            CloseHandle(thread_handles[i]);
            thread_handles[i] = NULL;
        }
    }

    if (lock_initialized) {
        DeleteCriticalSection(&log_lock);
        lock_initialized = false;
    }

    stats->succeeded = (unsigned int)InterlockedCompareExchange(&succeeded, 0L, 0L);
    stats->failed = (unsigned int)InterlockedCompareExchange(&failed, 0L, 0L);

    if (spawn_failed) {
        ok = false;
    } else if (stats->failed > 0U) {
        set_error(err, err_size, "completed with %u failures", stats->failed);
        ok = false;
    } else {
        ok = true;
    }

cleanup:
    if (sapi_mutex != NULL) {
        CloseHandle(sapi_mutex);
    }

    free(contexts);
    free(thread_handles);
    return ok;
}
