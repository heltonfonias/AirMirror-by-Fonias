[CmdletBinding()]
param(
    [string] $NotesFile = (Join-Path $PSScriptRoot '..\docs\RELEASE-NOTES.md')
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$version = (Get-Content -LiteralPath (Join-Path $root 'VERSION') -TotalCount 1).Trim()
if ($version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$') { throw 'Invalid VERSION file.' }
$tag = "v$version"
$artifacts = Join-Path $root 'out\x64\artifacts'
$msi = Join-Path $artifacts 'AirMirror-by-Fonias-x64.msi'
$portable = Join-Path $artifacts 'AirMirror-by-Fonias-x64-portable.zip'
$source = Join-Path $artifacts 'AirMirror-by-Fonias-source.zip'
$checksums = Join-Path $artifacts 'SHA256SUMS.txt'
foreach ($path in @($msi, $portable, $NotesFile)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing $path" }
}
& (Join-Path $PSScriptRoot 'Package-Source.ps1') -Output $source
if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) { throw 'Source packaging failed.' }

@($msi, $portable, $source) | ForEach-Object {
    $hash = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $(Split-Path $_ -Leaf)"
} | Set-Content -LiteralPath $checksums -Encoding ascii

$releases = & gh release list --repo heltonfonias/AirMirror-by-Fonias `
    --limit 100 --json tagName
if ($LASTEXITCODE -ne 0) { throw 'Could not inspect existing GitHub releases.' }
if (@($releases | ConvertFrom-Json | Where-Object tagName -eq $tag).Count -gt 0) {
    throw "Release $tag already exists. Refusing to replace it."
}
& gh release create $tag $msi $portable $source $checksums `
    --repo heltonfonias/AirMirror-by-Fonias `
    --verify-tag `
    --title "AirMirror by Fonias $version" `
    --notes-file $NotesFile
if ($LASTEXITCODE -ne 0) { throw "Could not publish $tag." }
