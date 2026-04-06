# text-segment-audio

一个基于 Windows SAPI 的批量文本转 WAV 工具，支持按行分段、命令行完整配置、并行多线程生成。

## 特性

- 按 `N` 行自动分段，并支持 `start/end` 段范围
- 多线程并行生成（每个线程独立 `ISpVoice`）
- 可选全局 SAPI 互斥锁（兼容模式）
- 输入编码可选：`auto` / `utf8` / `acp`
- 详细 CLI 配置项与 `--help`
- 清晰的失败日志和统计摘要

## 目录结构

- `include/`：头文件（配置解析、分段构建、文件路径、SAPI、线程池）
- `src/`：实现文件
- `CMakeLists.txt`：构建定义

## 构建

### 使用 PowerShell 构建脚本（推荐，零依赖）

```powershell
.\build.ps1
```

生成文件：`text_segment_audio.exe`

### 使用 CMake + MinGW

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build -j
```

生成文件：`build/text_segment_audio.exe`

## 快速开始

```powershell
.\build\text_segment_audio.exe --input input.txt --output-dir output
```

## 常用示例

### 1) 4 线程并行、只处理 1 到 20 段

```powershell
.\build\text_segment_audio.exe --input input.txt --output-dir output --threads 4 --start-segment 1 --end-segment 20
```

### 2) 指定语音名（模糊匹配）

```powershell
.\build\text_segment_audio.exe --voice-name "Huihui"
```

### 3) 启用全局 SAPI 互斥锁（兼容优先）

```powershell
.\build\text_segment_audio.exe --use-sapi-mutex
```

### 4) 仅预览任务，不实际生成

```powershell
.\build\text_segment_audio.exe --dry-run
```

## 全部参数

- `-i, --input <path>`：输入文本文件，默认 `input.txt`
- `-o, --output-dir <dir>`：输出目录，默认 `output`
- `--filename-template <tpl>`：输出文件名模板，必须包含 `{num}`，默认 `segment_{num}.wav`
- `--voice-name <text>`：语音名称关键字（SAPI voice description 模糊匹配）
- `--lines-per-segment <n>`：每段行数，默认 `20`
- `--start-segment <n>`：起始段号，默认 `1`
- `--end-segment <n>`：结束段号，`0` 表示不设上限，默认 `0`
- `--rate <n>`：语速，范围 `[-10, 10]`，默认 `3`
- `--volume <n>`：音量，范围 `[0, 100]`，默认 `100`
- `--sample-rate <n>`：WAV 采样率，默认 `22050`
- `--bits-per-sample <n>`：位深，默认 `16`
- `--channels <n>`：声道数，默认 `1`
- `-j, --threads <n>`：线程数，`0` 为自动（CPU 核数），默认 `0`
- `--encoding <auto|utf8|acp>`：输入解码方式，默认 `auto`
- `--skip-empty-lines`：忽略空行（默认开启）
- `--keep-empty-lines`：保留空行参与分段
- `--continue-on-error`：遇错继续（默认开启）
- `--fail-fast`：遇错即停
- `--use-sapi-mutex`：启用全局 SAPI 互斥锁
- `--no-sapi-mutex`：禁用全局 SAPI 互斥锁（默认）
- `--dry-run`：只打印计划任务，不调用 SAPI
- `-h, --help`：显示帮助

## 并发与互斥说明

- 默认模式：每个线程独立初始化 COM 与 `ISpVoice`，并行生成，吞吐优先。
- 互斥模式：`--use-sapi-mutex` 时，在合成关键区对 SAPI 调用加全局锁，兼容优先。

## 兼容性说明

- 仅支持 Windows（依赖 SAPI）
- 工具链建议：MSVC 或 MinGW-w64
