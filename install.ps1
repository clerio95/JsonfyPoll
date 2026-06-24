<#
.SYNOPSIS
    Instala o JsonfyPoll no Windows: baixa o binário publicado no GitHub.

.DESCRIPTION
    Uso rápido (PowerShell):

        irm https://raw.githubusercontent.com/clerio95/JsonfyPoll/main/install.ps1 | iex

    Ou com opções (baixe o arquivo e rode):

        .\install.ps1 -Version v1.0.0 -InstallDir 'C:\Tools\JsonfyPoll'

    Requisitos: o repositório precisa ter um *release* com o asset
    `jsonfypoll.exe` anexado. Veja a seção "Publicar" no README.

.PARAMETER Repo
    Slug owner/repo no GitHub.

.PARAMETER Version
    Tag do release (ex.: v1.0.0) ou "latest".

.PARAMETER InstallDir
    Pasta de instalação. Padrão: %LOCALAPPDATA%\JsonfyPoll.

.PARAMETER NoPath
    Não adicionar a pasta ao PATH do usuário.
#>
[CmdletBinding()]
param(
    [string]$Repo = 'clerio95/JsonfyPoll',
    [string]$Version = 'latest',
    [string]$InstallDir = "$env:LOCALAPPDATA\JsonfyPoll",
    [switch]$NoPath
)

$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

Write-Host "==> Instalando JsonfyPoll em $InstallDir"
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null

$exePath = Join-Path $InstallDir 'jsonfypoll.exe'

# Resolve o asset .exe via API de releases, em vez de fixar o nome exato.
# Assim um nome ligeiramente diferente (ex.: erro de digitacao) ainda funciona.
if ($Version -eq 'latest') {
    $apiUrl = "https://api.github.com/repos/$Repo/releases/latest"
}
else {
    $apiUrl = "https://api.github.com/repos/$Repo/releases/tags/$Version"
}

$exeUrl = $null
try {
    $headers = @{ 'User-Agent' = 'JsonfyPoll-installer'; 'Accept' = 'application/vnd.github+json' }
    $release = Invoke-RestMethod -Uri $apiUrl -Headers $headers -UseBasicParsing
    # Prefere o nome canonico; senao, qualquer asset .exe do release.
    $asset = $release.assets | Where-Object { $_.name -eq 'jsonfypoll.exe' } | Select-Object -First 1
    if (-not $asset) {
        $asset = $release.assets | Where-Object { $_.name -like '*.exe' } | Select-Object -First 1
    }
    if ($asset) { $exeUrl = $asset.browser_download_url }
}
catch {
    # Cai no fallback de URL direta abaixo.
}

if (-not $exeUrl) {
    if ($Version -eq 'latest') {
        $exeUrl = "https://github.com/$Repo/releases/latest/download/jsonfypoll.exe"
    }
    else {
        $exeUrl = "https://github.com/$Repo/releases/download/$Version/jsonfypoll.exe"
    }
}

Write-Host "==> Baixando $exeUrl"
try {
    Invoke-WebRequest -Uri $exeUrl -OutFile $exePath -UseBasicParsing
}
catch {
    Write-Error "Falha ao baixar jsonfypoll.exe. Confirme que o release '$Version' existe em https://github.com/$Repo/releases e tem um asset .exe.`n$($_.Exception.Message)"
    return
}

# Gera um config.example.toml com caminhos no estilo Windows, sem sobrescrever
# um config.toml já existente.
$cfgExample = Join-Path $InstallDir 'config.example.toml'
@"
# Arquivo relatorio.txt vigiado.
input = 'C:\relatorios\relatorio.txt'

# Intervalo de verificacao (segundos) no modo pooler.
poll_interval_secs = 2

# Pasta de destino por tipo de relatorio (JSON com nome fixo, sobrescrevendo).
[destinos]
posicao_estoque = 'C:\saidas\posicao'
valor_estoque   = 'C:\saidas\valor'
produtividade   = 'C:\saidas\produtividade'
"@ | Set-Content -Path $cfgExample -Encoding UTF8

if (-not $NoPath) {
    $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    if ($userPath -notlike "*$InstallDir*") {
        $novo = if ([string]::IsNullOrEmpty($userPath)) { $InstallDir } else { "$userPath;$InstallDir" }
        [Environment]::SetEnvironmentVariable('Path', $novo, 'User')
        Write-Host "==> Adicionado ao PATH do usuario (reabra o terminal para valer)."
    }
}

Write-Host ''
Write-Host "Pronto! Instalado em: $exePath"
Write-Host 'Proximos passos:'
Write-Host "  1. Copie '$cfgExample' para config.toml e ajuste os caminhos."
Write-Host '  2. Rode (vigiar):   jsonfypoll -c C:\caminho\config.toml'
Write-Host '     Ou (uma vez):    jsonfypoll --once -c C:\caminho\config.toml'
