#include "tac.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "obj.h"

/* global var */
int scope, next_tmp, next_label;
TAC *tac_first, *tac_last;

static const int default_hash_tab_size = 20;
static const double default_hash_tab_max_capacity = 2.0/3.0;

static unsigned int hash_str(const char *s) {
    static const unsigned long long base = 131;
    static const unsigned long long mod = (int)1e9+7;
    unsigned long long hash = 0;
    for (const char *c = s; *c; c++) {
        hash = (hash * base + *c) % mod;
    }
    return hash;
}

typedef struct {
    char **keys;
    SYM **values;
    size_t num;
    size_t size;
} HASH_TABLE;

static HASH_TABLE sym_hash_global = {}, sym_hash_local = {}, sym_hash_struct = {};

static SYM **insert_hash_idx(HASH_TABLE *table, const char *name) {
    if (!name || *name == '\0') {
        error("empty string!");
        return NULL;
    }
    if (!table->size) {
        table->keys = (char **) malloc(sizeof(char *) * default_hash_tab_size);
        table->values = (SYM **) malloc(sizeof(SYM*) * default_hash_tab_size);
        memset(table->keys, 0, sizeof(char *) * default_hash_tab_size);
        memset(table->values, 0, sizeof(SYM*) * default_hash_tab_size);
        table->num = 0;
        table->size = default_hash_tab_size;
    } else if ((double)table->num/(double)table->size >= default_hash_tab_max_capacity) {
        HASH_TABLE new_table = {
            (char **) malloc(sizeof(char *) * table->size * 2),
            (SYM **) malloc(sizeof(SYM*) * table->size * 2),
            0, table->size * 2
        };
        for (size_t i=0; i<table->size; i++) {
            if (table->keys[i]) {
                SYM **new_sym = insert_hash_idx(&new_table, table->keys[i]);
                *new_sym = table->values[i];
            }
        }
        free(table->keys);
        free(table->values);
        *table = new_table;
    }
    const unsigned int hash = hash_str(name);
    for (unsigned int i=hash%table->size; i!=(hash+table->size-1)%table->size; i=(i+1)%table->size) {
        if (!table->keys[i]) {
            table->keys[i] = strdup(name);
            table->num++;
            table->values[i] = (SYM *) malloc(sizeof(SYM));
            return &table->values[i];
        }
        if (strcmp(table->keys[i], name) == 0) {
            return &table->values[i];
        }
    }
    assert(0);
}

static SYM *insert_hash(HASH_TABLE *table, const char *name) {
    if (!name || *name == '\0') {
        error("empty string!");
        return NULL;
    }
    return *insert_hash_idx(table, name);
}

static SYM *lookup_hash(const HASH_TABLE *table, const char *name) {
    if (table->size == 0 || !table->keys)
        return NULL;
    const unsigned int hash = hash_str(name);
    for (unsigned int i=hash%table->size; i!=(hash+table->size-1)%table->size; i=(i+1)%table->size) {
        if (!table->keys[i]) {
            return NULL;
        }
        if (strcmp(table->keys[i], name) == 0) {
            return table->values[i];
        }
    }
    return NULL;
}

static void clear_hash(HASH_TABLE *table) {
    if (table->keys) {
        for (size_t i=0; i<table->size; i++) {
            if (table->keys[i]) {
                // free 由 strdup 生成的字符串
                free(table->keys[i]);
            }
        }
        free(table->keys);
    }
    // 可以安全地 free values 数组,其保存的是指针, free 数组本身并不会 free 那些 sym 指针
    if (table->values) free(table->values);
    table->keys = NULL;
    table->values = NULL;
    table->num = table->size = 0;
}

void clear_local_hash() {
    clear_hash(&sym_hash_local);
}

static SYM *cp_var(SYM *a, const SYM *b) {
    a->value_size = b->value_size;
    a->value_type = b->value_type;
    a->indirection = b->indirection;
    a->struct_sym = b->struct_sym;
    a->dim_size = b->dim_size;
    a->type = b->type;
    return a;
}

void tac_init(void) {
    scope = 0;
    next_tmp = 0;
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

static SYM *mk_sym(void) {
    SYM *t = (SYM *) malloc(sizeof(SYM));
    t->scope = scope;
    t->indirection = 0;
    t->dim_size = t->etc = t->name = NULL;
    t->value = t->value_size = t->value_type = 0;
    return t;
}

SYM *mk_var(const char *name, const int type) {
    SYM *sym = NULL;

    if (scope == SCOPE_LOCAL){
        sym = lookup_hash(&sym_hash_local, name);
    }
    else {
        sym = lookup_hash(&sym_hash_global, name);
    }

    /* var already declared */
    if (sym != NULL) {
        error("variable already declared");
        return NULL;
    }

    /* var unseen before, set up a new symbol table node, insert_sym it into the symbol table. */
    if (scope == SCOPE_LOCAL) {
        sym = insert_hash(&sym_hash_local, name);
    } else {
        sym = insert_hash(&sym_hash_global, name);
    }
    sym->type = SYM_VAR;
    sym->scope = scope;
    sym->name = strdup(name);
    sym->value_type = type;
    sym->value_size = get_size_of_type(type, -1); // the size of a pointer is 4
    sym->offset = -1; /* Unset address */
    sym->value = 0;
    sym->indirection = 0;
    sym->label = 0;
    sym->next = NULL;
    sym->address = NULL;
    sym->dim_size = NULL;
    sym->etc = NULL;
    sym->color = sym->register_id = 0;

    return sym;
}

TAC *mk_break(void) {
    SYM *t = mk_sym();
    t->type = SYM_LABEL;
    t->name = NULL;
    t->value_type = SYM_LABEL_BREAK;
    return mk_tac(TAC_GOTO, t, NULL, NULL);
}

TAC *mk_continue(void) {
    SYM *t = mk_sym();
    t->type = SYM_LABEL;
    t->name = NULL;
    t->value_type = SYM_LABEL_CONTINUE;
    return mk_tac(TAC_GOTO, t, NULL, NULL);
}

TAC *mk_case(SYM *sym) {
    return mk_tac(TAC_LABEL, sym, NULL, NULL);
}

TAC *mk_default() {
    SYM *s = mk_sym();
    s->type = SYM_CONST;
    s->name = NULL;
    s->value_type = SYM_LABEL_DEFAULT;
    return mk_tac(TAC_LABEL, s, NULL, NULL);
}

TAC *mk_struct_vars(const char *struct_name, TAC *tac) {
    const SYM *struct_sym = lookup_hash(&sym_hash_struct, struct_name);
    if (!struct_sym || !tac) {
        error("unknown struct name '%s'", struct_name);
        return NULL;
    }
    for (const TAC *t=tac; t && t->op == TAC_VAR; t=t->prev) {
        t->a->type = SYM_VAR;
        t->a->value_type = SYM_VAL_STRUCT;
        t->a->struct_sym = (SYM *)struct_sym;
        if (t->a->indirection > 0) {
            if (t->a->dim_size == NULL) {
                // 如果a是普通指针
                t->a->value_size = POINTER_SIZE;
            } else {
                t->a->value_size = t->a->value_size / get_size_of_type(SYM_VAL_DEFAULT, -1) *
                    (is_pointer(t->a) ? POINTER_SIZE : struct_sym->value_size);
            }
        } else {
            t->a->value_size = struct_sym->value_size;
        }
    }
    return tac;
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

// void free_sym(SYM *sym, const SYM **symtab)
// {
//     if (!sym) return;
//     if (symtab && *symtab)
//     {
//         if (strcmp((*symtab)->name, sym->name) == 0)
//             *symtab = (*symtab)->next;
//         else
//         {
//             SYM *t = (*symtab)->next;
//             while (t->next)
//             {
//                 if (strcmp(t->next->name, sym->name) == 0)
//                     break;
//                 t = t->next;
//             }
//             if (t->next)
//                 t->next = t->next->next;
//         }
//     }
//     free(sym->name);
//     free(sym);
// }

SYM *mk_dim(SYM *sym, const int size)
{
    if (sym->dim_size == NULL)
    {
        sym->value_size = get_size_of_type(sym->value_type, sym->value_size) * size;
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

SYM *declare_struct(const char *name) {
    SYM *sym = lookup_hash(&sym_hash_global, name);
    if (sym != NULL) {
        if (sym->type == SYM_STRUCT) {
            error("struct already declared");
            return NULL;
        }
        if (sym->type != SYM_UNDEF) {
            error("struct name already used");
            return NULL;
        }
        return sym;
    }
    sym = insert_hash(&sym_hash_global, name);
    sym->type = SYM_STRUCT;
    sym->name = strdup(name);
    sym->value_size = 0;
    return sym;
}

void do_struct(SYM *sym, TAC *declarations) {
    sym->next = sym->struct_sym = NULL;
    sym->value_size = 0;
    for (const TAC *t = declarations; t; t = t->prev) {
        if (t->op != TAC_VAR || !t->a) {
            error("cannot use non-declaration in a struct block!");
            return;
        }
        SYM *child = t->a;
        sym->value_size += child->value_size;
        child->next = sym->struct_sym;
        // child->struct_sym = sym;
        sym->struct_sym = child;
    }
    // 地址对齐!!!
    // 这里我们的实现与标准c语言不同,由于我们最后会直接通过地址操作拿到成员,所以每个成员的地址都必须与POINTER_SIZE对齐
    int offset = 0;
    for (SYM *child = sym->struct_sym; child && child->next; child = child->next) {
        offset += child->value_size;
        if (offset % POINTER_SIZE) {
            SYM *padding = mk_sym();
            padding->value_type = SYM_VAL_CHAR;
            padding->type = SYM_VAR;
            padding->name = NULL;
            padding->dim_size = NULL;
            padding->indirection = 0;
            padding->struct_sym = NULL;
            mk_dim(padding, POINTER_SIZE - offset % POINTER_SIZE);
            padding->next = child->next;
            child->next = padding;
        }
    }
    if (lookup_hash(&sym_hash_struct, sym->name) != NULL) {
        error("cannot redeclare struct!");
        return;
    }
    *insert_hash_idx(&sym_hash_struct, sym->name) = sym;
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

    sym = lookup_hash(&sym_hash_global, name);

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

    sym = insert_hash(&sym_hash_global, name);
    sym->type = SYM_FUNC;
    sym->name = strdup(name);
    sym->value_type = type;
    sym->value_size = get_size_of_type(type, -1);
    sym->address = NULL;

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
    if (!exp1 || !exp1->ret)
        return exp1;
    // // 如果exp1的返回值是取地址b=&a,那直接做加法/返回a
    // if (exp1->tac && exp1->tac->op == TAC_ADDR && exp1->tac->a && exp1->tac->b && exp1->tac->a == exp1->ret) {
    //     exp1->ret = exp1->tac->b;
    //     // if (exp1->tac->prev && exp1->tac->prev->op == TAC_VAR && exp1->tac->prev->a == exp1->tac->a) {
    //     //     exp1->tac = exp1->tac->prev->prev;
    //     // } else
    //         exp1->tac = exp1->tac->prev;
    //     if (!exp2) {
    //         return exp1;
    //     } else {
    //         TAC *tac = declare_var(mk_tmp(SYM_VAL_DEFAULT));
    //         tac = join_tac(tac, mk_tac(TAC_ADD, tac->a, exp1->ret, exp2->ret));
    //         exp1->ret = cp_var(tac->a, exp1->ret);
    //         exp1->tac = join_tac(exp1->tac, exp2->tac);
    //         exp1->tac = join_tac(exp1->tac, tac);
    //         return exp1;
    //     }
    // }
    if (!exp2) {
        return do_un(TAC_DEREF, exp1);
    }
    if (exp1->ret != NULL && exp1->tac && exp1->tac->op == TAC_DEREF) {
        // 对a[c][b]的多维数组取值做特殊处理,exp1: a[c], exp2: [b]
        // 满足exp1返回一个数组,且该数组由a[b]得到
        if (is_array(exp1->ret)) {
            // 常量计算
            // 除到最后value_size会自动回到定义该变量时的根据value_type设置的value_size
            exp1->ret->value_size /= exp1->ret->dim_size->size;
            exp1->ret->dim_size = exp1->ret->dim_size->next;
            exp1->ret->indirection -= 1;
        }
        if (exp1->tac->c && exp1->tac->c->type == SYM_CONST && exp2->ret->type == SYM_CONST) {
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
    cp_var(temp->a, exp1->ret);
    temp->a->indirection = exp1->ret->indirection - 1;
    if (exp1->ret->dim_size != NULL) {
        temp->a->dim_size = exp1->ret->dim_size->next;
        if (temp->a->dim_size == NULL) {
            temp->a->value_size = get_size_of_type_or_pointer(temp->a->value_type, exp1->ret->value_size/exp1->ret->dim_size->size, temp->a->indirection);
        } else
        temp->a->value_size = exp1->ret->value_size / exp1->ret->dim_size->size;
    } else {
        temp->a->dim_size = NULL;
        temp->a->value_size = get_size_of_type_or_pointer(temp->a->value_type, exp1->ret->value_size, temp->a->indirection>0);
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
    // 对TAC_ADDR做特判
    if (unop == TAC_ADDR && exp && exp->tac && exp->tac->op == TAC_DEREF && exp->tac->a == exp->ret) {
        SYM *c = exp->tac->c;
        exp->ret = exp->tac->b;
        if (exp->tac->prev && exp->tac->prev->op == TAC_VAR && exp->tac->prev->a == exp->tac->a) {
            exp->tac = exp->tac->prev->prev;
        } else
            exp->tac = exp->tac->prev;
        if (c) {
            TAC *tac = declare_var(mk_tmp(SYM_VAL_DEFAULT));
            tac = join_tac(tac, mk_tac(TAC_ADD, tac->a, exp->ret, c));
            tac = exp->tac = join_tac(exp->tac, tac);
            exp->ret = cp_var(tac->a, exp->ret);
            // 如果是&a[b],我们需要做特殊处理,当exp->tac->b是数组时需要擦除返回值的一维数组信息
            // 例如:有int arr[10][20],语句&arr[1]的返回值应该是int *(arr_p[20])而非int arr[10][20]
            // 这里我们直接让exp->ret->dim_size移到下一维即可
            if (exp->ret->dim_size) {
                exp->ret->value_size /= exp->ret->dim_size->size;
                exp->ret->dim_size = exp->ret->dim_size->next;
            }
            return exp;
        } else {
            return exp;
        }
    }
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
    tmp_sym->struct_sym = exp->ret->struct_sym;
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

const char *mk_lstr() {
    static char lstr[10];
    snprintf(lstr, 10, "Label%d", next_label++);
    return strdup(lstr);
}

const char *mk_bstr() {
    static unsigned char i;
    static char bstr[10];
    snprintf(bstr, 10, "Break%d", i++);
    return strdup(bstr);
}

const char *mk_cstr() {
    static unsigned char i;
    static char cstr[12];
    snprintf(cstr, 12, "Continue%d", i++);
    return strdup(cstr);
}

const char *mk_case_str() {
    static unsigned char i;
    static char case_str[10];
    snprintf(case_str, 10, "Case%d", i++);
    return strdup(case_str);
}

const char *mk_dstr() {
    static unsigned char i;
    static char dstr[11];
    snprintf(dstr, 11, "Default%d", i++);
    return strdup(dstr);
}

TAC *do_if(const EXP *exp, TAC *stmt) {
    TAC *label = mk_tac(TAC_LABEL, mk_label(mk_lstr()), NULL, NULL);
    TAC *code = mk_tac(TAC_IFZ, mk_label(label->a->name), exp->ret, NULL);

    code->prev = exp->tac;
    code = join_tac(code, stmt);
    label->prev = code;

    return label;
}

TAC *do_test(const EXP *exp, TAC *stmt1, TAC *stmt2) {
    TAC *label1 = mk_tac(TAC_LABEL, mk_label(mk_lstr()), NULL, NULL);
    TAC *label2 = mk_tac(TAC_LABEL, mk_label(mk_lstr()), NULL, NULL);
    TAC *code1 = mk_tac(TAC_IFZ, mk_label(label1->a->name), exp->ret, NULL);
    TAC *code2 = mk_tac(TAC_GOTO, mk_label(label2->a->name), NULL, NULL);

    code1->prev = exp->tac; /* Join the code */
    code1 = join_tac(code1, stmt1);
    code2->prev = code1;
    label1->prev = code2;
    label1 = join_tac(label1, stmt2);
    label2->prev = label1;

    return label2;
}

TAC *do_while(const EXP *exp, TAC *stmt) {
    TAC *label = mk_tac(TAC_LABEL, mk_label(mk_cstr()), NULL, NULL);
    TAC *code = mk_tac(TAC_GOTO, mk_label(label->a->name), NULL, NULL);
    TAC *break_label = NULL;

    for (const TAC *t=stmt; t; t=t->prev) {
        if (t->op == TAC_GOTO && t->a && t->a->type == SYM_LABEL && t->a->value_type) {
            if (t->a->value_type == SYM_LABEL_BREAK) {
                if (!break_label) {
                    break_label = mk_tac(TAC_LABEL, mk_label(mk_bstr()), NULL, NULL);
                }
                t->a->name = strdup(break_label->a->name);
                t->a->value = t->a->value_type = 0;
            } else if (t->a->value_type == SYM_LABEL_CONTINUE) {
                t->a->name = strdup(label->a->name);
                t->a->value = t->a->value_type = 0;
            }
        }
    }

    code->prev = stmt; /* Bolt on the goto */

    return join_tac(join_tac(label, do_if(exp, code)), break_label);
}

TAC *do_switch(const EXP *exp, TAC *stmt) {
    SYM *default_sym = NULL;
    TAC *break_tac = NULL, *start_tac = exp->tac;

    for (TAC *t=stmt; t; t=t->prev) {
        if (t->op == TAC_LABEL && t->a && t->a->type == SYM_CONST) {
            if (t->a->value_type == SYM_LABEL_DEFAULT) {
                if (default_sym) {
                    error("cannot redeclare default label!");
                    return stmt;
                }
                t->a = mk_label(mk_dstr());
                default_sym = mk_label(t->a->name);
            } else {
                SYM *case_value = t->a;
                t->a = mk_label(mk_case_str());
                // IFZ是条件为真时通过,条件为假时跳转
                EXP *case_exp = do_cmp(TAC_NE, mk_exp(NULL, exp->ret, NULL), mk_exp(NULL, case_value, NULL));
                case_exp->tac = join_tac(case_exp->tac, mk_tac(TAC_IFZ, mk_label(t->a->name), case_exp->ret, NULL));
                start_tac = join_tac(start_tac, case_exp->tac);
            }
        } else if (t->op == TAC_GOTO && t->a && t->a->type == SYM_LABEL && t->a->value_type == SYM_LABEL_BREAK) {
            if (!break_tac) {
                break_tac = mk_tac(TAC_LABEL, mk_label(mk_bstr()), NULL, NULL);
            }
            t->a->name = strdup(break_tac->a->name);
            t->a->value = t->a->value_type = 0;
        }
    }
    if (default_sym) {
        start_tac = join_tac(start_tac, mk_tac(TAC_GOTO, default_sym, NULL, NULL));
    } else {
        if (!break_tac) {
            break_tac = mk_tac(TAC_LABEL, mk_label(mk_bstr()), NULL, NULL);
        }
        start_tac = join_tac(start_tac, mk_tac(TAC_GOTO, mk_label(break_tac->a->name), NULL, NULL));
    }
    return join_tac(join_tac(start_tac, stmt), break_tac);
}


EXP *do_get_member(EXP *exp, const char *name) {
    if (!exp || !exp->ret) {
        error("invalid arguments");
        return exp;
    }
    if (exp->ret->value_type != SYM_VAL_STRUCT || !exp->ret->struct_sym || exp->ret->indirection) {
        error("cannot get member of a non-struct!");
        return exp;
    }
    return do_pointer_get_member(do_un(TAC_ADDR, exp), name);
}

/*
 * 本函数会把形如a->name翻译成*(a+offset)
 */
EXP *do_pointer_get_member(EXP *exp, const char *name) {
    if (!exp || !exp->ret || !name || !*name) {
        error("invalid arguments");
        return exp;
    }
    // 传入的必须是一个纯指针,不能是数组
    if (exp->ret->value_type != SYM_VAL_STRUCT || !exp->ret->struct_sym || !is_pointer(exp->ret)) {
        error("cannot get member of a non-struct pointer!");
        return exp;
    }
    size_t offset = 0;
    const SYM *s=exp->ret->struct_sym->struct_sym;
    for (; s; s=s->next) {
        if (s->name && strcmp(s->name, name) == 0) {
            break;
        }
        offset += s->value_size;
    }
    if (!s) {
        error("invalid member name '%s' in struct '%s'!", name, exp->ret->struct_sym->name);
        return exp;
    }
    // 这里我们要重写数据类型
    // exp->tac = join_tac(exp->tac, declare_var(mk_tmp(s->value_type)));
    SYM *tmp_sym = exp->tac->a;
    tmp_sym->indirection = s->indirection+1;
    tmp_sym->value_type = s->value_type;
    tmp_sym->value_size = s->value_size;
    tmp_sym->dim_size = s->dim_size;
    tmp_sym->struct_sym = s->struct_sym;
    // exp->tac = do_assign(tmp_sym, exp);
    // exp->ret = tmp_sym;
    EXP * result_exp = do_deref(exp, mk_exp(NULL, mk_const((int) offset, SYM_VAL_SIZE), NULL));
    // 再将结果还原为原来的类型
    result_exp->ret = cp_var(result_exp->ret, s);
    result_exp->tac->c = mk_const((int) offset, SYM_VAL_SIZE);
    return result_exp;
}

SYM *get_var(const char *name) {
    SYM *sym = NULL; /* Pointer to looked up symbol */

    if (scope == SCOPE_LOCAL)
        // sym = lookup_sym(sym_tab_local, name);
        sym = lookup_hash(&sym_hash_local, name);

    if (sym == NULL)
        // sym = lookup_sym(sym_tab_global, name);
        sym = lookup_hash(&sym_hash_global, name);

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

    sym = lookup_hash(&sym_hash_global, text);
    // sym = lookup_sym(sym_tab_global, text);

    /* text already used */
    if (sym != NULL) {
        return sym;
    }

    /* text unseen before */
    sym = insert_hash(&sym_hash_global, text);
    // sym = mk_sym();
    sym->type = SYM_TEXT;
    sym->name = strdup(text);
    sym->value_type = SYM_VAL_TEXT;
    sym->value_size = get_size_of_type(SYM_VAL_TEXT, -1);
    sym->label = next_label++;

    // insert_sym(&sym_tab_global, sym);
    return sym;
}

SYM *mk_const(const int n, const int type) {
    SYM *sym = NULL;

    char name[10];
    sprintf(name, "%d", n);

    sym = lookup_hash(&sym_hash_global, name);
    // sym = lookup_sym(sym_tab_global, name);
    if (sym != NULL) {
        return sym;
    }

    sym = insert_hash(&sym_hash_global, name);
    // sym = mk_sym();
    sym->type = SYM_CONST;
    sym->value = n;
    sym->value_type = type;
    sym->value_size = get_size_of_type(type, -1);
    sym->name = strdup(name);
    // insert_sym(&sym_tab_global, sym);

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

static void out_var(FILE *f, const SYM *a) {
    char sa[tmp_name_len+1];
    if (!a) return;
    switch (a->value_type)
    {
        case SYM_VAL_BOOL:
            fprintf(f, "bool ");break;
        case SYM_VAL_CHAR:
            fprintf(f, "char ");break;
        case SYM_VAL_INT:
            fprintf(f, "int ");break;
        case SYM_VAL_STRUCT:
            fprintf(f, "struct %s ", a->struct_sym->name);break;
        default:break;
    }
    if (a->dim_size == NULL || a->indirection-a->dim_size->level>1)
    {
        const int indirection = a->dim_size ? a->indirection-a->dim_size->level - 1 : a->indirection;
        for (int j = 0; j < indirection; j++)
        {
            fprintf(f, "*");
        }
    }
    fprintf(f, "%s", to_str(a, sa));
    if (a->dim_size)
    {
        const struct array_dim_size *dim = a->dim_size;
        while (dim != NULL)
        {
            if (dim->size > 0) fprintf(f, "[%d]", dim->size);
            else fprintf(f, "[]");
            dim = dim->next;
        }
    }
    fprintf(f, "(size=%d)", a->value_size);
}

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
            out_var(f, i->a);
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

static SYM *forloop_all_sym(const HASH_TABLE *table) {
    static size_t i;
    static const HASH_TABLE *hash_table;
    if (table) {
        i = 0;
        hash_table = table;
        return NULL;
    }
    while (i<hash_table->size && !hash_table->keys[i])
        ++i;
    if (i<hash_table->size)
        return hash_table->values[i++];
    return NULL;
}

SYM *forloop_all_global_sym(const bool reset) {
    if (reset) return forloop_all_sym(&sym_hash_global);
    return forloop_all_sym(NULL);
}

void print_structs(FILE *f) {
    if (!f) return;
    forloop_all_sym(&sym_hash_struct);
    for (const SYM *sym=forloop_all_sym(NULL); sym; sym=forloop_all_sym(NULL)) {
        fprintf(f, "struct %s {", sym->name);
        if (!sym->struct_sym) {
            fprintf(f, "};\n");
            continue;
        } else fprintf(f, "\n");
        for (const SYM *child=sym->struct_sym; child; child=child->next) {
            fprintf(f, "\t");
            if (child->name)
                out_var(f, child);
            else fprintf(f, "char [%d]", child->value_size);
            fprintf(f, ";\n");
        }
        fprintf(f, "};(size=%d)\n", sym->value_size);
    }
    fprintf(f, "\n");
}

int get_size_of_type(const int type, const int default_val) {
    switch (type) {
        case SYM_VAL_BOOL:
            return BOOL_SIZE;
        case SYM_VAL_CHAR:
            return CHAR_SIZE;
        case SYM_VAL_INT:
            return INT_SIZE;
        default:
            return default_val;
    }
}

bool is_pointer(const SYM *sym) {
    if (!sym || !sym->indirection)
        return false;
    const int indirection = sym->dim_size ? sym->indirection - sym->dim_size->level - 1 : sym->indirection;
    return indirection > 0;
}

bool is_array(const SYM *sym) {
    if (!sym || !sym->indirection)
        return false;
    if (is_pointer(sym))
        return false;
    return sym->dim_size != NULL;
}
