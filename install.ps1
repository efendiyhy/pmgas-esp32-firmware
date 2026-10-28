# EK Instrumentation - PM/Gas ESP32 Classic firmware installer (Windows PowerShell 5.1+).
# UTF-8 without BOM: compatible with irm ... | iex.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch {}

# If you use another repository name, change ONLY this value before pushing.
$Repository = 'efendiyhy/pmgas-esp32-firmware'
$Branch = 'main'
$destination = Join-Path $env:LOCALAPPDATA 'EKInstrumentation\PMGasFirmwareTool'
$downloadDir = Join-Path $env:TEMP ('EKFirmware-' + [Guid]::NewGuid().ToString('N'))
$archive = Join-Path $downloadDir 'source.zip'

try {
    Write-Host "`nEK INSTRUMENTATION / ESP32 PM + GAS FIRMWARE" -ForegroundColor Magenta
    Write-Host "Repository: $Repository ($Branch)"
    Write-Host '[1/3] Mengunduh firmware dan tool terbaru...'
    New-Item -ItemType Directory -Force -Path $downloadDir, $destination | Out-Null
    $url = "https://codeload.github.com/$Repository/zip/refs/heads/$Branch"
    Invoke-WebRequest -Uri $url -OutFile $archive -UseBasicParsing -ErrorAction Stop
    Expand-Archive -LiteralPath $archive -DestinationPath $downloadDir -Force
    $root = Get-ChildItem -LiteralPath $downloadDir -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'flash.ps1') } | Select-Object -First 1
    if (-not $root) { throw 'File flash.ps1 tidak ditemukan dalam repository. Pastikan struktur GitHub benar.' }

    Write-Host '[2/3] Memasang file firmware (log dan registrasi perangkat tetap disimpan)...'
    Copy-Item -LiteralPath (Join-Path $root.FullName 'flash.ps1') -Destination (Join-Path $destination 'flash.ps1') -Force
    Copy-Item -LiteralPath (Join-Path $root.FullName 'start_flash.cmd') -Destination (Join-Path $destination 'start_flash.cmd') -Force
    $firmwareDir = Join-Path $destination 'firmware'
    if (Test-Path $firmwareDir) { Remove-Item -LiteralPath $firmwareDir -Recurse -Force }
    Copy-Item -LiteralPath (Join-Path $root.FullName 'firmware') -Destination $firmwareDir -Recurse -Force
    Copy-Item -LiteralPath (Join-Path $root.FullName 'README.md') -Destination (Join-Path $destination 'README.md') -Force
    foreach ($folder in @('logs','build','tools','config')) { New-Item -ItemType Directory -Force -Path (Join-Path $destination $folder) | Out-Null }
    Copy-Item -LiteralPath (Join-Path $root.FullName 'config\devices.example.csv') -Destination (Join-Path $destination 'config\devices.example.csv') -Force

    Write-Host '[3/3] Membuka tool firmware...'
    Write-Host "Folder: $destination" -ForegroundColor Green
    Write-Host 'Perangkat baru harus dipilih dan dikonfirmasi sebelum di-flash.'
    # Use a child PowerShell with process-only policy bypass, even if Windows policy is Restricted.
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $destination 'flash.ps1')
    if ($LASTEXITCODE -ne 0) { throw "Firmware tool gagal (exit code $LASTEXITCODE)." }
}
catch {
    Write-Host "`nGAGAL: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host 'Jika repo belum dibuat: buat repo publik sesuai nama pada $Repository lalu push seluruh file.'
    throw
}
finally {
    if (Test-Path $downloadDir) { Remove-Item -LiteralPath $downloadDir -Recurse -Force -ErrorAction SilentlyContinue }
}
