#ifndef WORKER_POOL_H
#define WORKER_POOL_H

#include "app_config.h"
#include "segment_builder.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct WorkerStats {
    unsigned int succeeded;
    unsigned int failed;
} WorkerStats;

bool worker_pool_run(
    const AppConfig *cfg,
    const SegmentList *segments,
    WorkerStats *stats,
    char *err,
    size_t err_size);

#endif
