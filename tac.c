#include "tac.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "obj.h"

/* global var */
int scope, next_tmp, next_label;
SYM *sym_tab_global, *sym_tab_local;
TAC *tac_first, *tac_last;

void tac_init(void) {
    scope = 0;
    sym_tab_global = NULL;
    sym_tab_local = NULL;
    next_tmp = 0;
    next_label = 1;
}

void tac_complete(void) {
    TAC *cur = NULL; /* Current TAC */
    TAC *prev = tac_last; /* Previous TAC */

    while (prev != NULL) {
        prev->next = cur;
        cur = prev;
        prev = prev->prev;
    }

    tac_first = cur;
}

SYM *lookup_sym(SYM *symtab, const char *name) {
    SYM *t = symtab;

    while (t != NULL) {
        if (strcmp(t->name, name) == 0)
            break;
        else
            t = t->next;
    }

    return t; /* NULL if not found */
}

void insert_sym(SYM **symtab, SYM *sym) {
    sym->next = *symtab; /* Insert at head */
    *symtab = sym;
}

static SYM *mk_sym(void) {
    SYM *t = (SYM *) malloc(sizeof(SYM));
    t->indirection = 0;
    t->dim_size = t->etc = t->name = NULL;
    return t;
}

SYM *mk_var(const char *name, const int type) {
    SYM *sym = NULL;

    if (scope)
        sym = lookup_sym(sym_tab_local, name);
    else
        sym = lookup_sym(sym_tab_global, name);

    /* var already declared */
    if (sym != NULL) {
        error("variable already declared");
        return NULL;
    }

    /* var unseen before, set up a new symbol table node, insert_sym it into the symbol table. */
    sym = mk_sym();
    sym->type = SYM_VAR;
    sym->name = strdup(name);
    sym->value_type = type;
    sym->value_size = get_size_of_type(type); // the size of a pointer is 4
    sym->offset = -1; /* Unset address */

    if (scope)
        insert_sym(&sym_tab_local, sym);
    else
        insert_sym(&sym_tab_global, sym);

    return sym;
}

TAC *join_tac(TAC *c1, TAC *c2) {
    if (c1 == NULL)
        return c2;
    if (c2 == NULL)
        return c1;

    /* Run down c2, until we get to the beginning and then add c1 */
    TAC *t = c2;
    while (t->prev != NULL)
        t = t->prev;

    t->prev = c1;
    return c2;
}

void free_sym(SYM *sym, const SYM **symtab)
{
    if (!sym) return;
    if (symtab && *symtab)
    {
        if (strcmp((*symtab)->name, sym->name) == 0)
            *symtab = (*symtab)->next;
        else
        {
            SYM *t = (*symtab)->next;
            while (t->next)
            {
                if (strcmp(t->next->name, sym->name) == 0)
                    break;
                t = t->next;
            }
            if (t->next)
                t->next = t->next->next;
        }
    }
    free(sym->name);
    free(sym);
}

SYM *mk_dim(SYM *sym, const int size)
{
    if (sym->dim_size == NULL)
    {
        sym->value_size = get_size_of_type(sym->value_type) * size;
        sym->dim_size = (struct array_dim_size *) malloc(sizeof(struct array_dim_size));
        sym->dim_size->next = NULL;
        sym->dim_size->size = size;
        sym->dim_size->level = 0;
        sym->indirection += 1;
        return sym;
    }

    struct array_dim_size *dim = sym->dim_size;
    while (dim->next != NULL)
    {
        dim->level++;

        if (dim->size && !(dim->size - dim->next->size))
            error("cannot declare such an array!");
        dim = dim->next;
    }
    dim->level++;
    if (size)
        sym->value_size *= size;
    dim->next = (struct array_dim_size *) malloc(sizeof(struct array_dim_size));
    dim = dim->next;
    dim->level = 0;
    dim->size = size;
    dim->next = NULL;
    sym->indirection += 1;
    return sym;
}

TAC *declare_var(SYM *var) { return mk_tac(TAC_VAR, var, NULL, NULL); }

TAC *mk_tac(const int op, SYM *a, SYM *b, SYM *c) {
    TAC *t = (TAC *) malloc(sizeof(TAC));

    t->next = NULL; /* Set these for safety */
    t->prev = NULL;
    t->op = op;
    t->a = a;
    t->b = b;
    t->c = c;

    return t;
}

SYM *mk_label(const char *name) {
    SYM *t = mk_sym();

    t->type = SYM_LABEL;
    t->name = strdup(name);

    return t;
}

TAC *do_func(const SYM *func, TAC *args, TAC *code) {
    // TAC *tlist; /* The backpatch list */

    /* Label at start of function */
    TAC *tlab = mk_tac(TAC_LABEL, mk_label(func->name), NULL, NULL);
    /* BEGIN FUNC marker */
    TAC *tbegin = mk_tac(TAC_BEGINFUNC, NULL, NULL, NULL);
    /* END FUNC marker */
    TAC *tend = mk_tac(TAC_ENDFUNC, NULL, NULL, NULL);

    tbegin->prev = tlab;
    code = join_tac(args, code);
    tend->prev = join_tac(tbegin, code);

    return tend;
}

SYM *mk_tmp(const int type) {
    // SYM *sym;
    char name[tmp_name_len];
    sprintf(name, "$%d", next_tmp++); /* Set up text */
    return mk_var(name, type);
}

TAC *declare_para(SYM *var) { return mk_tac(TAC_FORMAL, var, NULL, NULL); }

// TODO: 此函数用于mini.y
SYM *declare_func(const char *name, const int type) {
    SYM *sym = NULL;

    sym = lookup_sym(sym_tab_global, name);

    /* name used before declared */
    if (sym != NULL) {
        if (sym->type == SYM_FUNC) {
            error("func already declared");
            return NULL;
        }

        if (sym->type != SYM_UNDEF) {
            error("func name already used");
            return NULL;
        }

        return sym;
    }

    sym = mk_sym();
    sym->type = SYM_FUNC;
    sym->name = strdup(name);
    sym->value_type = type;
    sym->value_size = get_size_of_type(type);
    sym->address = NULL;

    insert_sym(&sym_tab_global, sym);
    return sym;
}

TAC *do_assign(SYM *var, const EXP *exp) {
    if (var->type != SYM_VAR)
        error("assignment to non-variable");

    TAC *code = mk_tac(TAC_COPY, var, exp->ret, NULL);
    code->prev = exp->tac;

    return code;
}

TAC *do_store(SYM *dest, const EXP *exp, const EXP *offset)
{
    if (dest->type != SYM_VAR || !dest->indirection)
        error("assignment to non-pointer");

    if (offset == NULL) {
        return join_tac(exp->tac, mk_tac(TAC_STORE, dest, exp->ret, NULL));
    } else {
        return join_tac(join_tac(exp->tac, offset->tac), mk_tac(TAC_STORE, dest, exp->ret, offset->ret));
    }
}

TAC *do_input(SYM *var) {
    if (var->type != SYM_VAR)
        error("input to non-variable");

    TAC *code = mk_tac(TAC_INPUT, var, NULL, NULL);

    return code;
}

TAC *do_output(EXP* exp)
{
    TAC *code = mk_tac(TAC_OUTPUT, exp->ret, NULL, NULL);
    code->prev = exp->tac;

    free(exp);
    return code;
}

static int do_implicit_type_conversion(const int type1, const int type2) {
    if (type1 == type2)
        return type1;
    const int max_type = type1 > type2 ? type1 : type2;
    if (max_type <= SYM_VAL_MAX_INTEGER) {
        return max_type;
    } else {
        // TODO: support float number
        return SYM_VAL_MAX_INTEGER;
    }
}

EXP *do_bin(const int binop, EXP *exp1, EXP *exp2) {
    /*
    if((exp1->ret->type==SYM_INT) && (exp2->ret->type==SYM_INT))
    {
        int newval;

        switch(binop)
        {
            case TAC_ADD:
            newval=exp1->ret->value + exp2->ret->value;
            break;

            case TAC_SUB:
            newval=exp1->ret->value - exp2->ret->value;
            break;

            case TAC_MUL:
            newval=exp1->ret->value * exp2->ret->value;
            break;

            case TAC_DIV:
            newval=exp1->ret->value / exp2->ret->value;
            break;
        }

        exp1->ret=mk_const(newval);

        return exp1;
    }
    */

    if (!exp1 || !exp2 || !exp1->ret || !exp2->ret) {
        error("unexpected arguments!");
        return exp1;
    }

    /* TAC code for temp symbol */
    TAC *temp = mk_tac(
        TAC_VAR,
        mk_tmp(do_implicit_type_conversion(exp1->ret->value_type, exp2->ret->value_type)),
        NULL,
        NULL);

    temp->prev = join_tac(exp1->tac, exp2->tac);

    /* TAC code for result */
    TAC *ret = mk_tac(binop, temp->a, exp1->ret, exp2->ret);
    ret->prev = temp;

    exp1->ret = temp->a;
    exp1->tac = ret;

    return exp1;
}

EXP *do_deref(EXP *exp1, EXP *exp2) {
    if (!exp2) {
        return do_un(TAC_DEREF, exp1);
    }
    // 对a[c][b]的多维数组取值做特殊处理,exp1: a[c], exp2: [b]
    // 满足exp1返回一个数组,且该数组由a[b]得到
    if (exp1->ret->dim_size != NULL && exp1->tac && exp1->tac->op == TAC_DEREF && exp1->tac->c != NULL) {
        // 常量计算
        // 除到最后value_size会自动回到定义该变量时的根据value_type设置的value_size
        exp1->ret->value_size /= exp1->ret->dim_size->size;
        exp1->ret->dim_size = exp1->ret->dim_size->next;
        exp1->ret->indirection -= 1;
        if (exp1->tac->c->type == SYM_CONST && exp2->ret->type == SYM_CONST) {
            // a[1][2] -> a[1+2]
            exp1->tac->c = mk_const(exp1->tac->c->value + exp2->ret->value * exp1->ret->value_size, SYM_VAL_SIZE);
        } else {
            EXP *mul_exp = do_bin(TAC_MUL, exp2, mk_exp(NULL, mk_const(exp1->ret->value_size, SYM_VAL_SIZE), NULL));
            EXP *add_exp = do_bin(TAC_ADD, mk_exp(NULL, exp1->tac->c, NULL), mul_exp);
            exp1->tac->c = add_exp->ret;
            exp1->tac->prev = join_tac(exp1->tac->prev, add_exp->tac);
        }
        return exp1;
    }

    // temp = *(a+b) = a[b]
    TAC *temp = mk_tac(TAC_VAR, mk_tmp(exp1->ret->value_type), NULL, NULL);
    temp->a->indirection = exp1->ret->indirection - 1;
    if (exp1->ret->dim_size != NULL) {
        temp->a->dim_size = exp1->ret->dim_size->next;
        if (temp->a->dim_size == NULL) {
            temp->a->value_size = get_size_of_type_or_pointer(temp->a->value_type, temp->a->indirection);
        } else
        temp->a->value_size = exp1->ret->value_size / exp1->ret->dim_size->size;
    } else {
        temp->a->dim_size = NULL;
        temp->a->value_size = get_size_of_type_or_pointer(temp->a->value_type, temp->a->indirection>0);
    }
    if (exp2->ret->type == SYM_CONST) {
        // 生成一个新的常量,不能修改原常量
        exp2->ret = mk_const(exp2->ret->value * (temp->a->value_size), SYM_VAL_SIZE);
    } else {
        TAC *tmp_const = mk_tac(TAC_VAR, mk_tmp(SYM_VAL_SIZE), NULL, NULL);
        TAC *calc_dim_tac = join_tac(tmp_const, mk_tac(TAC_MUL, tmp_const->a, exp2->ret, mk_const(temp->a->value_size, SYM_VAL_SIZE)));
        exp2->tac = join_tac(exp2->tac, calc_dim_tac);
        exp2->ret = exp2->tac->a;
    }
    temp->prev = join_tac(exp2->tac, exp1->tac);
    temp = join_tac(temp, mk_tac(TAC_DEREF, temp->a, exp1->ret, exp2->ret));
    exp1->ret = temp->a;
    exp1->tac = temp;
    return exp1;
}

EXP *do_cmp(const int binop, EXP *exp1, const EXP *exp2) {
    /* TAC code for temp symbol */
    TAC *temp = mk_tac(TAC_VAR, mk_tmp(do_implicit_type_conversion(exp1->ret->value_type, exp2->ret->value_type)), NULL,
                       NULL);
    temp->prev = join_tac(exp1->tac, exp2->tac);

    /* TAC code for result */
    TAC *ret = mk_tac(binop, temp->a, exp1->ret, exp2->ret);
    ret->prev = temp;

    exp1->ret = temp->a;
    exp1->tac = ret;

    return exp1;
}

EXP *do_un(const int unop, EXP *exp) {
    /* TAC code for temp symbol */
    SYM *tmp_sym = mk_tmp(exp->ret->value_type);
    switch (unop)
    {
        case TAC_NEG: tmp_sym->value_size = exp->ret->value_size; break;
        case TAC_ADDR:
            tmp_sym->value_size = POINTER_SIZE;
            tmp_sym->indirection = exp->ret->indirection + 1;
            break;
        case TAC_DEREF:
            if (!exp->ret->value_size)
            {
                error("cannot dereference a non-pointer expression!");
            }
            tmp_sym->indirection = exp->ret->indirection - 1;
            if (tmp_sym->indirection)
            {
                tmp_sym->value_size = POINTER_SIZE;
            }
            // else 本来就会根据value_type自动设置value_size,保持原状即可
            // 对数组做处理
            if (exp->ret->dim_size != NULL) {
                tmp_sym->dim_size = exp->ret->dim_size->next;
                tmp_sym->value_size = exp->ret->value_size / exp->ret->dim_size->size;
            } else {
                tmp_sym->dim_size = NULL;
            }
            break;
        default: break;
    }
    TAC *temp = mk_tac(TAC_VAR, tmp_sym, NULL, NULL);

    temp->prev = exp->tac;

    /* TAC code for result */
    TAC *ret = mk_tac(unop, temp->a, exp->ret, NULL);
    ret->prev = temp;

    exp->ret = temp->a;
    exp->tac = ret;

    return exp;
}

TAC *do_call(const char *name, EXP *arglist) {
    EXP *alt; /* For counting args */
    TAC *code = NULL; /* Resulting code */
    TAC *temp; /* Temporary for building code */

    for (alt = arglist; alt != NULL; alt = alt->next)
        code = join_tac(code, alt->tac);

    while (arglist != NULL) /* Generate ARG instructions */
    {
        temp = mk_tac(TAC_ACTUAL, arglist->ret, NULL, NULL);
        temp->prev = code;
        code = temp;

        alt = arglist->next;
        arglist = alt;
    };

    // TODO: 为什么是(SYM *)strdup(name)?
    temp = mk_tac(TAC_CALL, NULL, (SYM *) strdup(name), NULL);
    temp->prev = code;
    code = temp;

    return code;
}

EXP *do_call_ret(const char *name, EXP *arglist) {
    /* Where function result will go */
    SYM *ret = mk_tmp(SYM_VAL_INT); /* For the result */
    /* Resulting code */
    TAC *code = mk_tac(TAC_VAR, ret, NULL, NULL);

    EXP *alt; /* For counting args */
    for (alt = arglist; alt != NULL; alt = alt->next)
        code = join_tac(code, alt->tac);

    TAC *temp; /* Temporary for building code */
    while (arglist != NULL) /* Generate ARG instructions */
    {
        temp = mk_tac(TAC_ACTUAL, arglist->ret, NULL, NULL);
        temp->prev = code;
        code = temp;

        alt = arglist->next;
        arglist = alt;
    }

    temp = mk_tac(TAC_CALL, ret, (SYM *) strdup(name), NULL);
    temp->prev = code;
    code = temp;

    return mk_exp(NULL, ret, code);
}

char *mk_lstr(int i) {
    char lstr[10] = "L";
    sprintf(lstr, "L%d", i);
    return (strdup(lstr));
}

TAC *do_if(const EXP *exp, TAC *stmt) {
    TAC *label = mk_tac(TAC_LABEL, mk_label(mk_lstr(next_label++)), NULL, NULL);
    TAC *code = mk_tac(TAC_IFZ, label->a, exp->ret, NULL);

    code->prev = exp->tac;
    code = join_tac(code, stmt);
    label->prev = code;

    return label;
}

TAC *do_test(const EXP *exp, TAC *stmt1, TAC *stmt2) {
    TAC *label1 = mk_tac(TAC_LABEL, mk_label(mk_lstr(next_label++)), NULL, NULL);
    TAC *label2 = mk_tac(TAC_LABEL, mk_label(mk_lstr(next_label++)), NULL, NULL);
    TAC *code1 = mk_tac(TAC_IFZ, label1->a, exp->ret, NULL);
    TAC *code2 = mk_tac(TAC_GOTO, label2->a, NULL, NULL);

    code1->prev = exp->tac; /* Join the code */
    code1 = join_tac(code1, stmt1);
    code2->prev = code1;
    label1->prev = code2;
    label1 = join_tac(label1, stmt2);
    label2->prev = label1;

    return label2;
}

TAC *do_while(const EXP *exp, TAC *stmt) {
    TAC *label = mk_tac(TAC_LABEL, mk_label(mk_lstr(next_label++)), NULL, NULL);
    TAC *code = mk_tac(TAC_GOTO, label->a, NULL, NULL);

    code->prev = stmt; /* Bolt on the goto */

    return join_tac(label, do_if(exp, code));
}

SYM *get_var(const char *name) {
    SYM *sym = NULL; /* Pointer to looked up symbol */

    if (scope)
        sym = lookup_sym(sym_tab_local, name);

    if (sym == NULL)
        sym = lookup_sym(sym_tab_global, name);

    if (sym == NULL) {
        error("name not declared as local/global variable");
        return NULL;
    }

    if (sym->type != SYM_VAR) {
        error("not a variable");
        return NULL;
    }

    return sym;
}

EXP *mk_exp(EXP *next, SYM *ret, TAC *code) {
    EXP *exp = (EXP *) malloc(sizeof(EXP));

    exp->next = next;
    exp->ret = ret;
    exp->tac = code;

    return exp;
}

SYM *mk_text(const char *text) {
    SYM *sym = NULL; /* Pointer to looked up symbol */

    sym = lookup_sym(sym_tab_global, text);

    /* text already used */
    if (sym != NULL) {
        return sym;
    }

    /* text unseen before */
    sym = mk_sym();
    sym->type = SYM_TEXT;
    sym->name = strdup(text);
    sym->value_type = SYM_VAL_TEXT;
    sym->value_size = get_size_of_type(SYM_VAL_TEXT);
    sym->label = next_label++;

    insert_sym(&sym_tab_global, sym);
    return sym;
}

SYM *mk_const(const int n, const int type) {
    SYM *sym = NULL;

    char name[10];
    sprintf(name, "%d", n);

    sym = lookup_sym(sym_tab_global, name);
    if (sym != NULL) {
        return sym;
    }

    sym = mk_sym();
    sym->type = SYM_CONST;
    sym->value = n;
    sym->value_type = type;
    sym->value_size = get_size_of_type(type);
    sym->name = strdup(name);
    insert_sym(&sym_tab_global, sym);

    return sym;
}

static char *to_str(const SYM *s, char *str) {
    if (s == NULL)
        return "NULL";


    switch (s->type) {
        case SYM_FUNC:
        case SYM_VAR:
            /* Just return the name */
            return s->name;

        case SYM_TEXT:
            /* Put the address of the text */
            sprintf(str, "L%d", s->label);
            return str;

        case SYM_CONST:
            /* Convert the number to string */
            sprintf(str, "%d", s->value);
            return str;

        default:
            /* Unknown arg type */
            error("unknown TAC arg type");
            return "?";
    }
}

void out_str(FILE *f, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(f, format, args);
    va_end(args);
}

void out_sym(FILE *f, const SYM *s) { out_str(f, "%p\t%s", s, s->name); }

void out_tac(FILE *f, const TAC *i) {
    char sa[tmp_name_len + 1]; /* For text of TAC args */
    char sb[tmp_name_len + 1];
    char sc[tmp_name_len + 1];

    switch (i->op) {
        case TAC_UNDEF:
            fprintf(f, "undef");
            break;

        case TAC_ADD:
            fprintf(f, "%s = %s + %s", to_str(i->a, sa), to_str(i->b, sb), to_str(i->c, sc));
            break;

        case TAC_SUB:
            fprintf(f, "%s = %s - %s", to_str(i->a, sa), to_str(i->b, sb), to_str(i->c, sc));
            break;

        case TAC_MUL:
            fprintf(f, "%s = %s * %s", to_str(i->a, sa), to_str(i->b, sb), to_str(i->c, sc));
            break;

        case TAC_DIV:
            fprintf(f, "%s = %s / %s", to_str(i->a, sa), to_str(i->b, sb), to_str(i->c, sc));
            break;

        case TAC_EQ:
            fprintf(f, "%s = (%s == %s)", to_str(i->a, sa), to_str(i->b, sb), to_str(i->c, sc));
            break;

        case TAC_NE:
            fprintf(f, "%s = (%s != %s)", to_str(i->a, sa), to_str(i->b, sb), to_str(i->c, sc));
            break;

        case TAC_LT:
            fprintf(f, "%s = (%s < %s)", to_str(i->a, sa), to_str(i->b, sb), to_str(i->c, sc));
            break;

        case TAC_LE:
            fprintf(f, "%s = (%s <= %s)", to_str(i->a, sa), to_str(i->b, sb), to_str(i->c, sc));
            break;

        case TAC_GT:
            fprintf(f, "%s = (%s > %s)", to_str(i->a, sa), to_str(i->b, sb), to_str(i->c, sc));
            break;

        case TAC_GE:
            fprintf(f, "%s = (%s >= %s)", to_str(i->a, sa), to_str(i->b, sb), to_str(i->c, sc));
            break;

        case TAC_NEG:
            fprintf(f, "%s = - %s", to_str(i->a, sa), to_str(i->b, sb));
            break;

        case TAC_ADDR:
            fprintf(f, "%s = & %s", to_str(i->a, sa), to_str(i->b, sb));
            break;

        case TAC_DEREF:
            if (!i->c) fprintf(f, "%s = * %s", to_str(i->a, sa), to_str(i->b, sb));
            else fprintf(f, "%s = %s[%s]", to_str(i->a, sa), to_str(i->b, sb), to_str(i->c, sc));
            break;

        case TAC_STORE:
            if (!i->c)
            fprintf(f, "*%s = %s", to_str(i->a, sa), to_str(i->b, sb));
            else
                fprintf(f, "%s[%s] = %s", to_str(i->a, sa), to_str(i->c, sc), to_str(i->b, sb));
            break;

        case TAC_COPY:
            fprintf(f, "%s = %s", to_str(i->a, sa), to_str(i->b, sb));
            break;

        case TAC_GOTO:
            if (i->a)
                fprintf(f, "goto %s", i->a->name);
            break;

        case TAC_IFZ:
            if (i->a)
                fprintf(f, "ifz %s goto %s", to_str(i->b, sb), i->a->name);
            break;

        case TAC_ACTUAL:
            fprintf(f, "actual %s", to_str(i->a, sa));
            break;

        case TAC_FORMAL:
            fprintf(f, "formal %s", to_str(i->a, sa));
            break;

        case TAC_CALL:
            if (i->a == NULL)
                fprintf(f, "call %s", (char *) i->b);
            else
                fprintf(f, "%s = call %s", to_str(i->a, sa), (char *) i->b);
            break;

        case TAC_INPUT:
            fprintf(f, "input %s", to_str(i->a, sa));
            break;

        case TAC_OUTPUT:
            fprintf(f, "output %s", to_str(i->a, sa));
            break;

        case TAC_RETURN:
            fprintf(f, "return %s", to_str(i->a, sa));
            break;

        case TAC_LABEL:
            if (i->a)
                fprintf(f, "label %s", i->a->name);
            break;

        case TAC_VAR:
            if (i->a)
            {
                switch (i->a->value_type)
                {
                    case SYM_VAL_BOOL:
                        fprintf(f, "bool ");break;
                    case SYM_VAL_CHAR:
                        fprintf(f, "char ");break;
                    case SYM_VAL_INT:
                        fprintf(f, "int ");break;
                    default:break;
                }
                if (i->a->dim_size == NULL || i->a->indirection-i->a->dim_size->level>1)
                {
                    const int indirection = i->a->dim_size ? i->a->indirection-i->a->dim_size->level - 1 : i->a->indirection;
                    for (int j = 0; j < indirection; j++)
                    {
                        fprintf(f, "*");
                    }
                }
            }
            fprintf(f, "%s", to_str(i->a, sa));
            if (i->a && i->a->dim_size)
            {
                const struct array_dim_size *dim = i->a->dim_size;
                while (dim != NULL)
                {
                    if (dim->size > 0) fprintf(f, "[%d]", dim->size);
                    else fprintf(f, "[]");
                    dim = dim->next;
                }
            }
            if (i->a) {
                fprintf(f, "(size=%d)", i->a->value_size);
            }
            break;

        case TAC_BEGINFUNC:
            fprintf(f, "begin");
            break;

        case TAC_ENDFUNC:
            fprintf(f, "end");
            break;

        default:
            error("unknown TAC opcode");
            break;
    }
}

int get_size_of_type(const int type) {
#ifndef NEW_ASM
    return 4;
#endif
    switch (type) {
        case SYM_VAL_BOOL:
            return BOOL_SIZE;
        case SYM_VAL_CHAR:
            return CHAR_SIZE;
        case SYM_VAL_INT:
            return INT_SIZE;
        default:
            return -1;
    }
}
