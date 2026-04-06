#include "app_config.h"
#include "common.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool parse_int_value(const char *text, int *out)
{
    char *end = NULL;
    long value = strtol(text, &end, 10);

    if (text == NULL || *text == '\0') {
        return false;
    }
    if (end == NULL || *end != '\0') {
        return false;
    }
    if (value < INT_MIN || value > INT_MAX) {
        return false;
    }

    *out = (int)value;
    return true;
}

static bool parse_uint_value(const char *text, unsigned int *out)
{
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 10);

    if (text == NULL || *text == '\0') {
        return false;
    }
    if (end == NULL || *end != '\0') {
        return false;
    }
    if (value > UINT_MAX) {
        return false;
    }

    *out = (unsigned int)value;
    return true;
}

static bool parse_ushort_value(const char *text, unsigned short *out)
{
    unsigned int value = 0U;
    if (!parse_uint_value(text, &value)) {
        return false;
    }
    if (value > 0xFFFFU) {
        return false;
    }
    *out = (unsigned short)value;
    return true;
}

static bool has_option_name(const char *arg, const char *name, const char **inline_value)
{
    size_t name_len = strlen(name);

    if (strcmp(arg, name) == 0) {
        *inline_value = NULL;
        return true;
    }

    if (strncmp(arg, name, name_len) == 0 && arg[name_len] == '=') {
        *inline_value = arg + name_len + 1U;
        return true;
    }

    return false;
}

static const char *require_value(
    int *index,
    int argc,
    char **argv,
    const char *option_name,
    const char *inline_value,
    char *err,
    size_t err_size)
{
    if (inline_value != NULL) {
        return inline_value;
    }

    if (*index + 1 >= argc) {
        set_error(err, err_size, "missing value for option %s", option_name);
        return NULL;
    }

    *index += 1;
    return argv[*index];
}

const char *config_encoding_to_string(InputEncoding encoding)
{
    switch (encoding) {
        case INPUT_ENCODING_UTF8:
            return "utf8";
        case INPUT_ENCODING_ACP:
            return "acp";
        case INPUT_ENCODING_AUTO:
        default:
            return "auto";
    }
}

void config_set_defaults(AppConfig *cfg)
{
    cfg->input_path = "input.txt";
    cfg->output_dir = "output";
    cfg->filename_template = "segment_{num}.wav";
    cfg->voice_name = NULL;

    cfg->start_segment = 1U;
    cfg->end_segment = 0U;
    cfg->lines_per_segment = 20U;

    cfg->rate = 3;
    cfg->volume = 100;

    cfg->threads = 0U; /* 0 means auto */
    cfg->sample_rate = 22050U;
    cfg->bits_per_sample = 16U;
    cfg->channels = 1U;

    cfg->encoding = INPUT_ENCODING_AUTO;

    cfg->skip_empty_lines = true;
    cfg->continue_on_error = true;
    cfg->dry_run = false;
    cfg->use_sapi_mutex = false;
    cfg->show_help = false;
}

bool config_parse_args(AppConfig *cfg, int argc, char **argv, char *err, size_t err_size)
{
    int i;

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        const char *inline_value = NULL;
        const char *value = NULL;

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            cfg->show_help = true;
            return true;
        }

        if (has_option_name(arg, "-i", &inline_value) || has_option_name(arg, "--input", &inline_value)) {
            value = require_value(&i, argc, argv, "--input", inline_value, err, err_size);
            if (value == NULL) {
                return false;
            }
            cfg->input_path = value;
            continue;
        }

        if (has_option_name(arg, "-o", &inline_value) || has_option_name(arg, "--output-dir", &inline_value)) {
            value = require_value(&i, argc, argv, "--output-dir", inline_value, err, err_size);
            if (value == NULL) {
                return false;
            }
            cfg->output_dir = value;
            continue;
        }

        if (has_option_name(arg, "--filename-template", &inline_value)) {
            value = require_value(&i, argc, argv, "--filename-template", inline_value, err, err_size);
            if (value == NULL) {
                return false;
            }
            cfg->filename_template = value;
            continue;
        }

        if (has_option_name(arg, "--voice-name", &inline_value)) {
            value = require_value(&i, argc, argv, "--voice-name", inline_value, err, err_size);
            if (value == NULL) {
                return false;
            }
            cfg->voice_name = value;
            continue;
        }

        if (has_option_name(arg, "--start-segment", &inline_value)) {
            unsigned int parsed = 0U;
            value = require_value(&i, argc, argv, "--start-segment", inline_value, err, err_size);
            if (value == NULL) {
                return false;
            }
            if (!parse_uint_value(value, &parsed)) {
                set_error(err, err_size, "invalid unsigned integer for --start-segment: %s", value);
                return false;
            }
            cfg->start_segment = parsed;
            continue;
        }

        if (has_option_name(arg, "--end-segment", &inline_value)) {
            unsigned int parsed = 0U;
            value = require_value(&i, argc, argv, "--end-segment", inline_value, err, err_size);
            if (value == NULL) {
                return false;
            }
            if (!parse_uint_value(value, &parsed)) {
                set_error(err, err_size, "invalid unsigned integer for --end-segment: %s", value);
                return false;
            }
            cfg->end_segment = parsed;
            continue;
        }

        if (has_option_name(arg, "--lines-per-segment", &inline_value)) {
            unsigned int parsed = 0U;
            value = require_value(&i, argc, argv, "--lines-per-segment", inline_value, err, err_size);
            if (value == NULL) {
                return false;
            }
            if (!parse_uint_value(value, &parsed)) {
                set_error(err, err_size, "invalid unsigned integer for --lines-per-segment: %s", value);
                return false;
            }
            cfg->lines_per_segment = parsed;
            continue;
        }

        if (has_option_name(arg, "--rate", &inline_value)) {
            int parsed_int = 0;
            value = require_value(&i, argc, argv, "--rate", inline_value, err, err_size);
            if (value == NULL) {
                return false;
            }
            if (!parse_int_value(value, &parsed_int)) {
                set_error(err, err_size, "invalid integer for --rate: %s", value);
                return false;
            }
            cfg->rate = parsed_int;
            continue;
        }

        if (has_option_name(arg, "--volume", &inline_value)) {
            unsigned int parsed_uint = 0U;
            value = require_value(&i, argc, argv, "--volume", inline_value, err, err_size);
            if (value == NULL) {
                return false;
            }
            if (!parse_uint_value(value, &parsed_uint)) {
                set_error(err, err_size, "invalid unsigned integer for --volume: %s", value);
                return false;
            }
            cfg->volume = (int)parsed_uint;
            continue;
        }

        if (has_option_name(arg, "-j", &inline_value) || has_option_name(arg, "--threads", &inline_value)) {
            unsigned int parsed = 0U;
            value = require_value(&i, argc, argv, "--threads", inline_value, err, err_size);
            if (value == NULL) {
                return false;
            }
            if (!parse_uint_value(value, &parsed)) {
                set_error(err, err_size, "invalid unsigned integer for --threads: %s", value);
                return false;
            }
            cfg->threads = parsed;
            continue;
        }

        if (has_option_name(arg, "--sample-rate", &inline_value)) {
            unsigned int parsed = 0U;
            value = require_value(&i, argc, argv, "--sample-rate", inline_value, err, err_size);
            if (value == NULL) {
                return false;
            }
            if (!parse_uint_value(value, &parsed)) {
                set_error(err, err_size, "invalid unsigned integer for --sample-rate: %s", value);
                return false;
            }
            cfg->sample_rate = parsed;
            continue;
        }

        if (has_option_name(arg, "--bits-per-sample", &inline_value)) {
            unsigned short parsed = 0U;
            value = require_value(&i, argc, argv, "--bits-per-sample", inline_value, err, err_size);
            if (value == NULL) {
                return false;
            }
            if (!parse_ushort_value(value, &parsed)) {
                set_error(err, err_size, "invalid unsigned integer for --bits-per-sample: %s", value);
                return false;
            }
            cfg->bits_per_sample = parsed;
            continue;
        }

        if (has_option_name(arg, "--channels", &inline_value)) {
            unsigned short parsed = 0U;
            value = require_value(&i, argc, argv, "--channels", inline_value, err, err_size);
            if (value == NULL) {
                return false;
            }
            if (!parse_ushort_value(value, &parsed)) {
                set_error(err, err_size, "invalid unsigned integer for --channels: %s", value);
                return false;
            }
            cfg->channels = parsed;
            continue;
        }

        if (has_option_name(arg, "--encoding", &inline_value)) {
            value = require_value(&i, argc, argv, "--encoding", inline_value, err, err_size);
            if (value == NULL) {
                return false;
            }
            if (strcmp(value, "auto") == 0) {
                cfg->encoding = INPUT_ENCODING_AUTO;
            } else if (strcmp(value, "utf8") == 0) {
                cfg->encoding = INPUT_ENCODING_UTF8;
            } else if (strcmp(value, "acp") == 0) {
                cfg->encoding = INPUT_ENCODING_ACP;
            } else {
                set_error(err, err_size, "invalid value for --encoding: %s", value);
                return false;
            }
            continue;
        }

        if (strcmp(arg, "--skip-empty-lines") == 0) {
            cfg->skip_empty_lines = true;
            continue;
        }

        if (strcmp(arg, "--keep-empty-lines") == 0) {
            cfg->skip_empty_lines = false;
            continue;
        }

        if (strcmp(arg, "--continue-on-error") == 0) {
            cfg->continue_on_error = true;
            continue;
        }

        if (strcmp(arg, "--fail-fast") == 0) {
            cfg->continue_on_error = false;
            continue;
        }

        if (strcmp(arg, "--dry-run") == 0) {
            cfg->dry_run = true;
            continue;
        }

        if (strcmp(arg, "--use-sapi-mutex") == 0) {
            cfg->use_sapi_mutex = true;
            continue;
        }

        if (strcmp(arg, "--no-sapi-mutex") == 0) {
            cfg->use_sapi_mutex = false;
            continue;
        }

        set_error(err, err_size, "unknown option: %s", arg);
        return false;
    }

    return true;
}

bool config_validate(const AppConfig *cfg, char *err, size_t err_size)
{
    if (cfg->start_segment == 0U) {
        set_error(err, err_size, "--start-segment must be >= 1");
        return false;
    }

    if (cfg->end_segment != 0U && cfg->end_segment < cfg->start_segment) {
        set_error(err, err_size, "--end-segment must be 0 or >= --start-segment");
        return false;
    }

    if (cfg->lines_per_segment == 0U) {
        set_error(err, err_size, "--lines-per-segment must be >= 1");
        return false;
    }

    if (cfg->rate < -10 || cfg->rate > 10) {
        set_error(err, err_size, "--rate must be in [-10, 10]");
        return false;
    }

    if (cfg->volume < 0 || cfg->volume > 100) {
        set_error(err, err_size, "--volume must be in [0, 100]");
        return false;
    }

    if (cfg->threads > 256U) {
        set_error(err, err_size, "--threads must be <= 256");
        return false;
    }

    if (cfg->sample_rate == 0U) {
        set_error(err, err_size, "--sample-rate must be >= 1");
        return false;
    }

    if (cfg->bits_per_sample == 0U) {
        set_error(err, err_size, "--bits-per-sample must be >= 1");
        return false;
    }

    if (cfg->channels == 0U) {
        set_error(err, err_size, "--channels must be >= 1");
        return false;
    }

    if (cfg->filename_template == NULL || strstr(cfg->filename_template, "{num}") == NULL) {
        set_error(err, err_size, "--filename-template must include {num}");
        return false;
    }

    if (cfg->input_path == NULL || cfg->input_path[0] == '\0') {
        set_error(err, err_size, "--input must not be empty");
        return false;
    }

    if (cfg->output_dir == NULL || cfg->output_dir[0] == '\0') {
        set_error(err, err_size, "--output-dir must not be empty");
        return false;
    }

    return true;
}

void config_print_help(const char *program_name)
{
    printf("Usage: %s [options]\n", program_name);
    printf("\n");
    printf("I/O options:\n");
    printf("  -i, --input <path>                Input text file (default: input.txt)\n");
    printf("  -o, --output-dir <dir>            Output wav directory (default: output)\n");
    printf("  --filename-template <tpl>         Output file template, must contain {num}\n");
    printf("                                    default: segment_{num}.wav\n");
    printf("\n");
    printf("Segmentation options:\n");
    printf("  --lines-per-segment <n>           Lines in one segment (default: 20)\n");
    printf("  --start-segment <n>               Start segment number (default: 1)\n");
    printf("  --end-segment <n>                 End segment number, 0 = no limit (default: 0)\n");
    printf("  --skip-empty-lines                Ignore empty lines (default)\n");
    printf("  --keep-empty-lines                Keep empty lines in segmentation\n");
    printf("\n");
    printf("SAPI options:\n");
    printf("  --voice-name <text>               Use a voice whose description contains text\n");
    printf("  --rate <n>                        Voice rate, range [-10, 10] (default: 3)\n");
    printf("  --volume <n>                      Voice volume, range [0, 100] (default: 100)\n");
    printf("  --sample-rate <n>                 WAV sample rate (default: 22050)\n");
    printf("  --bits-per-sample <n>             WAV bits per sample (default: 16)\n");
    printf("  --channels <n>                    WAV channels (default: 1)\n");
    printf("  --use-sapi-mutex                  Enable global mutex for SAPI synthesis\n");
    printf("  --no-sapi-mutex                   Disable global mutex (default)\n");
    printf("\n");
    printf("Performance options:\n");
    printf("  -j, --threads <n>                 Worker threads, 0 = auto CPU count (default: 0)\n");
    printf("\n");
    printf("Input encoding:\n");
    printf("  --encoding <auto|utf8|acp>        Input text decode mode (default: auto)\n");
    printf("\n");
    printf("Error handling:\n");
    printf("  --continue-on-error               Keep processing on line/segment errors (default)\n");
    printf("  --fail-fast                       Stop at first worker error\n");
    printf("\n");
    printf("Diagnostics:\n");
    printf("  --dry-run                         Do not call SAPI, only print output plan\n");
    printf("  -h, --help                        Show this help message\n");
}
