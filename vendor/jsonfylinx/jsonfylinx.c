#include "jsonfylinx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#define MAX_LINE           1024
#define MAX_PRODUTO        512
#define MAX_PRODS_PER_FUNC 64
#define MAX_MOV_PRODUTOS   512
#define DETECT_MAX_LINES   50

typedef struct {
    int    id;
    char   categoria[4];
    char   produto[MAX_PRODUTO];
    double estoque;
} ItemEstoque;

typedef struct {
    int    id;
    char   produto[MAX_PRODUTO];
    double custo;
    double venda;
} ItemReajuste;

typedef struct {
    long   codigo;
    char   nome[MAX_PRODUTO];
    char   fornecedor[MAX_PRODUTO];
    double quantidade;
    char   unidade[16];
    double valor;
    double participacao_pct;
} ProdutoVenda;

typedef struct {
    long   codigo;
    char   nome[MAX_PRODUTO];
    double total_entrada;   /* soma da coluna Entrada ao longo do mês (todos os depósitos) */
} MovProduto;

static void trim(char *s) {
    char *p = s + strlen(s) - 1;
    while (p >= s && isspace((unsigned char)*p)) *p-- = '\0';
    p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
}

static int is_digits_only(const char *s) {
    if (!*s) return 0;
    while (*s)
        if (!isdigit((unsigned char)*s++)) return 0;
    return 1;
}

/* Case-insensitive substring search over ASCII bytes. */
static int ci_contains(const char *haystack, const char *needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0) return 1;
    for (const char *h = haystack; *h; h++) {
        size_t i = 0;
        while (i < nlen &&
               tolower((unsigned char)h[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == nlen) return 1;
    }
    return 0;
}

/* Strips Brazilian thousands dots and converts decimal comma to dot. */
static double parse_br_number(const char *s) {
    char buf[64];
    int j = 0;
    for (int i = 0; s[i] && j < (int)sizeof(buf) - 1; i++) {
        if (s[i] == '.')       continue;
        else if (s[i] == ',')  buf[j++] = '.';
        else                   buf[j++] = s[i];
    }
    buf[j] = '\0';
    return strtod(buf, NULL);
}

/* Splits a pipe-delimited table row into inner fields, trimming whitespace. */
static int split_pipes(const char *line, char out[][MAX_PRODUTO], int max) {
    int n = 0;
    const char *p = line;
    while (*p && n < max) {
        if (*p++ != '|') continue;
        const char *start = p;
        while (*p && *p != '|') p++;
        size_t len = (size_t)(p - start);
        if (len >= MAX_PRODUTO) len = MAX_PRODUTO - 1;
        strncpy(out[n], start, len);
        out[n][len] = '\0';
        trim(out[n]);
        n++;
    }
    return n;
}

static int parse_row(const char *line, ItemEstoque *item) {
    if (line[0] != '|') return 0;

    char f[4][MAX_PRODUTO];
    if (split_pipes(line, f, 4) < 3) return 0;
    if (!is_digits_only(f[0])) return 0;

    item->id = atoi(f[0]);

    /* Extract optional (LETTER) prefix from the product name */
    if (f[1][0] == '(' && isalpha((unsigned char)f[1][1]) && f[1][2] == ')') {
        item->categoria[0] = (char)toupper((unsigned char)f[1][1]);
        item->categoria[1] = '\0';
        char *rest = f[1] + 3;
        while (*rest && isspace((unsigned char)*rest)) rest++;
        strncpy(item->produto, rest, MAX_PRODUTO - 1);
    } else {
        item->categoria[0] = '\0';
        strncpy(item->produto, f[1], MAX_PRODUTO - 1);
    }
    item->produto[MAX_PRODUTO - 1] = '\0';

    /* Quantity: handles Brazilian thousands dots + decimal comma (e.g. 10.616,72) */
    item->estoque = parse_br_number(f[2]);

    return 1;
}

static void json_escape(FILE *out, const char *s) {
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        switch (c) {
            case '"':  fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\n': fputs("\\n",  out); break;
            case '\r': fputs("\\r",  out); break;
            case '\t': fputs("\\t",  out); break;
            default:   fputc(c, out);      break;
        }
    }
}

static int parse_row_reajuste(const char *line, ItemReajuste *item) {
    if (line[0] != '|') return 0;

    char f[7][MAX_PRODUTO];
    if (split_pipes(line, f, 7) < 6) return 0;
    if (!is_digits_only(f[0])) return 0;

    item->id = atoi(f[0]);
    strncpy(item->produto, f[1], MAX_PRODUTO - 1);
    item->produto[MAX_PRODUTO - 1] = '\0';
    item->custo = parse_br_number(f[3]);
    item->venda = parse_br_number(f[5]);

    return 1;
}

static int parse_produto_venda_row(const char *line, ProdutoVenda *p) {
    if (line[0] != '|') return 0;
    char f[7][MAX_PRODUTO];
    if (split_pipes(line, f, 7) < 7) return 0;
    if (!is_digits_only(f[0])) return 0;

    p->codigo = atol(f[0]);
    snprintf(p->nome,       sizeof(p->nome),       "%s", f[1]);
    snprintf(p->fornecedor, sizeof(p->fornecedor), "%s", f[2]);
    p->quantidade       = parse_br_number(f[3]);
    size_t ulen = strlen(f[4]);
    if (ulen >= sizeof(p->unidade)) ulen = sizeof(p->unidade) - 1;
    memcpy(p->unidade, f[4], ulen);
    p->unidade[ulen] = '\0';
    p->valor            = parse_br_number(f[5]);
    p->participacao_pct = parse_br_number(f[6]);
    return 1;
}

static void write_relatorio_header(FILE *out,
    const char *generated_at,
    const char *periodo_inicio, const char *periodo_fim,
    long empresa_codigo, const char *empresa_nome)
{
    fputs("{\n", out);
    fprintf(out, "  \"generated_at\": \"%s\",\n", generated_at);
    fputs("  \"periodo\": {\n", out);
    fprintf(out, "    \"inicio\": \"%s\",\n", periodo_inicio);
    fprintf(out, "    \"fim\": \"%s\"\n", periodo_fim);
    fputs("  },\n", out);
    fputs("  \"empresa\": {\n", out);
    fprintf(out, "    \"codigo\": %ld,\n", empresa_codigo);
    fputs("    \"nome\": \"", out);
    json_escape(out, empresa_nome);
    fputs("\"\n", out);
    fputs("  },\n", out);
    fputs("  \"funcionarios\": [\n", out);
}

static void emit_funcionario(FILE *out,
    long codigo, const char *nome, long total_vendas,
    ProdutoVenda *produtos, int num_produtos,
    double t_qtd, double t_valor, double t_pct,
    int first)
{
    if (!first) fputs(",\n", out);
    fputs("    {\n", out);
    fprintf(out, "      \"codigo\": %ld,\n", codigo);
    fputs("      \"nome\": \"", out);
    json_escape(out, nome);
    fputs("\",\n", out);
    fprintf(out, "      \"total_vendas\": %ld,\n", total_vendas);
    fputs("      \"produtos\": [\n", out);
    for (int i = 0; i < num_produtos; i++) {
        ProdutoVenda *p = &produtos[i];
        if (i > 0) fputs(",\n", out);
        fputs("        {\n", out);
        fprintf(out, "          \"codigo\": %ld,\n", p->codigo);
        fputs("          \"nome\": \"", out);
        json_escape(out, p->nome);
        fputs("\",\n", out);
        fputs("          \"fornecedor\": ", out);
        if (p->fornecedor[0]) {
            fputc('"', out);
            json_escape(out, p->fornecedor);
            fputs("\",\n", out);
        } else {
            fputs("null,\n", out);
        }
        fprintf(out, "          \"quantidade\": %.3f,\n", p->quantidade);
        fputs("          \"unidade\": ", out);
        if (p->unidade[0]) {
            fprintf(out, "\"%s\",\n", p->unidade);
        } else {
            fputs("null,\n", out);
        }
        fprintf(out, "          \"valor\": %.3f,\n", p->valor);
        fprintf(out, "          \"participacao_pct\": %.3f\n", p->participacao_pct);
        fputs("        }", out);
    }
    if (num_produtos > 0) fputc('\n', out);
    fputs("      ],\n", out);
    fputs("      \"totais\": {\n", out);
    fprintf(out, "        \"quantidade\": %.3f,\n", t_qtd);
    fprintf(out, "        \"valor\": %.3f,\n", t_valor);
    fprintf(out, "        \"participacao_pct\": %.3f\n", t_pct);
    fputs("      }\n", out);
    fputs("    }", out);
}

static void write_totais_gerais(FILE *out,
    double com_qtd, double com_valor,
    double sem_qtd, double sem_valor,
    double per_qtd, double per_valor)
{
    fputs("  \"totais_gerais\": {\n", out);
    fputs("    \"com_vendedor\": {\n", out);
    fprintf(out, "      \"quantidade\": %.3f,\n", com_qtd);
    fprintf(out, "      \"valor\": %.3f\n", com_valor);
    fputs("    },\n", out);
    fputs("    \"sem_vendedor\": {\n", out);
    fprintf(out, "      \"quantidade\": %.3f,\n", sem_qtd);
    fprintf(out, "      \"valor\": %.3f\n", sem_valor);
    fputs("    },\n", out);
    fputs("    \"total_periodo\": {\n", out);
    fprintf(out, "      \"quantidade\": %.3f,\n", per_qtd);
    fprintf(out, "      \"valor\": %.3f\n", per_valor);
    fputs("    }\n", out);
    fputs("  }\n}\n", out);
}

/* Detects a "Grupo: <id> - <NOME>" header row (e.g. "|  Grupo: 1 - COMBUSTIVEL |")
   and fills *gid / gnome with the group code and trimmed name. Returns 1 on a
   group header, 0 for any other line. */
static int parse_grupo_header(const char *line, long *gid, char *gnome, size_t gnome_sz) {
    const char *p = strstr(line, "Grupo:");
    if (!p) return 0;
    p += 6; /* strlen("Grupo:") */
    while (*p && isspace((unsigned char)*p)) p++;
    if (!isdigit((unsigned char)*p)) return 0;

    char *end;
    long id = strtol(p, &end, 10);
    p = end;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '-') p++;                                 /* skip the "N - NOME" dash */
    while (*p && isspace((unsigned char)*p)) p++;

    size_t j = 0;
    while (*p && *p != '|' && j < gnome_sz - 1)         /* name up to the closing pipe */
        gnome[j++] = *p++;
    gnome[j] = '\0';
    while (j > 0 && isspace((unsigned char)gnome[j-1])) /* trim trailing padding */
        gnome[--j] = '\0';

    *gid = id;
    return 1;
}

/* Parses a "| <label> <codigo> - <NOME> ... |" header line (e.g.
   "| Produto.: 562 - GASOLINA C COMUM |" or "| Empresa: 1 - ZAM ... |").
   Search starts at `label`; the name runs from after " - " up to the last '|'
   on the line (or end of line). Fills *codigo and the trimmed *nome. Returns 1
   when both a numeric code and a name were extracted, 0 otherwise. */
static int parse_codigo_nome(const char *line, const char *label,
                             long *codigo, char *nome, size_t nome_sz) {
    const char *p = strstr(line, label);
    if (!p) return 0;
    const char *colon = strchr(p, ':');
    if (!colon) return 0;
    p = colon + 1;
    while (*p == ' ') p++;
    if (!isdigit((unsigned char)*p)) return 0;   /* skips lines with no code */
    *codigo = atol(p);

    const char *dp = strstr(p, " - ");
    if (!dp) return 0;
    dp += 3;

    const char *end = strrchr(line, '|');
    if (!end || end <= dp) end = line + strlen(line);
    int nlen = (int)(end - dp);
    while (nlen > 0 && isspace((unsigned char)dp[nlen-1])) nlen--;
    if (nlen <= 0 || (size_t)nlen >= nome_sz) return 0;

    memcpy(nome, dp, nlen);
    nome[nlen] = '\0';
    return 1;
}

static jfx_status_t parse_valor_estoque_reajustes(const char *in_path, const char *out_path) {
    FILE *in = fopen(in_path, "r");
    if (!in) return JFX_ERR_OPEN_INPUT;
    FILE *out = fopen(out_path, "w");
    if (!out) {
        fclose(in);
        return JFX_ERR_CREATE_OUTPUT;
    }

    char line[MAX_LINE];
    ItemReajuste item;
    int first = 1;
    long grupo_id = 0;                  /* current group, carried across rows */
    char grupo_nome[MAX_PRODUTO] = "";

    fputs("[\n", out);

    while (fgets(line, sizeof(line), in)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        if (parse_grupo_header(line, &grupo_id, grupo_nome, sizeof(grupo_nome)))
            continue;

        if (!parse_row_reajuste(line, &item)) continue;

        if (!first) fputs(",\n", out);
        first = 0;

        fputs("  {\n", out);
        fprintf(out, "    \"id\": %d,\n", item.id);
        fprintf(out, "    \"grupo_id\": %ld,\n", grupo_id);

        fputs("    \"grupo_nome\": \"", out);
        json_escape(out, grupo_nome);
        fputs("\",\n", out);

        fputs("    \"produto\": \"", out);
        json_escape(out, item.produto);
        fputs("\",\n", out);

        fprintf(out, "    \"custo\": %.3f,\n", item.custo);
        fprintf(out, "    \"venda\": %.3f\n", item.venda);

        fputs("  }", out);
    }

    fputs("\n]\n", out);

    fclose(in);
    fclose(out);
    return JFX_OK;
}

static jfx_status_t parse_posicao_estoque(const char *in_path, const char *out_path) {
    FILE *in = fopen(in_path, "r");
    if (!in) return JFX_ERR_OPEN_INPUT;
    FILE *out = fopen(out_path, "w");
    if (!out) {
        fclose(in);
        return JFX_ERR_CREATE_OUTPUT;
    }

    char line[MAX_LINE];
    ItemEstoque item;
    int first = 1;

    fputs("[\n", out);

    while (fgets(line, sizeof(line), in)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        if (!parse_row(line, &item)) continue;

        if (!first) fputs(",\n", out);
        first = 0;

        fputs("  {\n", out);
        fprintf(out, "    \"id\": %d,\n", item.id);

        if (item.categoria[0])
            fprintf(out, "    \"categoria\": \"%s\",\n", item.categoria);
        else
            fputs("    \"categoria\": null,\n", out);

        fputs("    \"produto\": \"", out);
        json_escape(out, item.produto);
        fputs("\",\n", out);

        fprintf(out, "    \"estoque\": %.2f\n", item.estoque);

        fputs("  }", out);
    }

    fputs("\n]\n", out);

    fclose(in);
    fclose(out);
    return JFX_OK;
}

static jfx_status_t parse_produtividade_funcionarios(const char *in_path, const char *out_path) {
    FILE *in = fopen(in_path, "r");
    if (!in) return JFX_ERR_OPEN_INPUT;
    FILE *out = fopen(out_path, "w");
    if (!out) {
        fclose(in);
        return JFX_ERR_CREATE_OUTPUT;
    }

    char generated_at[32]      = {0};
    char periodo_inicio[16]    = {0};
    char periodo_fim[16]       = {0};
    long empresa_codigo        = 0;
    char empresa_nome[MAX_PRODUTO] = {0};

    double totais_com_qtd = 0, totais_com_valor = 0;
    double totais_sem_qtd = 0, totais_sem_valor = 0;
    double totais_per_qtd = 0, totais_per_valor = 0;

    /* current funcionário being built */
    long         func_codigo  = 0;
    char         func_nome[MAX_PRODUTO] = {0};
    long         func_vendas  = 0;
    ProdutoVenda produtos[MAX_PRODS_PER_FUNC];
    int          num_produtos = 0;
    int          in_func      = 0;

    int first_line     = 1;
    int first_func     = 1;
    int header_written = 0;

    char line[MAX_LINE];

    while (fgets(line, sizeof(line), in)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        /* Line 1: timestamp is the last 16 chars "DD/MM/YYYY HH:MM" */
        if (first_line) {
            first_line = 0;
            if (len >= 16) {
                strncpy(generated_at, line + len - 16, 16);
                generated_at[16] = '\0';
                trim(generated_at);
            }
            continue;
        }

        /* Período: search for "odo: " (avoids matching on UTF-8 accented chars) */
        if (!periodo_inicio[0]) {
            char *pp = strstr(line, "odo: ");
            if (pp) {
                pp += 5;
                strncpy(periodo_inicio, pp, 10);
                periodo_inicio[10] = '\0';
                /* "01/05/2026 a 31/05/2026": fim starts at offset 13 */
                if (strlen(pp) >= 23) {
                    strncpy(periodo_fim, pp + 13, 10);
                    periodo_fim[10] = '\0';
                }
                continue;
            }
        }

        /* Empresa */
        if (!empresa_nome[0]) {
            char *ep = strstr(line, "Empresa: ");
            if (ep) {
                ep += 9;
                empresa_codigo = atol(ep);
                char *dp = strstr(ep, " - ");
                if (dp) {
                    dp += 3;
                    char *end = strrchr(line, '|');
                    if (end && end > dp) {
                        int nlen = (int)(end - dp);
                        while (nlen > 0 && isspace((unsigned char)dp[nlen-1])) nlen--;
                        if (nlen > 0 && nlen < MAX_PRODUTO) {
                            strncpy(empresa_nome, dp, nlen);
                            empresa_nome[nlen] = '\0';
                        }
                    }
                }
                continue;
            }
        }

        /* Totais gerais — must check before "Vendas:" to avoid mismatches */
        if (strstr(line, "Total geral de vendas com")) {
            char f[5][MAX_PRODUTO];
            if (split_pipes(line, f, 5) >= 4) {
                totais_com_qtd   = parse_br_number(f[1]);
                totais_com_valor = parse_br_number(f[3]);
            }
            continue;
        }
        if (strstr(line, "Total geral de vendas sem")) {
            char f[5][MAX_PRODUTO];
            if (split_pipes(line, f, 5) >= 4) {
                totais_sem_qtd   = parse_br_number(f[1]);
                totais_sem_valor = parse_br_number(f[3]);
            }
            continue;
        }
        if (strstr(line, "Total geral de vendas no ")) {
            char f[5][MAX_PRODUTO];
            if (split_pipes(line, f, 5) >= 4) {
                totais_per_qtd   = parse_br_number(f[1]);
                totais_per_valor = parse_br_number(f[3]);
            }
            continue;
        }

        /* Funcionário header — detected by "Vendas:" (capital V, colon) */
        char *vp = strstr(line, "Vendas:");
        if (vp) {
            if (!header_written) {
                write_relatorio_header(out, generated_at, periodo_inicio, periodo_fim,
                                       empresa_codigo, empresa_nome);
                header_written = 1;
            }

            func_codigo = 0; func_nome[0] = '\0'; func_vendas = 0; num_produtos = 0;
            in_func = 1;

            /* total_vendas: number after "Vendas:" */
            char *vnum = vp + 7;
            while (*vnum == ' ') vnum++;
            func_vendas = atol(vnum);

            /* codigo and nome: "| Funcionário: NNN - NAME ... Vendas:" */
            char *cp = strchr(line, ':');
            if (cp) {
                cp += 2;
                while (*cp == ' ') cp++;
                func_codigo = atol(cp);
                char *dp = strstr(cp, " - ");
                if (dp) {
                    dp += 3;
                    int nlen = (int)(vp - dp);
                    while (nlen > 0 && isspace((unsigned char)dp[nlen-1])) nlen--;
                    if (nlen > 0 && nlen < MAX_PRODUTO) {
                        strncpy(func_nome, dp, nlen);
                        func_nome[nlen] = '\0';
                    }
                }
            }
            continue;
        }

        /* Total do vendedor — emit the current funcionário */
        if (in_func && strstr(line, "Total do vendedor")) {
            char f[6][MAX_PRODUTO];
            double t_qtd = 0, t_valor = 0, t_pct = 0;
            if (split_pipes(line, f, 6) >= 6) {
                t_qtd   = parse_br_number(f[2]);
                t_valor = parse_br_number(f[4]);
                t_pct   = parse_br_number(f[5]);
            }
            emit_funcionario(out, func_codigo, func_nome, func_vendas,
                             produtos, num_produtos, t_qtd, t_valor, t_pct, first_func);
            first_func = 0;
            in_func    = 0;
            continue;
        }

        /* Product row */
        if (in_func && num_produtos < MAX_PRODS_PER_FUNC) {
            ProdutoVenda p;
            if (parse_produto_venda_row(line, &p))
                produtos[num_produtos++] = p;
        }
    }

    /* Close funcionarios array */
    if (header_written)
        fputs("\n  ],\n", out);

    write_totais_gerais(out,
        totais_com_qtd, totais_com_valor,
        totais_sem_qtd, totais_sem_valor,
        totais_per_qtd, totais_per_valor);

    fclose(in);
    fclose(out);
    return JFX_OK;
}

static jfx_status_t parse_movimentacao_produtos(const char *in_path, const char *out_path) {
    FILE *in = fopen(in_path, "r");
    if (!in) return JFX_ERR_OPEN_INPUT;
    FILE *out = fopen(out_path, "w");
    if (!out) {
        fclose(in);
        return JFX_ERR_CREATE_OUTPUT;
    }

    char periodo_inicio[16]        = {0};
    char periodo_fim[16]           = {0};
    long empresa_codigo            = 0;
    char empresa_nome[MAX_PRODUTO] = {0};

    MovProduto prods[MAX_MOV_PRODUTOS];
    int  nprods    = 0;

    /* current product block being read (set on "Produto.:", summed on total) */
    long cur_codigo            = 0;
    char cur_nome[MAX_PRODUTO] = {0};
    int  have_cur              = 0;

    char line[MAX_LINE];

    while (fgets(line, sizeof(line), in)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        /* Período: "odo: " avoids matching the UTF-8 accented "í". */
        if (!periodo_inicio[0]) {
            char *pp = strstr(line, "odo: ");
            if (pp) {
                pp += 5;
                strncpy(periodo_inicio, pp, 10);
                periodo_inicio[10] = '\0';
                if (strlen(pp) >= 23) {
                    strncpy(periodo_fim, pp + 13, 10);
                    periodo_fim[10] = '\0';
                }
                continue;
            }
        }

        /* Empresa: the boxed "| Empresa: N - NAME |" row (the header line above
           it has no code, so parse_codigo_nome rejects it). */
        if (!empresa_nome[0] &&
            parse_codigo_nome(line, "Empresa:", &empresa_codigo, empresa_nome, sizeof(empresa_nome)))
            continue;

        /* Product block header: "| Produto.: N - NAME |" */
        {
            long c;
            char n[MAX_PRODUTO];
            if (parse_codigo_nome(line, "Produto.:", &c, n, sizeof(n))) {
                cur_codigo = c;
                snprintf(cur_nome, sizeof(cur_nome), "%s", n);
                have_cur = 1;
                continue;
            }
        }

        /* "| Total no período ... |" closes a product block: field 2 (0-based)
           is the Entrada total for that product+deposit. Aggregate by code. */
        if (have_cur && strstr(line, "Total no per")) {
            char f[8][MAX_PRODUTO];
            if (split_pipes(line, f, 8) >= 3) {
                double entrada = parse_br_number(f[2]);

                int idx = -1;
                for (int i = 0; i < nprods; i++)
                    if (prods[i].codigo == cur_codigo) { idx = i; break; }
                if (idx < 0 && nprods < MAX_MOV_PRODUTOS) {
                    idx = nprods++;
                    prods[idx].codigo = cur_codigo;
                    snprintf(prods[idx].nome, sizeof(prods[idx].nome), "%s", cur_nome);
                    prods[idx].total_entrada = 0.0;
                }
                if (idx >= 0) prods[idx].total_entrada += entrada;
            }
            have_cur = 0;
            continue;
        }
    }

    fputs("{\n", out);
    fputs("  \"periodo\": {\n", out);
    fprintf(out, "    \"inicio\": \"%s\",\n", periodo_inicio);
    fprintf(out, "    \"fim\": \"%s\"\n", periodo_fim);
    fputs("  },\n", out);
    fputs("  \"empresa\": {\n", out);
    fprintf(out, "    \"codigo\": %ld,\n", empresa_codigo);
    fputs("    \"nome\": \"", out);
    json_escape(out, empresa_nome);
    fputs("\"\n", out);
    fputs("  },\n", out);
    fputs("  \"produtos\": [\n", out);

    int first = 1;
    for (int i = 0; i < nprods; i++) {
        if (prods[i].total_entrada <= 0.0) continue;   /* only products that entered stock */
        if (!first) fputs(",\n", out);
        first = 0;
        fputs("    {\n", out);
        fprintf(out, "      \"codigo\": %ld,\n", prods[i].codigo);
        fputs("      \"nome\": \"", out);
        json_escape(out, prods[i].nome);
        fputs("\",\n", out);
        fprintf(out, "      \"total_entrada\": %.3f\n", prods[i].total_entrada);
        fputs("    }", out);
    }
    if (!first) fputc('\n', out);
    fputs("  ]\n}\n", out);

    fclose(in);
    fclose(out);
    return JFX_OK;
}

/* ---- Venda Detalhada ------------------------------------------------------
 * One product per report ("Produto código: N" in the header). The sale rows
 * carry only a truncated product name, so a report exported without the
 * product filter can't say which product each sale was: jfx_venda_chave()
 * rejects it, and the JSON carries codigo 0. */

typedef struct {
    char gerado_em[17];           /* "AAAA-MM-DDTHH:MM", from the top-right corner */
    char inicio[11];              /* "AAAA-MM-DD" */
    char fim[11];
    long codigo;                  /* 0 = no product filter */
    char nome[MAX_PRODUTO];
    char tipo_data[32];           /* "CAIXA", ... — which date the rows are filed under */
} VendaHeader;

typedef struct {
    char   data[11];              /* "AAAA-MM-DD" */
    char   hora[6];
    char   documento[32];
    double quantidade;            /* negative for a returned sale */
    double preco_unitario;
    double valor;
    char   forma_pagamento[MAX_PRODUTO];
} VendaLinha;

/* Copies at most dst_sz-1 bytes of src (no truncation warning games). */
static void copy_field(char *dst, size_t dst_sz, const char *src) {
    size_t n = strlen(src);
    if (n >= dst_sz) n = dst_sz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* "DD/MM/AAAA" at p -> "AAAA-MM-DD" in out (11 bytes). Returns 1 on a match. */
static int br_date_iso(const char *p, char *out) {
    if (strlen(p) < 10) return 0;
    for (int i = 0; i < 10; i++) {
        if (i == 2 || i == 5) {
            if (p[i] != '/') return 0;
        } else if (!isdigit((unsigned char)p[i])) {
            return 0;
        }
    }
    memcpy(out, p + 6, 4);
    out[4] = '-';
    memcpy(out + 5, p + 3, 2);
    out[7] = '-';
    memcpy(out + 8, p, 2);
    out[10] = '\0';
    return 1;
}

/* Value after the colon of the "label...:" field on this line, trimmed. */
static int header_value(const char *line, const char *label, char *out, size_t out_sz) {
    const char *p = strstr(line, label);
    if (!p) return 0;
    const char *colon = strchr(p, ':');
    if (!colon) return 0;
    copy_field(out, out_sz, colon + 1);
    trim(out);
    return 1;
}

/* Fills whatever this header line carries. Each field is set once, so a header
   repeated on a later page can't overwrite the first. Labels stop before the
   accented letters ("Produto c" for "código", "odo: " for "Período"). */
static void parse_venda_header_line(const char *line, VendaHeader *h) {
    char v[MAX_PRODUTO];

    /* Emission stamp: the first "DD/MM/AAAA HH:MM" (Período has no time). */
    if (!h->gerado_em[0]) {
        for (const char *p = line; *p; p++) {
            char d[11];
            if (br_date_iso(p, d) && p[10] == ' ' &&
                isdigit((unsigned char)p[11]) && isdigit((unsigned char)p[12]) &&
                p[13] == ':' &&
                isdigit((unsigned char)p[14]) && isdigit((unsigned char)p[15])) {
                memcpy(h->gerado_em, d, 10);
                h->gerado_em[10] = 'T';
                memcpy(h->gerado_em + 11, p + 11, 5);
                h->gerado_em[16] = '\0';
                return;
            }
        }
    }

    if (!h->inicio[0]) {
        const char *pp = strstr(line, "odo: ");
        if (pp) {
            pp += 5;
            if (br_date_iso(pp, h->inicio)) {
                const char *a = strstr(pp + 10, " a ");
                if (!a || !br_date_iso(a + 3, h->fim)) h->fim[0] = '\0';
            }
            return;
        }
    }

    if (!h->codigo && header_value(line, "Produto c", v, sizeof(v))) {
        if (is_digits_only(v)) h->codigo = atol(v);
        return;
    }
    if (!h->nome[0] && header_value(line, "Produto nome", v, sizeof(v))) {
        copy_field(h->nome, sizeof(h->nome), v);
        return;
    }
    if (!h->tipo_data[0] && header_value(line, "Tipo data", v, sizeof(v))) {
        copy_field(h->tipo_data, sizeof(h->tipo_data), v);
        return;
    }
}

/* "| DD/MM/AAAA | HH:MM | abst | bico | produto | doc | qtd | preço | valor | forma |" */
static int parse_venda_row(const char *line, VendaLinha *v) {
    if (line[0] != '|') return 0;
    char f[12][MAX_PRODUTO];
    if (split_pipes(line, f, 12) < 10) return 0;
    if (strlen(f[0]) != 10 || !br_date_iso(f[0], v->data)) return 0;

    copy_field(v->hora, sizeof(v->hora), f[1]);
    copy_field(v->documento, sizeof(v->documento), f[5]);
    v->quantidade     = parse_br_number(f[6]);
    v->preco_unitario = parse_br_number(f[7]);
    v->valor          = parse_br_number(f[8]);
    copy_field(v->forma_pagamento, sizeof(v->forma_pagamento), f[9]);
    return 1;
}

/* "| Total ... | qtd | | valor | |" closing the table. */
static int parse_venda_total(const char *line, double *qtd, double *valor) {
    if (line[0] != '|') return 0;
    char f[6][MAX_PRODUTO];
    if (split_pipes(line, f, 6) < 4) return 0;
    if (strncmp(f[0], "Total", 5) != 0) return 0;
    *qtd   = parse_br_number(f[1]);
    *valor = parse_br_number(f[3]);
    return 1;
}

static int read_venda_header(const char *in_path, VendaHeader *h) {
    FILE *in = fopen(in_path, "r");
    if (!in) return 0;

    memset(h, 0, sizeof(*h));
    char line[MAX_LINE];
    int  scanned = 0;

    while (fgets(line, sizeof(line), in) && scanned < DETECT_MAX_LINES) {
        if (line[0] == '+' || line[0] == '|') break;   /* past the header */
        scanned++;
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';
        parse_venda_header_line(line, h);
    }

    fclose(in);
    return 1;
}

static void write_venda_header(FILE *out, const VendaHeader *h) {
    fputs("{\n", out);
    fprintf(out, "  \"gerado_em\": \"%s\",\n", h->gerado_em);
    fputs("  \"periodo\": {\n", out);
    fprintf(out, "    \"inicio\": \"%s\",\n", h->inicio);
    fprintf(out, "    \"fim\": \"%s\"\n", h->fim);
    fputs("  },\n", out);
    fputs("  \"produto\": {\n", out);
    fprintf(out, "    \"codigo\": %ld,\n", h->codigo);
    fputs("    \"nome\": \"", out);
    json_escape(out, h->nome);
    fputs("\"\n", out);
    fputs("  },\n", out);
    fputs("  \"tipo_data\": \"", out);
    json_escape(out, h->tipo_data);
    fputs("\",\n", out);
    fputs("  \"vendas\": [\n", out);
}

static jfx_status_t parse_venda_detalhada(const char *in_path, const char *out_path) {
    FILE *in = fopen(in_path, "r");
    if (!in) return JFX_ERR_OPEN_INPUT;
    FILE *out = fopen(out_path, "w");
    if (!out) {
        fclose(in);
        return JFX_ERR_CREATE_OUTPUT;
    }

    VendaHeader h;
    memset(&h, 0, sizeof(h));
    VendaLinha  v;
    int    header_written = 0, first = 1, have_total = 0;
    double total_qtd = 0.0, total_valor = 0.0;
    char   line[MAX_LINE];

    while (fgets(line, sizeof(line), in)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        if (line[0] != '+' && line[0] != '|') {
            parse_venda_header_line(line, &h);
            continue;
        }

        /* The header block always precedes the table. */
        if (!header_written) {
            write_venda_header(out, &h);
            header_written = 1;
        }

        if (parse_venda_row(line, &v)) {
            if (!first) fputs(",\n", out);
            first = 0;
            fputs("    {\n", out);
            fprintf(out, "      \"data\": \"%s\",\n", v.data);
            fputs("      \"hora\": \"", out);
            json_escape(out, v.hora);
            fputs("\",\n", out);
            fputs("      \"documento\": \"", out);
            json_escape(out, v.documento);
            fputs("\",\n", out);
            fprintf(out, "      \"quantidade\": %.3f,\n", v.quantidade);
            fprintf(out, "      \"preco_unitario\": %.3f,\n", v.preco_unitario);
            fprintf(out, "      \"valor\": %.2f,\n", v.valor);
            fputs("      \"forma_pagamento\": \"", out);
            json_escape(out, v.forma_pagamento);
            fputs("\"\n", out);
            fputs("    }", out);
            continue;
        }

        if (parse_venda_total(line, &total_qtd, &total_valor))
            have_total = 1;
    }

    if (!header_written) write_venda_header(out, &h);   /* no table at all */
    if (!first) fputc('\n', out);
    fputs("  ],\n", out);
    /* The ERP's own total, so a reader can check the rows add up to it — a row
       layout change shows up as a mismatch instead of silently lost sales. */
    if (have_total) {
        fprintf(out, "  \"total_quantidade\": %.3f,\n", total_qtd);
        fprintf(out, "  \"total_valor\": %.2f\n", total_valor);
    } else {
        fputs("  \"total_quantidade\": null,\n", out);
        fputs("  \"total_valor\": null\n", out);
    }
    fputs("}\n", out);

    fclose(in);
    fclose(out);
    return JFX_OK;
}

/* Viscosity grade in a product name ("... SP 5W30 GRANEL", "SAE 15W-40") as
   "5w30" / "15w40" in out. Only a whole token counts: 1-2 digits, W, an
   optional '-', 2 digits. Returns 1 when found. */
static int grau_viscosidade(const char *nome, char *out, size_t out_sz) {
    size_t n = strlen(nome);
    for (size_t i = 0; i < n; i++) {
        if (i > 0 && isalnum((unsigned char)nome[i-1])) continue;   /* token start */
        size_t j = i, d1 = 0;
        while (j < n && isdigit((unsigned char)nome[j]) && d1 < 2) { j++; d1++; }
        if (d1 == 0 || j >= n || toupper((unsigned char)nome[j]) != 'W') continue;
        j++;
        if (j < n && nome[j] == '-') j++;
        size_t k = j, d2 = 0;
        while (k < n && isdigit((unsigned char)nome[k]) && d2 < 2) { k++; d2++; }
        if (d2 != 2) continue;
        if (k < n && isalnum((unsigned char)nome[k])) continue;     /* token end */
        if (d1 + 1 + d2 + 1 > out_sz) return 0;
        memcpy(out, nome + i, d1);
        out[d1] = 'w';
        memcpy(out + d1 + 1, nome + j, d2);
        out[d1 + 1 + d2] = '\0';
        return 1;
    }
    return 0;
}

int jfx_venda_chave(const char *in_path, char *buf, size_t buf_sz) {
    if (!buf || buf_sz < 24) return 0;

    VendaHeader h;
    if (!read_venda_header(in_path, &h)) return 0;
    if (h.codigo <= 0 || !h.inicio[0] || !h.fim[0]) return 0;

    /* The grade names the file the way people call the oil; a product
       without one (not an engine oil) falls back to its ERP code. */
    if (!grau_viscosidade(h.nome, buf, buf_sz))
        snprintf(buf, buf_sz, "%ld", h.codigo);
    return 1;
}

jfx_parser_t jfx_detect(const char *in_path) {
    FILE *in = fopen(in_path, "r");
    if (!in) return JFX_AUTO;

    char line[MAX_LINE];
    jfx_parser_t result = JFX_AUTO;
    int scanned = 0;

    while (fgets(line, sizeof(line), in) && scanned < DETECT_MAX_LINES) {
        /* Title lives in the header block, above the +---/| table region. */
        if (line[0] == '+' || line[0] == '|') break;
        scanned++;

        /* Accent-free anchors, matched case-insensitively. Order matters:
         * both stock reports contain "ESTOQUE", so "VALOR DO ESTOQUE" first.
         * "MOVIMENTA" stops before the "Ç" of "MOVIMENTAÇÃO DE PRODUTOS".
         * "VENDA DETALHADA" goes first: its header names a product, and a
         * product name may contain any of the other anchors. */
        if (ci_contains(line, "VENDA DETALHADA")) { result = JFX_VENDA_DETALHADA; break; }
        if (ci_contains(line, "MOVIMENTA")) { result = JFX_MOVIMENTACAO_PRODUTOS; break; }
        if (ci_contains(line, "PRODUTIVIDADE")) { result = JFX_PRODUTIVIDADE; break; }
        if (ci_contains(line, "VALOR DO ESTOQUE")) { result = JFX_VALOR_ESTOQUE; break; }
        if (ci_contains(line, "ESTOQUE")) { result = JFX_POSICAO_ESTOQUE; break; }
    }

    fclose(in);
    return result;
}

int jfx_periodo_ym(const char *in_path, char *buf, size_t buf_sz) {
    if (!buf || buf_sz < 8) return 0;

    FILE *in = fopen(in_path, "r");
    if (!in) return 0;

    char line[MAX_LINE];
    int  scanned = 0, ok = 0;

    while (fgets(line, sizeof(line), in) && scanned < DETECT_MAX_LINES) {
        if (line[0] == '+' || line[0] == '|') break;   /* past the header */
        scanned++;

        char *pp = strstr(line, "odo: ");               /* "Período: DD/MM/AAAA ..." */
        if (pp) {
            pp += 5;
            /* Expect DD/MM/AAAA; build "AAAA-MM". */
            if (strlen(pp) >= 10 && isdigit((unsigned char)pp[0]) &&
                pp[2] == '/' && pp[5] == '/') {
                buf[0] = pp[6]; buf[1] = pp[7]; buf[2] = pp[8]; buf[3] = pp[9];
                buf[4] = '-';
                buf[5] = pp[3]; buf[6] = pp[4];
                buf[7] = '\0';
                ok = 1;
            }
            break;
        }
    }

    fclose(in);
    return ok;
}

jfx_status_t jfx_convert(const char *in_path, const char *out_path, jfx_parser_t parser) {
    struct stat st;
    if (stat(in_path, &st) != 0) return JFX_ERR_OPEN_INPUT;
    if (!S_ISREG(st.st_mode))    return JFX_ERR_NOT_REGULAR_FILE;

    if (parser == JFX_AUTO) {
        parser = jfx_detect(in_path);
        if (parser == JFX_AUTO) return JFX_ERR_DETECT_FAILED;
    }

    switch (parser) {
        case JFX_POSICAO_ESTOQUE:       return parse_posicao_estoque(in_path, out_path);
        case JFX_VALOR_ESTOQUE:         return parse_valor_estoque_reajustes(in_path, out_path);
        case JFX_PRODUTIVIDADE:         return parse_produtividade_funcionarios(in_path, out_path);
        case JFX_MOVIMENTACAO_PRODUTOS: return parse_movimentacao_produtos(in_path, out_path);
        case JFX_VENDA_DETALHADA:       return parse_venda_detalhada(in_path, out_path);
        default:                        return JFX_ERR_INVALID_PARSER;
    }
}

const char *jfx_status_str(jfx_status_t status) {
    switch (status) {
        case JFX_OK:                   return "sucesso";
        case JFX_ERR_OPEN_INPUT:       return "não foi possível abrir o arquivo de entrada";
        case JFX_ERR_CREATE_OUTPUT:    return "não foi possível criar o arquivo de saída";
        case JFX_ERR_NOT_REGULAR_FILE: return "a entrada não é um arquivo regular";
        case JFX_ERR_DETECT_FAILED:    return "não foi possível identificar o tipo de relatório";
        case JFX_ERR_INVALID_PARSER:   return "parser inválido";
        default:                       return "erro desconhecido";
    }
}
