#include <windows.h>
#include <sapi.h>
#ifndef SPDFID_WaveFormatEx
const GUID SPDFID_WaveFormatEx =
{ 0xC31ADBAE, 0x527F, 0x4ff5, {0xA2, 0x30, 0xF6, 0x2B, 0xB6, 0x1F, 0xF7, 0x0C} };
#endif
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define MAX_LINE_LENGTH 1024
#define LINES_PER_SEGMENT 20
#define MAX_SEGMENT_LINES 50
#define SEGMENT_TEXT_CAPACITY (MAX_LINE_LENGTH * MAX_SEGMENT_LINES)

/* Segment range to process */
#define START_SEGMENT 1
#define END_SEGMENT   20

#if LINES_PER_SEGMENT > MAX_SEGMENT_LINES
#error "LINES_PER_SEGMENT must be <= MAX_SEGMENT_LINES"
#endif

static void trim_line_end(char *line)
{
    size_t len = strlen(line);
    while (len > 0U && (line[len - 1U] == '\n' || line[len - 1U] == '\r')) {
        line[--len] = '\0';
    }
}

static void strip_utf8_bom_if_needed(char *line, bool *is_first_line)
{
    if (!(*is_first_line)) {
        return;
    }
    *is_first_line = false;
    if ((unsigned char)line[0] == 0xEFU &&
        (unsigned char)line[1] == 0xBBU &&
        (unsigned char)line[2] == 0xBFU) {
        memmove(line, line + 3, strlen(line + 3) + 1U);
    }
}

static bool convert_to_wide_with_fallback(
    const char *src,
    wchar_t *dst,
    int dst_chars)
{
    int converted = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        src,
        -1,
        dst,
        dst_chars);

    if (converted > 0) {
        return true;
    }

    converted = MultiByteToWideChar(
        CP_ACP,
        0,
        src,
        -1,
        dst,
        dst_chars);

    return converted > 0;
}

static bool append_text(
    wchar_t *out,
    size_t out_capacity,
    const wchar_t *text)
{
    size_t out_len = wcslen(out);
    size_t text_len = wcslen(text);

    if (out_len + text_len + 1U > out_capacity) {
        return false;
    }

    wmemcpy(out + out_len, text, text_len + 1U);
    return true;
}

/* Merge multiple lines into one segment */
static bool build_segment_text(
    wchar_t *out,
    size_t out_capacity,
    wchar_t lines[][MAX_LINE_LENGTH],
    size_t line_count,
    unsigned int seg_num)
{
    int written = swprintf(out, out_capacity, L"%u, ", seg_num);
    if (written < 0 || (size_t)written >= out_capacity) {
        return false;
    }

    for (size_t i = 0; i < line_count; ++i) {
        if (!append_text(out, out_capacity, lines[i])) {
            return false;
        }
        if (i + 1U < line_count && !append_text(out, out_capacity, L" ")) {
            return false;
        }
    }

    return true;
}

static bool ensure_output_directory(void)
{
    if (CreateDirectoryW(L"output", NULL)) {
        return true;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        DWORD attrs = GetFileAttributesW(L"output");
        return attrs != INVALID_FILE_ATTRIBUTES &&
               (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0U;
    }

    return false;
}

/* Generate a WAV file */
static bool speak_to_wav(
    ISpVoice *voice,
    const wchar_t *text,
    unsigned int seg_num)
{
    HRESULT hr;
    ISpStream *stream = NULL;
    bool output_is_bound = false;
    bool ok = false;
    WCHAR filename[64];
    WAVEFORMATEX fmt;

    int name_written = swprintf(filename, 64, L"output\\segment_%u.wav", seg_num);
    if (name_written < 0 || name_written >= 64) {
        fwprintf(stderr, L"output filename too long: segment_%u.wav\n", seg_num);
        return false;
    }

    hr = CoCreateInstance(
        &CLSID_SpStream,
        NULL,
        CLSCTX_ALL,
        &IID_ISpStream,
        (void **)&stream);

    if (FAILED(hr)) {
        fwprintf(stderr, L"Create stream failed, hr=0x%08lX\n", (unsigned long)hr);
        return false;
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 1;
    fmt.nSamplesPerSec = 22050;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = (WORD)(fmt.nChannels * (fmt.wBitsPerSample / 8U));
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

    hr = stream->lpVtbl->BindToFile(
        stream,
        filename,
        SPFM_CREATE_ALWAYS,
        &SPDFID_WaveFormatEx,
        &fmt,
        0);

    if (FAILED(hr)) {
        fwprintf(stderr, L"Bind file failed: %ls, hr=0x%08lX\n", filename, (unsigned long)hr);
        goto cleanup;
    }

    hr = voice->lpVtbl->SetOutput(voice, (IUnknown *)stream, TRUE);
    if (FAILED(hr)) {
        fwprintf(stderr, L"SetOutput(stream) failed, hr=0x%08lX\n", (unsigned long)hr);
        goto cleanup;
    }
    output_is_bound = true;

    hr = voice->lpVtbl->Speak(voice, text, SPF_DEFAULT, NULL);
    if (FAILED(hr)) {
        fwprintf(stderr, L"Speak failed, hr=0x%08lX\n", (unsigned long)hr);
        goto cleanup;
    }

    hr = voice->lpVtbl->WaitUntilDone(voice, INFINITE);
    if (FAILED(hr)) {
        fwprintf(stderr, L"WaitUntilDone failed, hr=0x%08lX\n", (unsigned long)hr);
        goto cleanup;
    }

    ok = true;

cleanup:
    if (output_is_bound) {
        (void)voice->lpVtbl->SetOutput(voice, NULL, TRUE);
    }
    if (stream != NULL) {
        stream->lpVtbl->Release(stream);
    }
    return ok;
}

int main(void)
{
    FILE *fp = NULL;
    HRESULT hr = S_OK;
    ISpVoice *voice = NULL;
    bool com_initialized = false;
    bool is_first_line = true;
    bool success = false;
    char line[MAX_LINE_LENGTH];
    wchar_t lines[MAX_SEGMENT_LINES][MAX_LINE_LENGTH];
    size_t line_count = 0U;
    unsigned int seg_num = 1U;

    if (!ensure_output_directory()) {
        fprintf(stderr, "cannot create or access output directory\n");
        return 1;
    }

    fp = fopen("input.txt", "rb");
    if (fp == NULL) {
        fprintf(stderr, "cannot open input.txt\n");
        return 1;
    }

    hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        fprintf(stderr, "CoInitialize failed, hr=0x%08lX\n", (unsigned long)hr);
        goto cleanup;
    }
    com_initialized = true;

    hr = CoCreateInstance(
        &CLSID_SpVoice,
        NULL,
        CLSCTX_ALL,
        &IID_ISpVoice,
        (void **)&voice);

    if (FAILED(hr)) {
        fprintf(stderr, "Create voice failed, hr=0x%08lX\n", (unsigned long)hr);
        goto cleanup;
    }

    hr = voice->lpVtbl->SetRate(voice, 3);
    if (FAILED(hr)) {
        fprintf(stderr, "SetRate failed, hr=0x%08lX\n", (unsigned long)hr);
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        wchar_t segment[SEGMENT_TEXT_CAPACITY];

        trim_line_end(line);
        strip_utf8_bom_if_needed(line, &is_first_line);

        if (!convert_to_wide_with_fallback(line, lines[line_count], MAX_LINE_LENGTH)) {
            fprintf(stderr, "line in segment %u encoding conversion failed, skipped\n", seg_num);
            continue;
        }

        line_count++;

        if (line_count == LINES_PER_SEGMENT) {
            segment[0] = L'\0';

            if (!build_segment_text(
                    segment,
                    SEGMENT_TEXT_CAPACITY,
                    lines,
                    line_count,
                    seg_num)) {
                fprintf(stderr, "segment %u text too long, skipped\n", seg_num);
            } else if (seg_num >= START_SEGMENT && seg_num <= END_SEGMENT) {
                (void)speak_to_wav(voice, segment, seg_num);
            }

            line_count = 0U;
            seg_num++;

            if (seg_num > END_SEGMENT) {
                break;
            }
        }
    }

    if (line_count > 0U && seg_num <= END_SEGMENT) {
        wchar_t segment[SEGMENT_TEXT_CAPACITY];
        segment[0] = L'\0';

        if (!build_segment_text(
                segment,
                SEGMENT_TEXT_CAPACITY,
                lines,
                line_count,
                seg_num)) {
            fprintf(stderr, "segment %u text too long, skipped\n", seg_num);
        } else if (seg_num >= START_SEGMENT && seg_num <= END_SEGMENT) {
            (void)speak_to_wav(voice, segment, seg_num);
        }
    }

    success = true;

cleanup:
    if (voice != NULL) {
        voice->lpVtbl->Release(voice);
    }
    if (com_initialized) {
        CoUninitialize();
    }
    if (fp != NULL) {
        fclose(fp);
    }
    return success ? 0 : 1;
}
