#ifndef JSONFYLINX_H
#define JSONFYLINX_H

#include <stddef.h>  /* size_t */

/*
 * jsonfylinx — biblioteca de conversão de relatórios TXT (ERP) para JSON.
 *
 * ABI plana em C, pensada para ser usada por outro programa via FFI
 * (ex.: Rust com `cc`/`build.rs` ou linkando `libjsonfylinx.a`).
 * Nenhuma função escreve em stdout/stderr: o chamador decide como reportar.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Qual parser aplicar. JFX_AUTO pede detecção pelo conteúdo do arquivo. */
typedef enum {
    JFX_AUTO                 = 0,
    JFX_POSICAO_ESTOQUE      = 1,
    JFX_VALOR_ESTOQUE        = 2,
    JFX_PRODUTIVIDADE        = 3,
    JFX_MOVIMENTACAO_PRODUTOS = 4
} jfx_parser_t;

/* Resultado de uma conversão. */
typedef enum {
    JFX_OK = 0,
    JFX_ERR_OPEN_INPUT,        /* arquivo de entrada inexistente ou ilegível */
    JFX_ERR_CREATE_OUTPUT,     /* não foi possível criar o arquivo de saída  */
    JFX_ERR_NOT_REGULAR_FILE,  /* a entrada não é um arquivo regular         */
    JFX_ERR_DETECT_FAILED,     /* auto-detecção não reconheceu o relatório   */
    JFX_ERR_INVALID_PARSER     /* valor de jfx_parser_t fora do intervalo    */
} jfx_status_t;

/*
 * Inspeciona o cabeçalho do arquivo e devolve o parser correspondente,
 * ou JFX_AUTO (0) se não conseguir classificar.
 */
jfx_parser_t jfx_detect(const char *in_path);

/*
 * Converte `in_path` para JSON gravado em `out_path`.
 * Com `parser == JFX_AUTO`, detecta o tipo antes de converter.
 * O caminho de saída é usado literalmente (sem inferir diretório/extensão).
 */
jfx_status_t jfx_convert(const char *in_path,
                         const char *out_path,
                         jfx_parser_t parser);

/*
 * Extrai o ano-mês do período do relatório (linha "Período: DD/MM/AAAA ...")
 * e grava em `buf` no formato "AAAA-MM" (ex.: "2018-03"). `buf_sz` deve ser
 * >= 8. Útil para o chamador nomear o arquivo de saída (ex.: 2018-03.json).
 * Retorna 1 em caso de sucesso, 0 se não encontrar/interpretar o período.
 */
int jfx_periodo_ym(const char *in_path, char *buf, size_t buf_sz);

/* Mensagem legível (pt-BR) para um jfx_status_t. */
const char *jfx_status_str(jfx_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* JSONFYLINX_H */
