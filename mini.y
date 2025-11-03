%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tac.h"

int yylex();
void yyerror(char* msg);

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

%token INT EQ NE LT LE GT GE UMINUS IF ELSE WHILE FUNC INPUT OUTPUT RETURN CHAR
%token <string> INTEGER IDENTIFIER TEXT CHARACTER

%left EQ NE LT LE GT GE
%left '+' '-'
%left '*' '/'
%right UMINUS

%type <tac> program function_declaration_list function_declaration function parameter_list parameter variable_list statement assignment_statement return_statement if_statement while_statement call_statement block declaration_list declaration statement_list input_statement output_statement
%type <exp> argument_list expression_list expression call_expression
%type <sym> function_head

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
	$$=$2;
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
		p = p->prev;
	}
}
;

variable_list : IDENTIFIER
{
	$$=declare_var($1, SYM_VAL_INT);
}
| variable_list ',' IDENTIFIER
{
	$$=join_tac($1, declare_var($3, SYM_VAL_INT));
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

parameter: IDENTIFIER
{
	$$=declare_para($1, SYM_VAL_INT);
}
| INT IDENTIFIER
{
	$$=declare_para($2, SYM_VAL_INT);
}
| CHAR IDENTIFIER
{
	$$=declare_para($2, SYM_VAL_CHAR);
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
;

expression : expression '+' expression
{
	$$=do_bin(TAC_ADD, $1, $3);
}
| expression '-' expression
{
	$$=do_bin(TAC_SUB, $1, $3);
}
| expression '*' expression
{
	$$=do_bin(TAC_MUL, $1, $3);
}
| expression '/' expression
{
	$$=do_bin(TAC_DIV, $1, $3);
}
| '-' expression  %prec UMINUS
{
	$$=do_un(TAC_NEG, $2);
}
| expression EQ expression
{
	$$=do_cmp(TAC_EQ, $1, $3);
}
| expression NE expression
{
	$$=do_cmp(TAC_NE, $1, $3);
}
| expression LT expression
{
	$$=do_cmp(TAC_LT, $1, $3);
}
| expression LE expression
{
	$$=do_cmp(TAC_LE, $1, $3);
}
| expression GT expression
{
	$$=do_cmp(TAC_GT, $1, $3);
}
| expression GE expression
{
	$$=do_cmp(TAC_GE, $1, $3);
}
| '(' expression ')'
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
		return;
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
				return;
		}
		if (c == 'x')
		{
			int result = hex2char($1+3, len-4);
			if (result == -1)
				return;
			c = (char) result;
		}
	} else
	{
		error("Invalid char %s", $1);
		return;
	}
	$$=mk_exp(NULL, mk_const(c, SYM_VAL_CHAR), NULL);
}
| IDENTIFIER
{
	$$=mk_exp(NULL, get_var($1), NULL);
}
| call_expression
{
	$$=$1;
}
| error
{
	error("Bad expression syntax");
	$$=mk_exp(NULL, NULL, NULL);
}
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

output_statement : OUTPUT IDENTIFIER
{
	$$=do_output(get_var($2));
}
| OUTPUT TEXT
{
	$$=do_output(mk_text($2));
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

while_statement : WHILE '(' expression ')' block
{
	$$=do_while($3, $5);
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
