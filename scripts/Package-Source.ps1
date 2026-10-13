[CmdletBinding()]
param(
    [string] $Output = (Join-Path $PSScriptRoot '..\out\x64\artifacts\AirMirror-by-Fonias-source.zip')
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$Output = [IO.Path]::GetFullPath($Output)
New-Item -ItemType Directory -Force -Path (Split-Path $Output -Parent) | Out-Null
$files = @(git -C $root ls-files --recurse-submodules)
if ($LASTEXITCODE -ne 0 -or $files.Count -eq 0) { throw 'Could not list source files.' }

$archive = [IO.Compression.ZipFile]::Open($Output, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($relative in $files) {
        $source = Join-Path $root $relative
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { continue }
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $archive, $source, ('AirMirror-by-Fonias-source/' + $relative.Replace('\', '/')),
            [IO.Compression.CompressionLevel]::Optimal
        ) | Out-Null
    }
} finally { $archive.Dispose() }

Write-Host "Packaged $($files.Count) tracked source files to $Output"
