$NDKPath = Get-Content $PSScriptRoot/NDKPath.txt
Write-Output "NDK located at: $NDKPath"

$buildScript = "$NDKPath/build/ndk-build"
if (Test-Path "$buildScript.cmd")
{
    $buildScript += ".cmd"
}

Write-Output "[BUILD] Starting NDK..."
& $buildScript NDK_PROJECT_PATH=$PSScriptRoot APP_BUILD_SCRIPT=$PSScriptRoot/Android.mk NDK_APPLICATION_MK=$PSScriptRoot/Application.mk NDK_DEBUG=0 -j4
Write-Output "[BUILD] Done!"

if ($LASTEXITCODE -eq 0)
{
    $parentLibs = "$PSScriptRoot/../libs"
    if (Test-Path $parentLibs)
    {
        Copy-Item -Path "$PSScriptRoot/libs/*" -Destination $parentLibs -Recurse -Force
        Write-Output "[BUILD] Synced binaries to $parentLibs"
    }
}

Exit $LASTEXITCODE