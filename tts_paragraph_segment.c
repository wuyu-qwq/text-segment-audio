#include <windows.h>
#include <sapi.h>
#ifndef SPDFID_WaveFormatEx
const GUID SPDFID_WaveFormatEx =
{ 0xC31ADBAE, 0x527F, 0x4ff5, {0xA2,0x30,0xF6,0x2B,0xB6,0x1F,0xF7,0x0C} };
#endif
#include <stdio.h>
#include <string.h>

#define MAX_LINE_LENGTH 1024
#define LINES_PER_SEGMENT 20
#define MAX_SEGMENT_LINES 50
/* 需要处理的段落范围 */
#define START_SEGMENT 130
#define END_SEGMENT   300


/* 合并多行文本为一个段落 */
void build_segment_text(
    wchar_t *out,
    wchar_t lines[][MAX_LINE_LENGTH],
    int line_count,
    int seg_num)
{
    swprintf(out, MAX_LINE_LENGTH*MAX_SEGMENT_LINES, L"%d, ", seg_num);

    for (unsigned int i=0; i<line_count; ++i) {
        wcscat(out, lines[i]);
        if (i < line_count-1) wcscat(out, L" ");
    }
}


/* 生成 WAV */
void speak_to_wav(
    ISpVoice *voice,
    const wchar_t *text,
    int seg_num)
{
    HRESULT hr;
    ISpStream *stream = NULL;

    WCHAR filename[64];

    swprintf(filename, 64, L"output\\segment_%d.wav", seg_num);

    hr = CoCreateInstance(
            &CLSID_SpStream,
            NULL,
            CLSCTX_ALL,
            &IID_ISpStream,
            (void**)&stream);

    if (FAILED(hr)) {
        wprintf(L"Create stream failed\n");
        return;
    }

    /* 指定 WAV 格式 */
    WAVEFORMATEX fmt;
    memset(&fmt, 0, sizeof(fmt));

    fmt.wFormatTag 		= WAVE_FORMAT_PCM;
    fmt.nChannels 	 	= 1;
    fmt.nSamplesPerSec 	= 22050;
    fmt.wBitsPerSample 	= 16;
    fmt.nBlockAlign 	= fmt.nChannels * fmt.wBitsPerSample/8;
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

    hr = stream->lpVtbl->BindToFile(
            stream,
            filename,
            SPFM_CREATE_ALWAYS,
            &SPDFID_WaveFormatEx,
            &fmt,
            0);

    if (FAILED(hr)) {
        wprintf(L"Bind file failed\n");
        stream->lpVtbl->Release(stream);
        return;
    }

    voice->lpVtbl->SetOutput(voice, (IUnknown*)stream, TRUE);

    voice->lpVtbl->Speak(
        voice,
        text,
        SPF_DEFAULT,
        NULL);

    voice->lpVtbl->WaitUntilDone(voice, INFINITE);

    voice->lpVtbl->SetOutput(voice, NULL, TRUE);

    stream->lpVtbl->Release(stream);
}



int main() {

    FILE *fp = fopen("input.txt", "rb");

    if (!fp) {
        printf("cannot open input.txt\n");
        return 1;
    }

    HRESULT hr = CoInitialize(NULL);

    if (FAILED(hr)) {
        printf("CoInitialize failed\n");
        return 1;
    }

    ISpVoice *voice = NULL;

    hr = CoCreateInstance(
            &CLSID_SpVoice,
            NULL,
            CLSCTX_ALL,
            &IID_ISpVoice,
            (void**)&voice);

    if (FAILED(hr)) {
        printf("Create voice failed\n");
        CoUninitialize();
        return 1;
    }

    /* 设置语速 (-10 ~ 10) */
    voice->lpVtbl->SetRate(voice, 3);

    char line[MAX_LINE_LENGTH];

    wchar_t lines[MAX_SEGMENT_LINES][MAX_LINE_LENGTH];

    unsigned int line_count = 0;
    unsigned int seg_num    = 1;


    while (fgets(line, sizeof(line), fp)) {

        size_t len = strlen(line);

        while (len > 0 &&
               (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = 0;

        /* UTF8 → UTF16 */
        MultiByteToWideChar(
            CP_UTF8,
            0,
            line,
            -1,
            lines[line_count],
            MAX_LINE_LENGTH);

        line_count++;

        if (line_count == LINES_PER_SEGMENT) {

            wchar_t segment[MAX_LINE_LENGTH*MAX_SEGMENT_LINES];
            segment[0] = 0;

            build_segment_text(
                segment,
                lines,
                line_count,
                seg_num);

            /* 只处理指定段落 */
            if (seg_num >= START_SEGMENT &&
                seg_num <= END_SEGMENT)
            {
                speak_to_wav(voice, segment, seg_num);
            }

            line_count = 0;
            seg_num++;

            /* 超过范围直接停止 */
            if (seg_num > END_SEGMENT) break;
        }
    }


    /* 处理最后一段 */
    if (line_count > 0 && seg_num <= END_SEGMENT) {

        wchar_t segment[MAX_LINE_LENGTH*MAX_SEGMENT_LINES];
        segment[0] = 0;

        build_segment_text(
            segment,
            lines,
            line_count,
            seg_num);

        if (seg_num >= START_SEGMENT &&
            seg_num <= END_SEGMENT)
        {
            speak_to_wav(voice, segment, seg_num);
        }
    }


    voice->lpVtbl->Release(voice);

    CoUninitialize();

    fclose(fp);

    return 0;
}