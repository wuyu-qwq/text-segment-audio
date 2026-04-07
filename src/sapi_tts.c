#include "sapi_tts.h"
#include "common.h"

#include <sapi.h>

#ifndef SPDFID_WaveFormatEx
const GUID SPDFID_WaveFormatEx =
{ 0xC31ADBAE, 0x527F, 0x4ff5, {0xA2, 0x30, 0xF6, 0x2B, 0xB6, 0x1F, 0xF7, 0x0C} };
#endif

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static bool ansi_to_wide_string(const char *src, wchar_t *dst, size_t dst_count)
{
    int converted = MultiByteToWideChar(CP_ACP, 0, src, -1, dst, (int)dst_count);
    return converted > 0;
}

static bool select_voice_by_name(
    ISpVoice *voice,
    const char *voice_name,
    char *err,
    size_t err_size)
{
    HRESULT hr;
    ISpObjectTokenCategory *category = NULL;
    IEnumSpObjectTokens *enum_tokens = NULL;
    ISpObjectToken *token = NULL;
    ULONG fetched = 0UL;
    wchar_t needle[256];
    bool found = false;

    if (!ansi_to_wide_string(voice_name, needle, sizeof(needle) / sizeof(needle[0]))) {
        set_error(err, err_size, "failed to convert --voice-name to wide string");
        return false;
    }

    hr = CoCreateInstance(
        &CLSID_SpObjectTokenCategory,
        NULL,
        CLSCTX_ALL,
        &IID_ISpObjectTokenCategory,
        (void **)&category);
    if (FAILED(hr)) {
        set_error(err, err_size, "CoCreateInstance(CLSID_SpObjectTokenCategory) failed, hr=0x%08lX", (unsigned long)hr);
        return false;
    }

    hr = category->lpVtbl->SetId(category, SPCAT_VOICES, FALSE);
    if (FAILED(hr)) {
        category->lpVtbl->Release(category);
        set_error(err, err_size, "ISpObjectTokenCategory::SetId failed, hr=0x%08lX", (unsigned long)hr);
        return false;
    }

    hr = category->lpVtbl->EnumTokens(category, NULL, NULL, &enum_tokens);
    if (FAILED(hr)) {
        category->lpVtbl->Release(category);
        set_error(err, err_size, "ISpObjectTokenCategory::EnumTokens failed, hr=0x%08lX", (unsigned long)hr);
        return false;
    }

    while (enum_tokens->lpVtbl->Next(enum_tokens, 1, &token, &fetched) == S_OK && fetched == 1UL) {
        WCHAR *description = NULL;
        WCHAR *token_id = NULL;
        bool matched = false;

        (void)token->lpVtbl->GetStringValue(token, NULL, &description);
        (void)token->lpVtbl->GetId(token, &token_id);

        if (description != NULL && wcsstr(description, needle) != NULL) {
            matched = true;
        } else if (token_id != NULL && wcsstr(token_id, needle) != NULL) {
            matched = true;
        }

        if (matched) {
            hr = voice->lpVtbl->SetVoice(voice, token);
            if (FAILED(hr)) {
                if (description != NULL) {
                    CoTaskMemFree(description);
                }
                if (token_id != NULL) {
                    CoTaskMemFree(token_id);
                }
                token->lpVtbl->Release(token);
                enum_tokens->lpVtbl->Release(enum_tokens);
                category->lpVtbl->Release(category);
                set_error(err, err_size, "SetVoice failed, hr=0x%08lX", (unsigned long)hr);
                return false;
            }
            found = true;
        }

        if (description != NULL) {
            CoTaskMemFree(description);
        }
        if (token_id != NULL) {
            CoTaskMemFree(token_id);
        }

        token->lpVtbl->Release(token);
        token = NULL;

        if (found) {
            break;
        }
    }

    enum_tokens->lpVtbl->Release(enum_tokens);
    category->lpVtbl->Release(category);

    if (!found) {
        set_error(err, err_size, "no voice matched --voice-name: %s", voice_name);
        return false;
    }

    return true;
}

bool sapi_engine_init(
    SapiEngine *engine,
    const AppConfig *cfg,
    HANDLE sapi_mutex,
    char *err,
    size_t err_size)
{
    HRESULT hr;

    memset(engine, 0, sizeof(*engine));
    engine->sapi_mutex = sapi_mutex;

    hr = CoCreateInstance(
        &CLSID_SpVoice,
        NULL,
        CLSCTX_ALL,
        &IID_ISpVoice,
        (void **)&engine->voice);
    if (FAILED(hr)) {
        set_error(err, err_size, "CoCreateInstance(CLSID_SpVoice) failed, hr=0x%08lX", (unsigned long)hr);
        return false;
    }

    hr = engine->voice->lpVtbl->SetRate(engine->voice, cfg->rate);
    if (FAILED(hr)) {
        set_error(err, err_size, "SetRate failed, hr=0x%08lX", (unsigned long)hr);
        sapi_engine_cleanup(engine);
        return false;
    }

    hr = engine->voice->lpVtbl->SetVolume(engine->voice, (USHORT)cfg->volume);
    if (FAILED(hr)) {
        set_error(err, err_size, "SetVolume failed, hr=0x%08lX", (unsigned long)hr);
        sapi_engine_cleanup(engine);
        return false;
    }

    if (cfg->voice_name != NULL && cfg->voice_name[0] != '\0') {
        if (!select_voice_by_name(engine->voice, cfg->voice_name, err, err_size)) {
            sapi_engine_cleanup(engine);
            return false;
        }
    }

    memset(&engine->format, 0, sizeof(engine->format));
    engine->format.wFormatTag = WAVE_FORMAT_PCM;
    engine->format.nChannels = cfg->channels;
    engine->format.nSamplesPerSec = cfg->sample_rate;
    engine->format.wBitsPerSample = cfg->bits_per_sample;
    engine->format.nBlockAlign = (WORD)(engine->format.nChannels * (engine->format.wBitsPerSample / 8U));
    engine->format.nAvgBytesPerSec = engine->format.nSamplesPerSec * engine->format.nBlockAlign;

    return true;
}

void sapi_engine_cleanup(SapiEngine *engine)
{
    if (engine->voice != NULL) {
        engine->voice->lpVtbl->Release(engine->voice);
        engine->voice = NULL;
    }
}

bool sapi_engine_speak_to_wav(
    SapiEngine *engine,
    const wchar_t *text,
    const wchar_t *output_path,
    char *err,
    size_t err_size)
{
    HRESULT hr;
    ISpStream *stream = NULL;
    bool set_output_ok = false;
    bool mutex_locked = false;

    hr = CoCreateInstance(
        &CLSID_SpStream,
        NULL,
        CLSCTX_ALL,
        &IID_ISpStream,
        (void **)&stream);
    if (FAILED(hr)) {
        set_error(err, err_size, "CoCreateInstance(CLSID_SpStream) failed, hr=0x%08lX", (unsigned long)hr);
        return false;
    }

    hr = stream->lpVtbl->BindToFile(
        stream,
        output_path,
        SPFM_CREATE_ALWAYS,
        &SPDFID_WaveFormatEx,
        &engine->format,
        0);
    if (FAILED(hr)) {
        set_error(err, err_size, "BindToFile failed, hr=0x%08lX", (unsigned long)hr);
        stream->lpVtbl->Release(stream);
        return false;
    }

    if (engine->sapi_mutex != NULL) {
        DWORD wait_result = WaitForSingleObject(engine->sapi_mutex, INFINITE);
        if (wait_result != WAIT_OBJECT_0) {
            set_error(err, err_size, "WaitForSingleObject(sapi_mutex) failed");
            stream->lpVtbl->Release(stream);
            return false;
        }
        mutex_locked = true;
    }

    hr = engine->voice->lpVtbl->SetOutput(engine->voice, (IUnknown *)stream, TRUE);
    if (FAILED(hr)) {
        set_error(err, err_size, "SetOutput(stream) failed, hr=0x%08lX", (unsigned long)hr);
        goto cleanup;
    }
    set_output_ok = true;

    hr = engine->voice->lpVtbl->Speak(engine->voice, text, SPF_DEFAULT, NULL);
    if (FAILED(hr)) {
        set_error(err, err_size, "Speak failed, hr=0x%08lX", (unsigned long)hr);
        goto cleanup;
    }

    hr = engine->voice->lpVtbl->WaitUntilDone(engine->voice, INFINITE);
    if (FAILED(hr)) {
        set_error(err, err_size, "WaitUntilDone failed, hr=0x%08lX", (unsigned long)hr);
        goto cleanup;
    }

cleanup:
    if (set_output_ok) {
        (void)engine->voice->lpVtbl->SetOutput(engine->voice, NULL, TRUE);
    }

    if (mutex_locked) {
        (void)ReleaseMutex(engine->sapi_mutex);
    }

    stream->lpVtbl->Release(stream);
    return SUCCEEDED(hr);
}
