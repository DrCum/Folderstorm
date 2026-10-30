param(
    [string]$Dotnet = 'dotnet',
    [string]$DevelopmentCache = (Join-Path $PSScriptRoot 'artifacts\dev'),
    [switch]$Test,
    [string]$BaseRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Path $DevelopmentCache -Force | Out-Null
$env:DOTNET_CLI_HOME = Join-Path $DevelopmentCache 'cli-home'
$env:NUGET_PACKAGES = Join-Path $DevelopmentCache 'nuget'
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
$env:DOTNET_GENERATE_ASPNET_CERTIFICATE = 'false'
$env:DOTNET_ADD_GLOBAL_TOOLS_TO_PATH = 'false'
Push-Location $PSScriptRoot
try {
    foreach ($project in @('Folderstorm.Theme.Cli', 'Folderstorm.Theme.Tests')) {
        & $Dotnet build "$project/$project.csproj" -c Release --nologo -p:NuGetAudit=false
        if ($LASTEXITCODE) { throw "$project build failed" }
    }
    if ($Test) {
        & $Dotnet 'Folderstorm.Theme.Tests/bin/Release/net10.0/Folderstorm.Theme.Tests.dll' $BaseRoot
        if ($LASTEXITCODE) { throw 'Theme contract tests failed' }
    }
} finally { Pop-Location }
