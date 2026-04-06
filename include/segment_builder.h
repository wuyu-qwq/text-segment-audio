#ifndef SEGMENT_BUILDER_H
#define SEGMENT_BUILDER_H

#include "app_config.h"

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

typedef struct SegmentTask {
    unsigned int segment_number;
    wchar_t *text;
} SegmentTask;

typedef struct SegmentList {
    SegmentTask *items;
    size_t count;
    size_t capacity;
} SegmentList;

void segment_list_init(SegmentList *list);
void segment_list_free(SegmentList *list);

bool segment_builder_build(
    const AppConfig *cfg,
    SegmentList *out,
    char *err,
    size_t err_size);

#endif
