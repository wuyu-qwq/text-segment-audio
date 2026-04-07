#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

typedef enum InputEncoding {
    INPUT_ENCODING_AUTO = 0,
    INPUT_ENCODING_UTF8,
    INPUT_ENCODING_ACP
} InputEncoding;

typedef struct AppConfig {
    const char *input_path;
    const char *output_dir;
    const char *filename_template;
    const char *voice_name;

    unsigned int start_segment;
    unsigned int end_segment; /* 0 means no upper bound */
    unsigned int lines_per_segment;

    int rate;
    int volume;

    unsigned int threads;
    unsigned int sample_rate;
    unsigned short bits_per_sample;
    unsigned short channels;

    InputEncoding encoding;

    bool skip_empty_lines;
    bool continue_on_error;
    bool dry_run;
    bool use_sapi_mutex;
    bool show_help;
} AppConfig;

void config_set_defaults(AppConfig *cfg);
bool config_parse_args(AppConfig *cfg, int argc, char **argv, char *err, size_t err_size);
bool config_validate(const AppConfig *cfg, char *err, size_t err_size);
void config_print_help(const char *program_name);
const char *config_encoding_to_string(InputEncoding encoding);

#endif
