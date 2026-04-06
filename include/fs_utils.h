#ifndef FS_UTILS_H
#define FS_UTILS_H

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

bool fs_ensure_directory_recursive(const char *path, char *err, size_t err_size);

bool fs_build_output_filename(
    const char *filename_template,
    unsigned int segment_number,
    char *out,
    size_t out_size,
    char *err,
    size_t err_size);

bool fs_build_output_path_wide(
    const char *output_dir,
    const char *filename_template,
    unsigned int segment_number,
    wchar_t *out,
    size_t out_count,
    char *err,
    size_t err_size);

bool fs_ansi_to_wide(
    const char *src,
    wchar_t *dst,
    size_t dst_count,
    char *err,
    size_t err_size);

#endif
