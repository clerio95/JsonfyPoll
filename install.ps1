<#
.SYNOPSIS
    Instala o JsonfyPool no Windows: baixa o binário publicado no GitHub.

.DESCRIPTION
    Uso rápido (PowerShell):

        irm https://raw.githubusercontent.com/clerio95/JsonfyPool/main/install.ps1 | iex

    Ou com opções (baixe o arquivo e rode):

        .\install.ps1 -Version v1.0.0 -InstallDir 'C:\Tools\JsonfyPool'

    Requisitos: o repositório precisa ter um *release* com o asset
    `jsonfypool.exe` anexado. Veja a seção "Publicar" no README.

.PARAMETER Repo
    Slug owner/repo no GitHub.

.PARAMETER Version
    Tag do release (ex.: v1.0.0) ou "latest".

.PARAMETER InstallDir
    Pasta de instalação. Padrão: %LOCALAPPDATA%\JsonfyPool.

.PARAMETER NoPath
    Não adicionar a pasta ao PATH do usuário.
#>
[CmdletBinding()]
param(
    [string]$Repo = 'clerio95/JsonfyPool',
    [string]$Version = 'latest',
    [string]$InstallDir = "$env:LOCALAPPDATA\JsonfyPool",
    [switch]$NoPath
)

$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

if ($Version -eq 'latest') {
    $exeUrl = "https://github.com/$Repo/releases/latest/download/jsonfypool.exe"
}
else {
    $exeUrl = "https://github.com/$Repo/releases/download/$Version/jsonfypool.exe"
}

Write-Host "==> Instalando JsonfyPool em $InstallDir"
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null

$exePath = Join-Path $InstallDir 'jsonfypool.exe'
Write-Host "==> Baixando $exeUrl"
try {
    Invoke-WebRequest -Uri $exeUrl -OutFile $exePath -UseBasicParsing
}
catch {
    Write-Error "Falha ao baixar jsonfypool.exe. Confirme que o release '$Version' existe em https://github.com/$Repo/releases e tem o asset jsonfypool.exe.`n$($_.Exception.Message)"
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
Write-Host '  2. Rode (vigiar):   jsonfypool -c C:\caminho\config.toml'
Write-Host '     Ou (uma vez):    jsonfypool --once -c C:\caminho\config.toml'
