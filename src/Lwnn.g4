/* Ahttps://stackoverflow.com/questions/29971097/how-to-create-ast-with-antlr4 */

grammar Lwnn;

module
    : (statements_=statement)* EOF
    ;

// continue following this:  https://github.com/antlr/grammars-v4/blob/master/java/Java.g4#L407
statement
    : expr ';'                                                          # exprStmt
    | '{' (stmts=statement )* '}'                                     # compoundExpr
    | KW_IF '(' cond=expr ')' thenExprStmt=statement ('else' elseExprStmt=statement)?  # ifExpr
    ;

expr
    : name=ID ':' type=ID                                             # varDeclExpr
    | '(' expr ')'                                                    # parensExpr
//  | op=('+'|'-') expr                                               # unaryExpr
    | left=expr op=(OP_MUL|OP_DIV) right=expr                         # binaryExpr
    | left=expr op=(OP_ADD|OP_SUB) right=expr                         # binaryExpr
//  | left=expr op=OP_AND right=expr                                # binaryExpr
//  | left=expr op=OP_OR right=expr                                 # binaryExpr
    | left=expr op=OP_EQ right=expr                                   # binaryExpr
    | <assoc=right> left=expr op=OP_ASSIGN right=expr                 # binaryExpr
//  | func=ID '(' expr ')'                                            # funcExpr
    | value=LIT_INT                                                   # literalInt32Expr
    | value=LIT_FLOAT                                                 # literalFloatExpr
    | value=litBool                                                   # literalBool
    | var=ID                                                          # variableRefExpr
    | 'cast' '<' type=ID '>' '(' expr ')'                             # castExpr
    | '(?' cond=expr ',' thenExpr=expr ',' elseExpr=expr ')'          # ternaryExpr
    ;



litBool
    : KW_TRUE
    | KW_FALSE;

OP_ADD: '+';
OP_SUB: '-';
OP_MUL: '*';
OP_DIV: '/';
OP_ASSIGN: '=';
OP_EQ: '==';
//OP_OR: '||';
//OP_AND: '&&';

KW_TRUE: 'true';
KW_FALSE: 'false';
KW_IF: 'if';

//NUM :   [0-9]+ ('.' [0-9]+)? ([eE] [+-]? [0-9]+)?;
LIT_INT:    '-'?[0-9]+;
LIT_FLOAT:  '-'?[0-9]+'.'[0-9]+;
ID:         [a-zA-Z_][a-zA-Z0-9_]*;
WS:         [ \t\r\n] -> channel(HIDDEN);
