param(
    [string]$Output = "text_segment_audio.exe"
)

$ErrorActionPreference = "Stop"

$Sources = @(
    "src/main.c",
    "src/common.c",
    "src/app_config.c",
    "src/fs_utils.c",
    "src/segment_builder.c",
    "src/sapi_tts.c",
    "src/worker_pool.c"
)

$Args = @(
    "-std=c11",
    "-Wall",
    "-Wextra",
    "-Wpedantic",
    "-Wconversion",
    "-Wshadow",
    "-Wformat=2",
    "-Iinclude"
) + $Sources + @(
    "-o", $Output,
    "-lole32",
    "-luuid",
    "-lsapi"
)

Write-Host "Building $Output ..."
& gcc @Args
if ($LASTEXITCODE -ne 0) {
    throw "gcc build failed with exit code $LASTEXITCODE"
}

Write-Host "Build succeeded: $Output"
