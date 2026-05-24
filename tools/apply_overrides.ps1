Write-Host "Applying branding overrides..."

$branding = Join-Path $PSScriptRoot "..\branding"
$ut99dc = Join-Path $PSScriptRoot "..\third_party\ut99dc"

if (!(Test-Path $ut99dc)) {
    Write-Warning "ut99dc not found. Skipping overrides."
    exit 0
}

# The branding textures are typically imported by the engine from PCX files.
# In the ut99dc source, we look for a place to put them.
# If the expected directory doesn't exist, we skip for now.
$dest = Join-Path $ut99dc "Source\UnrealTournament\Textures"
if (Test-Path $dest) {
    Copy-Item -Path (Join-Path $branding "UMenu\Textures\*.pcx") -Destination $dest -Force
    Write-Host "Branding textures applied to $dest"
} else {
    Write-Host "No destination found for branding textures. Skipping."
}

Write-Host "Done."
