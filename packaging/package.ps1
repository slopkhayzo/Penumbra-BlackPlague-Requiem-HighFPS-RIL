# Builds the two flat release archives from one ASI and one INI:
#
#   PenumbraBlackPlague.FPSFix-<version>-plugin-only.zip
#   PenumbraBlackPlague.FPSFix-<version>-with-Ultimate-ASI-Loader.zip
#
# Each archive is re-opened after it is written and every entry is checked
# against SHA256SUMS.txt from the archive bytes, so a package that does not
# verify is never left in the output directory.
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$BinDir,
    [Parameter(Mandatory = $true)][string]$OutDir,
    # Ultimate ASI Loader x86 DLL (any of its proxy names). When omitted or
    # missing, only the plugin-only archive is produced.
    [string]$LoaderDll = ''
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$root = Split-Path -Parent $PSScriptRoot
$name = 'PenumbraBlackPlague.FPSFix'

# The loader build that passed the winmm.dll forwarding test under both
# Penumbra.exe and Requiem.exe. Any other file is refused.
$loaderPin = @{
    Project = 'Ultimate ASI Loader'
    Version = '9.7.4'
    Architecture = 'x86 (Win32)'
    Sha256 = 'D5A059AA467A7A7127C8F6169F79FA63FF0F55986EE9EB2FD9A281BEBF2AA2E6'
    License = 'MIT'
    Url = 'https://github.com/ThirteenAG/Ultimate-ASI-Loader'
}
$loaderLicense = @'
MIT License

Copyright (c) 2023 ThirteenAG

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
'@

function Get-Sha256([byte[]]$Bytes) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        return ([System.BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-', '')
    } finally {
        $sha.Dispose()
    }
}

function Get-TextBytes([string]$Text) {
    # CRLF so the files read correctly in Notepad.
    $normalized = ($Text -replace "`r`n", "`n") -replace "`n", "`r`n"
    return [System.Text.Encoding]::UTF8.GetBytes($normalized)
}

function New-Package([string]$Path, [System.Collections.Specialized.OrderedDictionary]$Entries) {
    $allowed = '.asi', '.ini', '.dll', '.txt'
    $sums = New-Object System.Text.StringBuilder
    foreach ($entry in $Entries.Keys) {
        if ($allowed -notcontains [IO.Path]::GetExtension($entry).ToLowerInvariant()) {
            throw "Refusing to package unexpected file type: $entry"
        }
        [void]$sums.AppendLine(('{0} *{1}' -f (Get-Sha256 $Entries[$entry]), $entry))
    }
    $Entries['SHA256SUMS.txt'] = Get-TextBytes $sums.ToString()

    if (Test-Path $Path) { Remove-Item $Path -Confirm:$false }
    $stream = [IO.File]::Open($Path, [IO.FileMode]::CreateNew)
    try {
        $zip = New-Object System.IO.Compression.ZipArchive($stream, [System.IO.Compression.ZipArchiveMode]::Create)
        try {
            foreach ($entry in $Entries.Keys) {
                $item = $zip.CreateEntry($entry, [System.IO.Compression.CompressionLevel]::Optimal)
                $writer = $item.Open()
                try { $writer.Write($Entries[$entry], 0, $Entries[$entry].Length) } finally { $writer.Dispose() }
            }
        } finally { $zip.Dispose() }
    } finally { $stream.Dispose() }

    # Verify from the archive bytes, not from what we meant to write.
    try {
        $read = [System.IO.Compression.ZipFile]::OpenRead($Path)
        try {
            $actual = @{}
            foreach ($item in $read.Entries) {
                if ($item.FullName -ne $item.Name) { throw "Archive is not flat: $($item.FullName)" }
                $buffer = New-Object System.IO.MemoryStream
                $reader = $item.Open()
                try { $reader.CopyTo($buffer) } finally { $reader.Dispose() }
                $actual[$item.Name] = Get-Sha256 $buffer.ToArray()
                if ($item.Name -eq 'SHA256SUMS.txt') { $listed = [System.Text.Encoding]::UTF8.GetString($buffer.ToArray()) }
            }
        } finally { $read.Dispose() }
        $checked = 0
        foreach ($line in ($listed -split "`r?`n" | Where-Object { $_ })) {
            $hash, $file = $line -split ' \*', 2
            if ($actual[$file] -ne $hash) { throw "Checksum mismatch for $file" }
            $checked++
        }
        if ($checked -ne $actual.Count - 1) { throw 'Archive entries and SHA256SUMS.txt do not match one to one' }
    } catch {
        Remove-Item $Path -Confirm:$false
        throw
    }
    Write-Output ('{0}  {1} files  sha256 {2}' -f (Split-Path -Leaf $Path), $actual.Count,
                  (Get-FileHash $Path -Algorithm SHA256).Hash)
}

$asi = [IO.File]::ReadAllBytes((Join-Path $BinDir "$name.asi"))
$ini = [IO.File]::ReadAllBytes((Join-Path $root "config\$name.ini"))
$builtIni = [IO.File]::ReadAllBytes((Join-Path $BinDir "$name.ini"))
if ((Get-Sha256 $ini) -ne (Get-Sha256 $builtIni)) {
    throw 'The INI beside the built ASI differs from config; rebuild before packaging.'
}
$readme = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'README.txt'))
$footer = "Misc`n-------`nFor more game fixes, visit https://slop-blog.enkhayzomachines.net/fixes :)"
if (-not (($readme -replace "`r`n", "`n").TrimEnd().EndsWith($footer))) {
    throw 'packaging/README.txt has lost its required footer.'
}
$readmeBytes = Get-TextBytes ("Version $Version`r`n`r`n" + $readme)
$minhook = [IO.File]::ReadAllBytes((Join-Path $root 'third_party\minhook\LICENSE.txt'))

New-Item -ItemType Directory -Force $OutDir | Out-Null

$plugin = [ordered]@{}
$plugin["$name.asi"] = $asi
$plugin["$name.ini"] = $ini
$plugin['README.txt'] = $readmeBytes
$plugin['MINHOOK-LICENSE.txt'] = $minhook
New-Package (Join-Path $OutDir "$name-$Version-plugin-only.zip") $plugin

if ($LoaderDll -and (Test-Path $LoaderDll)) {
    $loader = [IO.File]::ReadAllBytes($LoaderDll)
    # Prefer the license file shipped with the loader over the copy above.
    $shippedLicense = Join-Path (Split-Path -Parent $LoaderDll) 'LICENSE.txt'
    if (Test-Path $shippedLicense) {
        $loaderLicense = [IO.File]::ReadAllText($shippedLicense).Trim()
    }
    if ((Get-Sha256 $loader) -ne $loaderPin.Sha256) {
        throw "Loader DLL is not the pinned $($loaderPin.Project) $($loaderPin.Version) build."
    }
    $notices = @"
This package includes one third-party binary.

winmm.dll
  Project:      $($loaderPin.Project)
  Version:      $($loaderPin.Version), $($loaderPin.Architecture)
  Source:       $($loaderPin.Url)
  SHA-256:      $($loaderPin.Sha256)
  License:      $($loaderPin.License)
  Distributed unmodified. It is the project's dinput8.dll renamed to
  winmm.dll, one of the proxy names the loader supports.

$loaderLicense
"@
    $bundle = [ordered]@{}
    $bundle["$name.asi"] = $asi
    $bundle["$name.ini"] = $ini
    $bundle['winmm.dll'] = $loader
    $bundle['README.txt'] = $readmeBytes
    $bundle['MINHOOK-LICENSE.txt'] = $minhook
    $bundle['THIRD-PARTY-NOTICES.txt'] = Get-TextBytes $notices
    New-Package (Join-Path $OutDir "$name-$Version-with-Ultimate-ASI-Loader.zip") $bundle
} else {
    Write-Output 'Loader DLL not supplied; the with-Ultimate-ASI-Loader archive was skipped.'
}
