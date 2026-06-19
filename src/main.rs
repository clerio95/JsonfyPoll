//! JsonfyPool — vigia um `relatorio.txt` e o converte para JSON via jsonfylinx.
//!
//! O tipo de relatório é detectado automaticamente; a pasta de destino é
//! escolhida pelo tipo e o JSON é gravado com nome fixo (sobrescrevendo).
//!
//! Modos:
//!   (padrão)        pooler — vigia o arquivo e reconverte a cada mudança.
//!   --once | -1     converte uma vez e sai.

mod config;
mod ffi;

use std::path::{Path, PathBuf};
use std::time::{Duration, SystemTime};

use config::Config;
use ffi::Parser;

const USO: &str = "\
Uso: jsonfypool [opções]

  -c, --config <arquivo>   Caminho do config TOML (padrão: config.toml)
  -1, --once               Converte uma vez e sai (sem vigiar)
  -h, --help               Mostra esta ajuda
";

struct Args {
    config: PathBuf,
    once: bool,
}

fn parse_args() -> Result<Args, String> {
    let mut config = PathBuf::from("config.toml");
    let mut once = false;

    let mut it = std::env::args().skip(1);
    while let Some(arg) = it.next() {
        match arg.as_str() {
            "-h" | "--help" => {
                print!("{USO}");
                std::process::exit(0);
            }
            "-1" | "--once" => once = true,
            "-c" | "--config" => {
                config = it
                    .next()
                    .map(PathBuf::from)
                    .ok_or_else(|| format!("'{arg}' requer um caminho"))?;
            }
            outro => return Err(format!("argumento desconhecido: '{outro}'")),
        }
    }

    Ok(Args { config, once })
}

/// Detecta, escolhe a pasta de destino pelo tipo e grava o JSON.
fn processar(cfg: &Config) -> Result<(), String> {
    if !cfg.input.is_file() {
        return Err(format!(
            "arquivo de entrada não encontrado: {}",
            cfg.input.display()
        ));
    }

    let parser = ffi::detect(&cfg.input)?;
    if parser == Parser::Auto {
        return Err("não foi possível identificar o tipo do relatório".to_string());
    }

    let out = cfg
        .destino(parser)
        .ok_or_else(|| "tipo de relatório sem destino configurado".to_string())?;

    if let Some(dir) = out.parent() {
        std::fs::create_dir_all(dir)
            .map_err(|e| format!("não foi possível criar a pasta '{}': {e}", dir.display()))?;
    }

    ffi::convert(&cfg.input, &out, parser)?;
    println!("[ok] {} → {}", parser.nome(), out.display());
    Ok(())
}

/// mtime do arquivo, ou `None` se ele não existe ainda.
fn mtime(path: &Path) -> Option<SystemTime> {
    std::fs::metadata(path).and_then(|m| m.modified()).ok()
}

fn vigiar(cfg: &Config) {
    let intervalo = Duration::from_secs(cfg.poll_interval_secs);
    println!(
        "Vigiando {} (a cada {}s). Ctrl-C para sair.",
        cfg.input.display(),
        cfg.poll_interval_secs
    );

    // Converte o que já existe ao iniciar, depois reage a mudanças.
    let mut visto = mtime(&cfg.input);
    if visto.is_some() {
        if let Err(e) = processar(cfg) {
            eprintln!("[erro] {e}");
        }
    }

    loop {
        std::thread::sleep(intervalo);
        let atual = mtime(&cfg.input);
        if atual != visto {
            visto = atual;
            if atual.is_some() {
                if let Err(e) = processar(cfg) {
                    eprintln!("[erro] {e}");
                }
            }
        }
    }
}

fn main() {
    let args = match parse_args() {
        Ok(a) => a,
        Err(e) => {
            eprintln!("Erro: {e}\n");
            eprint!("{USO}");
            std::process::exit(2);
        }
    };

    let cfg = match Config::load(&args.config) {
        Ok(c) => c,
        Err(e) => {
            eprintln!("Erro: {e}");
            std::process::exit(1);
        }
    };

    if args.once {
        if let Err(e) = processar(&cfg) {
            eprintln!("[erro] {e}");
            std::process::exit(1);
        }
    } else {
        vigiar(&cfg);
    }
}
