$thirdParty = Join-Path $PSScriptRoot "..\third_party"
if (!(Test-Path $thirdParty)) { New-Item -ItemType Directory -Path $thirdParty }

# SDL2 2.28.5
Write-Host "Fetching SDL2 2.28.5..."
$sdlZip = Join-Path $thirdParty "SDL2.zip"
$sdlUrl = "https://github.com/libsdl-org/SDL/releases/download/release-2.28.5/SDL2-2.28.5.zip"
if (!(Test-Path (Join-Path $thirdParty "SDL2\CMakeLists.txt"))) {
    Invoke-WebRequest -Uri $sdlUrl -OutFile $sdlZip
    Expand-Archive -Path $sdlZip -DestinationPath $thirdParty -Force
    if (Test-Path (Join-Path $thirdParty "SDL2-2.28.5")) {
        Rename-Item -Path (Join-Path $thirdParty "SDL2-2.28.5") -NewName "SDL2"
    }
    Remove-Item $sdlZip
} else {
    Write-Host "SDL2 already exists."
}

# ut99dc
Write-Host "Fetching ut99dc..."
$ut99Zip = Join-Path $thirdParty "ut99dc.zip"
$ut99Url = "https://github.com/maximqaxd/ut99dc/archive/refs/heads/master.zip"
if (!(Test-Path (Join-Path $thirdParty "ut99dc\Source\CMakeLists.txt"))) {
    Invoke-WebRequest -Uri $ut99Url -OutFile $ut99Zip
    Expand-Archive -Path $ut99Zip -DestinationPath $thirdParty -Force
    if (Test-Path (Join-Path $thirdParty "ut99dc-master")) {
        Rename-Item -Path (Join-Path $thirdParty "ut99dc-master") -NewName "ut99dc"
    }
    Remove-Item $ut99Zip
} else {
    Write-Host "ut99dc already exists."
}

Write-Host "Done."
