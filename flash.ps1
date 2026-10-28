# EK Instrumentation - ESP32 Classic PM & Gas One-Click Compiler / Flasher
# No EspUsbHost. Windows PowerShell 5.1+.
param(
  [ValidateSet('pm','gas')][string]$Firmware,
  [string]$Port,
  [switch]$CompileOnly,
  [switch]$SkipSetup
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch {}
if ($env:EK_PMGAS_ONESHOT -ne '1') { throw 'Jalankan melalui installer GitHub: irm https://raw.githubusercontent.com/efendiyhy/pmgas-esp32-firmware/main/install.ps1 | iex' }
$script:Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$script:Fqbn = 'esp32:esp32:esp32'
$script:BoardUrl = 'https://espressif.github.io/arduino-esp32/package_esp32_index.json'
$script:CoreVersion = '3.3.12'
$script:LogDir = Join-Path $script:Root 'logs'
$script:BuildRoot = Join-Path $script:Root 'build'
$script:ToolsDir = Join-Path $script:Root 'tools'
foreach ($dir in @($script:LogDir, $script:BuildRoot, $script:ToolsDir)) {
  New-Item -ItemType Directory -Force -Path $dir | Out-Null
}
$logName = 'flash-{0}.log' -f (Get-Date -Format 'yyyyMMdd-HHmmss')
$logPath = Join-Path $script:LogDir $logName
$transcribing = $false
try { Start-Transcript -LiteralPath $logPath -ErrorAction Stop | Out-Null; $transcribing = $true } catch { Write-Host 'Pencatatan transcript tidak didukung di terminal ini.' }

function Check-Exit([string]$Description) {
  if ($LASTEXITCODE -ne 0) { throw "$Description gagal (exit code $LASTEXITCODE). Periksa error terminal; file log sementara akan dibersihkan." }
}
function Ask-Choice([string]$Question, [string[]]$Allowed) {
  while ($true) {
    $r = (Read-Host $Question).Trim()
    if ($Allowed -contains $r) { return $r }
    Write-Host "Pilihan tidak valid: $($Allowed -join ', ')" -ForegroundColor Yellow
  }
}
function Get-ArduinoCLI {
  # Always use portable Arduino CLI inside this session. Never modify global CLI.
  $localExe = Join-Path $script:ToolsDir 'arduino-cli.exe'
  if (Test-Path $localExe) { return $localExe }
  if (-not [Environment]::Is64BitOperatingSystem) { throw 'Installer otomatis hanya mendukung Windows 64-bit.' }
  Write-Host 'Mengunduh Arduino CLI resmi (instal lokal tanpa administrator)...' -ForegroundColor Cyan
  $tempFile = Join-Path $script:ToolsDir 'arduino-cli.zip'
  Invoke-WebRequest -Uri 'https://downloads.arduino.cc/arduino-cli/arduino-cli_latest_Windows_64bit.zip' -OutFile $tempFile -UseBasicParsing
  Expand-Archive -LiteralPath $tempFile -DestinationPath $script:ToolsDir -Force
  Remove-Item -LiteralPath $tempFile -Force
  if (-not (Test-Path $localExe)) { throw 'Gagal menemukan arduino-cli.exe dari arsip resmi.' }
  return $localExe
}
function Ensure-Toolchain([string]$Cli) {
  Write-Host "Arduino CLI: $Cli" -ForegroundColor DarkCyan
  & $Cli version
  Check-Exit 'Periksa Arduino CLI'
  $actualData = ((& $Cli config get directories.data) | Out-String).Trim().Trim('"')
  Check-Exit 'Periksa lokasi Arduino data'
  $actualCache = ((& $Cli config get build_cache.path) | Out-String).Trim().Trim('"')
  Check-Exit 'Periksa lokasi Arduino build cache'
  if ($actualData -ne $env:ARDUINO_DIRECTORIES_DATA -or $actualCache -ne $env:ARDUINO_BUILD_CACHE_PATH) {
    throw "PENGAMAN: konfigurasi Arduino tidak terisolasi. Data=$actualData; Cache=$actualCache. Global Arduino tidak akan dimodifikasi."
  }
  Write-Host 'Arduino CLI memakai data & cache TEMP TERISOLASI.' -ForegroundColor Green
  if ($SkipSetup) {
    Write-Host 'SkipSetup aktif: lewati pemasangan board & library.' -ForegroundColor Yellow
    return
  }
  $cores = (& $Cli core list) | Out-String
  Check-Exit 'Daftar core'
  if ($cores -notmatch '(?m)^esp32:esp32\s+3\.') {
    Write-Host "Menyiapkan ESP32 core versi $($script:CoreVersion) ..." -ForegroundColor Cyan
    & $Cli core update-index --additional-urls $script:BoardUrl
    Check-Exit 'Update board index'
    & $Cli core install "esp32:esp32@$($script:CoreVersion)" --additional-urls $script:BoardUrl
    Check-Exit 'Instal ESP32 core'
  } else {
    Write-Host 'ESP32 core 3.x telah terpasang.' -ForegroundColor Green
  }
  $libs = (& $Cli lib list) | Out-String
  Check-Exit 'Daftar library'
  foreach ($lib in @('Adafruit SHT31 Library','Adafruit BusIO')) {
    if ($libs -notmatch [regex]::Escape($lib)) {
      Write-Host "Memasang library: $lib" -ForegroundColor Cyan
      & $Cli lib install $lib
      Check-Exit "Instal library $lib"
    }
  }
}
function Get-SerialPorts([string]$Cli) {
  $ports = @()
  try {
    $raw = (& $Cli board list --format json) | Out-String
    if ($LASTEXITCODE -eq 0) {
      $data = $raw | ConvertFrom-Json
      foreach ($item in @($data.detected_ports)) {
        if ($null -ne $item -and $null -ne $item.port -and $item.port.address -match '^COM\d+$') {
          $ports += [pscustomobject]@{ Name=$item.port.address.ToUpperInvariant(); Label=[string]$item.port.label }
        }
      }
    }
  } catch { Write-Host 'Deteksi Arduino CLI tidak lengkap, memakai daftar COM Windows.' -ForegroundColor Yellow }
  foreach ($p in [System.IO.Ports.SerialPort]::GetPortNames()) {
    if ($p -match '^COM\d+$' -and -not ($ports.Name -contains $p.ToUpperInvariant())) {
      $ports += [pscustomobject]@{Name=$p.ToUpperInvariant();Label='Serial USB (Windows)'}
    }
  }
  return @($ports | Sort-Object @{Expression={[int]($_.Name -replace '\D','')}} -Unique)
}
function Pick-Port([string]$Cli, [string]$GivenPort) {
  $ports = @(Get-SerialPorts $Cli)
  if ($ports.Count -eq 0) { throw 'Tidak ada port COM. Periksa kabel DATA USB dan driver CH340/CP210x.' }
  Write-Host "`nPORT USB TERDETEKSI" -ForegroundColor Cyan
  for ($i=0; $i -lt $ports.Count; $i++) { Write-Host " [$($i+1)] $($ports[$i].Name)  $($ports[$i].Label)" }
  if ($GivenPort) {
    if ($ports.Name -notcontains $GivenPort.ToUpperInvariant()) { throw "Port $GivenPort tidak ditemukan di Windows." }
    return $GivenPort.ToUpperInvariant()
  }
  while ($true) {
    $choice = Read-Host 'Pilih nomor port COM (bukan langsung flash)'
    $num = 0
    if ([int]::TryParse($choice, [ref]$num) -and $num -ge 1 -and $num -le $ports.Count) { return $ports[$num-1].Name }
    Write-Host 'Nomor COM tidak valid.' -ForegroundColor Yellow
  }
}
function Find-Esptool([string]$Cli) {
  $dirs = @()
  # Only consider the isolated Arduino package directory of this invocation.
  if ($env:ARDUINO_DIRECTORIES_DATA) { $dirs += (Join-Path $env:ARDUINO_DIRECTORIES_DATA 'packages\esp32\tools') }
  try {
    $d = (& $Cli config get directories.data) | Out-String
    if ($LASTEXITCODE -eq 0 -and $d.Trim()) { $dirs += (Join-Path $d.Trim() 'packages\esp32\tools') }
  } catch {}
  $candidates = @()
  foreach ($dir in ($dirs | Select-Object -Unique)) {
    if (Test-Path $dir) { $candidates += @(Get-ChildItem -LiteralPath $dir -Filter esptool.exe -File -Recurse -ErrorAction SilentlyContinue) }
  }
  $found = $candidates | Sort-Object -Property FullName -Descending | Select-Object -First 1
  if (-not $found) { throw 'esptool.exe tidak ditemukan di ESP32 board package. Jalankan tanpa -SkipSetup dan pastikan core Espressif terpasang.' }
  return $found.FullName
}
function Probe-ClassicESP32([string]$Esptool, [string]$SelectedPort) {
  Write-Host "`nVERIFIKASI CHIP (port $SelectedPort)..." -ForegroundColor Cyan
  # --chip esp32 refuses ESP32-S2 / S3 / C3 etc before any flashing.
  $probes = @('read_mac','read-mac')
  foreach ($operation in $probes) {
    $oldPref = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { $out = (& $Esptool --chip esp32 --port $SelectedPort --baud 115200 $operation 2>&1 | Out-String); $code = $LASTEXITCODE }
    finally { $ErrorActionPreference = $oldPref }
    if ($code -eq 0 -and $out -match '(?i)(?:MAC|MAC address)\s*:\s*([0-9A-F]{2}(?::[0-9A-F]{2}){5})') {
      # A successful read_mac with chip=esp32 is the definitive chip-family check.
      $mac = $Matches[1].ToUpperInvariant()
      Write-Host $out.Trim()
      Write-Host "ESP32 CLASSIC TERKONFIRMASI. MAC: $mac" -ForegroundColor Green
      return $mac
    }
  }
  throw 'Gagal memastikan ESP32 klasik dan MAC. Pastikan ESP32 biasa, tidak dipakai Serial Monitor, port benar. Coba tahan BOOT saat verifikasi. Flash dibatalkan.'
}

try {
  Write-Host "`n===============================================" -ForegroundColor Magenta
  Write-Host ' EK INSTRUMENTATION / ESP32 FIRMWARE FLASHER' -ForegroundColor Magenta
  Write-Host ' Hardware: ESP32-WROOM-32 / ESP32 DevKit V1' -ForegroundColor Magenta
  Write-Host "===============================================`n" -ForegroundColor Magenta
  if (-not $Firmware) {
    Write-Host '[1] Monitoring PM (2 x PMS5003T + SHT31)'
    Write-Host '[2] Monitoring Gas (MH-Z19C + MiCS-4514 + SHT31)'
    $selection = Ask-Choice 'Pilih alat: 1 atau 2' @('1','2')
    if ($selection -eq '1') { $Firmware = 'pm' } else { $Firmware = 'gas' }
  }
  $sketchName = if ($Firmware -eq 'pm') { 'PM_ESP32_CLASSIC' } else { 'GAS_ESP32_CLASSIC' }
  $sketch = Join-Path $script:Root "firmware\$sketchName"
  if (-not (Test-Path (Join-Path $sketch "$sketchName.ino"))) { throw "Tidak ada sketch $sketchName.ino di $sketch" }
  if (-not (Test-Path (Join-Path $sketch 'EK_Dashboard.h'))) { throw "Header EK_Dashboard.h tidak ada di $sketch" }
  Write-Host "Firmware terpilih: $($Firmware.ToUpperInvariant())" -ForegroundColor Green
  $cli = Get-ArduinoCLI
  Ensure-Toolchain $cli
  $buildDir = Join-Path $script:BuildRoot $sketchName
  New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
  Write-Host "`nKOMPILASI $sketchName ..." -ForegroundColor Cyan
  & $cli compile --fqbn $script:Fqbn --build-path $buildDir $sketch
  Check-Exit "Compile $sketchName"
  Write-Host 'KOMPILASI BERHASIL.' -ForegroundColor Green
  if ($CompileOnly) { Write-Host 'MODE COMPILE ONLY: tidak ada perangkat yang di-flash.' -ForegroundColor Yellow; return }
  $mode = Ask-Choice 'Lanjut verifikasi & upload? Ketik Y (ya) atau N (tidak)' @('Y','y','N','n')
  if ($mode -match '^[Nn]$') { Write-Host 'Upload dibatalkan oleh pengguna.'; return }
  $portChosen = Pick-Port $cli $Port
  $esptool = Find-Esptool $cli
  $mac = Probe-ClassicESP32 $esptool $portChosen
  Write-Host "`nKONFIRMASI FINAL" -ForegroundColor Yellow
  Write-Host " ALAT    : $($Firmware.ToUpperInvariant())"
  Write-Host " CHIP    : ESP32 CLASSIC"
  Write-Host " PORT    : $portChosen"
  Write-Host " MAC     : $mac"
  Write-Host ' PERHATIAN: Upload akan mengganti firmware yang saat ini ada di ESP32.'
  Write-Host ' JANGAN pilih GAS jika perangkat fisiknya PM, atau sebaliknya.'
  $literal = Ask-Choice "Ketik $($Firmware.ToUpperInvariant()) untuk setuju FLASH, atau N untuk batal" @($Firmware.ToUpperInvariant(),'N','n')
  if ($literal -ne $Firmware.ToUpperInvariant()) { Write-Host 'Upload dibatalkan.'; return }
  Write-Host "`nUPLOAD KE $portChosen ..." -ForegroundColor Cyan
  & $cli upload --fqbn $script:Fqbn --port $portChosen --build-path $buildDir $sketch
  Check-Exit "Upload $sketchName"
  Write-Host "`nSUKSES: firmware $($Firmware.ToUpperInvariant()) berhasil di-upload ke $portChosen / $mac" -ForegroundColor Green
  Write-Host 'MAC hanya dipakai untuk verifikasi sesi ini; tidak disimpan ke disk permanen.' -ForegroundColor Cyan
  Write-Host 'Hotspot perangkat muncul setelah boot; buka 192.168.4.1 untuk pengaturan.'
}
catch {
  Write-Host "`n[ERROR] $($_.Exception.Message)" -ForegroundColor Red
  Write-Host 'Log sementara akan otomatis dihapus; salin pesan error terminal bila diperlukan.' -ForegroundColor Yellow
  throw
}
finally {
  if ($transcribing) { try { Stop-Transcript | Out-Null } catch {} }
}
