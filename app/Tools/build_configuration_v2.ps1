param(
    [ValidateSet('baseline', 'integrated')]
    [string]$Phase = 'integrated',
    [ValidateSet('app_A', 'app_B')]
    [string[]]$Targets = @('app_A', 'app_B'),
    [string]$Keil = 'C:/Program Files/Keil_v5/UV4/UV4.exe',
    [string]$BuiltRevision,
    [switch]$CollectOnly
)

$ErrorActionPreference = 'Stop'
$repository = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$project = Join-Path $repository 'app/MDK-ARM/modbus-gateway-stm32.uvprojx'
$revision = (git -C $repository rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot read source revision' }
if ($CollectOnly)
{
    if (-not $BuiltRevision) { throw 'CollectOnly requires the verified BuiltRevision of the existing outputs' }
    $revision = (git -C $repository rev-parse $BuiltRevision).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Invalid BuiltRevision' }
}
$sourceStatus = @(git -C $repository status --short)
foreach ($target in $Targets)
{
    $destination = Join-Path $repository "artifacts/configuration_v2/$Phase/$target"
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    $log = Join-Path $destination 'keil-build.log'
    $exitCode = $null
    if (-not $CollectOnly)
    {
        $arguments = '-r "{0}" -t "{1}" -o "{2}"' -f $project, $target, $log
        $process = Start-Process -FilePath $Keil -ArgumentList $arguments -WorkingDirectory (Split-Path $project) -WindowStyle Hidden -PassThru -Wait
        $exitCode = $process.ExitCode
        if ($exitCode -notin @(0, 1)) { throw "Keil $target failed ($exitCode); see $log" }
        $buildLog = Get-Content -LiteralPath $log -Raw
        if ($buildLog -notmatch '0 Error\(s\)') { throw "No successful build summary in $log" }
    }
    $output = Join-Path $repository "app/MDK-ARM/$target"
    foreach ($extension in @('axf', 'bin', 'hex', 'map', 'build_log.htm', 'htm'))
    {
        $source = Join-Path $output "$target.$extension"
        if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $destination }
    }
    $mapPath = Join-Path $destination "$target.map"
    $map = Get-Content -LiteralPath $mapPath -Raw
    $regions = @{}
    foreach ($match in [regex]::Matches($map, 'Execution Region (\w+) \(Exec base: (0x[0-9a-f]+), Load base: (0x[0-9a-f]+), Size: (0x[0-9a-f]+), Max: (0x[0-9a-f]+)'))
    {
        $used = [Convert]::ToInt32($match.Groups[4].Value, 16)
        $capacity = [Convert]::ToInt32($match.Groups[5].Value, 16)
        $regions[$match.Groups[1].Value] = @{ address = $match.Groups[2].Value; used = $used; capacity = $capacity; free = $capacity - $used }
    }
    $rom = [regex]::Match($map, 'Total ROM Size \(Code \+ RO Data \+ RW Data\)\s+(\d+)')
    if (-not $rom.Success -or $regions.Count -ne 4) { throw "Incomplete map data: $mapPath" }
    $buffers = @{}
    foreach ($symbol in @('workspace', 'tx_buf', 'management_received_frame', 'active_configuration', 'temporary_configuration', 'ram_heap', 'ucHeap'))
    {
        $symbolMatch = [regex]::Match($map, '(?m)^\s+' + $symbol + '\s+(0x[0-9a-f]+)\s+Data\s+(\d+)\s+([^\r\n]+)')
        if (-not $symbolMatch.Success) { throw "Missing map symbol: $symbol" }
        $buffers[$symbol] = @{ address = $symbolMatch.Groups[1].Value; length = [int]$symbolMatch.Groups[2].Value; object_section = $symbolMatch.Groups[3].Value }
    }
    foreach ($symbol in @('workspace', 'tx_buf'))
    {
        $address = [Convert]::ToInt64($buffers[$symbol].address, 16)
        if ($address -lt 0x20000000 -or ($address + $buffers[$symbol].length) -gt 0x20020000) { throw "$symbol is outside DMA-accessible SRAM" }
    }
    if ($Phase -eq 'integrated')
    {
        if ($buffers.workspace.length -ne 8495 -or $buffers.tx_buf.length -ne 8494 -or $buffers.management_received_frame.length -ne 8494) { throw 'Unexpected v2 buffer capacity' }
        $parserAddress = [Convert]::ToInt64($buffers.management_received_frame.address, 16)
        if ($parserAddress -lt 0x10000000 -or ($parserAddress + $buffers.management_received_frame.length) -gt 0x10010000) { throw 'Parser is outside CCM' }
    }
    $files = @(Get-ChildItem -LiteralPath $destination -File | Where-Object Name -ne 'evidence.json' | ForEach-Object {
        @{ name = $_.Name; length = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
    $evidence = @{ target = $target; phase = $Phase; source_revision = $revision; source_status_at_invocation = $sourceStatus; collected_utc = [DateTime]::UtcNow.ToString('o'); keil_exit_code = $exitCode; total_rom = [int]$rom.Groups[1].Value; regions = $regions; buffers = $buffers; files = $files }
    $evidence | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $destination 'evidence.json') -Encoding utf8
    $evidence | ConvertTo-Json -Depth 4
}
