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
    pub movimentacao: PathBuf,
    /// Opcional: um config anterior à Venda Detalhada continua válido; sem a
    /// pasta, esse relatório é recusado com uma mensagem em vez de derrubar o
    /// pooler na leitura do config.
    #[serde(default)]
    pub venda_detalhada: Option<PathBuf>,
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

    /// Pasta de destino e nome de arquivo fixo para os tipos de nome fixo.
    /// `Parser::Movimentacao` e `Parser::VendaDetalhada` usam nome dinâmico (ver
    /// `destino_movimentacao`/`destino_venda`); `Parser::Auto` não tem destino.
    /// Todos retornam `None` aqui.
    pub fn destino(&self, parser: Parser) -> Option<PathBuf> {
        let (dir, nome) = match parser {
            Parser::PosicaoEstoque => (&self.destinos.posicao_estoque, "posicao_estoque.json"),
            Parser::ValorEstoque => (&self.destinos.valor_estoque, "valor_estoque.json"),
            Parser::Produtividade => (&self.destinos.produtividade, "produtividade.json"),
            Parser::Movimentacao | Parser::VendaDetalhada | Parser::Auto => return None,
        };
        Some(dir.join(nome))
    }

    /// Destino da Movimentação de Produtos: nome dinâmico "AAAA-MM.json"
    /// (ex.: 2018-03.json) dentro da pasta configurada.
    pub fn destino_movimentacao(&self, ym: &str) -> PathBuf {
        self.destinos.movimentacao.join(format!("{ym}.json"))
    }

    /// Destino da Venda Detalhada: "CODIGO_AAAA-MM-DD_AAAA-MM-DD.json" (produto e
    /// período). Um arquivo por produto e período: exportar o 5W30 não apaga o
    /// 15W40, e reexportar o mesmo período o substitui. `None` sem a pasta.
    pub fn destino_venda(&self, chave: &str) -> Option<PathBuf> {
        self.destinos
            .venda_detalhada
            .as_ref()
            .map(|dir| dir.join(format!("{chave}.json")))
    }
}
