# Launch the ncurses build from Windows via WSL and an independently owned WezTerm window.
# RunOnly skips compilation; all paths are resolved from ProjectRoot, including paths with spaces.
param(
    [Parameter(Mandatory = $true)][string]$ProjectRoot,
    [switch]$RunOnly
)
$ErrorActionPreference = 'Stop'

# Prefer an explicit executable override, then search standard installation locations.
function Find-WezTerm {
    if ($env:AI_SIM_WEZTERM) {
        if (-not (Test-Path -LiteralPath $env:AI_SIM_WEZTERM -PathType Leaf)) {
            throw 'AI_SIM_WEZTERM does not point to an existing executable.'
        }
        return $env:AI_SIM_WEZTERM
    }
    foreach ($name in @('wezterm-gui.exe', 'wezterm.exe')) {
        $command = Get-Command $name -CommandType Application -ErrorAction SilentlyContinue
        if ($command) { return $command.Source }
    }
    $folders = @(
        (Join-Path $env:ProgramFiles 'WezTerm'),
        (Join-Path $env:LOCALAPPDATA 'Programs\WezTerm'),
        (Join-Path $env:USERPROFILE 'scoop\apps\wezterm\current'),
        $ProjectRoot
    )
    foreach ($folder in $folders) {
        foreach ($name in @('wezterm-gui.exe', 'wezterm.exe')) {
            $candidate = Join-Path $folder $name
            if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
        }
    }
    throw 'WezTerm was not found. Add its folder to PATH or set AI_SIM_WEZTERM to the full path of wezterm-gui.exe.'
}

try {
    if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
        throw 'WSL is required. Install Ubuntu in WSL before using this launcher.'
    }
    $root = [System.IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\')
    $distributionArguments = @()
    if ($root -match '^\\\\(?:wsl\.localhost|wsl\$)\\([^\\]+)(\\.*)?$') {
        # UNC paths identify the exact distribution; do not use the default by accident.
        $distributionArguments = @('--distribution', $Matches[1])
        $linuxRoot = if ($Matches[2]) { $Matches[2].Replace('\', '/') } else { '/' }
    }
    else {
        $converted = & wsl.exe --exec wslpath -a -u $root
        if ($LASTEXITCODE -ne 0) { throw 'Could not translate the project folder into a WSL path.' }
        $linuxRoot = ($converted -join "`n").Trim()
    }
    if ($RunOnly) {
        $host.UI.RawUI.WindowTitle = 'AI Sim'
        Write-Host "Starting AI Sim in $linuxRoot ..."
        & wsl.exe @distributionArguments --cd $linuxRoot --exec bash "Development Tools/scripts/build-and-run.sh" run
        if ($LASTEXITCODE -ne 0) {
            throw "The game or WSL exited with code $LASTEXITCODE. See the output above."
        }
        return
    }

    $wezterm = Find-WezTerm
    Write-Host "Using WezTerm: $wezterm"
    Write-Host "Building AI Sim in $linuxRoot ..."
    & wsl.exe @distributionArguments --cd $linuxRoot --exec bash "Development Tools/scripts/build-and-run.sh" build
    if ($LASTEXITCODE -ne 0) { throw 'Build failed. The game has not been launched.' }

    # Launch a Windows shell that stays alive even when WSL itself fails to start.
    # A successful terminal launch alone does not prove that the child game started.
    $childArguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-NoExit',
        '-File', $PSCommandPath, '-ProjectRoot', $root, '-RunOnly')
    $powershell = Join-Path $PSHOME 'powershell.exe'
    # Use the local Windows domain even if WezTerm defaults to a WSL/SSH domain.
    # The wrapper then selects the correct WSL distribution and project directory.
    $terminalArguments = @('start', '--always-new-process', '--no-auto-connect',
        '--domain', 'local', '--', $powershell) + $childArguments
    $terminalQuoted = $terminalArguments | ForEach-Object {
        '"' + ($_ -replace '(\\*)"', '$1$1\"' -replace '(\\+)$', '$1$1') + '"'
    }
    Write-Host 'Opening AI Sim in WezTerm. Startup errors will remain visible.'
    $terminalProcess = Start-Process -FilePath $wezterm -ArgumentList ($terminalQuoted -join ' ') -PassThru -Wait
    if ($terminalProcess.ExitCode -ne 0) {
        throw "WezTerm exited with code $($terminalProcess.ExitCode). See the terminal output above."
    }

}
catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    if (-not $RunOnly) { exit 1 }
    Write-Host 'This window will stay open. Copy the error above if the game did not start.'
}
