# JsonfyPoll

Vigia um arquivo `relatorio.txt` e o converte para JSON usando
[jsonfylinx](../../Utils/jsonfylinx) (linkado como biblioteca C via FFI).

Fluxo: detecta o tipo do relatório → escolhe a pasta de destino pelo tipo →
grava o JSON com nome fixo (sobrescrevendo).

## Como funciona

- **Entrada:** um caminho fixo de `relatorio.txt` (vigiado).
- **Detecção:** automática, feita pelo próprio jsonfylinx (`jfx_detect`).
- **Destino:** uma pasta por tipo de relatório (configurável):

  | Tipo                          | Arquivo gerado          |
  |-------------------------------|-------------------------|
  | Posição de Estoque            | `posicao_estoque.json`  |
  | Valor do Estoque              | `valor_estoque.json`    |
  | Produtividade por Funcionários | `produtividade.json`    |

  O nome é fixo: cada conversão **sobrescreve** o JSON anterior daquele tipo.

## Configuração

Copie `config.example.toml` para `config.toml` e ajuste os caminhos:

```toml
input = "/home/clerio95/relatorio.txt"
poll_interval_secs = 2

[destinos]
posicao_estoque = "/home/clerio95/saidas/posicao"
valor_estoque   = "/home/clerio95/saidas/valor"
produtividade   = "/home/clerio95/saidas/produtividade"
```

As pastas de destino são criadas automaticamente se não existirem.

## Uso

```
jsonfypoll [opções]

  -c, --config <arquivo>   Caminho do config TOML (padrão: config.toml)
  -1, --once               Converte uma vez e sai (sem vigiar)
  -h, --help               Ajuda
```

- **Modo poller (padrão):** converte o arquivo atual ao iniciar e depois
  reconverte a cada mudança (verifica o mtime a cada `poll_interval_secs`).
- **Modo único (`--once`):** converte uma vez e sai — útil para cron/systemd.

## Build

```fish
cargo build --release
```

O `build.rs` compila `jsonfylinx.c` e o linka **estaticamente** no binário (não
há `.so` para distribuir). Por padrão usa a cópia vendorizada em
`vendor/jsonfylinx/`. Para apontar para outro local:

```fish
set -x JSONFYLINX_DIR /caminho/para/jsonfylinx
cargo build --release
```

## Atualizar o jsonfylinx

A biblioteca C vive em `vendor/jsonfylinx/` e vem do repositório
[clerio95/jsonfylinx](https://github.com/clerio95/jsonfylinx) (branch `main`).
Para puxar a versão mais recente e reconstruir:

```fish
./update.sh
```

O script baixa `jsonfylinx.c`/`.h` do GitHub, e:

- se nada mudou, não faz nada;
- se mudou, substitui o vendor e roda `cargo build --release`;
- se o build falhar (ex.: a ABI em `jsonfylinx.h` mudou e `src/ffi.rs` ficou
  desatualizado), **reverte** o vendor e preserva o binário anterior — aí basta
  ajustar `src/ffi.rs` para casar com a nova ABI e rodar de novo.

Como o link é estático, atualizar exige reconstruir o binário (o `update.sh` já
faz isso); um `jsonfypoll` já compilado carrega o jsonfylinx da época do build.

## Instalar no Windows

Com o `jsonfypoll.exe` publicado num *release* do GitHub, instale pelo
PowerShell:

```powershell
irm https://raw.githubusercontent.com/clerio95/Jsonfypoll/main/install.ps1 | iex
```

O `install.ps1` baixa o `.exe` para `%LOCALAPPDATA%\Jsonfypoll`, gera um
`config.example.toml` com caminhos no estilo Windows e adiciona a pasta ao PATH
do usuário. Opções:

```powershell
# baixe o script e rode com parâmetros
.\install.ps1 -Version v1.0.0 -InstallDir 'C:\Tools\Jsonfypoll' -NoPath
```

## Publicar (gerar e subir o .exe)

O binário Windows é gerado por *cross-compile* a partir do Linux (precisa do
alvo `x86_64-pc-windows-gnu` e do `mingw-w64`):

```fish
rustup target add x86_64-pc-windows-gnu   # uma vez
cargo build --release --target x86_64-pc-windows-gnu
# -> target/x86_64-pc-windows-gnu/release/jsonfypoll.exe
```

Depois crie um release no GitHub anexando esse `jsonfypoll.exe` como asset (a
URL `releases/latest/download/jsonfypoll.exe` usada pelo instalador aponta
sempre para o release mais recente). Com o `gh` CLI:

```fish
gh release create v1.0.0 target/x86_64-pc-windows-gnu/release/jsonfypoll.exe \
    --title v1.0.0 --notes "..."
```
