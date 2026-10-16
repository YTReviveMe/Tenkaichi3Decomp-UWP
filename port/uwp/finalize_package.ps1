param(
    [Parameter(Mandatory=$true)][string]$PackagePath,
    [Parameter(Mandatory=$true)][string]$GameExePath,
    [Parameter(Mandatory=$true)][string]$StateMapPath,
    [Parameter(Mandatory=$true)][string]$CertificateThumbprint
)

$ErrorActionPreference = 'Stop'
$package = (Resolve-Path -LiteralPath $PackagePath).Path
$gameExe = (Resolve-Path -LiteralPath $GameExePath).Path
$stateMap = (Resolve-Path -LiteralPath $StateMapPath).Path
$sdk = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64'
$makeappx = Join-Path $sdk 'makeappx.exe'
$signtool = Join-Path $sdk 'signtool.exe'
$staging = Join-Path (Split-Path -Parent $package) ('bt3-finalize-' + [guid]::NewGuid().ToString('N'))
$repacked = Join-Path (Split-Path -Parent $package) ('bt3-repacked-' + [guid]::NewGuid().ToString('N') + '.msix')
$verifyDir = $staging + '-verify'

function Test-LargeAddressAware([string]$path) {
    $stream = [IO.File]::OpenRead($path)
    try {
        $reader = [IO.BinaryReader]::new($stream)
        $stream.Position = 0x3c
        $peOffset = $reader.ReadInt32()
        $stream.Position = $peOffset + 22
        return (($reader.ReadUInt16() -band 0x20) -ne 0)
    }
    finally { $stream.Dispose() }
}

if (-not (Test-LargeAddressAware $gameExe)) {
    throw "The game EXE is still limited to 2 GB: $gameExe"
}
New-Item -ItemType Directory -Path $staging | Out-Null
try {
    & $makeappx unpack /p $package /d $staging /o | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'makeappx unpack failed' }
    Copy-Item -LiteralPath $gameExe -Destination (Join-Path $staging 'Tenkaichi3DecompUWP.exe') -Force
    Copy-Item -LiteralPath $stateMap -Destination (Join-Path $staging 'Tenkaichi3DecompUWP.exe.mem') -Force
    Remove-Item -LiteralPath (Join-Path $staging 'AppxBlockMap.xml') -ErrorAction SilentlyContinue
    & $makeappx pack /d $staging /p $repacked /o | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'makeappx pack failed' }
    & $makeappx unpack /p $repacked /d $verifyDir /o | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'makeappx verification unpack failed' }
    $verifiedExe = Join-Path $verifyDir 'Tenkaichi3DecompUWP.exe'
    if (-not (Test-LargeAddressAware $verifiedExe) -or
        (Get-FileHash -LiteralPath $verifiedExe).Hash -ne (Get-FileHash -LiteralPath $gameExe).Hash) {
        throw 'The repacked MSIX does not contain the edited game EXE'
    }
    if ((Get-FileHash -LiteralPath (Join-Path $verifyDir 'Tenkaichi3DecompUWP.exe.mem')).Hash -ne
        (Get-FileHash -LiteralPath $stateMap).Hash) {
        throw 'The repacked MSIX does not contain the matching state map'
    }
    & $signtool sign /fd SHA256 /sha1 $CertificateThumbprint /s My $repacked | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'signtool sign failed' }
    $signature = Get-AuthenticodeSignature -LiteralPath $repacked
    if (-not $signature.SignerCertificate -or
        $signature.SignerCertificate.Thumbprint -ne $CertificateThumbprint -or
        ($signature.Status -ne 'Valid' -and
         $signature.StatusMessage -notlike '*root certificate which is not trusted*')) {
        throw "Package signature check failed: $($signature.StatusMessage)"
    }
    Move-Item -LiteralPath $repacked -Destination $package -Force
    Write-Output "Final signed package: $package"
}
finally {
    if ((Split-Path -Parent $staging) -eq (Split-Path -Parent $package)) {
        Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
        if (Test-Path -LiteralPath $verifyDir) {
            Remove-Item -LiteralPath $verifyDir -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
    if (Test-Path -LiteralPath $repacked) { Remove-Item -LiteralPath $repacked -Force }
}
