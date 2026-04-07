#include "fs_utils.h"
#include "common.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

#define FS_MAX_PATH_CHARS 2048

static bool create_directory_if_needed(const char *path)
{
    DWORD attrs;

    if (CreateDirectoryA(path, NULL)) {
        return true;
    }

    if (GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;
    }

    attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0U;
}

bool fs_ensure_directory_recursive(const char *path, char *err, size_t err_size)
{
    char tmp[FS_MAX_PATH_CHARS];
    size_t i;
    size_t len;

    if (path == NULL || path[0] == '\0') {
        set_error(err, err_size, "output directory path is empty");
        return false;
    }

    len = strlen(path);
    if (len >= sizeof(tmp)) {
        set_error(err, err_size, "output directory path too long");
        return false;
    }

    memcpy(tmp, path, len + 1U);

    for (i = 0U; i < len; ++i) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            char saved = tmp[i];
            tmp[i] = '\0';

            if (tmp[0] != '\0' && !(i == 2U && tmp[1] == ':')) {
                if (!create_directory_if_needed(tmp)) {
                    set_error(err, err_size, "failed to create directory: %s", tmp);
                    return false;
                }
            }

            tmp[i] = saved;
        }
    }

    if (!create_directory_if_needed(tmp)) {
        set_error(err, err_size, "failed to create directory: %s", tmp);
        return false;
    }

    return true;
}

bool fs_build_output_filename(
    const char *filename_template,
    unsigned int segment_number,
    char *out,
    size_t out_size,
    char *err,
    size_t err_size)
{
    const char *cursor = filename_template;
    const char token[] = "{num}";
    size_t token_len = sizeof(token) - 1U;
    size_t out_len = 0U;
    char number[32];
    int printed = snprintf(number, sizeof(number), "%u", segment_number);

    if (filename_template == NULL || out == NULL || out_size == 0U) {
        set_error(err, err_size, "invalid filename template buffers");
        return false;
    }

    if (printed < 0 || (size_t)printed >= sizeof(number)) {
        set_error(err, err_size, "segment number formatting failed");
        return false;
    }

    while (*cursor != '\0') {
        if (strncmp(cursor, token, token_len) == 0) {
            size_t number_len = strlen(number);
            if (out_len + number_len + 1U > out_size) {
                set_error(err, err_size, "filename too long for segment %u", segment_number);
                return false;
            }
            memcpy(out + out_len, number, number_len);
            out_len += number_len;
            cursor += token_len;
            continue;
        }

        if (out_len + 2U > out_size) {
            set_error(err, err_size, "filename too long for segment %u", segment_number);
            return false;
        }

        out[out_len++] = *cursor++;
    }

    out[out_len] = '\0';

    if (out_len == 0U) {
        set_error(err, err_size, "filename template produced empty name");
        return false;
    }

    return true;
}

bool fs_ansi_to_wide(
    const char *src,
    wchar_t *dst,
    size_t dst_count,
    char *err,
    size_t err_size)
{
    int result;

    if (src == NULL || dst == NULL || dst_count == 0U) {
        set_error(err, err_size, "invalid ansi to wide conversion buffers");
        return false;
    }

    result = MultiByteToWideChar(CP_ACP, 0, src, -1, dst, (int)dst_count);
    if (result <= 0) {
        set_error(err, err_size, "MultiByteToWideChar failed for path");
        return false;
    }

    return true;
}

bool fs_build_output_path_wide(
    const char *output_dir,
    const char *filename_template,
    unsigned int segment_number,
    wchar_t *out,
    size_t out_count,
    char *err,
    size_t err_size)
{
    char filename[FS_MAX_PATH_CHARS];
    char full_path[FS_MAX_PATH_CHARS];
    int printed;
    size_t dir_len;

    if (!fs_build_output_filename(
            filename_template,
            segment_number,
            filename,
            sizeof(filename),
            err,
            err_size)) {
        return false;
    }

    dir_len = strlen(output_dir);
    if (dir_len == 0U) {
        set_error(err, err_size, "output directory is empty");
        return false;
    }

    if (output_dir[dir_len - 1U] == '\\' || output_dir[dir_len - 1U] == '/') {
        printed = snprintf(full_path, sizeof(full_path), "%s%s", output_dir, filename);
    } else {
        printed = snprintf(full_path, sizeof(full_path), "%s\\%s", output_dir, filename);
    }

    if (printed < 0 || (size_t)printed >= sizeof(full_path)) {
        set_error(err, err_size, "output path too long for segment %u", segment_number);
        return false;
    }

    return fs_ansi_to_wide(full_path, out, out_count, err, err_size);
}
