%{
#include "tac.h"
#include "mini.y.h"
%}

%option yylineno

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

[A-Za-z]([A-Za-z]|[0-9])*  {
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

[ \t\r\n]|#.*

. 	{	return *yytext; }

%%

int yywrap()
{
	return 1;
}
