//! Ligações seguras para a biblioteca C jsonfylinx (ABI plana de `jsonfylinx.h`).
//!
//! Nenhuma função da biblioteca escreve em stdout/stderr — o controle das
//! mensagens é todo nosso.

use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int};
use std::path::Path;

/// Tipos de relatório que o jsonfylinx reconhece. Os valores espelham
/// `jfx_parser_t` em jsonfylinx.h.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Parser {
    /// Não classificado / detectar automaticamente.
    Auto = 0,
    PosicaoEstoque = 1,
    ValorEstoque = 2,
    Produtividade = 3,
}

impl Parser {
    fn from_int(v: c_int) -> Parser {
        match v {
            1 => Parser::PosicaoEstoque,
            2 => Parser::ValorEstoque,
            3 => Parser::Produtividade,
            _ => Parser::Auto,
        }
    }

    /// Nome legível (pt-BR) do tipo de relatório.
    pub fn nome(self) -> &'static str {
        match self {
            Parser::PosicaoEstoque => "Posição de Estoque",
            Parser::ValorEstoque => "Valor do Estoque",
            Parser::Produtividade => "Produtividade por Funcionários",
            Parser::Auto => "desconhecido",
        }
    }
}

extern "C" {
    fn jfx_detect(in_path: *const c_char) -> c_int;
    fn jfx_convert(in_path: *const c_char, out_path: *const c_char, parser: c_int) -> c_int;
    fn jfx_status_str(status: c_int) -> *const c_char;
}

fn cstring(path: &Path) -> Result<CString, String> {
    // jsonfylinx usa fopen/stat com char*; passamos o caminho como UTF-8.
    // Portável entre Linux e Windows (mingw).
    let s = path
        .to_str()
        .ok_or_else(|| format!("caminho não é UTF-8 válido: {}", path.display()))?;
    CString::new(s).map_err(|_| format!("caminho contém byte nulo: {}", path.display()))
}

fn status_str(status: c_int) -> String {
    // SAFETY: jfx_status_str sempre retorna um ponteiro válido para string
    // estática terminada em nulo, para qualquer valor de status.
    unsafe {
        let ptr = jfx_status_str(status);
        CStr::from_ptr(ptr).to_string_lossy().into_owned()
    }
}

/// Detecta o tipo de relatório lendo o cabeçalho do arquivo.
/// Retorna `Parser::Auto` quando não consegue classificar.
pub fn detect(in_path: &Path) -> Result<Parser, String> {
    let c_in = cstring(in_path)?;
    // SAFETY: c_in é um ponteiro válido terminado em nulo, vivo durante a chamada.
    let code = unsafe { jfx_detect(c_in.as_ptr()) };
    Ok(Parser::from_int(code))
}

/// Converte `in_path` para JSON em `out_path` usando `parser`.
/// `Parser::Auto` faz a detecção automática primeiro.
pub fn convert(in_path: &Path, out_path: &Path, parser: Parser) -> Result<(), String> {
    let c_in = cstring(in_path)?;
    let c_out = cstring(out_path)?;
    // SAFETY: ambos os ponteiros são válidos e terminados em nulo durante a chamada.
    let status = unsafe { jfx_convert(c_in.as_ptr(), c_out.as_ptr(), parser as c_int) };
    if status == 0 {
        Ok(())
    } else {
        Err(status_str(status))
    }
}
