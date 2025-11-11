%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tac.h"
#include "obj.h"

int yylex();
void yyerror(char* msg);
extern char *yytext;

int hex2char(char* str, int len)
{
	unsigned char c = 0, d;
	while(len--)
	{
		d =  *(str++);
		if ('0' <= d && '9' >= d)
			d -= '0';
		else if ('A' <= d && 'F' >= d)
			d = d - 'A' + 10;
		else if ('a' <= d && 'f' >= d)
			d = d - 'a' + 10;
		else {
			error("Invalid hex character '%c'", d);
			return -1;
		}
		c = c << 4 | d;
	}
	return c;
}

%}

%union
{
	char character;
	char *string;
	SYM *sym;
	TAC *tac;
	EXP	*exp;
}

%token INT EQ NE LT LE GT GE UMINUS IF ELSE WHILE FUNC INPUT OUTPUT RETURN CHAR FOR BREAK CONTINUE
%token <string> INTEGER IDENTIFIER TEXT CHARACTER

%left EQ NE LT LE GT GE
%left '+' '-'
%left '*' '/'
%right UMINUS

%type <tac> program function_declaration_list function_declaration function parameter_list parameter variable_list statement assignment_statement return_statement if_statement while_statement call_statement block declaration_list declaration statement_list input_statement output_statement
%type <tac> store_assignment_statement
%type <exp> argument_list expression_list
%type <exp> primary_expression unary_expression multiplicative_expression additive_expression relational_expression equality_expression expression call_expression
%type <exp> array_expression for_exp_opt
%type <sym> function_head variable array_variable

%%

program : function_declaration_list
{
	tac_last=$1;
	tac_complete();
}
;

function_declaration_list : function_declaration
| function_declaration_list function_declaration
{
	$$=join_tac($1, $2);
}
;

function_declaration : function
| declaration
;

declaration : INT variable_list ';'
{
	$$=$2;  // 默认的类型就是int,所有值都会自动设置,无需修改
}
| CHAR variable_list ';'
{
	TAC *p = $2;
	SYM *sym;
	$$=$2;
	while (p && p->op == TAC_VAR)
	{
        sym = p->a;
		sym->value_type = SYM_VAL_CHAR;
        int new_size = get_size_of_type(SYM_VAL_CHAR);
        if (sym->dim_size) {
            if (sym->indirection - sym->dim_size->level > 1) {
                new_size = POINTER_SIZE;
            }
        } else if (sym->indirection > 0) {
            new_size = POINTER_SIZE;
        }
        sym->value_size = sym->value_size / get_size_of_type(SYM_VAL_DEFAULT) * new_size;
		p = p->prev;
	}
}
;

variable: IDENTIFIER
{
    $$=mk_var($1, SYM_VAL_DEFAULT);
}
| '*' variable
{
    $$=$2;
    if (!$2->dim_size) {
        $2->value_size = POINTER_SIZE;
    }
    $2->indirection += 1;
}
| array_variable
;

array_variable: IDENTIFIER
{
    $$=mk_var($1, SYM_VAL_DEFAULT);
}
| array_variable '[' ']'
{
    // 我们不支持0数组
    $$=mk_dim($1, 0);
}
| array_variable '[' INTEGER ']'
{
    int dim_size = atoi($3);
    if (dim_size <= 0) {
        error("invalid dim size for array!");
    } else {
        $$=mk_dim($1, dim_size);
    }
}
;

variable_list : variable
{
	$$=declare_var($1);
}
| variable_list ',' variable
{
	$$=join_tac($1, declare_var($3));
}
;

function : function_head '(' parameter_list ')' block
{
	$$=do_func($1, $3, $5);
	scope=0; /* Leave local scope. */
	sym_tab_local=NULL; /* Clear local symbol table. */
}
| error
{
	error("Bad function syntax");
	$$=NULL;
}
;

function_head : IDENTIFIER
{
	$$=declare_func($1, SYM_VAL_INT);	// default return value for a function is INT
	scope=1; /* Enter local scope. */
	sym_tab_local=NULL; /* Init local symbol table. */
}
| INT IDENTIFIER
{
	$$=declare_func($2, SYM_VAL_INT);	// default return value for a function is INT
	scope=1; /* Enter local scope. */
	sym_tab_local=NULL; /* Init local symbol table. */
}
| CHAR IDENTIFIER
{
	$$=declare_func($2, SYM_VAL_CHAR);	// default return value for a function is INT
	scope=1; /* Enter local scope. */
	sym_tab_local=NULL; /* Init local symbol table. */
}
;

parameter_list : parameter
| parameter_list ',' parameter
{
	$$=join_tac($1, $3);
}
|
{
	$$=NULL;
}
;

parameter: variable
{
    if ($1->dim_size != NULL && $1->dim_size->size>0){
        error("cannot declare an array with dim size!");
    } else
	$$=declare_para($1);
}
| INT variable
{
    $2->value_type = SYM_VAL_INT;
    $2->value_size = $2->indirection ? POINTER_SIZE : get_size_of_type(SYM_VAL_INT);
	$$=declare_para($2);
}
| CHAR variable
{
    $2->value_type = SYM_VAL_CHAR;
    $2->value_size = $2->indirection ? POINTER_SIZE : get_size_of_type(SYM_VAL_CHAR);
	$$=declare_para($2);
}
;

statement : assignment_statement ';'
| input_statement ';'
| output_statement ';'
| call_statement ';'
| return_statement ';'
| if_statement
| while_statement
| block
| BREAK ';'
{
    $$=mk_break();
}
| CONTINUE ';'
{
    $$=mk_continue();
}
| error
{
	error("Bad statement syntax");
	$$=NULL;
}
;

block : '{' declaration_list statement_list '}'
{
	$$=join_tac($2, $3);
}
;

declaration_list        :
{
	$$=NULL;
}
| declaration_list declaration
{
	$$=join_tac($1, $2);
}
;

statement_list : statement
| statement_list statement
{
	$$=join_tac($1, $2);
}
;

assignment_statement : IDENTIFIER '=' expression
{
	$$=do_assign(get_var($1), $3);
}
| store_assignment_statement
| array_expression '=' expression
{
    // array_expression必然返回一个Expression,其中$1->tac为{.op=TAC_DEREF,.a=$1->ret,.b=b,.c=c},构成$1->ret=b[c]
    // 我们只需要将其重新排列即可得到正确的TAC_STORE: 丢弃$1->ret,令{.op=TAC_STORE, .a=$1->b, .b=expression, .c=c}即可构成b[c]=expression
    $$=$1->tac;
    $$->a = $$->b;
    $$->b = $3->ret;
    join_tac($3->tac, $$);
    $$->op = TAC_STORE;
}
;

store_assignment_statement : '*' IDENTIFIER '=' expression
{
    $$=do_store(get_var($2), $4, NULL);
}
| '*' store_assignment_statement
{
    const SYM *underef_sym = $2->a;
    const EXP *deref_exp = do_un(TAC_DEREF, mk_exp(NULL, underef_sym, NULL));
    SYM *tmp_sym = deref_exp->ret;
    TAC *deref_tac = deref_exp->tac;
    join_tac($2->prev, deref_tac);
    $2->prev = deref_tac;
    $2->a = tmp_sym;
    $$ = $2;
}
;

array_expression: IDENTIFIER '[' expression ']'
{
    $$=do_deref(mk_exp(NULL, get_var($1), NULL), $3);
}
| array_expression '[' expression ']'
{
    $$=do_deref($1, $3);
}
;

// 优先级: primary > unary (*, &, -) > multiplicative (*, /) > additive (+, -) > relational (> , <, >=, <=) > equality (==, !=)

primary_expression: '(' expression ')'
{
    $$=$2;
}
| INTEGER
{
    $$=mk_exp(NULL, mk_const(atoi($1), SYM_VAL_INT), NULL);
}
| CHARACTER
{
    int c = -1;
    int len = strlen($1);
    if (*$1 != '\'' || $1[len-1] != '\'' || len < 3)
    {
        error("Unknown char '%s'", $1);
        return 0;
    }
    if (len == 3)
    {
        c = $1[1];
    } else if ($1[1] == '\\')
    {
        switch($1[2])
        {
            case '\\': c = '\\'; break;
            case 'n' : c = '\n'; break;
            case 't' : c = '\t'; break;
            case 'b' : c = '\b'; break;
            case 'a' : c = '\a'; break;
            case 'f' : c = '\f'; break;
            case 'v' : c = '\v'; break;
            case '\'': c = '\''; break;
            case '\"': c = '\"'; break;
            case '0' : c = '\0'; break;
            case 'x': case 'X':
                if (len > 4)
                {
                    c = 'x'; break;
                }
            // fallthrough!!
            default:
                c = -1;
                error("Invalid escape sequence '\\%c'", $1[2]);
                return 0;
        }
        if (c == 'x')
        {
            int result = hex2char($1+3, len-4);
            if (result == -1)
                return 0;
            c = (char) result;
        }
    } else
    {
        error("Invalid char %s", $1);
        return 0;
    }
    $$=mk_exp(NULL, mk_const(c, SYM_VAL_CHAR), NULL);
}
| IDENTIFIER
{
    $$=mk_exp(NULL, get_var($1), NULL);
}
| call_expression
| array_expression
| error
{
    error("Bad primary expression syntax");
    $$=mk_exp(NULL, NULL, NULL);
}
;

unary_expression: primary_expression
| '-' unary_expression  %prec UMINUS
{
    $$=do_un(TAC_NEG, $2);
}
| '*' unary_expression
{
    $$=do_un(TAC_DEREF, $2);
}
| '&' primary_expression
{
    // 不允许对右值取地址!
    $$=do_un(TAC_ADDR, $2);
}
;

multiplicative_expression: unary_expression
| multiplicative_expression '*' unary_expression
{
    $$=do_bin(TAC_MUL, $1, $3);
}
| multiplicative_expression '/' unary_expression
{
    $$=do_bin(TAC_DIV, $1, $3);
}
;

additive_expression: multiplicative_expression
| additive_expression '+' multiplicative_expression
{
    $$=do_bin(TAC_ADD, $1, $3);
}
| additive_expression '-' multiplicative_expression
{
    $$=do_bin(TAC_SUB, $1, $3);
}
;

relational_expression: additive_expression
| relational_expression LT additive_expression
{
    $$=do_cmp(TAC_LT, $1, $3);
}
| relational_expression LE additive_expression
{
    $$=do_cmp(TAC_LE, $1, $3);
}
| relational_expression GT additive_expression
{
    $$=do_cmp(TAC_GT, $1, $3);
}
| relational_expression GE additive_expression
{
    $$=do_cmp(TAC_GE, $1, $3);
}
;

equality_expression: relational_expression
| equality_expression EQ relational_expression
{
    $$=do_cmp(TAC_EQ, $1, $3);
}
| equality_expression NE relational_expression
{
    $$=do_cmp(TAC_NE, $1, $3);
}
;

expression: equality_expression
;

argument_list           :
{
	$$=NULL;
}
| expression_list
;

expression_list : expression
|  expression_list ',' expression
{
	$3->next=$1;
	$$=$3;
}
;

input_statement : INPUT IDENTIFIER
{
	$$=do_input(get_var($2));
}
;

output_statement : OUTPUT expression
{
	$$=do_output($2);
}
| OUTPUT TEXT
{
    EXP *exp = mk_exp(NULL, mk_text($2), NULL);
	$$=do_output(exp);
}
;

return_statement : RETURN expression
{
	TAC *t=mk_tac(TAC_RETURN, $2->ret, NULL, NULL);
	t->prev=$2->tac;
	$$=t;
}
;

if_statement : IF '(' expression ')' block
{
	$$=do_if($3, $5);
}
| IF '(' expression ')' block ELSE block
{
	$$=do_test($3, $5, $7);
}
;

for_exp_opt:
{
    $$=NULL;
}
| expression
{
    $$=$1->tac;
}
| assignment_statement
;

while_statement : WHILE '(' expression ')' block
{
	$$=do_while($3, $5);
}
| FOR '(' for_exp_opt ';' expression ';' for_exp_opt ')' block
{
    TAC *for_continue_label = NULL;
    for (const TAC* t=$9;t;t=t->prev) {
        if (t->op == TAC_GOTO && t->a && t->a->value_type == SYM_LABEL_CONTINUE) {
            if (!for_continue_label) {
                for_continue_label = mk_tac(TAC_LABEL, mk_label(mk_cstr()), NULL, NULL);
            }
            t->a->name = strdup(for_continue_label->a->name);
            t->a->value = t->a->value_type = 0;
        }
    }
    $$=join_tac($9, join_tac(for_continue_label, $7));
    $$=do_while($5, $$);
    $$=join_tac($3, $$);
}
;

call_statement : IDENTIFIER '(' argument_list ')'
{
	$$=do_call($1, $3);
}
;

call_expression : IDENTIFIER '(' argument_list ')'
{
	$$=do_call_ret($1, $3);
}
;

%%

void yyerror(char* msg)
{
	fprintf(stderr, "%s: line %d\n", msg, yylineno);
	exit(0);
}
