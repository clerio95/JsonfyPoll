//! Configuração lida de um arquivo TOML.

use serde::Deserialize;
use std::path::{Path, PathBuf};

use crate::ffi::Parser;

/// Pastas de destino, uma por tipo de relatório.
#[derive(Debug, Deserialize)]
pub struct Destinos {
    pub posicao_estoque: PathBuf,
    pub valor_estoque: PathBuf,
    pub produtividade: PathBuf,
}

#[derive(Debug, Deserialize)]
pub struct Config {
    /// Arquivo `relatorio.txt` vigiado.
    pub input: PathBuf,

    /// Intervalo de verificação, em segundos, no modo pooler.
    #[serde(default = "default_intervalo")]
    pub poll_interval_secs: u64,

    pub destinos: Destinos,
}

fn default_intervalo() -> u64 {
    2
}

impl Config {
    pub fn load(path: &Path) -> Result<Config, String> {
        let texto = std::fs::read_to_string(path)
            .map_err(|e| format!("não foi possível ler config '{}': {e}", path.display()))?;
        toml::from_str(&texto).map_err(|e| format!("config inválida '{}': {e}", path.display()))
    }

    /// Pasta de destino e nome de arquivo fixo para um tipo de relatório.
    /// `Parser::Auto` não tem destino.
    pub fn destino(&self, parser: Parser) -> Option<PathBuf> {
        let (dir, nome) = match parser {
            Parser::PosicaoEstoque => (&self.destinos.posicao_estoque, "posicao_estoque.json"),
            Parser::ValorEstoque => (&self.destinos.valor_estoque, "valor_estoque.json"),
            Parser::Produtividade => (&self.destinos.produtividade, "produtividade.json"),
            Parser::Auto => return None,
        };
        Some(dir.join(nome))
    }
}
