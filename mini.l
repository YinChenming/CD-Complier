%{
#include "tac.h"
#include "mini.y.h"
%}

%option yylineno
%x C_COMMENT

%%

"int"  {  return INT;  }

"char" {  return CHAR; }

"input"  {  return INPUT;  }

"output"  {  return OUTPUT;  }

"return"  {  return RETURN;  }

"if"  {  return IF;  }

"else"  {  return ELSE;  }

"while"  {  return WHILE;  }

"for" {  return FOR;  }

"break" {  return BREAK;  }

"continue" {  return CONTINUE;  }

"switch"  {  return SWITCH;  }

"case"  {  return CASE;  }

"default"  {  return DEFAULT;  }

"struct"  {  return STRUCT;  }

"->"  {  return RIGHT_ARROW;  }

[A-Za-z_]([A-Za-z]|[0-9]|_)*  {
	yylval.string = strdup(yytext);
	return IDENTIFIER;
}

[0-9]*	{
	yylval.string = strdup(yytext);
	return INTEGER;
}

\'([^'\\\n]|\\.)+\' {
	yylval.string = strdup(yytext);
	return CHARACTER;
}

\"[^\"]*\"  {
	yylval.string = strdup(yytext);
	return TEXT;
}

"=="  {  return EQ;  }

"!="  {  return NE;  }

"<="  {  return LE;  }

"<"  {  return LT;  }

">="  {  return GE;  }

">"  {  return GT;  }

"//".*

"/*"                { BEGIN(C_COMMENT); }
<C_COMMENT>"*/"     { BEGIN(INITIAL); }
<C_COMMENT>[^*\n]+  {}
<C_COMMENT>"*"      {}
<C_COMMENT>"\n"     { yylineno++; }

[ \t\r\n]

. 	{	return *yytext; }

%%

int yywrap()
{
	return 1;
}
