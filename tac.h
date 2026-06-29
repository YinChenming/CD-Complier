#ifndef TAC_H
#define TAC_H

/* type of symbol */
#define SYM_UNDEF 0
#define SYM_VAR 1
#define SYM_FUNC 2
#define SYM_TEXT 3
#define SYM_CONST 4
#define SYM_LABEL 5
#define SYM_STRUCT 6

/* type of symbol's value */
#define SYM_VAL_UNDEF (-1)
#define SYM_VAL_VOID 0
#define SYM_VAL_TEXT 1
#define SYM_VAL_BOOL 2
#define SYM_VAL_CHAR 3
#define SYM_VAL_INT 5
/* We define that the value of float type MUST GREATER THAN integer type!!! */
#define SYM_VAL_MAX_INTEGER SYM_VAL_INT
#define SYM_VAL_DEFAULT SYM_VAL_INT
#define SYM_VAL_SIZE SYM_VAL_INT

#define SYM_VAL_STRUCT 9
#define SYM_VAL_MAX 10

#define SYM_LABEL_BREAK (-2)
#define SYM_LABEL_CONTINUE (-3)
#define SYM_LABEL_DEFAULT (-4)

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

#define TAC_MIN_CALC TAC_ADD
#define TAC_MAX_CALC TAC_NEG

#define TAC_ADDR 12 /* a=&b */
#define TAC_DEREF 13 /* a=*b or a=b[c] */

#define TAC_STORE 20 /* *a=b or a[c]=b */
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

#define SCOPE_GLOBAL 0
#define SCOPE_LOCAL 1
#define SCOPE_STRUCT 2

#include <stdio.h>
#include <stdbool.h>

struct array_dim_size
{
    int size;
    int level;
    struct array_dim_size *next;
};

typedef struct sym {
    /*
        type:SYM_VAR name:abc value:98 offset:-1
        type:SYM_VAR name:bcd value:99 offset:4
        type:SYM_LABEL name:L1/max
        type:SYM_CONST value:1
        type:SYM_FUNC name:max address:1234
        type:SYM_TEXT name:"hello" label:10
    */
    int type;
    int scope; /* 0:global, 1:local */
    char *name;
    int offset;
    int value;
    int value_type; /* <0 means a label need to be fill in for/while/switch, >SYM_VAL_MAX means a struct */
    int value_size;
    int indirection; /* 0: normal value, 1: value_type*, 2: value_type**, ... */
    struct array_dim_size *dim_size; /* default NULL */
    int label;
    struct tac *address; /* SYM_FUNC */
    struct sym *struct_sym;
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
// extern SYM *sym_tab_global, *sym_tab_local;
extern TAC *tac_first, *tac_last;

static const int tmp_name_len = 12;

/* function */
void tac_init(void);

void tac_complete(void);

TAC *join_tac(TAC *c1, TAC *c2);

void out_str(FILE *f, const char *format, ...);

void out_sym(FILE *f, const SYM *s);

void out_tac(FILE *f, const TAC *i);

// void free_sym(SYM *sym, const SYM **symtab);

void free_tac(TAC *tac);

SYM *mk_var(const char *name, int type);

SYM *mk_label(const char *name);

SYM *mk_tmp(int type);

SYM *mk_const(int n, int type);

SYM *mk_text(const char *text);

SYM *mk_dim(SYM *sym, int size);

TAC *mk_tac(int op, SYM *a, SYM *b, SYM *c);

EXP *mk_exp(EXP *next, SYM *ret, TAC *code);

const char *mk_lstr();

const char *mk_bstr();

const char *mk_cstr();

const char *mk_case_str();

const char *mk_dstr();

TAC *mk_break(void);

TAC *mk_continue(void);

TAC *mk_case(SYM *sym);

TAC *mk_default();

TAC *mk_struct_vars(const char *struct_name, TAC *tac);

SYM *get_var(const char *name);

SYM *declare_func(const char *name, int type);

TAC *declare_var(SYM *var);

TAC *declare_para(SYM *var);

SYM *declare_struct(const char *name);

TAC *do_func(const SYM *func, TAC *args, TAC *code);

void do_struct(SYM *sym, TAC *declarations);

TAC *do_assign(SYM *var, const EXP *exp);

TAC *do_store(SYM *dest, const EXP *exp, const EXP *offset);

TAC *do_output(EXP* exp);

TAC *do_input(SYM *var);

TAC *do_call(const char *name, EXP *arglist);

TAC *do_if(const EXP *exp, TAC *stmt);

TAC *do_test(const EXP *exp, TAC *stmt1, TAC *stmt2);

TAC *do_while(const EXP *exp, TAC *stmt);

TAC *do_switch(const EXP *exp, TAC *stmt);

EXP *do_bin(int binop, EXP *exp1, EXP *exp2);

EXP *do_deref(EXP *exp1, EXP *exp2);

EXP *do_cmp(int binop, EXP *exp1, const EXP *exp2);

EXP *do_un(int unop, EXP *exp);

EXP *do_call_ret(const char *name, EXP *arglist);

EXP *do_get_member(EXP *exp, const char *name);

EXP *do_pointer_get_member(EXP *exp, const char *name);

SYM *forloop_all_global_sym(bool reset);

void print_structs(FILE *f);

void clear_local_hash(void);

bool is_pointer(const SYM *sym);

bool is_array(const SYM *sym);

int get_size_of_type(int type, int default_val);

#define get_size_of_type_or_pointer(TYPE, DEFAULT_VAL, IS_POINTER) (!(IS_POINTER) ? get_size_of_type(TYPE, DEFAULT_VAL) : POINTER_SIZE)

void error(const char *format, ...);

#endif // TAC_H
