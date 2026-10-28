# EK Instrumentation | PM + Gas ESP32 | One-Shot GitHub Flasher
# UTF-8 without BOM. Windows PowerShell 5.1+, 64-bit.
# Every invocation downloads the current main branch and removes ONLY its own temp workspace.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch {}

$Repository = 'efendiyhy/pmgas-esp32-firmware'
$Branch = 'main'
$LegacyInstall = Join-Path $env:LOCALAPPDATA 'EKInstrumentation\PMGasFirmwareTool'
$Session = Join-Path $env:TEMP ('EK-PMGAS-ONESHOT-' + [Guid]::NewGuid().ToString('N'))
$Archive = Join-Path $Session 'source.zip'
$SourceDir = Join-Path $Session 'src'
$previousEnv = @{}
$isolatedEnv = @{
  EK_PMGAS_ONESHOT = '1'
  ARDUINO_DIRECTORIES_DATA = (Join-Path $Session 'arduino\data')
  ARDUINO_DIRECTORIES_DOWNLOADS = (Join-Path $Session 'arduino\downloads')
  ARDUINO_DIRECTORIES_USER = (Join-Path $Session 'arduino\user')
  ARDUINO_BUILD_CACHE_PATH = (Join-Path $Session 'arduino\cache')
  ARDUINO_BOARD_MANAGER_ADDITIONAL_URLS = 'https://espressif.github.io/arduino-esp32/package_esp32_index.json'
}
$exitCode = 0
try {
  if (-not [Environment]::Is64BitOperatingSystem) { throw 'Diperlukan Windows 64-bit.' }
  Write-Host ''
  Write-Host '===============================================' -ForegroundColor Magenta
  Write-Host '  EK INSTRUMENTATION | ESP32 PM + GAS' -ForegroundColor Magenta
  Write-Host '      GITHUB ONE-SHOT / NO LOCAL INSTALL' -ForegroundColor Magenta
  Write-Host '===============================================' -ForegroundColor Magenta
  Write-Host "Repo : $Repository / $Branch"
  Write-Host 'Setiap eksekusi mengambil kode terbaru dari GitHub.'
  Write-Host '[1/3] Download source sementara...'
  New-Item -ItemType Directory -Force -Path $Session, $SourceDir | Out-Null
  $uri = "https://codeload.github.com/$Repository/zip/refs/heads/$Branch"
  Invoke-WebRequest -Uri $uri -OutFile $Archive -UseBasicParsing
  Expand-Archive -LiteralPath $Archive -DestinationPath $SourceDir -Force
  $root = Get-ChildItem -LiteralPath $SourceDir -Directory | Where-Object {
    (Test-Path -LiteralPath (Join-Path $_.FullName 'flash.ps1')) -and
    (Test-Path -LiteralPath (Join-Path $_.FullName 'firmware\PM_ESP32_CLASSIC\PM_ESP32_CLASSIC.ino')) -and
    (Test-Path -LiteralPath (Join-Path $_.FullName 'firmware\GAS_ESP32_CLASSIC\GAS_ESP32_CLASSIC.ino'))
  } | Select-Object -First 1
  if (-not $root) { throw 'Struktur firmware tidak lengkap pada repository GitHub.' }
  Write-Host "[2/3] Menyiapkan lingkungan Arduino TERISOLASI..." -ForegroundColor Cyan
  foreach ($item in $isolatedEnv.GetEnumerator()) {
    $previousEnv[$item.Key] = [Environment]::GetEnvironmentVariable($item.Key,'Process')
    [Environment]::SetEnvironmentVariable($item.Key,$item.Value,'Process')
  }
  foreach ($path in @($isolatedEnv.ARDUINO_DIRECTORIES_DATA,$isolatedEnv.ARDUINO_DIRECTORIES_DOWNLOADS,$isolatedEnv.ARDUINO_DIRECTORIES_USER,$isolatedEnv.ARDUINO_BUILD_CACHE_PATH)) {
    New-Item -ItemType Directory -Path $path -Force | Out-Null
  }
  Write-Host '[3/3] Memulai compile / upload (konfirmasi diperlukan)...' -ForegroundColor Cyan
  Write-Host 'Catatan: toolchain ESP32 diunduh ulang pada setiap proses; ukurannya besar.' -ForegroundColor Yellow
  & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root.FullName 'flash.ps1')
  $exitCode = $LASTEXITCODE
  if ($exitCode -ne 0) { throw "Compile/upload gagal (exit code $exitCode). Salin pesan error di terminal sebelum menutupnya." }
  Write-Host 'SELESAI. Tidak ada folder firmware permanen yang dibuat.' -ForegroundColor Green
}
catch {
  $exitCode = 1
  Write-Host "`n[GAGAL] $($_.Exception.Message)" -ForegroundColor Red
  Write-Host 'Pembersihan folder sesi tetap akan dijalankan.' -ForegroundColor Yellow
}
finally {
  Write-Host "`nMembersihkan semua file sementara yang dibuat installer ini..." -ForegroundColor Cyan
  foreach ($key in $isolatedEnv.Keys) {
    [Environment]::SetEnvironmentVariable($key,$previousEnv[$key],'Process')
  }
  if (Test-Path -LiteralPath $Session) {
    $removed = $false
    for ($i=0;$i -lt 4;$i++) {
      try {
        Remove-Item -LiteralPath $Session -Recurse -Force -ErrorAction Stop
        $removed = -not (Test-Path -LiteralPath $Session)
        if ($removed) { break }
      }
      catch { Start-Sleep -Milliseconds 450 }
    }
    if (-not $removed) {
      Write-Host "PERINGATAN: folder sementara gagal dihapus otomatis: $Session" -ForegroundColor Yellow
      Write-Host 'Periksa proses yang masih mengunci file, lalu hapus folder tersebut secara manual.' -ForegroundColor Yellow
    } else {
      Write-Host 'Folder sesi sementara berhasil dihapus.' -ForegroundColor Green
    }
  }
  # Remove ONLY the known permanent folder used by the OLD EK firmware installer.
  # Do not touch Arduino IDE, the user's own sketches, other Arduino toolchains, or laptop-server.
  if (Test-Path -LiteralPath $LegacyInstall) {
    try {
      Remove-Item -LiteralPath $LegacyInstall -Recurse -Force -ErrorAction Stop
      Write-Host 'Folder firmware installer versi lama juga berhasil dibersihkan.' -ForegroundColor Green
    }
    catch {
      Write-Host "PERINGATAN: instalasi firmware lama tidak bisa dihapus: $LegacyInstall" -ForegroundColor Yellow
    }
  }
}
# No exit here: an exit in irm | iex can also close the caller PowerShell session.
if ($exitCode -ne 0) { Write-Host 'Proses berakhir dengan kegagalan. Periksa pesan di atas.' -ForegroundColor Red }
