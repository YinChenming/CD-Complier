#ifndef TAC_H
#define TAC_H

/* type of symbol */
#define SYM_UNDEF 0
#define SYM_VAR 1
#define SYM_FUNC 2
#define SYM_TEXT 3
#define SYM_CONST 4
#define SYM_LABEL 5

/* type of symbol's value */
#define SYM_VAL_UNDEF (-1)
#define SYM_VAL_VOID 0
#define SYM_VAL_TEXT 1
#define SYM_VAL_BOOL 2
#define SYM_VAL_CHAR 3
#define SYM_VAL_INT 5
/* We define that the value of float type MUST GREATER THAN integer type!!! */
#define SYM_VAL_MAX_INTEGER SYM_VAL_INT

/* type of tac */
#define TAC_UNDEF 0 /* undefine */
#define TAC_ADD 1 /* a=b+c */
#define TAC_SUB 2 /* a=b-c */
#define TAC_MUL 3 /* a=b*c */
#define TAC_DIV 4 /* a=b/c */
#define TAC_EQ 5 /* a=(b==c) */
#define TAC_NE 6 /* a=(b!=c) */
#define TAC_LT 7 /* a=(b<c) */
#define TAC_LE 8 /* a=(b<=c) */
#define TAC_GT 9 /* a=(b>c) */
#define TAC_GE 10 /* a=(b>=c) */
#define TAC_NEG 11 /* a=-b */
#define TAC_ADDR 12 /* a=&b */
#define TAC_DEREF 13 /* a=*b */

#define TAC_STORE 20 /* *a=b */
#define TAC_COPY 21 /* a=b */
#define TAC_GOTO 22 /* goto a */
#define TAC_IFZ 23 /* ifz b goto a */
#define TAC_BEGINFUNC 24 /* function begin */
#define TAC_ENDFUNC 25 /* function end */
#define TAC_LABEL 26 /* label a */
#define TAC_VAR 27 /* int a */
#define TAC_FORMAL 28 /* formal a */
#define TAC_ACTUAL 29 /* actual a */
#define TAC_CALL 30 /* a=call b */
#define TAC_RETURN 31 /* return a */
#define TAC_INPUT 32 /* input a */
#define TAC_OUTPUT 33 /* output a */

#include <stdio.h>

typedef struct sym {
    /*
        type:SYM_VAR name:abc value:98 offset:-1
        type:SYM_VAR name:bcd value:99 offset:4
        type:SYM_LABEL name:L1/max
        type:SYM_INT value:1
        type:SYM_FUNC name:max address:1234
        type:SYM_TEXT name:"hello" label:10
    */
    int type;
    int scope; /* 0:global, 1:local */
    char *name;
    int offset;
    int value;
    int value_type;
    int value_size;
    int indirection; /* 0: normal global, 1: value_type*, 2: value_type**, ... */
    int label;
    struct tac *address; /* SYM_FUNC */
    struct sym *next;
    void *etc;
} SYM;

typedef struct tac {
    struct tac *next;
    struct tac *prev;
    int op;
    SYM *a;
    SYM *b;
    SYM *c;
    void *etc;
} TAC;

typedef struct exp {
    struct exp *next; /* for argument list */
    TAC *tac; /* code */
    SYM *ret; /* return value */
    void *etc;
} EXP;

/* global var */
extern FILE *file_x, *file_s;
extern int yylineno, scope, next_tmp, next_label;
extern SYM *sym_tab_global, *sym_tab_local;
extern TAC *tac_first, *tac_last;

static const int tmp_name_len = 12;

/* function */
void tac_init(void);

void tac_complete(void);

TAC *join_tac(TAC *c1, TAC *c2);

void out_str(FILE *f, const char *format, ...);

void out_sym(FILE *f, const SYM *s);

void out_tac(FILE *f, const TAC *i);

void free_sym(SYM *sym, const SYM **symtab);

void free_tac(TAC *tac);

SYM *mk_var(const char *name, int type);

SYM *mk_label(const char *name);

SYM *mk_tmp(int type);

SYM *mk_const(int n, int type);

SYM *mk_text(const char *text);

TAC *mk_tac(int op, SYM *a, SYM *b, SYM *c);

EXP *mk_exp(EXP *next, SYM *ret, TAC *code);

char *mk_lstr(int i);

SYM *get_var(const char *name);

SYM *declare_func(const char *name, int type);

TAC *declare_var(SYM *var);

TAC *declare_para(SYM *var);

TAC *do_func(const SYM *func, TAC *args, TAC *code);

TAC *do_assign(SYM *var, const EXP *exp);

TAC *do_store(SYM *dest, const EXP *exp);

TAC *do_output(EXP* exp);

TAC *do_input(SYM *var);

TAC *do_call(const char *name, EXP *arglist);

TAC *do_if(const EXP *exp, TAC *stmt);

TAC *do_test(const EXP *exp, TAC *stmt1, TAC *stmt2);

TAC *do_while(const EXP *exp, TAC *stmt);

EXP *do_bin(int binop, EXP *exp1, const EXP *exp2);

EXP *do_cmp(int binop, EXP *exp1, const EXP *exp2);

EXP *do_un(int unop, EXP *exp);

EXP *do_call_ret(const char *name, EXP *arglist);

int get_size_of_type(int type);

void error(const char *format, ...);

#endif // TAC_H
