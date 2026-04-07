#include "app_config.h"
#include "fs_utils.h"
#include "segment_builder.h"
#include "worker_pool.h"

#include <stdbool.h>
#include <stdio.h>
#include <windows.h>

static unsigned int cpu_count(void)
{
    DWORD count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (count == 0U) {
        count = 1U;
    }
    return (unsigned int)count;
}

static void print_config_summary(const AppConfig *cfg, size_t segments)
{
    unsigned int thread_count = cfg->threads == 0U ? cpu_count() : cfg->threads;
    char range_buf[64];

    if (segments > 0U && (size_t)thread_count > segments) {
        thread_count = (unsigned int)segments;
    }

    if (cfg->end_segment == 0U) {
        (void)snprintf(range_buf, sizeof(range_buf), "[%u, inf)", cfg->start_segment);
    } else {
        (void)snprintf(range_buf, sizeof(range_buf), "[%u, %u]", cfg->start_segment, cfg->end_segment);
    }

    printf("Configuration:\n");
    printf("  input:               %s\n", cfg->input_path);
    printf("  output_dir:          %s\n", cfg->output_dir);
    printf("  filename_template:   %s\n", cfg->filename_template);
    printf("  voice_name:          %s\n", (cfg->voice_name && cfg->voice_name[0] != '\0') ? cfg->voice_name : "<default>");
    printf("  lines_per_segment:   %u\n", cfg->lines_per_segment);
    printf("  segment_range:       %s\n", range_buf);
    printf("  rate:                %d\n", cfg->rate);
    printf("  volume:              %d\n", cfg->volume);
    printf("  sample_rate:         %u\n", cfg->sample_rate);
    printf("  bits_per_sample:     %u\n", (unsigned int)cfg->bits_per_sample);
    printf("  channels:            %u\n", (unsigned int)cfg->channels);
    printf("  encoding:            %s\n", config_encoding_to_string(cfg->encoding));
    printf("  skip_empty_lines:    %s\n", cfg->skip_empty_lines ? "true" : "false");
    printf("  continue_on_error:   %s\n", cfg->continue_on_error ? "true" : "false");
    printf("  use_sapi_mutex:      %s\n", cfg->use_sapi_mutex ? "true" : "false");
    printf("  dry_run:             %s\n", cfg->dry_run ? "true" : "false");
    printf("  threads:             %u\n", thread_count);
    printf("  tasks_to_process:    %zu\n", segments);
}

int main(int argc, char **argv)
{
    AppConfig cfg;
    SegmentList segments;
    WorkerStats stats;
    char err[512] = {0};
    bool worker_ok;
    ULONGLONG begin_tick;
    ULONGLONG end_tick;

    config_set_defaults(&cfg);

    if (!config_parse_args(&cfg, argc, argv, err, sizeof(err))) {
        fprintf(stderr, "Argument error: %s\n\n", err);
        config_print_help(argv[0]);
        return 1;
    }

    if (cfg.show_help) {
        config_print_help(argv[0]);
        return 0;
    }

    if (!config_validate(&cfg, err, sizeof(err))) {
        fprintf(stderr, "Config error: %s\n", err);
        return 1;
    }

    if (!cfg.dry_run) {
        if (!fs_ensure_directory_recursive(cfg.output_dir, err, sizeof(err))) {
            fprintf(stderr, "Output directory error: %s\n", err);
            return 1;
        }
    }

    if (!segment_builder_build(&cfg, &segments, err, sizeof(err))) {
        fprintf(stderr, "Segment build failed: %s\n", err);
        return 1;
    }

    print_config_summary(&cfg, segments.count);

    if (segments.count == 0U) {
        printf("No segments to process.\n");
        segment_list_free(&segments);
        return 0;
    }

    begin_tick = GetTickCount64();
    worker_ok = worker_pool_run(&cfg, &segments, &stats, err, sizeof(err));
    end_tick = GetTickCount64();

    printf("\nResult:\n");
    printf("  succeeded: %u\n", stats.succeeded);
    printf("  failed:    %u\n", stats.failed);
    printf("  elapsed:   %.2f s\n", (double)(end_tick - begin_tick) / 1000.0);

    segment_list_free(&segments);

    if (!worker_ok) {
        fprintf(stderr, "Worker error: %s\n", err);
        return 1;
    }

    return 0;
}
