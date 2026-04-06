#include "segment_builder.h"
#include "common.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

#define READ_BUFFER_BYTES 8192

static void trim_line_end(char *line)
{
    size_t len = strlen(line);
    while (len > 0U && (line[len - 1U] == '\n' || line[len - 1U] == '\r')) {
        line[--len] = '\0';
    }
}

static void strip_utf8_bom_if_needed(char *line, bool *first_line)
{
    if (!(*first_line)) {
        return;
    }

    *first_line = false;
    if ((unsigned char)line[0] == 0xEFU &&
        (unsigned char)line[1] == 0xBBU &&
        (unsigned char)line[2] == 0xBFU) {
        memmove(line, line + 3, strlen(line + 3) + 1U);
    }
}

static void skip_long_line_remainder(FILE *fp)
{
    int c;
    do {
        c = fgetc(fp);
    } while (c != EOF && c != '\n');
}

static bool convert_using_codepage(
    const char *src,
    UINT codepage,
    DWORD flags,
    wchar_t **out_wline,
    char *err,
    size_t err_size)
{
    int needed_chars;
    wchar_t *wline;

    needed_chars = MultiByteToWideChar(codepage, flags, src, -1, NULL, 0);
    if (needed_chars <= 0) {
        return false;
    }

    wline = (wchar_t *)malloc((size_t)needed_chars * sizeof(wchar_t));
    if (wline == NULL) {
        set_error(err, err_size, "memory allocation failed while converting line");
        return false;
    }

    if (MultiByteToWideChar(codepage, flags, src, -1, wline, needed_chars) <= 0) {
        free(wline);
        return false;
    }

    *out_wline = wline;
    return true;
}

static bool convert_line(
    const char *src,
    InputEncoding encoding,
    wchar_t **out_wline,
    char *err,
    size_t err_size)
{
    if (encoding == INPUT_ENCODING_UTF8) {
        if (convert_using_codepage(src, CP_UTF8, MB_ERR_INVALID_CHARS, out_wline, err, err_size)) {
            return true;
        }
        set_error(err, err_size, "UTF-8 decoding failed");
        return false;
    }

    if (encoding == INPUT_ENCODING_ACP) {
        if (convert_using_codepage(src, CP_ACP, 0, out_wline, err, err_size)) {
            return true;
        }
        set_error(err, err_size, "ACP decoding failed");
        return false;
    }

    if (convert_using_codepage(src, CP_UTF8, MB_ERR_INVALID_CHARS, out_wline, err, err_size)) {
        return true;
    }

    if (convert_using_codepage(src, CP_ACP, 0, out_wline, err, err_size)) {
        return true;
    }

    set_error(err, err_size, "AUTO decoding failed for both UTF-8 and ACP");
    return false;
}

static bool wide_builder_append(
    wchar_t **buffer,
    size_t *length,
    size_t *capacity,
    const wchar_t *text,
    char *err,
    size_t err_size)
{
    size_t add_len = wcslen(text);
    size_t required_len = *length + add_len + 1U;

    if (required_len > *capacity) {
        size_t new_capacity = *capacity;
        wchar_t *new_buffer;

        while (new_capacity < required_len) {
            new_capacity *= 2U;
            if (new_capacity < *capacity) {
                set_error(err, err_size, "segment text overflow while reallocating");
                return false;
            }
        }

        new_buffer = (wchar_t *)realloc(*buffer, new_capacity * sizeof(wchar_t));
        if (new_buffer == NULL) {
            set_error(err, err_size, "memory allocation failed while building segment");
            return false;
        }

        *buffer = new_buffer;
        *capacity = new_capacity;
    }

    wmemcpy(*buffer + *length, text, add_len + 1U);
    *length += add_len;
    return true;
}

static bool segment_list_push(
    SegmentList *list,
    unsigned int segment_number,
    wchar_t *text,
    char *err,
    size_t err_size)
{
    if (list->count == list->capacity) {
        size_t new_capacity = (list->capacity == 0U) ? 16U : list->capacity * 2U;
        SegmentTask *new_items = (SegmentTask *)realloc(list->items, new_capacity * sizeof(SegmentTask));
        if (new_items == NULL) {
            set_error(err, err_size, "memory allocation failed while appending segment task");
            return false;
        }

        list->items = new_items;
        list->capacity = new_capacity;
    }

    list->items[list->count].segment_number = segment_number;
    list->items[list->count].text = text;
    list->count += 1U;
    return true;
}

static bool finalize_segment(
    const AppConfig *cfg,
    SegmentList *out,
    unsigned int segment_number,
    const wchar_t *body,
    char *err,
    size_t err_size)
{
    wchar_t prefix[64];
    int prefix_chars;
    size_t prefix_len;
    size_t body_len;
    size_t total_len;
    wchar_t *full_text;

    if (body[0] == L'\0') {
        return true;
    }

    if (segment_number < cfg->start_segment) {
        return true;
    }

    if (cfg->end_segment != 0U && segment_number > cfg->end_segment) {
        return true;
    }

    prefix_chars = swprintf(prefix, sizeof(prefix) / sizeof(prefix[0]), L"%u, ", segment_number);
    if (prefix_chars < 0) {
        set_error(err, err_size, "segment prefix formatting failed for #%u", segment_number);
        return false;
    }

    prefix_len = (size_t)prefix_chars;
    body_len = wcslen(body);
    total_len = prefix_len + body_len;

    full_text = (wchar_t *)malloc((total_len + 1U) * sizeof(wchar_t));
    if (full_text == NULL) {
        set_error(err, err_size, "memory allocation failed for segment #%u text", segment_number);
        return false;
    }

    wmemcpy(full_text, prefix, prefix_len);
    wmemcpy(full_text + prefix_len, body, body_len + 1U);

    if (!segment_list_push(out, segment_number, full_text, err, err_size)) {
        free(full_text);
        return false;
    }

    return true;
}

void segment_list_init(SegmentList *list)
{
    list->items = NULL;
    list->count = 0U;
    list->capacity = 0U;
}

void segment_list_free(SegmentList *list)
{
    size_t i;

    if (list == NULL) {
        return;
    }

    for (i = 0U; i < list->count; ++i) {
        free(list->items[i].text);
    }

    free(list->items);
    list->items = NULL;
    list->count = 0U;
    list->capacity = 0U;
}

bool segment_builder_build(
    const AppConfig *cfg,
    SegmentList *out,
    char *err,
    size_t err_size)
{
    FILE *fp = NULL;
    char line[READ_BUFFER_BYTES];
    bool first_line = true;
    unsigned long logical_line = 0UL;
    unsigned int segment_number = 1U;
    unsigned int lines_in_segment = 0U;

    wchar_t *segment_body = NULL;
    size_t body_len = 0U;
    size_t body_capacity = 128U;

    segment_list_init(out);

    fp = fopen(cfg->input_path, "rb");
    if (fp == NULL) {
        set_error(err, err_size, "cannot open input file: %s", cfg->input_path);
        return false;
    }

    segment_body = (wchar_t *)malloc(body_capacity * sizeof(wchar_t));
    if (segment_body == NULL) {
        fclose(fp);
        set_error(err, err_size, "memory allocation failed for segment builder");
        return false;
    }
    segment_body[0] = L'\0';

    while (fgets(line, sizeof(line), fp) != NULL) {
        size_t line_len = strlen(line);
        bool truncated = false;
        wchar_t *wline = NULL;
        char decode_err[256] = {0};

        logical_line += 1UL;

        if (line_len > 0U && line[line_len - 1U] != '\n' && !feof(fp)) {
            truncated = true;
        }

        if (truncated) {
            skip_long_line_remainder(fp);
            if (!cfg->continue_on_error) {
                set_error(err, err_size, "line %lu exceeds %u bytes", logical_line, (unsigned int)(READ_BUFFER_BYTES - 1U));
                goto fail;
            }
            fprintf(stderr, "[warn] line %lu exceeds %u bytes and was skipped\n", logical_line, (unsigned int)(READ_BUFFER_BYTES - 1U));
            continue;
        }

        trim_line_end(line);
        strip_utf8_bom_if_needed(line, &first_line);

        if (cfg->skip_empty_lines && line[0] == '\0') {
            continue;
        }

        if (!convert_line(line, cfg->encoding, &wline, decode_err, sizeof(decode_err))) {
            if (!cfg->continue_on_error) {
                set_error(err, err_size, "line %lu decode error: %s", logical_line, decode_err);
                goto fail;
            }
            fprintf(stderr, "[warn] line %lu decode error (%s), skipped\n", logical_line, decode_err);
            continue;
        }

        if (lines_in_segment > 0U) {
            if (!wide_builder_append(&segment_body, &body_len, &body_capacity, L" ", err, err_size)) {
                free(wline);
                goto fail;
            }
        }

        if (!wide_builder_append(&segment_body, &body_len, &body_capacity, wline, err, err_size)) {
            free(wline);
            goto fail;
        }

        free(wline);

        lines_in_segment += 1U;
        if (lines_in_segment == cfg->lines_per_segment) {
            if (!finalize_segment(cfg, out, segment_number, segment_body, err, err_size)) {
                goto fail;
            }

            segment_number += 1U;
            lines_in_segment = 0U;
            segment_body[0] = L'\0';
            body_len = 0U;

            if (cfg->end_segment != 0U && segment_number > cfg->end_segment) {
                break;
            }
        }
    }

    if (ferror(fp)) {
        set_error(err, err_size, "failed while reading input file: %s", cfg->input_path);
        goto fail;
    }

    if (lines_in_segment > 0U) {
        if (!finalize_segment(cfg, out, segment_number, segment_body, err, err_size)) {
            goto fail;
        }
    }

    free(segment_body);
    fclose(fp);
    return true;

fail:
    free(segment_body);
    fclose(fp);
    segment_list_free(out);
    return false;
}
