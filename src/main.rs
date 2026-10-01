//! JsonfyPoll — vigia um `relatorio.txt` e o converte para JSON via jsonfylinx.
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
Uso: jsonfypoll [opções]

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

    // Movimentação de Produtos usa nome dinâmico "AAAA-MM.json", derivado do
    // período do relatório, e a Venda Detalhada um nome por produto (5w30.json);
    // os demais tipos têm nome fixo.
    let out = match parser {
        Parser::Movimentacao => {
            let ym = ffi::periodo_ym(&cfg.input)?.ok_or_else(|| {
                "não foi possível ler o período (AAAA-MM) do relatório".to_string()
            })?;
            cfg.destino_movimentacao(&ym)
        }
        Parser::VendaDetalhada => {
            let chave = ffi::venda_chave(&cfg.input)?.ok_or_else(|| {
                "venda detalhada sem código de produto ou período: exporte um produto por vez"
                    .to_string()
            })?;
            cfg.destino_venda(&chave).ok_or_else(|| {
                "venda detalhada sem destino: defina [destinos] venda_detalhada".to_string()
            })?
        }
        _ => cfg
            .destino(parser)
            .ok_or_else(|| "tipo de relatório sem destino configurado".to_string())?,
    };

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

#[cfg(test)]
mod tests {
    use super::*;

    /// Exportação real do ERP (5W30, setembro/2026), como o operador salva.
    const VENDA: &str = concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/tests/fixtures/venda_detalhada_1096.txt"
    );

    /// Pasta temporária própria de cada teste (sem dependência extra).
    fn tmp(nome: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!("jsonfypoll-{}-{nome}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        dir
    }

    fn config(input: &Path, saida: &Path, com_venda: bool) -> Config {
        let venda = if com_venda {
            format!("venda_detalhada = '{}'\n", saida.join("venda").display())
        } else {
            String::new()
        };
        let texto = format!(
            "input = '{}'\n[destinos]\nposicao_estoque = '{d}/p'\nvalor_estoque = '{d}/v'\n\
             produtividade = '{d}/pr'\nmovimentacao = '{d}/m'\n{venda}",
            input.display(),
            d = saida.display(),
        );
        toml::from_str(&texto).unwrap()
    }

    #[test]
    fn detecta_venda_detalhada_e_monta_a_chave() {
        let input = Path::new(VENDA);
        assert_eq!(ffi::detect(input).unwrap(), Parser::VendaDetalhada);
        assert_eq!(
            ffi::venda_chave(input).unwrap().as_deref(),
            Some("5w30")
        );
    }

    #[test]
    fn grava_um_arquivo_por_produto_com_o_grau_no_nome() {
        let saida = tmp("grava");
        processar(&config(Path::new(VENDA), &saida, true)).unwrap();

        let json = std::fs::read_to_string(saida.join("venda/5w30.json")).unwrap();
        assert!(json.contains("\"codigo\": 1096"));
        assert!(json.contains("\"gerado_em\": \"2026-10-01T14:51\""));
        assert!(json.contains("\"total_quantidade\": 23.200"));
        assert_eq!(json.matches("\"documento\"").count(), 8);
    }

    #[test]
    fn produto_sem_grau_no_nome_usa_o_codigo() {
        let saida = tmp("semgrau");
        let input = saida.join("relatorio.txt");
        let texto = std::fs::read_to_string(VENDA).unwrap();
        std::fs::write(
            &input,
            texto.replace("PETRONAS SELENIA PERFORM SP 5W30 GRANEL", "ARLA 32 GRANEL"),
        )
        .unwrap();

        processar(&config(&input, &saida, true)).unwrap();
        assert!(saida.join("venda/1096.json").is_file());
    }

    #[test]
    fn grau_com_hifen_e_numero_parecido_nao_confundem() {
        let saida = tmp("hifen");
        let input = saida.join("relatorio.txt");
        let texto = std::fs::read_to_string(VENDA).unwrap();
        std::fs::write(
            &input,
            texto.replace(
                "PETRONAS SELENIA PERFORM SP 5W30 GRANEL",
                "SYNTIUM 800 SL(1X200) SAE 15W-40",
            ),
        )
        .unwrap();
        assert_eq!(ffi::venda_chave(&input).unwrap().as_deref(), Some("15w40"));
    }

    #[test]
    fn recusa_relatorio_sem_filtro_de_produto() {
        let saida = tmp("semprod");
        let input = saida.join("relatorio.txt");
        let texto = std::fs::read_to_string(VENDA).unwrap();
        std::fs::write(&input, texto.replace("Produto código: 1096", "Produto código:")).unwrap();

        let erro = processar(&config(&input, &saida, true)).unwrap_err();
        assert!(erro.contains("um produto por vez"), "{erro}");
        assert!(!saida.join("venda").exists());
    }

    #[test]
    fn config_sem_pasta_de_venda_continua_valido() {
        let saida = tmp("antigo");
        let cfg = config(Path::new(VENDA), &saida, false);
        assert!(cfg.destinos.venda_detalhada.is_none());

        let erro = processar(&cfg).unwrap_err();
        assert!(erro.contains("venda_detalhada"), "{erro}");
    }
}
