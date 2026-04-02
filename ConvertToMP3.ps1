$inputDir = ".\output"
$outputDir = ".\mp3"

# 如果 mp3 目录不存在则创建
if (!(Test-Path $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir | Out-Null
}

# 遍历 wav 文件
Get-ChildItem -Path $inputDir -Filter *.wav | ForEach-Object {

    $wavFile = $_.FullName
    $mp3Name = [System.IO.Path]::ChangeExtension($_.Name, ".mp3")
    $mp3File = Join-Path $outputDir $mp3Name

    # 如果 mp3 已存在则跳过
    if (Test-Path $mp3File) {
        Write-Host "Skip existing: $mp3Name"
        return
    }

    Write-Host "Converting: $($_.Name) -> $mp3Name"

    ffmpeg -i "$wavFile" -vn -codec:a libmp3lame -qscale:a 2 "$mp3File"
}