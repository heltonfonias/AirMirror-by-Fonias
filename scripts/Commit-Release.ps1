[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $Message,
    [ValidateSet('Patch', 'Minor', 'Major')] [string] $Bump = 'Patch',
    [switch] $SkipBootstrap
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
Push-Location $root
try {
    $branch = (& git branch --show-current).Trim()
    if ($branch -ne 'main') { throw 'Release commits must be made on main.' }
    $remote = (& git remote get-url origin).Trim()
    if ($remote -notmatch '(^https://github\.com/heltonfonias/AirMirror-by-Fonias(\.git)?$|^git@github\.com:heltonfonias/AirMirror-by-Fonias(\.git)?$)') {
        throw 'origin must point to the Fonias AirMirror repository.'
    }
    $unstaged = @(git diff --name-only)
    $untracked = @(git ls-files --others --exclude-standard)
    if ($unstaged.Count -gt 0 -or $untracked.Count -gt 0) {
        throw 'Stage only the intended source changes before running this script.'
    }
    & git diff --cached --quiet
    if ($LASTEXITCODE -eq 0) { throw 'No staged changes to release.' }

    $versionFile = Join-Path $root 'VERSION'
    $old = (Get-Content -LiteralPath $versionFile -TotalCount 1).Trim()
    if ($old -notmatch '^(\d+)\.(\d+)\.(\d+)$') { throw 'Invalid VERSION.' }
    $major = [int]$Matches[1]; $minor = [int]$Matches[2]; $patch = [int]$Matches[3]
    switch ($Bump) {
        Major { $major++; $minor = 0; $patch = 0 }
        Minor { $minor++; $patch = 0 }
        Patch { $patch++ }
    }
    $version = "$major.$minor.$patch"
    Set-Content -LiteralPath $versionFile -Value $version -Encoding ascii
    & git add -- VERSION
    & git diff --cached --check
    if ($LASTEXITCODE -ne 0) { throw 'Staged diff has whitespace errors.' }
    & git commit -m $Message
    if ($LASTEXITCODE -ne 0) { throw 'Commit failed.' }

    & (Join-Path $root 'build.ps1') package -Architecture x64 -SkipBootstrap:$SkipBootstrap
    if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) { throw 'Build failed; nothing was published.' }
    $tag = "v$version"
    & git tag -a $tag -m "AirMirror by Fonias $version"
    if ($LASTEXITCODE -ne 0) { throw 'Tag failed.' }
    & git push origin main
    if ($LASTEXITCODE -ne 0) { throw 'Push failed.' }
    & git push origin $tag
    if ($LASTEXITCODE -ne 0) { throw 'Tag push failed.' }
    $notes = Join-Path $root "out\x64\artifacts\release-notes-$tag.md"
    @("# AirMirror by Fonias $version", '', $Message, '',
      'Download the MSI for Windows installation or the portable ZIP for manual extraction.') |
        Set-Content -LiteralPath $notes -Encoding utf8
    & (Join-Path $PSScriptRoot 'Publish-Release.ps1') -NotesFile $notes
    Write-Host "Published AirMirror by Fonias $version"
} finally { Pop-Location }
