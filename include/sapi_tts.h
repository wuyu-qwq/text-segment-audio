#ifndef SAPI_TTS_H
#define SAPI_TTS_H

#include "app_config.h"

#include <stdbool.h>
#include <stddef.h>
#include <windows.h>
#include <sapi.h>

typedef struct SapiEngine {
    ISpVoice *voice;
    WAVEFORMATEX format;
    HANDLE sapi_mutex;
} SapiEngine;

bool sapi_engine_init(
    SapiEngine *engine,
    const AppConfig *cfg,
    HANDLE sapi_mutex,
    char *err,
    size_t err_size);

void sapi_engine_cleanup(SapiEngine *engine);

bool sapi_engine_speak_to_wav(
    SapiEngine *engine,
    const wchar_t *text,
    const wchar_t *output_path,
    char *err,
    size_t err_size);

#endif
