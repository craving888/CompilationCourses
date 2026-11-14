%option noyywrap noinput nounput nodefault
%x COMMENT
%{
#include "frontend.hpp"
static Scan scanned;
static int line, comment_line;
static void token(Token::Kind k, const char* text) { scanned.tokens.push_back({k, text, line}); }
%}
newline (\r\n|\r|\n)
utf ([\xC2-\xDF][\x80-\xBF]|[\xE0-\xEF][\x80-\xBF]{2}|[\xF0-\xF4][\x80-\xBF]{3})
%%
"//"[^\r\n]*                        ;
"/*"                               { comment_line = line; BEGIN(COMMENT); }
<COMMENT>"*/"                       { BEGIN(INITIAL); }
<COMMENT>{newline}                  { ++line; }
<COMMENT>.                          ;
<COMMENT><<EOF>>                    { scanned.errors.push_back({comment_line,"unterminated comment"}); BEGIN(INITIAL); yyterminate(); }
{newline}                          { ++line; }
[ \t]+                             ;
int|void|const|if|else|while|break|continue|return { token(Token::Word, yytext); }
[_A-Za-z][_A-Za-z0-9]*             { token(Token::Ident, yytext); }
0|[1-9][0-9]*                      { token(Token::Number, yytext); }
"=="|"!="|"<="|">="|"&&"|"||"|[+*/%=<>!(){};,\-] { token(Token::Word, yytext); }
{utf}|.                            { scanned.errors.push_back({line,"illegal character '" + std::string(yytext, yyleng) + "'"}); }
%%
Scan scan(const std::string& source) {
    scanned = {};
    line = 0;
    auto buffer = yy_scan_bytes(source.data(), static_cast<int>(source.size()));
    yylex();
    yy_delete_buffer(buffer);
    scanned.tokens.push_back({Token::End, "", line});
    return std::move(scanned);
}
