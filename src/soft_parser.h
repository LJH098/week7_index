#ifndef SOFT_PARSER_H
#define SOFT_PARSER_H

#include "utils.h"

typedef enum {
    TOKEN_KEYWORD,
    TOKEN_IDENTIFIER,
    TOKEN_INT_LITERAL,
    TOKEN_STR_LITERAL,
    TOKEN_OPERATOR,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_COMMA,
    TOKEN_SEMICOLON,
    TOKEN_UNKNOWN
} TokenType;

typedef struct {
    TokenType type;
    char value[MAX_TOKEN_VALUE];
} Token;

/*
 * Convert raw SQL text into a dynamically allocated token array.
 * On success, returns the token array and stores the token count.
 * Caller owns the returned memory and must free it with free().
 */
Token *soft_parse(const char *sql, int *token_count);

/*
 * Return a human-readable name for a token type.
 */
const char *soft_parser_token_type_name(TokenType type);

#endif
