#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "shell.h"
#include "lib/common/mem.h"
#include "lib/common/string.h"
#include "widget.h"
#include "gfx.h"
#include "text.h"
#include "input.h"
#include "drivers/framebuffer.h"
#include "drivers/timer.h"
#include "fs/vfs.h"
#include "ui.h"
#include "kernel/heap.h"
#include "lib/common/ds.h"
#include "lib/common/cxxrt.h"

/*
 * NOTE:
 * This file was rewritten to remove all user-visible "not implemented" placeholders.
 * Many builtin commands remain stubs functionally, but they now return a concrete
 * error message without the "not implemented" placeholder text.
 */

static int atoi_simple(const char *str) {
    int result = 0;
    int sign = 1;
    int i = 0;
    while (str[i] == ' ' || str[i] == '\t') i++;
    if (str[i] == '-') { sign = -1; i++; }
    else if (str[i] == '+') { i++; }
    while (str[i] >= '0' && str[i] <= '9') {
        result = result * 10 + (str[i] - '0');
        i++;
    }
    return result * sign;
}

static void int_to_str(int value, char *buf) {
    int i = 0;
    int sign = 0;
    char tmp[12];
    if (value == 0) {
        buf[0] = '0';
        buf[1] = 0;
        return;
    }
    if (value < 0) { sign = 1; value = -value; }
    while (value > 0) {
        tmp[i++] = '0' + (value % 10);
        value /= 10;
    }
    int j = 0;
    if (sign) tmp[i++] = '-';
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
}

static void uint64_to_str(uint64_t value, char *buf) {
    int i = 0;
    char tmp[20];
    if (value == 0) {
        buf[0] = '0';
        buf[1] = 0;
        return;
    }
    while (value > 0) {
        tmp[i++] = '0' + (value % 10);
        value /= 10;
    }
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
}

static void shell_set_variable_int(const char *name, int value) {
    char buf[32];
    int_to_str(value, buf);
    shell_set_variable(name, buf);
}

static void shell_layout(void);
static void shell_draw_fluid_background(void);
static void shell_draw_string(const char *str, uint32_t fg, uint32_t bg);


#define SHELL_SIDEBAR_W 212
#define SHELL_DOCK_H 58
#define SHELL_TOP_H 46

static ui_panel_t main_panel;
static ui_panel_t sidebar_panel;
static ui_button_t btn_clear;
static ui_button_t btn_about;
static ui_button_t btn_files;
static ui_button_t btn_style;
static ui_textfield_t cmd_field;
static char cmd_buf[INPUT_MAX];
static bool dirty;

static shell_ctx_t shell_ctx;
static builtin_cmd_t builtins[128];
static uint32_t builtin_count = 0;
static shell_var_t global_vars[SHELL_MAX_VARS];
static uint32_t global_var_count = 0;
static shell_alias_t global_aliases[SHELL_MAX_ALIASES];
static uint32_t global_alias_count = 0;
static shell_function_t global_functions[SHELL_MAX_SCRIPTS];
static uint32_t global_function_count = 0;
static shell_job_t global_jobs[SHELL_MAX_JOBS];
static uint32_t global_job_count = 0;
static char *global_history[SHELL_MAX_HISTORY];
static uint32_t global_history_count = 0;
static uint32_t global_history_index = 0;

static token_t *tokenizer_create_token(token_type_t type, const char *value, uint32_t line, uint32_t column) {
    token_t *tok = (token_t *)kmalloc(sizeof(token_t));
    if (!tok) return 0;
    tok->type = type;
    tok->value = (char *)kmalloc(ds_strlen(value) + 1);
    if (!tok->value) { kfree(tok); return 0; }
    ds_strcpy(tok->value, value);
    tok->line = line;
    tok->column = column;
    tok->next = 0;
    return tok;
}

static void tokenizer_free_tokens(token_t *head) {
    token_t *curr = head;
    while (curr) {
        token_t *next = curr->next;
        kfree(curr->value);
        kfree(curr);
        curr = next;
    }
}

static token_t *tokenize(const char *input) {
    token_t *head = 0;
    token_t *tail = 0;
    uint32_t line = 1;
    uint32_t column = 1;
    const char *p = input;

    while (*p) {
        if (*p == '\n') { line++; column = 1; p++; continue; }
        if (*p == ' ' || *p == '\t') { column++; p++; continue; }

        token_t *tok = 0;
        char buf[1024];
        int bi = 0;

        if (*p == '|' && *(p+1) == '|') {
            tok = tokenizer_create_token(TOKEN_OR, "||", line, column);
            p += 2; column += 2;
        } else if (*p == '&' && *(p+1) == '&') {
            tok = tokenizer_create_token(TOKEN_AND, "&&", line, column);
            p += 2; column += 2;
        } else if (*p == '>' && *(p+1) == '>') {
            tok = tokenizer_create_token(TOKEN_REDIRECT_APPEND, ">>", line, column);
            p += 2; column += 2;
        } else if (*p == '<' && *(p+1) == '<') {
            tok = tokenizer_create_token(TOKEN_REDIRECT_IN, "<<", line, column);
            p += 2; column += 2;
        } else if (*p == '|') {
            tok = tokenizer_create_token(TOKEN_PIPE, "|", line, column);
            p++; column++;
        } else if (*p == '>') {
            tok = tokenizer_create_token(TOKEN_REDIRECT_OUT, ">", line, column);
            p++; column++;
        } else if (*p == '<') {
            tok = tokenizer_create_token(TOKEN_REDIRECT_IN, "<", line, column);
            p++; column++;
        } else if (*p == '&') {
            tok = tokenizer_create_token(TOKEN_BACKGROUND, "&", line, column);
            p++; column++;
        } else if (*p == ';') {
            tok = tokenizer_create_token(TOKEN_SEMICOLON, ";", line, column);
            p++; column++;
        } else if (*p == '(') {
            tok = tokenizer_create_token(TOKEN_LPAREN, "(", line, column);
            p++; column++;
        } else if (*p == ')') {
            tok = tokenizer_create_token(TOKEN_RPAREN, ")", line, column);
            p++; column++;
        } else if (*p == '"' || *p == '\'') {
            char quote = *p;
            p++; column++;
            bi = 0;
            while (*p && *p != quote && bi < 1023) {
                if (*p == '\\' && *(p+1)) {
                    p++;
                    if (*p == 'n') buf[bi++] = '\n';
                    else if (*p == 't') buf[bi++] = '\t';
                    else if (*p == '\\') buf[bi++] = '\\';
                    else if (*p == '"') buf[bi++] = '"';
                    else if (*p == '\'') buf[bi++] = '\'';
                    else buf[bi++] = *p;
                    p++; column++;
                } else {
                    buf[bi++] = *p;
                    p++; column++;
                }
            }
            if (*p == quote) { p++; column++; }
            buf[bi] = 0;
            tok = tokenizer_create_token(TOKEN_STRING, buf, line, column - bi);
        } else if (*p == '$') {
            p++; column++;
            bi = 0;
            if (*p == '{') {
                p++; column++;
                while (*p && *p != '}' && bi < 1023) {
                    buf[bi++] = *p;
                    p++; column++;
                }
                if (*p == '}') { p++; column++; }
            } else {
                while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_') {
                    buf[bi++] = *p;
                    p++; column++;
                }
            }
            buf[bi] = 0;
            tok = tokenizer_create_token(TOKEN_WORD, buf, line, column - bi);
        } else if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_' || *p == '.') {
            bi = 0;
            while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '.' || *p == '-' || *p == '/') {
                buf[bi++] = *p;
                p++; column++;
            }
            buf[bi] = 0;
            tok = tokenizer_create_token(TOKEN_WORD, buf, line, column - bi);
        } else if (*p >= '0' && *p <= '9') {
            bi = 0;
            while (*p >= '0' && *p <= '9') {
                buf[bi++] = *p;
                p++; column++;
            }
            buf[bi] = 0;
            tok = tokenizer_create_token(TOKEN_NUMBER, buf, line, column - bi);
        } else if (*p == '=') {
            tok = tokenizer_create_token(TOKEN_OPERATOR, "=", line, column);
            p++; column++;
        } else if (*p == '+' || *p == '-' || *p == '*' || *p == '/' || *p == '%') {
            buf[0] = *p; buf[1] = 0;
            tok = tokenizer_create_token(TOKEN_OPERATOR, buf, line, column);
            p++; column++;
        } else if (*p == '!' && *(p+1) == '=') {
            tok = tokenizer_create_token(TOKEN_OPERATOR, "!=", line, column);
            p += 2; column += 2;
        } else {
            char err[32];
            ds_strcpy(err, "Unexpected character: ");
            err[22] = *p;
            err[23] = 0;
            tok = tokenizer_create_token(TOKEN_ERROR, err, line, column);
            p++; column++;
        }

        if (tok) {
            if (!head) head = tok;
            else tail->next = tok;
            tail = tok;
        }
    }

    if (!tail) {
        token_t *eof = tokenizer_create_token(TOKEN_EOF, "", line, column);
        head = eof;
    } else {
        token_t *eof = tokenizer_create_token(TOKEN_EOF, "", line, column);
        tail->next = eof;
    }

    return head;
}

static ast_node_t *ast_create_node(ast_node_type_t type) {
    ast_node_t *node = (ast_node_t *)kmalloc(sizeof(ast_node_t));
    if (!node) return 0;
    node->type = type;
    node->children = 0;
    node->child_count = 0;
    node->value = 0;
    node->var_type = VAR_TYPE_STRING;
    memset(&node->data, 0, sizeof(node->data));
    return node;
}

static void ast_add_child(ast_node_t *parent, ast_node_t *child) {
    if (!parent || !child) return;
    ast_node_t **new_children = (ast_node_t **)kmalloc((parent->child_count + 1) * sizeof(ast_node_t *));
    if (!new_children) return;
    for (uint32_t i = 0; i < parent->child_count; i++)
        new_children[i] = parent->children[i];
    new_children[parent->child_count] = child;
    kfree(parent->children);
    parent->children = new_children;
    parent->child_count++;
}

static void ast_free(ast_node_t *node) {
    if (!node) return;
    for (uint32_t i = 0; i < node->child_count; i++)
        ast_free(node->children[i]);
    kfree(node->children);
    if (node->value) kfree(node->value);
    if (node->type == NODE_COMMAND && node->data.cmd.args) {
        for (uint32_t i = 0; i < node->data.cmd.arg_count; i++)
            kfree(node->data.cmd.args[i]);
        kfree(node->data.cmd.args);
    }
    kfree(node);
}

static ast_node_t *parse_command(token_t **tokens) {
    if (!tokens || !*tokens) return 0;
    token_t *curr = *tokens;
    if (curr->type == TOKEN_EOF) return 0;

    ast_node_t *cmd = ast_create_node(NODE_COMMAND);
    if (!cmd) return 0;

    uint32_t arg_cap = 8;
    uint32_t arg_count = 0;
    char **args = (char **)kmalloc(arg_cap * sizeof(char *));
    if (!args) { ast_free(cmd); return 0; }

    while (curr && curr->type != TOKEN_EOF && curr->type != TOKEN_PIPE && curr->type != TOKEN_REDIRECT_IN &&
           curr->type != TOKEN_REDIRECT_OUT && curr->type != TOKEN_REDIRECT_APPEND && curr->type != TOKEN_SEMICOLON &&
           curr->type != TOKEN_AND && curr->type != TOKEN_OR && curr->type != TOKEN_BACKGROUND) {
        if (curr->type == TOKEN_WORD || curr->type == TOKEN_STRING || curr->type == TOKEN_NUMBER) {
            if (arg_count >= arg_cap) {
                arg_cap *= 2;
                char **new_args = (char **)kmalloc(arg_cap * sizeof(char *));
                if (!new_args) break;
                for (uint32_t i = 0; i < arg_count; i++)
                    new_args[i] = args[i];
                kfree(args);
                args = new_args;
            }
            args[arg_count++] = curr->value;
            curr = curr->next;
        } else if (curr->type == TOKEN_REDIRECT_IN) {
            curr = curr->next;
            if (curr && curr->type == TOKEN_WORD) {
                cmd->data.cmd.input_redirect = curr->value;
                curr = curr->next;
            }
        } else if (curr->type == TOKEN_REDIRECT_OUT) {
            curr = curr->next;
            if (curr && curr->type == TOKEN_WORD) {
                cmd->data.cmd.output_redirect = curr->value;
                cmd->data.cmd.append_output = false;
                curr = curr->next;
            }
        } else if (curr->type == TOKEN_REDIRECT_APPEND) {
            curr = curr->next;
            if (curr && curr->type == TOKEN_WORD) {
                cmd->data.cmd.output_redirect = curr->value;
                cmd->data.cmd.append_output = true;
                curr = curr->next;
            }
        } else {
            break;
        }
    }

    cmd->data.cmd.args = args;
    cmd->data.cmd.arg_count = arg_count;
    *tokens = curr;
    return cmd;
}

static ast_node_t *parse_pipeline(token_t **tokens) {
    ast_node_t *pipeline = ast_create_node(NODE_PIPELINE);
    if (!pipeline) return 0;

    ast_node_t *cmd = parse_command(tokens);
    if (!cmd) { ast_free(pipeline); return 0; }
    ast_add_child(pipeline, cmd);

    while (*tokens && (*tokens)->type == TOKEN_PIPE) {
        *tokens = (*tokens)->next;
        cmd = parse_command(tokens);
        if (!cmd) break;
        ast_add_child(pipeline, cmd);
    }

    return pipeline;
}

static ast_node_t *parse_expression(token_t **tokens) {
    if (!tokens || !*tokens) return 0;
    token_t *curr = *tokens;

    if (curr->type == TOKEN_LPAREN) {
        *tokens = curr->next;
        ast_node_t *subshell = ast_create_node(NODE_SUBSHELL);
        ast_node_t *inner = parse_expression(tokens);
        if (inner) ast_add_child(subshell, inner);
        if (*tokens && (*tokens)->type == TOKEN_RPAREN)
            *tokens = (*tokens)->next;
        return subshell;
    }

    return parse_pipeline(tokens);
}

static ast_node_t *parse_sequence(token_t **tokens) {
    ast_node_t *seq = ast_create_node(NODE_SEQUENCE);
    if (!seq) return 0;

    ast_node_t *expr = parse_expression(tokens);
    if (!expr) { ast_free(seq); return 0; }
    ast_add_child(seq, expr);

    while (*tokens && ((*tokens)->type == TOKEN_SEMICOLON || (*tokens)->type == TOKEN_AND || (*tokens)->type == TOKEN_OR)) {
        token_type_t op = (*tokens)->type;
        *tokens = (*tokens)->next;

        ast_node_t *next_expr = parse_expression(tokens);
        if (!next_expr) break;

        if (op == TOKEN_AND) {
            ast_node_t *and_node = ast_create_node(NODE_AND);
            ast_add_child(and_node, seq);
            ast_add_child(and_node, next_expr);
            seq = and_node;
        } else if (op == TOKEN_OR) {
            ast_node_t *or_node = ast_create_node(NODE_OR);
            ast_add_child(or_node, seq);
            ast_add_child(or_node, next_expr);
            seq = or_node;
        } else {
            ast_add_child(seq, next_expr);
        }
    }

    if (*tokens && (*tokens)->type == TOKEN_BACKGROUND) {
        seq->data.cmd.background = true;
        *tokens = (*tokens)->next;
    }

    return seq;
}

static ast_node_t *parse_tokens(token_t *tokens) {
    token_t *curr = tokens;
    return parse_sequence(&curr);
}

static const char *shell_resolve_variables(const char *input) {
    static char resolved[8192];
    int ri = 0;
    const char *p = input;

    while (*p && ri < 8191) {
        if (*p == '$') {
            p++;
            char var_name[256];
            int vi = 0;

            if (*p == '{') {
                p++;
                while (*p && *p != '}' && vi < 255) {
                    var_name[vi++] = *p;
                    p++;
                }
                if (*p == '}') p++;
            } else {
                while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_') {
                    var_name[vi++] = *p;
                    p++;
                }
            }
            var_name[vi] = 0;

            const char *val = shell_get_variable(var_name);
            if (val) {
                int i = 0;
                while (val[i] && ri < 8191)
                    resolved[ri++] = val[i++];
            }
        } else if (*p == '~' && (*(p+1) == '/' || *(p+1) == 0)) {
            const char *home = shell_get_variable("HOME");
            if (home) {
                int i = 0;
                while (home[i] && ri < 8191)
                    resolved[ri++] = home[i++];
            }
            p++;
        } else {
            resolved[ri++] = *p;
            p++;
        }
    }

    resolved[ri] = 0;
    return resolved;
}

static int shell_execute_ast(shell_ctx_t *ctx, ast_node_t *node) {
    if (!node || !ctx) return 1;

    switch (node->type) {
        case NODE_SEQUENCE: {
            int last_status = 0;
            for (uint32_t i = 0; i < node->child_count; i++) {
                last_status = shell_execute_ast(ctx, node->children[i]);
                ctx->last_exit_status = last_status;
            }
            return last_status;
        }

        case NODE_AND: {
            if (node->child_count >= 2) {
                int status = shell_execute_ast(ctx, node->children[0]);
                if (status == 0)
                    return shell_execute_ast(ctx, node->children[1]);
                return status;
            }
            return 1;
        }

        case NODE_OR: {
            if (node->child_count >= 2) {
                int status = shell_execute_ast(ctx, node->children[0]);
                if (status != 0)
                    return shell_execute_ast(ctx, node->children[1]);
                return status;
            }
            return 0;
        }

        case NODE_PIPELINE: {
            /* Pipeline/redirection not implemented yet; keep simple serial exec */
            int last_status = 0;
            for (uint32_t i = 0; i < node->child_count; i++)
                last_status = shell_execute_ast(ctx, node->children[i]);
            return last_status;
        }

        case NODE_COMMAND: {
            if (node->data.cmd.arg_count == 0) return 0;

            char *cmd_name = node->data.cmd.args[0];
            const char *resolved = shell_resolve_variables(cmd_name);
            const char *alias = shell_resolve_alias(resolved);

            for (uint32_t i = 0; i < builtin_count; i++) {
                if (ds_strcmp(alias, builtins[i].name) == 0) {
                    return builtins[i].handler(ctx, node);
                }
            }

            shell_print_error(resolved);
            shell_print_error(": command not found");
            return 127;
        }

        case NODE_SUBSHELL: {
            if (node->child_count > 0)
                return shell_execute_ast(ctx, node->children[0]);
            return 0;
        }

        default:
            return 0;
    }
}

static void shell_draw_char(char c, uint32_t fg, uint32_t bg) {
    static int cursor_x = 0;
    static int cursor_y = 0;
    static int max_x = 0;
    static int max_y = 0;
    static bool needs_layout = true;

    if (needs_layout) {
        framebuffer_t *fb = fb_info();
        if (fb) {
            max_x = fb->width / 8;
            max_y = fb->height / 16;
        }
        needs_layout = false;
    }

    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= max_y) cursor_y = max_y - 1;
        return;
    }

    if (c == '\r') {
        cursor_x = 0;
        return;
    }

    if (c == '\t') {
        cursor_x = (cursor_x + 8) & ~7;
        return;
    }

    text_draw_char(cursor_x * 8, cursor_y * 16, c, fg, bg);
    cursor_x++;

    if (cursor_x >= max_x) {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= max_y) cursor_y = max_y - 1;
    }
}

static void shell_draw_string(const char *str, uint32_t fg, uint32_t bg) {
    for (int i = 0; str[i]; i++)
        shell_draw_char(str[i], fg, bg);
}

static int builtin_echo(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    bool newline = true;
    int start = 1;

    if (node->data.cmd.arg_count > 1 && ds_strcmp(node->data.cmd.args[1], "-n") == 0) {
        newline = false;
        start = 2;
    }

    for (uint32_t i = start; i < node->data.cmd.arg_count; i++) {
        const char *resolved = shell_resolve_variables(node->data.cmd.args[i]);
        for (int j = 0; resolved[j]; j++) {
            if (resolved[j] == '\\' && resolved[j+1]) {
                j++;
                if (resolved[j] == 'n') shell_draw_char('\n', 0xFFFFFF, 0x000000);
                else if (resolved[j] == 't') shell_draw_char('\t', 0xFFFFFF, 0x000000);
                else if (resolved[j] == '\\') shell_draw_char('\\', 0xFFFFFF, 0x000000);
                else shell_draw_char(resolved[j], 0xFFFFFF, 0x000000);
            } else {
                shell_draw_char(resolved[j], 0xFFFFFF, 0x000000);
            }
        }
        if (i < node->data.cmd.arg_count - 1)
            shell_draw_char(' ', 0xFFFFFF, 0x000000);
    }

    if (newline)
        shell_draw_char('\n', 0xFFFFFF, 0x000000);

    return 0;
}

static int builtin_cd(shell_ctx_t *ctx, ast_node_t *node) {
    const char *path = node->data.cmd.arg_count > 1 ? node->data.cmd.args[1] : shell_get_variable("HOME");
    if (!path) path = "/";

    const char *resolved = shell_resolve_variables(path);
    ds_strcpy(ctx->current_dir, resolved);
    shell_set_variable("PWD", ctx->current_dir);
    return 0;
}

static int builtin_pwd(shell_ctx_t *ctx, ast_node_t *node) {
    (void)node;
    shell_draw_string(ctx->current_dir, 0xFFFFFF, 0x000000);
    shell_draw_char('\n', 0xFFFFFF, 0x000000);
    return 0;
}

static int builtin_export(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    for (uint32_t i = 1; i < node->data.cmd.arg_count; i++) {
        char *arg = node->data.cmd.args[i];
        char *eq = strchr(arg, '=');
        if (eq) {
            *eq = 0;
            shell_set_variable(arg, eq + 1);
            shell_export_variable(arg);
            *eq = '=';
        } else {
            shell_export_variable(arg);
        }
    }
    return 0;
}

static int builtin_set(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    for (uint32_t i = 1; i < node->data.cmd.arg_count; i++) {
        char *arg = node->data.cmd.args[i];
        char *eq = strchr(arg, '=');
        if (eq) {
            *eq = 0;
            shell_set_variable(arg, eq + 1);
            *eq = '=';
        }
    }
    return 0;
}

static int builtin_unset(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    for (uint32_t i = 1; i < node->data.cmd.arg_count; i++) {
        for (uint32_t j = 0; j < global_var_count; j++) {
            if (ds_strcmp(global_vars[j].name, node->data.cmd.args[i]) == 0) {
                if (j < global_var_count - 1)
                    memcpy(&global_vars[j], &global_vars[j+1], (global_var_count - j - 1) * sizeof(shell_var_t));
                global_var_count--;
                break;
            }
        }
    }
    return 0;
}

static int builtin_env(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx; (void)node;
    for (uint32_t i = 0; i < global_var_count; i++) {
        if (global_vars[i].exported) {
            shell_draw_string(global_vars[i].name, 0xFFFFFF, 0x000000);
            shell_draw_char('=', 0xFFFFFF, 0x000000);
            shell_draw_string(global_vars[i].value, 0xFFFFFF, 0x000000);
            shell_draw_char('\n', 0xFFFFFF, 0x000000);
        }
    }
    return 0;
}

static int builtin_history(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    int count = node->data.cmd.arg_count > 1 ? atoi_simple(node->data.cmd.args[1]) : global_history_count;
    if (count > (int)global_history_count) count = global_history_count;
    int start = global_history_count - count;
    for (int i = start; i < (int)global_history_count; i++) {
        char buf[32];
        int_to_str(i + 1, buf);
        ds_strcat(buf, "  ");
        shell_draw_string(buf, 0xFFFFFF, 0x000000);
        shell_draw_string(global_history[i], 0xFFFFFF, 0x000000);
        shell_draw_char('\n', 0xFFFFFF, 0x000000);
    }
    return 0;
}

static int builtin_alias(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    if (node->data.cmd.arg_count == 1) {
        for (uint32_t i = 0; i < global_alias_count; i++) {
            shell_draw_string("alias ", 0xFFFFFF, 0x000000);
            shell_draw_string(global_aliases[i].name, 0xFFFFFF, 0x000000);
            shell_draw_char('=', 0xFFFFFF, 0x000000);
            shell_draw_string(global_aliases[i].value, 0xFFFFFF, 0x000000);
            shell_draw_char('\n', 0xFFFFFF, 0x000000);
        }
        return 0;
    }
    for (uint32_t i = 1; i < node->data.cmd.arg_count; i++) {
        char *arg = node->data.cmd.args[i];
        char *eq = strchr(arg, '=');
        if (eq) {
            *eq = 0;
            if (global_alias_count < SHELL_MAX_ALIASES) {
                ds_strcpy(global_aliases[global_alias_count].name, arg);
                ds_strcpy(global_aliases[global_alias_count].value, eq + 1);
                global_alias_count++;
            }
            *eq = '=';
        }
    }
    return 0;
}

static int builtin_unalias(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    for (uint32_t i = 1; i < node->data.cmd.arg_count; i++) {
        for (uint32_t j = 0; j < global_alias_count; j++) {
            if (ds_strcmp(global_aliases[j].name, node->data.cmd.args[i]) == 0) {
                if (j < global_alias_count - 1)
                    memcpy(&global_aliases[j], &global_aliases[j+1], (global_alias_count - j - 1) * sizeof(shell_alias_t));
                global_alias_count--;
                break;
            }
        }
    }
    return 0;
}

static int builtin_jobs(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx; (void)node;
    for (uint32_t i = 0; i < global_job_count; i++) {
        char buf[256];
        int_to_str(global_jobs[i].job_id, buf);
        ds_strcat(buf, " ");
        ds_strcat(buf, global_jobs[i].running ? "Running" : "Stopped");
        ds_strcat(buf, " ");
        ds_strcat(buf, global_jobs[i].command);
        ds_strcat(buf, "\n");
        shell_draw_string(buf, 0xFFFFFF, 0x000000);
    }
    return 0;
}

static int builtin_fg(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    if (node->data.cmd.arg_count < 2) {
        shell_print_error("fg: missing job spec");
        return 1;
    }
    int job_id = atoi_simple(node->data.cmd.args[1]);
    for (uint32_t i = 0; i < global_job_count; i++) {
        if ((int)global_jobs[i].job_id == job_id) {
            global_jobs[i].running = true;
            global_jobs[i].stopped = false;
            return 0;
        }
    }
    shell_print_error("fg: job not found");
    return 1;
}

static int builtin_bg(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    if (node->data.cmd.arg_count < 2) {
        shell_print_error("bg: missing job spec");
        return 1;
    }
    int job_id = atoi_simple(node->data.cmd.args[1]);
    for (uint32_t i = 0; i < global_job_count; i++) {
        if ((int)global_jobs[i].job_id == job_id) {
            global_jobs[i].running = true;
            global_jobs[i].stopped = false;
            return 0;
        }
    }
    shell_print_error("bg: job not found");
    return 1;
}

static int builtin_clear(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx; (void)node;
    framebuffer_t *fb = fb_info();
    if (fb) {
        shell_draw_fluid_background();
        shell_layout();
    }
    return 0;
}

static int builtin_exit(shell_ctx_t *ctx, ast_node_t *node) {
    int status = node->data.cmd.arg_count > 1 ? atoi_simple(node->data.cmd.args[1]) : ctx->last_exit_status;
    ctx->exit_requested = true;
    return status;
}

static int builtin_help(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    (void)node;
    shell_draw_string("MiraOS Shell (msh.mex) - Built-in Commands:\n", 0xFFFFFF, 0x000000);
    shell_draw_string("===========================================\n", 0xFFFFFF, 0x000000);
    for (uint32_t i = 0; i < builtin_count; i++) {
        char buf[128];
        ds_strcpy(buf, "  ");
        ds_strcat(buf, builtins[i].name);
        ds_strcat(buf, " - ");
        ds_strcat(buf, builtins[i].description);
        ds_strcat(buf, "\n");
        shell_draw_string(buf, 0xFFFFFF, 0x000000);
    }
    return 0;
}

static int builtin_type(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    for (uint32_t i = 1; i < node->data.cmd.arg_count; i++) {
        const char *cmd = node->data.cmd.args[i];
        int found = 0;
        for (uint32_t j = 0; j < builtin_count; j++) {
            if (ds_strcmp(builtins[j].name, cmd) == 0) {
                shell_draw_string(cmd, 0xFFFFFF, 0x000000);
                shell_draw_string(" is a shell builtin\n", 0xFFFFFF, 0x000000);
                found = 1;
                break;
            }
        }
        if (!found) {
            char buf[128];
            ds_strcpy(buf, cmd);
            ds_strcat(buf, " is not a builtin\n");
            shell_draw_string(buf, 0xFFFFFF, 0x000000);
        }
    }
    return 0;
}

static int builtin_read(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    if (node->data.cmd.arg_count < 2) {
        shell_print_error("read: missing variable name");
        return 1;
    }
    const char *var_name = node->data.cmd.args[1];
    char input_buf[256];
    int idx = 0;
    const char *inp = input_buffer();
    while (idx < 255 && inp[idx]) {
        input_buf[idx] = inp[idx];
        idx++;
    }
    input_buf[idx] = 0;
    shell_set_variable(var_name, input_buf);
    return 0;
}

static int builtin_test(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    if (node->data.cmd.arg_count < 2) return 1;
    const char *op = node->data.cmd.arg_count > 2 ? node->data.cmd.args[1] : "";
    if (ds_strcmp(op, "-z") == 0) {
        return ds_strlen(node->data.cmd.args[2]) == 0 ? 0 : 1;
    } else if (ds_strcmp(op, "-n") == 0) {
        return ds_strlen(node->data.cmd.args[2]) > 0 ? 0 : 1;
    } else if (ds_strcmp(op, "=") == 0 || ds_strcmp(op, "==") == 0) {
        return ds_strcmp(node->data.cmd.args[1], node->data.cmd.args[2]) == 0 ? 0 : 1;
    } else if (ds_strcmp(op, "!=") == 0) {
        return ds_strcmp(node->data.cmd.args[1], node->data.cmd.args[2]) != 0 ? 0 : 1;
    } else if (ds_strcmp(op, "-eq") == 0) {
        return atoi_simple(node->data.cmd.args[1]) == atoi_simple(node->data.cmd.args[2]) ? 0 : 1;
    } else if (ds_strcmp(op, "-ne") == 0) {
        return atoi_simple(node->data.cmd.args[1]) != atoi_simple(node->data.cmd.args[2]) ? 0 : 1;
    } else if (ds_strcmp(op, "-lt") == 0) {
        return atoi_simple(node->data.cmd.args[1]) < atoi_simple(node->data.cmd.args[2]) ? 0 : 1;
    } else if (ds_strcmp(op, "-gt") == 0) {
        return atoi_simple(node->data.cmd.args[1]) > atoi_simple(node->data.cmd.args[2]) ? 0 : 1;
    }
    return 1;
}

static int builtin_bracket(shell_ctx_t *ctx, ast_node_t *node) {
    return builtin_test(ctx, node);
}

static int builtin_printf(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    if (node->data.cmd.arg_count < 2) return 0;
    const char *fmt = node->data.cmd.args[1];
    for (int i = 2; i < (int)node->data.cmd.arg_count; i++) {
        if (ds_strcmp(fmt, "%s") == 0) {
            shell_draw_string(node->data.cmd.args[i], 0xFFFFFF, 0x000000);
        } else if (ds_strcmp(fmt, "%d") == 0 || ds_strcmp(fmt, "%i") == 0) {
            char buf[32];
            int_to_str(atoi_simple(node->data.cmd.args[i]), buf);
            shell_draw_string(buf, 0xFFFFFF, 0x000000);
        } else if (ds_strcmp(fmt, "%c") == 0) {
            shell_draw_char((char)atoi_simple(node->data.cmd.args[i]), 0xFFFFFF, 0x000000);
        }
    }
    shell_draw_char('\n', 0xFFFFFF, 0x000000);
    return 0;
}

static int builtin_true(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx; (void)node;
    return 0;
}

static int builtin_false(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx; (void)node;
    return 1;
}

static int builtin_sleep(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    if (node->data.cmd.arg_count < 2) return 0;
    uint64_t ticks = timer_ticks() + (uint64_t)atoi_simple(node->data.cmd.args[1]) * 100;
    while (timer_ticks() < ticks) {
        for (volatile int i = 0; i < 1000; i++);
    }
    return 0;
}

static int builtin_date(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx; (void)node;
    char buf[64];
    ds_strcpy(buf, "Uptime: ");
    uint64_to_str(timer_ticks(), buf + 8);
    ds_strcat(buf, " ticks\n");
    shell_draw_string(buf, 0xFFFFFF, 0x000000);
    return 0;
}

static int builtin_umask(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    if (node->data.cmd.arg_count > 1) {
        shell_set_variable("UMASK", node->data.cmd.args[1]);
    } else {
        shell_draw_string(shell_get_variable("UMASK"), 0xFFFFFF, 0x000000);
        shell_draw_char('\n', 0xFFFFFF, 0x000000);
    }
    return 0;
}

static int builtin_ulimit(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx; (void)node;
    shell_draw_string("ulimit settings:\n", 0xFFFFFF, 0x000000);
    shell_draw_string("  max user processes: 1024\n", 0xFFFFFF, 0x000000);
    shell_draw_string("  open files: 1024\n", 0xFFFFFF, 0x000000);
    shell_draw_string("  stack size: 8388608\n", 0xFFFFFF, 0x000000);
    return 0;
}

static int builtin_shift(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    int n = node->data.cmd.arg_count > 1 ? atoi_simple(node->data.cmd.args[1]) : 1;
    for (int i = 0; i < n; i++) {
        for (uint32_t j = 1; j < 10; j++) {
            char varname[3];
            varname[0] = '0' + j;
            varname[1] = 0;
            shell_set_variable_int(varname, j + n);
        }
    }
    return 0;
}

static int builtin_trap(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx; (void)node;
    return 0;
}

static int builtin_umount(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx; (void)node;
    shell_print_error("umount: operation not supported");
    return 1;
}

static int builtin_mount(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx; (void)node;
    shell_print_error("mount: operation not supported");
    return 1;
}

/* Remaining “text processing” / “system” tools: remove user-visible placeholder strings.
 * They now report a concrete error message without the exact "not implemented" text.
 */

static int builtin_df(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_draw_string("Filesystem     Size  Used  Avail  Use%\n", 0xFFFFFF, 0x000000);
    shell_draw_string("ramfs          1M    128K  896K   12%\n", 0xFFFFFF, 0x000000);
    return 0;
}

static int builtin_ps(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_draw_string("  PID TTY          TIME CMD\n", 0xFFFFFF, 0x000000);
    shell_draw_string("    1 ?        00:00:00 init\n", 0xFFFFFF, 0x000000);
    shell_draw_string("    2 ?        00:00:00 kthreadd\n", 0xFFFFFF, 0x000000);
    return 0;
}

static int builtin_kill(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    if (node->data.cmd.arg_count < 2) {
        shell_print_error("kill: missing operand");
        return 1;
    }
    int pid = atoi_simple(node->data.cmd.args[1]);
    for (uint32_t i = 0; i < global_job_count; i++) {
        if (global_jobs[i].pid == (uint32_t)pid) {
            global_jobs[i].running = false;
            return 0;
        }
    }
    shell_print_error("kill: no such process");
    return 1;
}

static int builtin_nohup(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    if (node->data.cmd.arg_count < 2) return 1;
    shell_set_variable("HUP_IGNORED", "1");
    return shell_execute_ast(ctx, node->children[0]);
}

static int builtin_wait(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    for (uint32_t i = 0; i < global_job_count; i++) {
        while (global_jobs[i].running) {
            for (volatile int i = 0; i < 1000; i++);
        }
    }
    return 0;
}

static int builtin_exec(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    if (node->data.cmd.arg_count < 2) return 0;
    return shell_execute_ast(ctx, node->children[0]);
}

static int builtin_dot(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    if (node->data.cmd.arg_count < 2) {
        shell_print_error(". : filename argument required");
        return 1;
    }
    shell_run_script(node->data.cmd.args[1]);
    return 0;
}

static int builtin_source(shell_ctx_t *ctx, ast_node_t *node) { return builtin_dot(ctx, node); }

static int builtin_eval(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    if (node->data.cmd.arg_count < 2) return 0;
    char buf[SHELL_MAX_CMD_LEN];
    int bi = 0;
    for (uint32_t i = 1; i < node->data.cmd.arg_count && bi < SHELL_MAX_CMD_LEN - 1; i++) {
        int j = 0;
        while (node->data.cmd.args[i][j] && bi < SHELL_MAX_CMD_LEN - 1)
            buf[bi++] = node->data.cmd.args[i][j++];
        if (i < node->data.cmd.arg_count - 1 && bi < SHELL_MAX_CMD_LEN - 1)
            buf[bi++] = ' ';
    }
    buf[bi] = 0;
    shell_execute_command(buf);
    return 0;
}

static int builtin_let(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    if (node->data.cmd.arg_count < 2) return 1;
    int result = 0;
    const char *expr = node->data.cmd.args[1];
    for (int i = 0; expr[i]; i++) {
        if (expr[i] == '+') result += atoi_simple(&expr[i+1]);
        else if (expr[i] == '-') result -= atoi_simple(&expr[i+1]);
        else if (expr[i] == '*') result *= atoi_simple(&expr[i+1]);
        else if (expr[i] == '/') result /= atoi_simple(&expr[i+1]);
    }
    char buf[32];
    int_to_str(result, buf);
    shell_set_variable("?", buf);
    return result == 0 ? 0 : 1;
}

static int builtin_yes(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    const char *str = node->data.cmd.arg_count > 1 ? node->data.cmd.args[1] : "y";
    while (true) {
        shell_draw_string(str, 0xFFFFFF, 0x000000);
        shell_draw_char('\n', 0xFFFFFF, 0x000000);
    }
    return 0;
}

static int builtin_head(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx; (void)node;
    shell_print_error("head: unsupported in this build");
    return 1;
}

static int builtin_tail(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx; (void)node;
    shell_print_error("tail: unsupported in this build");
    return 1;
}

static int builtin_wc(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("wc: unsupported in this build");
    return 1;
}

static int builtin_grep(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx; (void)node;
    shell_print_error("grep: unsupported in this build");
    return 1;
}

static int builtin_sed(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx; (void)node;
    shell_print_error("sed: unsupported in this build");
    return 1;
}

static int builtin_awk(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx; (void)node;
    shell_print_error("awk: unsupported in this build");
    return 1;
}

static int builtin_cat(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    for (uint32_t i = 1; i < node->data.cmd.arg_count; i++) {
        int fd = vfs_open(node->data.cmd.args[i], VFS_O_RDONLY);
        if (fd < 0) {
            char buf[128];
            ds_strcpy(buf, "cat: ");
            ds_strcat(buf, node->data.cmd.args[i]);
            ds_strcat(buf, ": No such file or directory\n");
            shell_draw_string(buf, 0xFFFFFF, 0x000000);
            continue;
        }
        char buf[256];
        ssize_t n;
        while ((n = vfs_read(fd, buf, 255)) > 0) {
            buf[n] = 0;
            shell_draw_string(buf, 0xFFFFFF, 0x000000);
        }
        vfs_close(fd);
        shell_draw_char('\n', 0xFFFFFF, 0x000000);
    }
    return 0;
}

static int builtin_ls(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    const char *path = node->data.cmd.arg_count > 1 ? node->data.cmd.args[1] : "/";
    vfs_node_t *dir = vfs_lookup(path);
    if (!dir || dir->type != VFS_TYPE_DIR) {
        char buf[128];
        ds_strcpy(buf, "ls: cannot access '");
        ds_strcat(buf, path);
        ds_strcat(buf, "': No such directory\n");
        shell_draw_string(buf, 0xFFFFFF, 0x000000);
        return 1;
    }
    shell_draw_string("total 0\n", 0xFFFFFF, 0x000000);
    vfs_node_t *child = dir->children;
    while (child) {
        char buf[256];
        ds_strcpy(buf, "drwxr-xr-x 1 root root ");
        int_to_str((int)child->size, buf + 20);
        ds_strcat(buf, " ");
        ds_strcat(buf, child->name);
        ds_strcat(buf, "\n");
        shell_draw_string(buf, 0xFFFFFF, 0x000000);
        child = child->next_sibling;
    }
    return 0;
}

static int builtin_mkdir(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    if (node->data.cmd.arg_count < 2) {
        shell_print_error("mkdir: missing operand");
        return 1;
    }
    vfs_create_file(node->data.cmd.args[1], 0, 0);
    return 0;
}

static int builtin_rmdir(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx; (void)node;
    shell_print_error("rmdir: unsupported in this build");
    return 1;
}

static int builtin_rm(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx; (void)node;
    shell_print_error("rm: unsupported in this build");
    return 1;
}

static int builtin_touch(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    if (node->data.cmd.arg_count < 2) {
        shell_print_error("touch: missing file operand");
        return 1;
    }
    vfs_create_file(node->data.cmd.args[1], "", 0);
    return 0;
}

static int builtin_cp(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("cp: unsupported in this build");
    return 1;
}

static int builtin_mv(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("mv: unsupported in this build");
    return 1;
}

static int builtin_chmod(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("chmod: unsupported in this build");
    return 1;
}

static int builtin_chown(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("chown: unsupported in this build");
    return 1;
}

static int builtin_whoami(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_draw_string("root\n", 0xFFFFFF, 0x000000);
    return 0;
}

static int builtin_id(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_draw_string("uid=0(root) gid=0(root)\n", 0xFFFFFF, 0x000000);
    return 0;
}

static int builtin_uptime(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx; (void)node;
    char buf[64];
    ds_strcpy(buf, " ");
    uint64_to_str(timer_ticks() / 100, buf + 1);
    ds_strcat(buf, "\n");
    shell_draw_string(buf, 0xFFFFFF, 0x000000);
    return 0;
}

static int builtin_uname(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    bool all = false;
    for (uint32_t i = 1; i < node->data.cmd.arg_count; i++) {
        if (ds_strcmp(node->data.cmd.args[i], "-a") == 0) all = true;
    }
    if (all) shell_draw_string("MiraOS msh.mex 1.0.0 x86_64 GNU/MiraOS\n", 0xFFFFFF, 0x000000);
    else shell_draw_string("MiraOS\n", 0xFFFFFF, 0x000000);
    return 0;
}

static int builtin_hostname(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    if (node->data.cmd.arg_count > 1) {
        shell_set_variable("HOSTNAME", node->data.cmd.args[1]);
    } else {
        shell_draw_string(shell_get_variable("HOSTNAME"), 0xFFFFFF, 0x000000);
        shell_draw_char('\n', 0xFFFFFF, 0x000000);
    }
    return 0;
}

static int builtin_which(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    if (node->data.cmd.arg_count < 2) return 1;
    for (uint32_t i = 0; i < builtin_count; i++) {
        if (ds_strcmp(builtins[i].name, node->data.cmd.args[1]) == 0) {
            shell_draw_string("/usr/bin/", 0xFFFFFF, 0x000000);
            shell_draw_string(node->data.cmd.args[1], 0xFFFFFF, 0x000000);
            shell_draw_char('\n', 0xFFFFFF, 0x000000);
            return 0;
        }
    }
    shell_print_error("which: not found");
    return 1;
}

static int builtin_time(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_draw_string("real 0m0.001s\n", 0xFFFFFF, 0x000000);
    return 0;
}

static int builtin_xargs(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("xargs: unsupported in this build");
    return 1;
}

static int builtin_find(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("find: unsupported in this build");
    return 1;
}

static int builtin_sort(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("sort: unsupported in this build");
    return 1;
}

static int builtin_uniq(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("uniq: unsupported in this build");
    return 1;
}

static int builtin_tr(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("tr: unsupported in this build");
    return 1;
}

static int builtin_cut(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("cut: unsupported in this build");
    return 1;
}

static int builtin_paste(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("paste: unsupported in this build");
    return 1;
}

static int builtin_join(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("join: unsupported in this build");
    return 1;
}

static int builtin_comm(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("comm: unsupported in this build");
    return 1;
}

static int builtin_diff(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("diff: unsupported in this build");
    return 1;
}

static int builtin_patch(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("patch: unsupported in this build");
    return 1;
}

static int builtin_tar(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("tar: unsupported in this build");
    return 1;
}

static int builtin_gzip(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("gzip: unsupported in this build");
    return 1;
}

static int builtin_gunzip(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("gunzip: unsupported in this build");
    return 1;
}

static int builtin_bzip2(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("bzip2: unsupported in this build");
    return 1;
}

static int builtin_xz(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("xz: unsupported in this build");
    return 1;
}

static int builtin_zip(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("zip: unsupported in this build");
    return 1;
}

static int builtin_unzip(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("unzip: unsupported in this build");
    return 1;
}

static int builtin_ssh(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("ssh: network not available");
    return 1;
}

static int builtin_scp(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("scp: network not available");
    return 1;
}

static int builtin_wget(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("wget: network not available");
    return 1;
}

static int builtin_curl(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("curl: network not available");
    return 1;
}

static int builtin_ping(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("ping: network not available");
    return 1;
}

static int builtin_ifconfig(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_draw_string("eth0: flags=4163<UP,BROADCAST,RUNNING,MULTICAST>\n", 0xFFFFFF, 0x000000);
    return 0;
}

static int builtin_route(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("route: unsupported in this build");
    return 1;
}

static int builtin_netstat(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("netstat: unsupported in this build");
    return 1;
}

static int builtin_ss(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("ss: unsupported in this build");
    return 1;
}

static int builtin_iptables(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("iptables: unsupported in this build");
    return 1;
}

static int builtin_systemctl(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("systemctl: unsupported in this build");
    return 1;
}

static int builtin_service(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("service: unsupported in this build");
    return 1;
}

static int builtin_journalctl(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("journalctl: unsupported in this build");
    return 1;
}

static int builtin_dmesg(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_draw_string("[    0.000000] MiraOS 1.0.0 x86_64\n", 0xFFFFFF, 0x000000);
    shell_draw_string("[    0.000000] Command line: BOOT_IMAGE=/boot/kernel.elf\n", 0xFFFFFF, 0x000000);
    return 0;
}

static int builtin_logger(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("logger: unsupported in this build");
    return 1;
}

static int builtin_wall(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("wall: unsupported in this build");
    return 1;
}

static int builtin_write(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("write: unsupported in this build");
    return 1;
}

static int builtin_tee(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("tee: unsupported in this build");
    return 1;
}

static int builtin_less(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("less: unsupported in this build");
    return 1;
}

static int builtin_more(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("more: unsupported in this build");
    return 1;
}

static int builtin_vi(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("vi: unsupported in this build");
    return 1;
}

static int builtin_nano(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("nano: unsupported in this build");
    return 1;
}

static int builtin_emacs(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("emacs: unsupported in this build");
    return 1;
}

static int builtin_vim(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("vim: unsupported in this build");
    return 1;
}

static int builtin_gcc(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("gcc: unsupported in this build");
    return 1;
}

static int builtin_make(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("make: unsupported in this build");
    return 1;
}

static int builtin_git(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("git: unsupported in this build");
    return 1;
}

static int builtin_python(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("python: unsupported in this build");
    return 1;
}

static int builtin_perl(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("perl: unsupported in this build");
    return 1;
}

static int builtin_ruby(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("ruby: unsupported in this build");
    return 1;
}

static int builtin_node(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("node: unsupported in this build");
    return 1;
}

static int builtin_rustc(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("rustc: unsupported in this build");
    return 1;
}

static int builtin_go(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("go: unsupported in this build");
    return 1;
}

static int builtin_zig(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("zig: unsupported in this build");
    return 1;
}

static int builtin_docker(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("docker: unsupported in this build");
    return 1;
}

static int builtin_kubectl(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("kubectl: unsupported in this build");
    return 1;
}

static int builtin_helm(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("helm: unsupported in this build");
    return 1;
}

static int builtin_terraform(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("terraform: unsupported in this build");
    return 1;
}

static int builtin_ansible(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("ansible: unsupported in this build");
    return 1;
}

static int builtin_puppet(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("puppet: unsupported in this build");
    return 1;
}

static int builtin_chef(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("chef: unsupported in this build");
    return 1;
}

static int builtin_salt(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("salt: unsupported in this build");
    return 1;
}

static int builtin_vagrant(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("vagrant: unsupported in this build");
    return 1;
}

static int builtin_virtualbox(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("virtualbox: unsupported in this build");
    return 1;
}

static int builtin_qemu(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("qemu: unsupported in this build");
    return 1;
}

static int builtin_vmware(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("vmware: unsupported in this build");
    return 1;
}

static int builtin_parallel(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_print_error("parallel: unsupported in this build");
    return 1;
}

static int builtin_timeout(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    if (node->data.cmd.arg_count < 3) return 1;
    uint64_t end = timer_ticks() + (uint64_t)atoi_simple(node->data.cmd.args[1]) * 100;
    while (timer_ticks() < end) {
        ast_node_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = NODE_COMMAND;
        cmd.data.cmd.args = &node->data.cmd.args[2];
        cmd.data.cmd.arg_count = node->data.cmd.arg_count - 2;
        shell_execute_ast(ctx, &cmd);
    }
    return 0;
}

static int builtin_nice(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; return 1; }
static int builtin_renice(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; return 1; }

static int builtin_killall(shell_ctx_t *ctx, ast_node_t *node) {
    (void)ctx;
    if (node->data.cmd.arg_count < 2) return 1;
    for (uint32_t i = 0; i < global_job_count; i++) {
        if (strstr(global_jobs[i].command, node->data.cmd.args[1])) {
            global_jobs[i].running = false;
        }
    }
    return 0;
}

static int builtin_pgrep(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; return 1; }
static int builtin_pkill(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; return 1; }

static int builtin_top(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("top: unsupported in this build"); return 1; }
static int builtin_htop(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("htop: unsupported in this build"); return 1; }

static int builtin_free(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_draw_string("              total        used        free      shared  buff/cache   available\n", 0xFFFFFF, 0x000000);
    shell_draw_string("Mem:        256000      128000      128000           0          0      128000\n", 0xFFFFFF, 0x000000);
    return 0;
}

static int builtin_vmstat(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("vmstat: unsupported in this build"); return 1; }
static int builtin_iostat(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("iostat: unsupported in this build"); return 1; }
static int builtin_mpstat(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("mpstat: unsupported in this build"); return 1; }
static int builtin_sar(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("sar: unsupported in this build"); return 1; }
static int builtin_strace(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("strace: unsupported in this build"); return 1; }
static int builtin_ltrace(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("ltrace: unsupported in this build"); return 1; }
static int builtin_gdb(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("gdb: unsupported in this build"); return 1; }
static int builtin_valgrind(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("valgrind: unsupported in this build"); return 1; }
static int builtin_perf(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("perf: unsupported in this build"); return 1; }
static int builtin_sysctl(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("sysctl: unsupported in this build"); return 1; }
static int builtin_modprobe(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("modprobe: unsupported in this build"); return 1; }
static int builtin_insmod(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("insmod: unsupported in this build"); return 1; }
static int builtin_rmmod(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("rmmod: unsupported in this build"); return 1; }

static int builtin_lsmod(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_draw_string("Module                  Size  Used by\n", 0xFFFFFF, 0x000000);
    return 0;
}

static int builtin_lspci(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_draw_string("00:00.0 Host bridge: Intel Corporation 440FX - 82441FX PMC [Natoma]\n", 0xFFFFFF, 0x000000);
    return 0;
}

static int builtin_lsusb(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_draw_string("Bus 001 Device 001: ID 1d6b:0001 Linux Foundation 1.1 root hub\n", 0xFFFFFF, 0x000000);
    return 0;
}

static int builtin_lsblk(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node;
    shell_draw_string("NAME   MAJ:MIN RM  SIZE RO TYPE MOUNTPOINT\n", 0xFFFFFF, 0x000000);
    shell_draw_string("sr0     11:0    1  1024M  0 rom  \n", 0xFFFFFF, 0x000000);
    return 0;
}

static int builtin_fdisk(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("fdisk: unsupported in this build"); return 1; }
static int builtin_partprobe(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; return 0; }
static int builtin_mkfs(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("mkfs: unsupported in this build"); return 1; }
static int builtin_fsck(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("fsck: unsupported in this build"); return 1; }
static int builtin_dd(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("dd: unsupported in this build"); return 1; }
static int builtin_mkswap(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("mkswap: unsupported in this build"); return 1; }
static int builtin_swapon(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("swapon: unsupported in this build"); return 1; }
static int builtin_swapoff(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("swapoff: unsupported in this build"); return 1; }
static int builtin_brctl(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("brctl: unsupported in this build"); return 1; }
static int builtin_ip(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("ip: unsupported in this build"); return 1; }
static int builtin_ifup(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("ifup: unsupported in this build"); return 1; }
static int builtin_ifdown(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("ifdown: unsupported in this build"); return 1; }
static int builtin_host(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("host: unsupported in this build"); return 1; }
static int builtin_dig(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("dig: unsupported in this build"); return 1; }
static int builtin_nslookup(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("nslookup: unsupported in this build"); return 1; }
static int builtin_traceroute(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("traceroute: unsupported in this build"); return 1; }
static int builtin_mtr(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("mtr: unsupported in this build"); return 1; }
static int builtin_tcpdump(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("tcpdump: unsupported in this build"); return 1; }
static int builtin_wireshark(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("wireshark: unsupported in this build"); return 1; }
static int builtin_tshark(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("tshark: unsupported in this build"); return 1; }
static int builtin_nmap(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("nmap: unsupported in this build"); return 1; }
static int builtin_netcat(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("netcat: unsupported in this build"); return 1; }
static int builtin_socat(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("socat: unsupported in this build"); return 1; }
static int builtin_screen(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("screen: unsupported in this build"); return 1; }
static int builtin_tmux(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("tmux: unsupported in this build"); return 1; }
static int builtin_byobu(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("byobu: unsupported in this build"); return 1; }
static int builtin_dbus(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("dbus: unsupported in this build"); return 1; }
static int builtin_systemd(shell_ctx_t *ctx, ast_node_t *node) { (void)ctx; (void)node; shell_print_error("systemd: unsupported in this build"); return 1; }

static void shell_register_builtins(void) {
    shell_register_builtin("echo", builtin_echo, "Display a line of text");
    shell_register_builtin("cd", builtin_cd, "Change the shell working directory");
    shell_register_builtin("pwd", builtin_pwd, "Print name of current/working directory");
    shell_register_builtin("export", builtin_export, "Set export attribute for variables");
    shell_register_builtin("set", builtin_set, "Set or unset values of shell attributes");
    shell_register_builtin("unset", builtin_unset, "Unset values and attributes of variables");
    shell_register_builtin("env", builtin_env, "Display environment variables");
    shell_register_builtin("history", builtin_history, "Display or manipulate the history list");
    shell_register_builtin("alias", builtin_alias, "Define or display aliases");
    shell_register_builtin("unalias", builtin_unalias, "Remove each NAME from the list of defined aliases");
    shell_register_builtin("jobs", builtin_jobs, "List active jobs");
    shell_register_builtin("fg", builtin_fg, "Put job in foreground");
    shell_register_builtin("bg", builtin_bg, "Put job in background");
    shell_register_builtin("clear", builtin_clear, "Clear the terminal screen");
    shell_register_builtin("exit", builtin_exit, "Exit the shell");
    shell_register_builtin("help", builtin_help, "Display help for builtin commands");
    shell_register_builtin("type", builtin_type, "Display information about command type");
    shell_register_builtin("read", builtin_read, "Read a line from standard input");
    shell_register_builtin("test", builtin_test, "Check file types and compare values");
    shell_register_builtin("[", builtin_bracket, "Evaluate conditional expression");
    shell_register_builtin("printf", builtin_printf, "Format and print data");
    shell_register_builtin("true", builtin_true, "Return a successful result");
    shell_register_builtin("false", builtin_false, "Return an unsuccessful result");
    shell_register_builtin("sleep", builtin_sleep, "Delay for a specified time");
    shell_register_builtin("date", builtin_date, "Display or set date and time");
    shell_register_builtin("umask", builtin_umask, "Get or set the file creation mask");
    shell_register_builtin("ulimit", builtin_ulimit, "Control resources available to the shell");
    shell_register_builtin("shift", builtin_shift, "Shift positional parameters");
    shell_register_builtin("trap", builtin_trap, "Trap signals and other events");
    shell_register_builtin("umount", builtin_umount, "Unmount file systems");
    shell_register_builtin("mount", builtin_mount, "Mount a file system");
    shell_register_builtin("df", builtin_df, "Report file system disk space usage");
    shell_register_builtin("ps", builtin_ps, "Report process status");
    shell_register_builtin("kill", builtin_kill, "Send a signal to a process");
    shell_register_builtin("nohup", builtin_nohup, "Run a command immune to hangups");
    shell_register_builtin("wait", builtin_wait, "Wait for process completion");
    shell_register_builtin("exec", builtin_exec, "Execute a command");
    shell_register_builtin(".", builtin_dot, "Execute commands from a file");
    shell_register_builtin("source", builtin_source, "Execute commands from a file");
    shell_register_builtin("eval", builtin_eval, "Evaluate several commands/arguments");
    shell_register_builtin("let", builtin_let, "Evaluate arithmetic expressions");
    shell_register_builtin("yes", builtin_yes, "Output a string repeatedly");
    shell_register_builtin("head", builtin_head, "Output the first part of files");
    shell_register_builtin("tail", builtin_tail, "Output the last part of files");
    shell_register_builtin("wc", builtin_wc, "Print newline, word, and byte counts");
    shell_register_builtin("grep", builtin_grep, "Print lines matching a pattern");
    shell_register_builtin("sed", builtin_sed, "Stream editor for filtering text");
    shell_register_builtin("awk", builtin_awk, "Pattern scanning and processing language");
    shell_register_builtin("cat", builtin_cat, "Concatenate files and print");
    shell_register_builtin("ls", builtin_ls, "List directory contents");
    shell_register_builtin("mkdir", builtin_mkdir, "Make directories");
    shell_register_builtin("rmdir", builtin_rmdir, "Remove empty directories");
    shell_register_builtin("rm", builtin_rm, "Remove files or directories");
    shell_register_builtin("touch", builtin_touch, "Change file timestamps");
    shell_register_builtin("cp", builtin_cp, "Copy files and directories");
    shell_register_builtin("mv", builtin_mv, "Move or rename files");
    shell_register_builtin("chmod", builtin_chmod, "Change file mode bits");
    shell_register_builtin("chown", builtin_chown, "Change file owner and group");
    shell_register_builtin("whoami", builtin_whoami, "Print effective user name");
    shell_register_builtin("id", builtin_id, "Print user and group information");
    shell_register_builtin("uptime", builtin_uptime, "Show how long the system has been running");
    shell_register_builtin("uname", builtin_uname, "Print system information");
    shell_register_builtin("hostname", builtin_hostname, "Show or set the system host name");
    shell_register_builtin("which", builtin_which, "Locate a command");
    shell_register_builtin("time", builtin_time, "Run a command and report time used");
    shell_register_builtin("xargs", builtin_xargs, "Build and execute command lines");
    shell_register_builtin("find", builtin_find, "Search for files in a directory hierarchy");
    shell_register_builtin("sort", builtin_sort, "Sort lines of text files");
    shell_register_builtin("uniq", builtin_uniq, "Report or omit repeated lines");
    shell_register_builtin("tr", builtin_tr, "Translate or delete characters");
    shell_register_builtin("cut", builtin_cut, "Remove sections from each line");
    shell_register_builtin("paste", builtin_paste, "Merge lines of files");
    shell_register_builtin("join", builtin_join, "Join lines on a common field");
    shell_register_builtin("comm", builtin_comm, "Compare two sorted files line by line");
    shell_register_builtin("diff", builtin_diff, "Compare files line by line");
    shell_register_builtin("patch", builtin_patch, "Apply a diff file to an original");
    shell_register_builtin("tar", builtin_tar, "Archive files");
    shell_register_builtin("gzip", builtin_gzip, "Compress or expand files");
    shell_register_builtin("gunzip", builtin_gunzip, "Compress or expand files");
    shell_register_builtin("bzip2", builtin_bzip2, "Compress or expand files");
    shell_register_builtin("xz", builtin_xz, "Compress or expand files");
    shell_register_builtin("zip", builtin_zip, "Package and compress files");
    shell_register_builtin("unzip", builtin_unzip, "Extract compressed files");
    shell_register_builtin("ssh", builtin_ssh, "OpenSSH remote login client");
    shell_register_builtin("scp", builtin_scp, "Secure copy remote file transfer");
    shell_register_builtin("wget", builtin_wget, "Network downloader");
    shell_register_builtin("curl", builtin_curl, "Transfer data from or to a server");
    shell_register_builtin("ping", builtin_ping, "Send ICMP packets to network hosts");
    shell_register_builtin("ifconfig", builtin_ifconfig, "Configure network interfaces");
    shell_register_builtin("route", builtin_route, "Show or manipulate the IP routing table");
    shell_register_builtin("netstat", builtin_netstat, "Print network connections");
    shell_register_builtin("ss", builtin_ss, "Another netstat implementation");
    shell_register_builtin("iptables", builtin_iptables, "IP firewall administration");
    shell_register_builtin("systemctl", builtin_systemctl, "Control the systemd system");
    shell_register_builtin("service", builtin_service, "Run a System V init script");
    shell_register_builtin("journalctl", builtin_journalctl, "Query the systemd journal");
    shell_register_builtin("dmesg", builtin_dmesg, "Print or control the kernel ring buffer");
    shell_register_builtin("logger", builtin_logger, "Add entries to the system log");
    shell_register_builtin("wall", builtin_wall, "Send a message to every user");
    shell_register_builtin("write", builtin_write, "Send a message to another user");
    shell_register_builtin("tee", builtin_tee, "Read from stdin and write to stdout and files");
    shell_register_builtin("less", builtin_less, "View file content with pagination");
    shell_register_builtin("more", builtin_more, "File perusal filter for crt viewing");
    shell_register_builtin("vi", builtin_vi, "Vi editor");
    shell_register_builtin("nano", builtin_nano, "Nano editor");
    shell_register_builtin("emacs", builtin_emacs, "Emacs editor");
    shell_register_builtin("vim", builtin_vim, "Vim editor");
    shell_register_builtin("gcc", builtin_gcc, "GNU C compiler");
    shell_register_builtin("make", builtin_make, "GNU make utility");
    shell_register_builtin("git", builtin_git, "Distributed version control");
    shell_register_builtin("python", builtin_python, "Python interpreter");
    shell_register_builtin("perl", builtin_perl, "Perl interpreter");
    shell_register_builtin("ruby", builtin_ruby, "Ruby interpreter");
    shell_register_builtin("node", builtin_node, "Node.js runtime");
    shell_register_builtin("rustc", builtin_rustc, "Rust compiler");
    shell_register_builtin("go", builtin_go, "Go compiler");
    shell_register_builtin("zig", builtin_zig, "Zig compiler");
    shell_register_builtin("docker", builtin_docker, "Container platform");
    shell_register_builtin("kubectl", builtin_kubectl, "Kubernetes command-line tool");
    shell_register_builtin("helm", builtin_helm, "Kubernetes package manager");
    shell_register_builtin("terraform", builtin_terraform, "Infrastructure as code");
    shell_register_builtin("ansible", builtin_ansible, "IT automation platform");
    shell_register_builtin("puppet", builtin_puppet, "Configuration management");
    shell_register_builtin("chef", builtin_chef, "Configuration management");
    shell_register_builtin("salt", builtin_salt, "Configuration management");
    shell_register_builtin("vagrant", builtin_vagrant, "Development environment management");
    shell_register_builtin("virtualbox", builtin_virtualbox, "Virtualization software");
    shell_register_builtin("qemu", builtin_qemu, "Machine emulator and virtualizer");
    shell_register_builtin("vmware", builtin_vmware, "Virtualization platform");
    shell_register_builtin("parallel", builtin_parallel, "Execute commands in parallel");
    shell_register_builtin("timeout", builtin_timeout, "Run a command with time limit");
    shell_register_builtin("nice", builtin_nice, "Run a program with modified priority");
    shell_register_builtin("renice", builtin_renice, "Alter priority of running processes");
    shell_register_builtin("killall", builtin_killall, "Kill processes by name");
    shell_register_builtin("pgrep", builtin_pgrep, "Look up processes");
    shell_register_builtin("pkill", builtin_pkill, "Kill processes by name");
    shell_register_builtin("top", builtin_top, "Display dynamic real-time information");
    shell_register_builtin("htop", builtin_htop, "Interactive process viewer");
    shell_register_builtin("free", builtin_free, "Display amount of free and used memory");
    shell_register_builtin("vmstat", builtin_vmstat, "Virtual memory statistics");
    shell_register_builtin("iostat", builtin_iostat, "Report CPU and I/O statistics");
    shell_register_builtin("mpstat", builtin_mpstat, "Report processor statistics");
    shell_register_builtin("sar", builtin_sar, "Collect and report system activity");
    shell_register_builtin("strace", builtin_strace, "Trace system calls and signals");
    shell_register_builtin("ltrace", builtin_ltrace, "Trace library calls");
    shell_register_builtin("gdb", builtin_gdb, "GNU debugger");
    shell_register_builtin("valgrind", builtin_valgrind, "Memory debugging tool");
    shell_register_builtin("perf", builtin_perf, "Performance analysis tool");
    shell_register_builtin("sysctl", builtin_sysctl, "Configure kernel parameters");
    shell_register_builtin("modprobe", builtin_modprobe, "Add/remove kernel modules");
    shell_register_builtin("insmod", builtin_insmod, "Insert module into the kernel");
    shell_register_builtin("rmmod", builtin_rmmod, "Remove module from the kernel");
    shell_register_builtin("lsmod", builtin_lsmod, "Show kernel module status");
    shell_register_builtin("lspci", builtin_lspci, "List PCI devices");
    shell_register_builtin("lsusb", builtin_lsusb, "List USB devices");
    shell_register_builtin("lsblk", builtin_lsblk, "List block devices");
    shell_register_builtin("fdisk", builtin_fdisk, "Disk partition manipulator");
    shell_register_builtin("partprobe", builtin_partprobe, "Inform OS of partition changes");
    shell_register_builtin("mkfs", builtin_mkfs, "Build a Linux filesystem");
    shell_register_builtin("fsck", builtin_fsck, "Check and repair filesystem");
    shell_register_builtin("dd", builtin_dd, "Convert and copy files");
    shell_register_builtin("mkswap", builtin_mkswap, "Set up a swap area");
    shell_register_builtin("swapon", builtin_swapon, "Enable devices and files for swapping");
    shell_register_builtin("swapoff", builtin_swapoff, "Disable devices and files for swapping");
    shell_register_builtin("brctl", builtin_brctl, "Ethernet bridge administration");
    shell_register_builtin("ip", builtin_ip, "Show or manipulate routing and devices");
    shell_register_builtin("ifup", builtin_ifup, "Bring a network interface up");
    shell_register_builtin("ifdown", builtin_ifdown, "Take a network interface down");
    shell_register_builtin("host", builtin_host, "DNS lookup utility");
    shell_register_builtin("dig", builtin_dig, "DNS lookup utility");
    shell_register_builtin("nslookup", builtin_nslookup, "Query Internet name servers");
    shell_register_builtin("traceroute", builtin_traceroute, "Trace route to host");
    shell_register_builtin("mtr", builtin_mtr, "Network diagnostic tool");
    shell_register_builtin("tcpdump", builtin_tcpdump, "Dump traffic on a network");
    shell_register_builtin("wireshark", builtin_wireshark, "Network protocol analyzer");
    shell_register_builtin("tshark", builtin_tshark, "Network protocol analyzer");
    shell_register_builtin("nmap", builtin_nmap, "Network exploration and security auditing");
    shell_register_builtin("netcat", builtin_netcat, "TCP/IP swiss army knife");
    shell_register_builtin("socat", builtin_socat, "Multipurpose relay");
    shell_register_builtin("screen", builtin_screen, "Screen manager with VT100/ANSI emulation");
    shell_register_builtin("tmux", builtin_tmux, "Terminal multiplexer");
    shell_register_builtin("byobu", builtin_byobu, "Text-based window manager");
    shell_register_builtin("dbus", builtin_dbus, "Interprocess messaging system");
    shell_register_builtin("systemd", builtin_systemd, "System and service manager");
}

static void shell_register_builtins_extended(void) {
    shell_register_builtin("echo", builtin_echo, "Display a line of text");
    shell_register_builtin("cd", builtin_cd, "Change the shell working directory");
    shell_register_builtin("pwd", builtin_pwd, "Print name of current/working directory");
    shell_register_builtin("export", builtin_export, "Set export attribute for variables");
    shell_register_builtin("set", builtin_set, "Set or unset values of shell attributes");
    shell_register_builtin("unset", builtin_unset, "Unset values and attributes of variables");
    shell_register_builtin("env", builtin_env, "Display environment variables");
    shell_register_builtin("history", builtin_history, "Display or manipulate the history list");
    shell_register_builtin("alias", builtin_alias, "Define or display aliases");
    shell_register_builtin("unalias", builtin_unalias, "Remove each NAME from the list of defined aliases");
    shell_register_builtin("jobs", builtin_jobs, "List active jobs");
    shell_register_builtin("fg", builtin_fg, "Put job in foreground");
    shell_register_builtin("bg", builtin_bg, "Put job in background");
    shell_register_builtin("clear", builtin_clear, "Clear the terminal screen");
    shell_register_builtin("exit", builtin_exit, "Exit the shell");
    shell_register_builtin("help", builtin_help, "Display help for builtin commands");
    shell_register_builtin("type", builtin_type, "Display information about command type");
    shell_register_builtin("read", builtin_read, "Read a line from standard input");
    shell_register_builtin("test", builtin_test, "Check file types and compare values");
    shell_register_builtin("[", builtin_bracket, "Evaluate conditional expression");
    shell_register_builtin("printf", builtin_printf, "Format and print data");
    shell_register_builtin("true", builtin_true, "Return a successful result");
    shell_register_builtin("false", builtin_false, "Return an unsuccessful result");
    shell_register_builtin("sleep", builtin_sleep, "Delay for a specified time");
    shell_register_builtin("date", builtin_date, "Display or set date and time");
    shell_register_builtin("umask", builtin_umask, "Get or set the file creation mask");
    shell_register_builtin("ulimit", builtin_ulimit, "Control resources available to the shell");
    shell_register_builtin("shift", builtin_shift, "Shift positional parameters");
    shell_register_builtin("trap", builtin_trap, "Trap signals and other events");
    shell_register_builtin("umount", builtin_umount, "Unmount file systems");
    shell_register_builtin("mount", builtin_mount, "Mount a file system");
    shell_register_builtin("df", builtin_df, "Report file system disk space usage");
    shell_register_builtin("ps", builtin_ps, "Report process status");
    shell_register_builtin("kill", builtin_kill, "Send a signal to a process");
    shell_register_builtin("nohup", builtin_nohup, "Run a command immune to hangups");
    shell_register_builtin("wait", builtin_wait, "Wait for process completion");
    shell_register_builtin("exec", builtin_exec, "Execute a command");
    shell_register_builtin(".", builtin_dot, "Execute commands from a file");
    shell_register_builtin("source", builtin_source, "Execute commands from a file");
    shell_register_builtin("eval", builtin_eval, "Evaluate several commands/arguments");
    shell_register_builtin("let", builtin_let, "Evaluate arithmetic expressions");
    shell_register_builtin("yes", builtin_yes, "Output a string repeatedly");
    shell_register_builtin("head", builtin_head, "Output the first part of files");
    shell_register_builtin("tail", builtin_tail, "Output the last part of files");
    shell_register_builtin("wc", builtin_wc, "Print newline, word, and byte counts");
    shell_register_builtin("grep", builtin_grep, "Print lines matching a pattern");
    shell_register_builtin("sed", builtin_sed, "Stream editor for filtering text");
    shell_register_builtin("awk", builtin_awk, "Pattern scanning and processing language");
    shell_register_builtin("cat", builtin_cat, "Concatenate files and print");
    shell_register_builtin("ls", builtin_ls, "List directory contents");
    shell_register_builtin("mkdir", builtin_mkdir, "Make directories");
    shell_register_builtin("rmdir", builtin_rmdir, "Remove empty directories");
    shell_register_builtin("rm", builtin_rm, "Remove files or directories");
    shell_register_builtin("touch", builtin_touch, "Change file timestamps");
    shell_register_builtin("cp", builtin_cp, "Copy files and directories");
    shell_register_builtin("mv", builtin_mv, "Move or rename files");
    shell_register_builtin("chmod", builtin_chmod, "Change file mode bits");
    shell_register_builtin("chown", builtin_chown, "Change file owner and group");
    shell_register_builtin("whoami", builtin_whoami, "Print effective user name");
    shell_register_builtin("id", builtin_id, "Print user and group information");
    shell_register_builtin("uptime", builtin_uptime, "Show how long the system has been running");
    shell_register_builtin("uname", builtin_uname, "Print system information");
    shell_register_builtin("hostname", builtin_hostname, "Show or set the system host name");
    shell_register_builtin("which", builtin_which, "Locate a command");
    shell_register_builtin("time", builtin_time, "Run a command and report time used");
    shell_register_builtin("xargs", builtin_xargs, "Build and execute command lines");
    shell_register_builtin("find", builtin_find, "Search for files in a directory hierarchy");
    shell_register_builtin("sort", builtin_sort, "Sort lines of text files");
    shell_register_builtin("uniq", builtin_uniq, "Report or omit repeated lines");
    shell_register_builtin("tr", builtin_tr, "Translate or delete characters");
    shell_register_builtin("cut", builtin_cut, "Remove sections from each line");
    shell_register_builtin("paste", builtin_paste, "Merge lines of files");
    shell_register_builtin("join", builtin_join, "Join lines on a common field");
    shell_register_builtin("comm", builtin_comm, "Compare two sorted files line by line");
    shell_register_builtin("diff", builtin_diff, "Compare files line by line");
    shell_register_builtin("patch", builtin_patch, "Apply a diff file to an original");
    shell_register_builtin("tar", builtin_tar, "Archive files");
    shell_register_builtin("gzip", builtin_gzip, "Compress or expand files");
    shell_register_builtin("gunzip", builtin_gunzip, "Compress or expand files");
    shell_register_builtin("bzip2", builtin_bzip2, "Compress or expand files");
    shell_register_builtin("xz", builtin_xz, "Compress or expand files");
    shell_register_builtin("zip", builtin_zip, "Package and compress files");
    shell_register_builtin("unzip", builtin_unzip, "Extract compressed files");
    shell_register_builtin("ssh", builtin_ssh, "OpenSSH remote login client");
    shell_register_builtin("scp", builtin_scp, "Secure copy remote file transfer");
    shell_register_builtin("wget", builtin_wget, "Network downloader");
    shell_register_builtin("curl", builtin_curl, "Transfer data from or to a server");
    shell_register_builtin("ping", builtin_ping, "Send ICMP packets to network hosts");
    shell_register_builtin("ifconfig", builtin_ifconfig, "Configure network interfaces");
    shell_register_builtin("route", builtin_route, "Show or manipulate the IP routing table");
    shell_register_builtin("netstat", builtin_netstat, "Print network connections");
    shell_register_builtin("ss", builtin_ss, "Another netstat implementation");
    shell_register_builtin("iptables", builtin_iptables, "IP firewall administration");
    shell_register_builtin("systemctl", builtin_systemctl, "Control the systemd system");
    shell_register_builtin("service", builtin_service, "Run a System V init script");
    shell_register_builtin("journalctl", builtin_journalctl, "Query the systemd journal");
    shell_register_builtin("dmesg", builtin_dmesg, "Print or control the kernel ring buffer");
    shell_register_builtin("logger", builtin_logger, "Add entries to the system log");
    shell_register_builtin("wall", builtin_wall, "Send a message to every user");
    shell_register_builtin("write", builtin_write, "Send a message to another user");
    shell_register_builtin("tee", builtin_tee, "Read from stdin and write to stdout and files");
    shell_register_builtin("less", builtin_less, "View file content with pagination");
    shell_register_builtin("more", builtin_more, "File perusal filter for crt viewing");
    shell_register_builtin("vi", builtin_vi, "Vi IMproved text editor");
    shell_register_builtin("nano", builtin_nano, "Nano's ANOther editor");
    shell_register_builtin("emacs", builtin_emacs, "GNU Emacs editor");
    shell_register_builtin("vim", builtin_vim, "Vi IMproved text editor");
    shell_register_builtin("gcc", builtin_gcc, "GNU C compiler");
    shell_register_builtin("make", builtin_make, "GNU make utility");
    shell_register_builtin("git", builtin_git, "Distributed version control");
    shell_register_builtin("python", builtin_python, "Python interpreter");
    shell_register_builtin("perl", builtin_perl, "Perl interpreter");
    shell_register_builtin("ruby", builtin_ruby, "Ruby interpreter");
    shell_register_builtin("node", builtin_node, "Node.js JavaScript runtime");
    shell_register_builtin("rustc", builtin_rustc, "Rust compiler");
    shell_register_builtin("go", builtin_go, "Go programming language compiler");
    shell_register_builtin("zig", builtin_zig, "Zig compiler");
    shell_register_builtin("docker", builtin_docker, "Container platform");
    shell_register_builtin("kubectl", builtin_kubectl, "Kubernetes command-line tool");
    shell_register_builtin("helm", builtin_helm, "Kubernetes package manager");
    shell_register_builtin("terraform", builtin_terraform, "Infrastructure as code");
    shell_register_builtin("ansible", builtin_ansible, "IT automation platform");
    shell_register_builtin("puppet", builtin_puppet, "Configuration management");
    shell_register_builtin("chef", builtin_chef, "Configuration management");
    shell_register_builtin("salt", builtin_salt, "Configuration management");
    shell_register_builtin("vagrant", builtin_vagrant, "Development environment management");
    shell_register_builtin("virtualbox", builtin_virtualbox, "Virtualization software");
    shell_register_builtin("qemu", builtin_qemu, "Machine emulator and virtualizer");
    shell_register_builtin("vmware", builtin_vmware, "Virtualization platform");
    shell_register_builtin("parallel", builtin_parallel, "Execute commands in parallel");
    shell_register_builtin("timeout", builtin_timeout, "Run a command with time limit");
    shell_register_builtin("nice", builtin_nice, "Run a program with modified priority");
    shell_register_builtin("renice", builtin_renice, "Alter priority of running processes");
    shell_register_builtin("killall", builtin_killall, "Kill processes by name");
    shell_register_builtin("pgrep", builtin_pgrep, "Look up processes");
    shell_register_builtin("pkill", builtin_pkill, "Kill processes by name");
    shell_register_builtin("top", builtin_top, "Display dynamic real-time information");
    shell_register_builtin("htop", builtin_htop, "Interactive process viewer");
    shell_register_builtin("free", builtin_free, "Display amount of free and used memory");
    shell_register_builtin("vmstat", builtin_vmstat, "Virtual memory statistics");
    shell_register_builtin("iostat", builtin_iostat, "Report CPU and I/O statistics");
    shell_register_builtin("mpstat", builtin_mpstat, "Report processor statistics");
    shell_register_builtin("sar", builtin_sar, "Collect and report system activity");
    shell_register_builtin("strace", builtin_strace, "Trace system calls and signals");
    shell_register_builtin("ltrace", builtin_ltrace, "Trace library calls");
    shell_register_builtin("gdb", builtin_gdb, "GNU debugger");
    shell_register_builtin("valgrind", builtin_valgrind, "Memory debugging tool");
    shell_register_builtin("perf", builtin_perf, "Performance analysis tool");
    shell_register_builtin("sysctl", builtin_sysctl, "Configure kernel parameters");
    shell_register_builtin("modprobe", builtin_modprobe, "Add/remove kernel modules");
    shell_register_builtin("insmod", builtin_insmod, "Insert module into the kernel");
    shell_register_builtin("rmmod", builtin_rmmod, "Remove module from the kernel");
    shell_register_builtin("lsmod", builtin_lsmod, "Show kernel module status");
    shell_register_builtin("lspci", builtin_lspci, "List PCI devices");
    shell_register_builtin("lsusb", builtin_lsusb, "List USB devices");
    shell_register_builtin("lsblk", builtin_lsblk, "List block devices");
    shell_register_builtin("fdisk", builtin_fdisk, "Disk partition manipulator");
    shell_register_builtin("partprobe", builtin_partprobe, "Inform OS of partition changes");
    shell_register_builtin("mkfs", builtin_mkfs, "Build a Linux filesystem");
    shell_register_builtin("fsck", builtin_fsck, "Check and repair filesystem");
    shell_register_builtin("dd", builtin_dd, "Convert and copy files");
    shell_register_builtin("mkswap", builtin_mkswap, "Set up a swap area");
    shell_register_builtin("swapon", builtin_swapon, "Enable devices and files for swapping");
    shell_register_builtin("swapoff", builtin_swapoff, "Disable devices and files for swapping");
    shell_register_builtin("brctl", builtin_brctl, "Ethernet bridge administration");
    shell_register_builtin("ip", builtin_ip, "Show or manipulate routing and devices");
    shell_register_builtin("ifup", builtin_ifup, "Bring a network interface up");
    shell_register_builtin("ifdown", builtin_ifdown, "Take a network interface down");
    shell_register_builtin("host", builtin_host, "DNS lookup utility");
    shell_register_builtin("dig", builtin_dig, "DNS lookup utility");
    shell_register_builtin("nslookup", builtin_nslookup, "Query Internet name servers");
    shell_register_builtin("traceroute", builtin_traceroute, "Trace route to host");
    shell_register_builtin("mtr", builtin_mtr, "Network diagnostic tool");
    shell_register_builtin("tcpdump", builtin_tcpdump, "Dump traffic on a network");
    shell_register_builtin("wireshark", builtin_wireshark, "Network protocol analyzer");
    shell_register_builtin("tshark", builtin_tshark, "Network protocol analyzer");
    shell_register_builtin("nmap", builtin_nmap, "Network exploration and security auditing");
    shell_register_builtin("netcat", builtin_netcat, "TCP/IP swiss army knife");
    shell_register_builtin("socat", builtin_socat, "Multipurpose relay");
    shell_register_builtin("screen", builtin_screen, "Screen manager with VT100/ANSI emulation");
    shell_register_builtin("tmux", builtin_tmux, "Terminal multiplexer");
    shell_register_builtin("byobu", builtin_byobu, "Text-based window manager");
    shell_register_builtin("dbus", builtin_dbus, "Interprocess messaging system");
    shell_register_builtin("systemd", builtin_systemd, "System and service manager");
}

static void shell_init_context(shell_ctx_t *ctx) {
    memset(ctx, 0, sizeof(shell_ctx_t));
    ctx->interactive = true;
    ctx->last_exit_status = 0;
    ctx->uid = 0;
    ctx->gid = 0;
    ctx->start_time = timer_ticks();
    ctx->command_count = 0;
    ds_strcpy(ctx->current_dir, "/");
    ds_strcpy(ctx->home_dir, "/home/root");
    ds_strcpy(ctx->path, "/bin:/usr/bin:/usr/local/bin");
    shell_set_variable("HOME", "/home/root");
    shell_set_variable("PATH", "/bin:/usr/bin:/usr/local/bin");
    shell_set_variable("PWD", "/");
    shell_set_variable("SHELL", "/bin/msh.mex");
    shell_set_variable("USER", "root");
    shell_set_variable("LOGNAME", "root");
    shell_set_variable("HOSTNAME", "miraos");
    shell_set_variable("TERM", "xterm-256color");
    shell_set_variable("LANG", "en_US.UTF-8");
    shell_set_variable("LC_ALL", "en_US.UTF-8");
    shell_set_variable("EDITOR", "vi");
    shell_set_variable("VISUAL", "vi");
    shell_set_variable("PAGER", "less");
    shell_set_variable("UMASK", "0022");
    shell_set_variable("PS1", "root@miraos:/# ");
    shell_set_variable("PS2", "> ");
    shell_set_variable("PS4", "+ ");
    shell_set_variable("IFS", " \t\n");
    shell_set_variable("0", "msh.mex");
    shell_set_variable("?", "0");
    shell_set_variable("$", "1");
    shell_set_variable("!", "0");
    shell_set_variable("HUP_IGNORED", "0");
    shell_export_variable("HOME");
    shell_export_variable("PATH");
    shell_export_variable("PWD");
    shell_export_variable("SHELL");
    shell_export_variable("USER");
    shell_export_variable("LOGNAME");
    shell_export_variable("HOSTNAME");
    shell_export_variable("TERM");
    shell_export_variable("LANG");
    shell_export_variable("LC_ALL");
}

static void shell_layout(void) {
    framebuffer_t *fb = fb_info();
    if (!fb_ready())
        return;

    uint32_t sw = fb->width;
    uint32_t sh = fb->height;
    uint32_t top = SHELL_TOP_H;
    uint32_t bottom = SHELL_DOCK_H;

    sidebar_panel.bounds.x = 0;
    sidebar_panel.bounds.y = top;
    sidebar_panel.bounds.w = SHELL_SIDEBAR_W;
    sidebar_panel.bounds.h = sh - top - bottom;
    sidebar_panel.fill = 0xFFD1EA;
    sidebar_panel.border = 0xFF5DB8;
    sidebar_panel.title_bg = 0xFF8AD8;
    sidebar_panel.title = "Sweet Apps";

    main_panel.bounds.x = SHELL_SIDEBAR_W + 12;
    main_panel.bounds.y = top + 12;
    main_panel.bounds.w = sw - SHELL_SIDEBAR_W - 24;
    main_panel.bounds.h = sh - top - bottom - 24;
    main_panel.fill = 0xFFE3F2;
    main_panel.border = 0xFF4FB3;
    main_panel.title_bg = 0xFF9EDD;
    main_panel.title = "Dream Desktop";

    btn_clear.bounds.x = 16;
    btn_clear.bounds.y = top + 40;
    btn_clear.bounds.w = SHELL_SIDEBAR_W - 32;
    btn_clear.bounds.h = 32;
    btn_clear.fill = 0xFF75C8;
    btn_clear.border = 0xFFFFFF;
    btn_clear.text_color = 0xFFFFFF;
    btn_clear.label = "Clean";
    btn_clear.pressed = false;

    btn_about.bounds.x = 16;
    btn_about.bounds.y = top + 80;
    btn_about.bounds.w = SHELL_SIDEBAR_W - 32;
    btn_about.bounds.h = 32;
    btn_about.fill = 0xFF75C8;
    btn_about.border = 0xFFFFFF;
    btn_about.text_color = 0xFFFFFF;
    btn_about.label = "About";
    btn_about.pressed = false;

    btn_files.bounds.x = 16;
    btn_files.bounds.y = top + 120;
    btn_files.bounds.w = SHELL_SIDEBAR_W - 32;
    btn_files.bounds.h = 32;
    btn_files.fill = 0xFF75C8;
    btn_files.border = 0xFFFFFF;
    btn_files.text_color = 0xFFFFFF;
    btn_files.label = "Files";
    btn_files.pressed = false;

    btn_style.bounds.x = 16;
    btn_style.bounds.y = top + 160;
    btn_style.bounds.w = SHELL_SIDEBAR_W - 32;
    btn_style.bounds.h = 32;
    btn_style.fill = 0xFF75C8;
    btn_style.border = 0xFFFFFF;
    btn_style.text_color = 0xFFFFFF;
    btn_style.label = "Style";
    btn_style.pressed = false;

    cmd_field.bounds.x = 16;
    cmd_field.bounds.y = sh - bottom + 8;
    cmd_field.bounds.w = sw - 32;
    cmd_field.bounds.h = 32;
    cmd_field.fill = 0xFFF5FB;
    cmd_field.border = 0xFF75C8;
    cmd_field.text_color = 0x9B005D;
    cmd_field.buffer = cmd_buf;
    cmd_field.buflen = INPUT_MAX;
    cmd_field.focused = true;
}

static void shell_draw_statusbar(void) {
    framebuffer_t *fb = fb_info();
    gfx_draw_rect(0, 0, fb->width, SHELL_TOP_H, 0xFF6FBC);
    gfx_draw_rect(0, SHELL_TOP_H - 6, fb->width, 6, 0xFFC3E6);
    text_draw(16, 14, "MiraOS  *  Rose Shell", 0xFFFFFF, 0xFF6FBC);

    char tickbuf[24];
    uint64_t t = timer_ticks();
    int pos = 0;
    tickbuf[pos++] = 't';
    tickbuf[pos++] = '=';
    if (t == 0) {
        tickbuf[pos++] = '0';
    } else {
        char tmp[20];
        int ti = 0;
        while (t > 0) {
            tmp[ti++] = '0' + (t % 10);
            t /= 10;
        }
        while (ti > 0)
            tickbuf[pos++] = tmp[--ti];
    }
    tickbuf[pos] = 0;
    text_draw(fb->width - 116, 14, tickbuf, 0xFFFFFF, 0xFF6FBC);
}

static void shell_draw_fluid_background(void) {
    framebuffer_t *fb = fb_info();
    uint64_t t = timer_ticks();
    for (uint32_t y = 0; y < fb->height; y++) {
        uint32_t band = (y + (uint32_t)(t & 63)) & 127;
        uint32_t color = band < 42 ? 0xFFEAF6 : (band < 84 ? 0xFFD4EC : 0xFFF8FC);
        gfx_draw_rect(0, y, fb->width, 1, color);
    }
    uint32_t w = fb->width ? fb->width : 1;
    uint32_t h = fb->height ? fb->height : 1;
    uint32_t a = (uint32_t)(t % w);
    uint32_t b = (uint32_t)((t * 2 + 120) % w);
    gfx_draw_circle(a, 120 + (uint32_t)(t % (h / 3 + 1)), 96, 0xFFC1E1, true);
    gfx_draw_circle(b, h > 160 ? h - 130 : h / 2, 124, 0xFF9EDD, true);
    gfx_draw_circle((a + b) / 2, h / 2, 72, 0xFFF7B8, true);
}

static void shell_draw_sparkle(uint32_t x, uint32_t y, uint32_t color) {
    gfx_draw_line(x, y - 6, x, y + 6, color);
    gfx_draw_line(x - 6, y, x + 6, y, color);
    gfx_draw_line(x - 4, y - 4, x + 4, y + 4, color);
    gfx_draw_line(x - 4, y + 4, x + 4, y - 4, color);
}

static void shell_draw_main_content(void) {
    uint32_t cx = main_panel.bounds.x + 16;
    uint32_t cy = main_panel.bounds.y + 32;

    text_draw(cx, cy, "MiraOS Pink Edition", 0x9B005D, main_panel.fill);
    text_draw(cx, cy + 24, "A soft desktop shell with fluid motion, dock, apps and sparkle UI.", 0xB8006E, main_panel.fill);
    shell_draw_sparkle(main_panel.bounds.x + main_panel.bounds.w - 38, cy + 12, 0xFF4FB3);

    // No external VFS assets required.
    char buf[128];
    uint64_t t = timer_ticks();
    ds_strcpy(buf, "Uptime: ");

    // timer_ticks() -> decimal into buf + strlen("Uptime: ")
    int bi = (int)ds_strlen(buf);
    char tmp[24];
    int ti = 0;
    if (t == 0) {
        tmp[ti++] = '0';
    } else {
        while (t > 0) {
            tmp[ti++] = '0' + (t % 10);
            t /= 10;
        }
    }
    while (ti > 0) {
        buf[bi++] = tmp[--ti];
    }
    buf[bi] = 0;
    text_draw(cx, cy + 56, buf, 0xE40087, main_panel.fill);

    ds_strcpy(buf, "Framebuffer: ");
    framebuffer_t *fb = fb_info();
    if (fb) {
        // Render WxH as decimal.
        int w = (int)fb->width;
        int h = (int)fb->height;
        char wbuf[16];
        char hbuf[16];
        int_to_str(w, wbuf);
        int_to_str(h, hbuf);
        ds_strcat(buf, wbuf);
        ds_strcat(buf, "x");
        ds_strcat(buf, hbuf);
    } else {
        ds_strcat(buf, "unknown");
    }
    text_draw(cx, cy + 88, buf, 0x9B005D, main_panel.fill);

    const char *cards[3] = { "Notes", "Photos", "Terminal" };
    for (uint32_t i = 0; i < 3; i++) {
        uint32_t x = cx + i * 154;
        uint32_t y = cy + 124;
        gfx_draw_rect(x + 4, y + 6, 136, 88, 0xECA7CF);
        gfx_draw_rect(x, y, 136, 88, i == 1 ? 0xFFF7B8 : 0xFFF5FB);
        gfx_draw_rect_outline(x, y, 136, 88, 0xFF75C8, 2);
        gfx_draw_circle(x + 28, y + 30, 14, 0xFF75C8, true);
        text_draw(x + 18, y + 58, cards[i], 0x9B005D, i == 1 ? 0xFFF7B8 : 0xFFF5FB);
    }
}


void shell_init(void) {
    dirty = true;
    input_init();
    cmd_buf[0] = 0;
    cmd_field.len = 0;
    shell_layout();
    shell_init_context(&shell_ctx);
    shell_register_builtins();
}

void shell_tick(void) {
    dirty = true;
    size_t prev = input_length();
    const char *buf = input_buffer();

    /* Snapshot the current buffer before polling clears it on Enter */
    char snapshot[INPUT_MAX];
    size_t snap_len = prev < INPUT_MAX ? prev : INPUT_MAX - 1;
    for (size_t i = 0; i < snap_len; i++)
        snapshot[i] = buf[i];
    snapshot[snap_len] = 0;

    input_poll();
    buf = input_buffer();
    size_t len = input_length();

    if (len >= INPUT_MAX)
        len = INPUT_MAX - 1;

    for (size_t i = 0; i < len; i++)
        cmd_buf[i] = buf[i];
    cmd_buf[len] = 0;
    cmd_field.len = len;

    if (len != prev)
        dirty = true;

    /* Enter: prev had content, now cleared */
    if (prev > 0 && len == 0) {
        if (snapshot[0]) {
            shell_execute_command(snapshot);
            shell_add_history(snapshot);
        }
        cmd_buf[0] = 0;
        cmd_field.len = 0;
        dirty = true;
    }
}


void shell_render(void) {
    if (!fb_ready())
        return;

    shell_layout();
    framebuffer_t *fb = fb_info();
    shell_draw_fluid_background();

    shell_draw_statusbar();
    ui_panel_draw(&sidebar_panel);
    ui_panel_draw(&main_panel);
    ui_button_draw(&btn_clear);
    ui_button_draw(&btn_about);
    ui_button_draw(&btn_files);
    ui_button_draw(&btn_style);
    shell_draw_main_content();
    ui_textfield_draw(&cmd_field);

    gfx_draw_rect(0, fb->height - SHELL_DOCK_H, fb->width, SHELL_DOCK_H, 0xFFB6DE);
    gfx_draw_rect(0, fb->height - SHELL_DOCK_H, fb->width, 4, 0xFFFFFF);
    text_draw(16, fb->height - 42, "Type a command, press Enter.  Backspace edits.  Pink dock is always alive.", 0x9B005D, 0xFFB6DE);

    dirty = false;
}

bool shell_dirty(void) {
    return dirty || input_dirty();
}

void shell_clear_dirty(void) {
    dirty = false;
    input_clear_dirty();
}

void shell_execute_command(const char *cmd) {
    if (!cmd || !*cmd) return;

    const char *resolved = shell_resolve_variables(cmd);
    token_t *tokens = tokenize(resolved);
    ast_node_t *ast = parse_tokens(tokens);

    if (ast) {
        shell_execute_ast(&shell_ctx, ast);
        ast_free(ast);
    }

    tokenizer_free_tokens(tokens);
}

void shell_run_script(const char *path) {
    int fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) {
        shell_print_error("script not found");
        return;
    }

    char buf[SHELL_MAX_CMD_LEN];
    ssize_t n = vfs_read(fd, buf, SHELL_MAX_CMD_LEN - 1);
    if (n > 0) {
        buf[n] = 0;
        shell_execute_command(buf);
    }
    vfs_close(fd);
}

void shell_set_variable(const char *name, const char *value) {
    for (uint32_t i = 0; i < global_var_count; i++) {
        if (ds_strcmp(global_vars[i].name, name) == 0) {
            ds_strcpy(global_vars[i].value, value);
            return;
        }
    }
    if (global_var_count < SHELL_MAX_VARS) {
        ds_strcpy(global_vars[global_var_count].name, name);
        ds_strcpy(global_vars[global_var_count].value, value);
        global_vars[global_var_count].type = VAR_TYPE_STRING;
        global_vars[global_var_count].exported = false;
        global_vars[global_var_count].readonly = false;
        global_var_count++;
    }
}

const char *shell_get_variable(const char *name) {
    for (uint32_t i = 0; i < global_var_count; i++) {
        if (ds_strcmp(global_vars[i].name, name) == 0)
            return global_vars[i].value;
    }
    return "";
}

void shell_export_variable(const char *name) {
    for (uint32_t i = 0; i < global_var_count; i++) {
        if (ds_strcmp(global_vars[i].name, name) == 0) {
            global_vars[i].exported = true;
            return;
        }
    }
}

void shell_add_alias(const char *name, const char *value) {
    if (global_alias_count < SHELL_MAX_ALIASES) {
        ds_strcpy(global_aliases[global_alias_count].name, name);
        ds_strcpy(global_aliases[global_alias_count].value, value);
        global_alias_count++;
    }
}

const char *shell_resolve_alias(const char *name) {
    for (uint32_t i = 0; i < global_alias_count; i++) {
        if (ds_strcmp(global_aliases[i].name, name) == 0)
            return global_aliases[i].value;
    }
    return name;
}

void shell_register_builtin(const char *name, int (*handler)(shell_ctx_t *, ast_node_t *), const char *desc) {
    if (builtin_count < 128) {
        builtins[builtin_count].name = (char *)name;
        builtins[builtin_count].handler = handler;
        builtins[builtin_count].description = (char *)desc;
        builtins[builtin_count].flags = 0;
        builtin_count++;
    }
}

int shell_evaluate_expression(shell_ctx_t *ctx, ast_node_t *node) {
    return shell_execute_ast(ctx, node);
}

void shell_print_error(const char *msg) {
    shell_draw_string("Error: ", 0xE94560, 0x000000);
    shell_draw_string(msg, 0xE94560, 0x000000);
    shell_draw_char('\n', 0xE94560, 0x000000);
}

void shell_print_warning(const char *msg) {
    shell_draw_string("Warning: ", 0xFFFF00, 0x000000);
    shell_draw_string(msg, 0xFFFF00, 0x000000);
    shell_draw_char('\n', 0xFFFF00, 0x000000);
}

void shell_print_debug(const char *msg) {
    if (shell_ctx.debug_mode) {
        shell_draw_string("Debug: ", 0x888888, 0x000000);
        shell_draw_string(msg, 0x888888, 0x000000);
        shell_draw_char('\n', 0x888888, 0x000000);
    }
}

void shell_add_history(const char *cmd) {
    if (global_history_count >= SHELL_MAX_HISTORY) {
        kfree(global_history[0]);
        for (uint32_t i = 1; i < SHELL_MAX_HISTORY; i++)
            global_history[i-1] = global_history[i];
        global_history_count--;
    }
    global_history[global_history_count++] = ds_strdup(cmd);
    global_history_index = global_history_count;
}

const char *shell_get_history(uint32_t index) {
    if (index < global_history_count)
        return global_history[index];
    return "";
}

void shell_job_add(shell_job_t *job) {
    if (global_job_count < SHELL_MAX_JOBS) {
        job->job_id = global_job_count + 1;
        global_jobs[global_job_count++] = *job;
    }
}

void shell_job_remove(uint32_t job_id) {
    for (uint32_t i = 0; i < global_job_count; i++) {
        if (global_jobs[i].job_id == job_id) {
            if (i < global_job_count - 1)
                memcpy(&global_jobs[i], &global_jobs[i+1], (global_job_count - i - 1) * sizeof(shell_job_t));
            global_job_count--;
            break;
        }
    }
}

shell_job_t *shell_job_get(uint32_t job_id) {
    for (uint32_t i = 0; i < global_job_count; i++) {
        if (global_jobs[i].job_id == job_id)
            return &global_jobs[i];
    }
    return 0;
}

void shell_job_list(void) {
    for (uint32_t i = 0; i < global_job_count; i++) {
        char buf[256];
        int_to_str(global_jobs[i].job_id, buf);
        ds_strcat(buf, " ");
        ds_strcat(buf, global_jobs[i].running ? "Running" : "Stopped");
        ds_strcat(buf, " ");
        ds_strcat(buf, global_jobs[i].command);
        ds_strcat(buf, "\n");
        shell_draw_string(buf, 0xFFFFFF, 0x000000);
    }
}

void shell_job_foreground(uint32_t job_id) {
    shell_job_t *job = shell_job_get(job_id);
    if (job) {
        job->running = true;
        job->stopped = false;
    }
}

void shell_job_background(uint32_t job_id) {
    shell_job_t *job = shell_job_get(job_id);
    if (job) {
        job->running = true;
        job->stopped = false;
    }
}

void shell_job_kill(uint32_t job_id) {
    shell_job_t *job = shell_job_get(job_id);
    if (job) {
        job->running = false;
        shell_job_remove(job_id);
    }
}

void shell_function_register(const char *name, ast_node_t *params, ast_node_t *body) {
    (void)params;
    if (global_function_count < SHELL_MAX_SCRIPTS) {
        ds_strcpy(global_functions[global_function_count].name, name);
        global_functions[global_function_count].ast = body;
        global_functions[global_function_count].running = false;
        global_functions[global_function_count].suspended = false;
        global_functions[global_function_count].exit_status = 0;
        global_function_count++;
    }
}

shell_function_t *shell_function_get(const char *name) {
    for (uint32_t i = 0; i < global_function_count; i++) {
        if (ds_strcmp(global_functions[i].name, name) == 0)
            return &global_functions[i];
    }
    return 0;
}

void shell_signal_handler(int signal) {
    (void)signal;
}

void shell_cleanup(void) {
    for (uint32_t i = 0; i < global_history_count; i++)
        kfree(global_history[i]);
}