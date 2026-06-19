//! Compila a biblioteca C jsonfylinx e a linka estaticamente neste binário.
//!
//! Por padrão usa a cópia vendorizada em `vendor/jsonfylinx` (mantida por
//! `update.sh`, que baixa o código do GitHub). Sobrescreva com a variável de
//! ambiente `JSONFYLINX_DIR` para apontar para outro local (ex.:
//! `set -x JSONFYLINX_DIR /caminho/para/jsonfylinx`).

use std::path::Path;

fn main() {
    let dir = std::env::var("JSONFYLINX_DIR").unwrap_or_else(|_| "vendor/jsonfylinx".to_string());

    let src = Path::new(&dir).join("jsonfylinx.c");
    let header = Path::new(&dir).join("jsonfylinx.h");

    if !src.exists() {
        panic!(
            "jsonfylinx.c não encontrado em '{}'. Ajuste JSONFYLINX_DIR.",
            src.display()
        );
    }

    println!("cargo:rerun-if-env-changed=JSONFYLINX_DIR");
    println!("cargo:rerun-if-changed={}", src.display());
    println!("cargo:rerun-if-changed={}", header.display());

    cc::Build::new()
        .file(&src)
        .include(&dir)
        .warnings(true)
        .compile("jsonfylinx");
}
