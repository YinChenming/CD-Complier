#include "obj.h"
#include "tac.h"

#include <ctype.h>
#include <stdlib.h>
#include <time.h>

/* global var */
int tos; /* top of static */
int tof; /* top of frame */
int oof; /* offset of formal */
int oon; /* offset of next frame */
struct rdesc rdesc[R_NUM];

// 这里使用value_type是为了正确读写数组
#define STO(VALUE, REG_DESC, ...)\
    do {\
        if ((VALUE)->value_type == SYM_VAL_CHAR && !is_pointer(VALUE) && !is_array(VALUE) || (VALUE)->value_size == CHAR_SIZE) {\
            out_str(file_s, "\tSTC " REG_DESC "\n", __VA_ARGS__);\
        } else if ((VALUE)->value_type == SYM_VAL_INT || is_pointer(VALUE) || is_array(VALUE) || (VALUE)->value_size == INT_SIZE) {\
            out_str(file_s, "\tSTO " REG_DESC "\n", __VA_ARGS__);\
        } else {\
            error("unexpect value type %d\n", (VALUE)->value_type);\
        }\
    } while (0)

#define LOD(VALUE, REG_DESC, ...)\
    do {\
        if ((VALUE)->value_type == SYM_VAL_CHAR && !is_pointer(VALUE) && !is_array(VALUE)) {\
            out_str(file_s, "\tLDC " REG_DESC "\n", __VA_ARGS__);\
        } else if ((VALUE)->value_type == SYM_VAL_INT || is_pointer(VALUE) || is_array(VALUE)) {\
            out_str(file_s, "\tLOD " REG_DESC "\n", __VA_ARGS__);\
        } else {\
            error("unexpect value type %d\n", (VALUE)->value_type);\
        }\
    } while (0)

static void rdesc_clear(const int r)
{
    rdesc[r].var = NULL;
    rdesc[r].mod = 0;
}

static void rdesc_fill(const int r, SYM* s, const int mod)
{
    for (int old = R_GEN; old < R_NUM; old++)
    {
        if (rdesc[old].var == s)
        {
            rdesc_clear(old);
        }
    }

    rdesc[r].var = s;
    rdesc[r].mod = mod;
}

static void asm_write_back(const int r)
{
    if ((rdesc[r].var != NULL) && rdesc[r].mod)
    {
        if (rdesc[r].var->scope == 1) /* local var */
        {
            if (rdesc[r].var->value_type!=SYM_VAL_STRUCT){
                STO(rdesc[r].var, "(R%u+%u),R%u", R_BP, rdesc[r].var->offset, r);
                // out_str(file_s, "\tSTO (R%u+%u),R%u\n", R_BP, rdesc[r].var->offset, r);
            }
        } else /* global var */
        {
            if (rdesc[r].var->value_type!=SYM_VAL_STRUCT){
                out_str(file_s, "\tLOD R%u,STATIC\n", R_TP);
                STO(rdesc[r].var, "(R%u+%u),R%u", R_TP, rdesc[r].var->offset, r);
                //  out_str(file_s, "\tSTO (R%u+%u),R%u\n", R_TP, rdesc[r].var->offset, r);
            }
        }
        rdesc[r].mod = UNMODIFIED;
    }
}

static void asm_load(const int r, SYM* s)
{
    /* already in a reg */
    for (int i = R_GEN; i < R_NUM; i++)
    {
        if (rdesc[i].var == s)
        {
            /* load from the reg */
            if (r != i)
            {
                out_str(file_s, "\tLOD R%u,R%u\n", r, i);
            }

            /* update rdesc */
            rdesc_fill(r, s, rdesc[i].mod);
            rdesc_clear(r);
            return;
        }
    }

    /* not in a reg */
    switch (s->type)
    {
        case SYM_CONST:
            out_str(file_s, "\tLOD R%u,%u\n", r, s->value);
            break;

        case SYM_VAR:
            if (s->scope == 1) /* local var */
            {
                if ((s->offset) >= 0)
                {
                    LOD(s, "R%u,(R%u+%d)", r, R_BP, s->offset);
                    // out_str(file_s, "\tLOD R%u,(R%u+%d)\n", r, R_BP, s->offset);
                } else
                {
                    LOD(s, "R%u,(R%u-%d)", r, R_BP, -(s->offset));
                    // out_str(file_s, "\tLOD R%u,(R%u-%d)\n", r, R_BP, -(s->offset));
                }
            } else /* global var */
            {
                out_str(file_s, "\tLOD R%u,STATIC\n", R_TP);
                LOD(s, "R%u,(R%u+%d)", r, R_TP, s->offset);
                // out_str(file_s, "\tLOD R%u,(R%u+%d)\n", r, R_TP, s->offset);
            }
            break;

        case SYM_TEXT:
            out_str(file_s, "\tLOD R%u,L%u\n", r, s->label);
            break;

        default:
            break;
    }

    rdesc_fill(r, s, UNMODIFIED);
}

static int reg_alloc(SYM* s)
{
    int r;

    /* already in a register */
    for (r = R_GEN; r < R_NUM; r++)
    {
        if (rdesc[r].var == s)
        {
            if (rdesc[r].mod)
                asm_write_back(r);
            return r;
        }
    }

    /* empty register */
    for (r = R_GEN; r < R_NUM; r++)
    {
        if (rdesc[r].var == NULL)
        {
            asm_load(r, s);
            rdesc_fill(r, s, UNMODIFIED);
            return r;
        }
    }

    /* unmodified register */
    for (r = R_GEN; r < R_NUM; r++)
    {
        if (!rdesc[r].mod)
        {
            asm_load(r, s);
            rdesc_fill(r, s, UNMODIFIED);
            return r;
        }
    }

    /* random register */
    srand(time(NULL));
    const int random = (rand() % (R_NUM - R_GEN)) + R_GEN;
    asm_write_back(random);
    asm_load(random, s);
    rdesc_fill(random, s, UNMODIFIED);
    return random;
}

static void asm_bin(const char* op, SYM* a, SYM* b, SYM* c)
{
    int reg_b = -1, reg_c = -1;

    while (reg_b == reg_c)
    {
        reg_b = reg_alloc(b);
        reg_c = reg_alloc(c);
    }

    out_str(file_s, "\t%s R%u,R%u\n", op, reg_b, reg_c);
    rdesc_fill(reg_b, a, MODIFIED);
}

static void asm_cmp(const int op, SYM* a, SYM* b, SYM* c)
{
    int reg_b = -1, reg_c = -1;

    while (reg_b == reg_c)
    {
        reg_b = reg_alloc(b);
        reg_c = reg_alloc(c);
    }

    out_str(file_s, "\tSUB R%u,R%u\n", reg_b, reg_c);
    out_str(file_s, "\tTST R%u\n", reg_b);

    switch (op)
    {
        case TAC_EQ:
            out_str(file_s, "\tLOD R3,R1+40\n");
            out_str(file_s, "\tJEZ R3\n");
            out_str(file_s, "\tLOD R%u,0\n", reg_b);
            out_str(file_s, "\tLOD R3,R1+24\n");
            out_str(file_s, "\tJMP R3\n");
            out_str(file_s, "\tLOD R%u,1\n", reg_b);
            break;

        case TAC_NE:
            out_str(file_s, "\tLOD R3,R1+40\n");
            out_str(file_s, "\tJEZ R3\n");
            out_str(file_s, "\tLOD R%u,1\n", reg_b);
            out_str(file_s, "\tLOD R3,R1+24\n");
            out_str(file_s, "\tJMP R3\n");
            out_str(file_s, "\tLOD R%u,0\n", reg_b);
            break;

        case TAC_LT:
            out_str(file_s, "\tLOD R3,R1+40\n");
            out_str(file_s, "\tJLZ R3\n");
            out_str(file_s, "\tLOD R%u,0\n", reg_b);
            out_str(file_s, "\tLOD R3,R1+24\n");
            out_str(file_s, "\tJMP R3\n");
            out_str(file_s, "\tLOD R%u,1\n", reg_b);
            break;

        case TAC_LE:
            out_str(file_s, "\tLOD R3,R1+40\n");
            out_str(file_s, "\tJGZ R3\n");
            out_str(file_s, "\tLOD R%u,1\n", reg_b);
            out_str(file_s, "\tLOD R3,R1+24\n");
            out_str(file_s, "\tJMP R3\n");
            out_str(file_s, "\tLOD R%u,0\n", reg_b);
            break;

        case TAC_GT:
            out_str(file_s, "\tLOD R3,R1+40\n");
            out_str(file_s, "\tJGZ R3\n");
            out_str(file_s, "\tLOD R%u,0\n", reg_b);
            out_str(file_s, "\tLOD R3,R1+24\n");
            out_str(file_s, "\tJMP R3\n");
            out_str(file_s, "\tLOD R%u,1\n", reg_b);
            break;

        case TAC_GE:
            out_str(file_s, "\tLOD R3,R1+40\n");
            out_str(file_s, "\tJLZ R3\n");
            out_str(file_s, "\tLOD R%u,1\n", reg_b);
            out_str(file_s, "\tLOD R3,R1+24\n");
            out_str(file_s, "\tJMP R3\n");
            out_str(file_s, "\tLOD R%u,0\n", reg_b);
            break;

        default:
            out_str(file_s, "\tUNEXPECT OP %d\n", op);
    }

    /* Delete c from the descriptors and insert a */
    rdesc_clear(reg_b);
    rdesc_fill(reg_b, a, MODIFIED);
}

static void asm_cond(const char* op, SYM* a, const char* l)
{
    for (int r = R_GEN; r < R_NUM; r++)
        asm_write_back(r);

    if (a != NULL)
    {
        int r;

        for (r = R_GEN; r < R_NUM; r++) /* Is it in reg? */
        {
            if (rdesc[r].var == a)
                break;
        }

        if (r < R_NUM)
            out_str(file_s, "\tTST R%u\n", r);
        else
            out_str(file_s, "\tTST R%u\n", reg_alloc(a)); /* Load into new register */
    }

    out_str(file_s, "\t%s %s\n", op, l);
}

static void asm_call(SYM* a, SYM* b)
{
    int r;
    for (r = R_GEN; r < R_NUM; r++)
        asm_write_back(r);
    for (r = R_GEN; r < R_NUM; r++)
        rdesc_clear(r);
    out_str(file_s, "\tSTO (R2+%d),R2\n", tof + oon); /* store old bp */
    oon += 4;
    out_str(file_s, "\tLOD R4,R1+32\n"); /* return addr: 4*8=32 */
    out_str(file_s, "\tSTO (R2+%d),R4\n", tof + oon); /* store return addr */
    oon += 4;
    out_str(file_s, "\tLOD R2,R2+%d\n", tof + oon - 8); /* load new bp */
    out_str(file_s, "\tJMP %s\n", (char*) b); /* jump to new func */
    if (a != NULL)
    {
        r = reg_alloc(a);
        out_str(file_s, "\tLOD R%u,R%u\n", r, R_TP);
        rdesc[r].mod = MODIFIED;
    }
    oon = 0;
}

static void asm_return(SYM* a)
{
    for (int r = R_GEN; r < R_NUM; r++)
        asm_write_back(r);
    for (int r = R_GEN; r < R_NUM; r++)
        rdesc_clear(r);

    if (a != NULL) /* return value */
    {
        asm_load(R_TP, a);
    }

    out_str(file_s, "\tLOD R3,(R2+4)\n"); /* return address */
    out_str(file_s, "\tLOD R2,(R2)\n"); /* restore bp */
    out_str(file_s, "\tJMP R3\n"); /* return */
}

static void asm_head()
{
    char head[] = "\t# head\n"
            "\tLOD R2,STACK\n"
            "\tSTO (R2),0\n"
            "\tLOD R4,EXIT\n"
            "\tSTO (R2+4),R4\n";

    out_str(file_s, "%s", head);
}

static void asm_tail()
{
    char tail[] = "\n\t# tail\n"
            "EXIT:\n"
            "\tEND\n";

    out_str(file_s, "%s", tail);
}

static void asm_str(const SYM* s)
{
    const char* t = s->name; /* The text */

    out_str(file_s, "L%u:\n", s->label); /* Label for the string */
    out_str(file_s, "\tDBS "); /* Label for the string */

    for (int i = 1; t[i + 1] != 0; i++)
    {
        if (t[i] == '\\')
        {
            switch (t[++i])
            {
                case 'n':
                    out_str(file_s, "%u,", '\n');
                    break;

                case '\"':
                    out_str(file_s, "%u,", '\"');
                    break;

                default:
                    break; // TODO: 其他转义字符
            }
        } else
            out_str(file_s, "%u,", t[i]);
    }

    out_str(file_s, "0\n"); /* End of string */
}

static void asm_static(void)
{
    forloop_all_global_sym(1);
    for (const SYM* sl = forloop_all_global_sym(0); sl != NULL; sl = forloop_all_global_sym(0))
    {
        if (sl->type == SYM_TEXT)
            asm_str(sl);
    }

    out_str(file_s, "STATIC:\n");
    out_str(file_s, "\tDBN 0,%u\n", tos);
    out_str(file_s, "STACK:\n");
}

static void asm_addr(SYM* a, const SYM* b)
{
    if (!a || !b) return;
    const int reg_a = reg_alloc(a);
    if (b->scope == 1)
    {
        out_str(file_s, "\tLOD R%u, R%u%+d\n", reg_a, R_BP, b->offset);
    } else
    {
        out_str(file_s, "\tLOD R%u, STATIC\n", reg_a, R_TP);
        if (b->offset > 0)
            out_str(file_s, "\tADD R%u, %d\n", reg_a, b->offset);
        else if (b->offset < 0)
            out_str(file_s, "\tSUB R%u, %d\n", reg_a, -b->offset);
    }
    rdesc_fill(reg_a, a, MODIFIED);
}

static void asm_deref(SYM* a, SYM* b, SYM *c)
{
    if (!a || !b) return;
    // 为了避免寄存器与内存不一致,在解引用时需要把全部寄存器写回内存后再读取
    // 如果要避免全部写回,就需要**指针分析**的技术了
    int reg_b = 0;
    for (int r = R_GEN; r < R_NUM; r++)
    {
        if (rdesc[r].var == b)
        {
            reg_b = r;
            continue;
        }
        if (rdesc[r].var == NULL)
            continue;
        asm_write_back(r);
        rdesc_clear(r);
    }
    if (!c || (c->type == SYM_CONST && !c->value)) {
        if (!reg_b) {
            reg_b = reg_alloc(b);
        }
        LOD(a, "R%u, (R%u)", reg_b, reg_b);
    } else if (c->type == SYM_CONST) {
        if (!reg_b) {
            reg_b = reg_alloc(b);
        }
        LOD(a, "R%u, (R%u%+d)", reg_b, reg_b, c->value);
        // 更新offset
        a->offset = b->offset + c->value;
    } else {
        asm_bin("ADD", a, b, c);
        for (reg_b=R_GEN; reg_b<R_NUM; reg_b++)
            if (rdesc[reg_b].var == a)
                break;
        if (reg_b == R_NUM)
            error("ADD operation failed!");
        LOD(a, "R%u, (R%u)", reg_b, reg_b);
    }
    rdesc_fill(reg_b, a, MODIFIED);
}

static void asm_store(SYM* a, SYM* b, SYM *c)
{
    int reg_b = 0;
    // 此处依然要把所有寄存器写回,因为我们不能保证解引用后获得的指针已从寄存器中写回内存
    for (int r = R_GEN; r < R_NUM; r++)
    {
        if (rdesc[r].var == b)
        {
            reg_b = r;
            continue;
        }
        if (rdesc[r].var == NULL)
            continue;
        asm_write_back(r);
        rdesc_clear(r);
    }

    int reg_a = reg_alloc(a);
    if (c == NULL || c->type == SYM_CONST) {
        if (!reg_b)
        {
            reg_b = reg_alloc(b);
        }
        while (reg_a == reg_b)
        {
            reg_a = reg_alloc(a);
            reg_b = reg_alloc(b);
        }
        // 然后我们计算*(a+c)=b
        if (c==NULL) STO(b, "(R%u), R%u", reg_a, reg_b);
        else STO(b, "(R%u%+d), R%u", reg_a, c->value, reg_b);
    } else {
        int reg_c = reg_alloc(c);
        while (reg_a == reg_c) {
            reg_a = reg_alloc(a);
            reg_c = reg_alloc(c);
        }
        out_str(file_s, "\tADD R%u, R%u", reg_a, reg_c);
        reg_c = reg_alloc(b);
        while (reg_c == reg_a) {
            reg_a = reg_alloc(a);
            reg_c = reg_alloc(b);
        }
        STO(b, "(R%u), R%u", reg_a, reg_c);
    }
    rdesc_fill(reg_a, a, UNMODIFIED);
}

static void asm_code(const TAC* c)
{
    int r;
    SYM* cb = c->b,* cc = c->c;
    int offset;

    switch (c->op)
    {
        case TAC_UNDEF:
            error("cannot translate TAC_UNDEF");
            return;

        case TAC_ADD:
            if (c->b->type == SYM_CONST)
            {
                const SYM* tmp_ptr = cb;
                cb = cc;
                cc = (SYM*) tmp_ptr;
            }
            if (cc->type == SYM_CONST)
            {
                const int reg_b = reg_alloc(cb);
                LOD(c->a, "R%u, R%u%+d", reg_b, reg_b, cc->value);
                rdesc_fill(reg_b, c->a, MODIFIED);
                break;
            }
            asm_bin("ADD", c->a, c->b, c->c);
            return;

        case TAC_SUB:
            if (c->c->type == SYM_CONST)
            {
                const int reg_b = reg_alloc(c->b);
                LOD(c->a, "R%u, R%u%+d", reg_b, reg_b, -c->c->value);
                rdesc_fill(reg_b, c->a, MODIFIED);
                break;
            }
            asm_bin("SUB", c->a, c->b, c->c);
            return;

        case TAC_MUL:
            asm_bin("MUL", c->a, c->b, c->c);
            return;

        case TAC_DIV:
            asm_bin("DIV", c->a, c->b, c->c);
            return;

        case TAC_NEG:
            asm_bin("SUB", c->a, mk_const(0, c->b->value_type), c->b);
            return;

        case TAC_ADDR:
            asm_addr(c->a, c->b);
            return;

        case TAC_DEREF:
            asm_deref(c->a, c->b, c->c);
            break;

        case TAC_STORE:
            asm_store(c->a, c->b, c->c);
            break;

        case TAC_EQ:
        case TAC_NE:
        case TAC_LT:
        case TAC_LE:
        case TAC_GT:
        case TAC_GE:
            asm_cmp(c->op, c->a, c->b, c->c);
            return;

        case TAC_COPY:
            r = reg_alloc(c->b);
            rdesc_fill(r, c->a, MODIFIED);
            return;

        case TAC_INPUT:
            r = reg_alloc(c->a);
            if (c->a->value_type == SYM_VAL_CHAR)
                out_str(file_s, "\tITC\n");
            else if (c->a->value_type == SYM_VAL_INT)
                out_str(file_s, "\tITI\n");
            else
                error("cannot input unknown type");
            out_str(file_s, "\tLOD R%u,R15\n", r);
            rdesc[r].mod = MODIFIED;
            return;

        case TAC_OUTPUT:
            if (c->a->type == SYM_VAR || c->a->type == SYM_CONST)
            {
                r = reg_alloc(c->a);
                if (c->a->type == SYM_VAR) {
                    if (c->a->indirection) {
                        offset = rdesc[r].mod;
                        asm_addr(c->a, c->a);
                        rdesc[r].mod = offset;
                        out_str(file_s, "\tLOD R15,R%u\n", r);
                    } else {
                        out_str(file_s, "\tLOD R15,R%u\n", r);
                    }
                } else {
                    LOD(c->a, "R15, %d", c->a->value);
                }
                if (c->a->value_type == SYM_VAL_CHAR)
                {
                    out_str(file_s, "\tOTC\n");
                } else if (c->a->value_type == SYM_VAL_INT)
                {
                    out_str(file_s, "\tOTI\n");
                }
            } else if (c->a->type == SYM_TEXT)
            {
                r = reg_alloc(c->a);
                out_str(file_s, "\tLOD R15,R%u\n", r);
                out_str(file_s, "\tOTS\n");
            }
            return;

        case TAC_GOTO:
            asm_cond("JMP", NULL, c->a->name);
            return;

        case TAC_IFZ:
            asm_cond("JEZ", c->b, c->a->name);
            return;

        case TAC_LABEL:
            for (r = R_GEN; r < R_NUM; r++)
                asm_write_back(r);
            for (r = R_GEN; r < R_NUM; r++)
                rdesc_clear(r);
            out_str(file_s, "%s:\n", c->a->name);
            return;

        case TAC_ACTUAL:
            r = reg_alloc(c->a);
            out_str(file_s, "\tSTO (R2+%d),R%u\n", tof + oon, r);
            oon += 4;
            return;

        case TAC_CALL:
            asm_call(c->a, c->b);
            return;

        case TAC_BEGINFUNC:
            /* We reset the top of stack, since it is currently empty apart from the link information. */
            scope = 1;
            tof = LOCAL_OFF;
            oof = FORMAL_OFF;
            oon = 0;
            return;

        case TAC_FORMAL:
            c->a->scope = 1; /* parameter is special local var */
            c->a->offset = oof;
            offset = is_pointer(c->a) ? POINTER_SIZE : c->a->value_size;
            if (offset % POINTER_SIZE) {
                offset += POINTER_SIZE - offset % POINTER_SIZE;
            }
            oof -= offset;
            return;

        case TAC_VAR:
            if (scope)
            {
                c->a->scope = 1; /* local var */
                c->a->offset = tof;
                offset = is_pointer(c->a) ? POINTER_SIZE : c->a->value_size;
                if (offset % POINTER_SIZE) {
                    offset += POINTER_SIZE - offset % POINTER_SIZE;
                }
                tof += offset;
            } else
            {
                c->a->scope = 0; /* global var */
                c->a->offset = tos;
                offset = is_pointer(c->a) ? POINTER_SIZE : c->a->value_size;
                if (offset % POINTER_SIZE) {
                    offset += POINTER_SIZE - offset % POINTER_SIZE;
                }
                tos += offset;
            }
            return;

        case TAC_RETURN:
            asm_return(c->a);
            return;

        case TAC_ENDFUNC:
            asm_return(NULL);
            scope = 0;
            return;

        default:
            /* Don't know what this one is */
            error("unknown TAC opcode to translate");
            return;
    }
}

void tac_obj(void)
{
    tof = LOCAL_OFF; /* TOS allows space for link info */
    oof = FORMAL_OFF;
    oon = 0;

    for (int r = 0; r < R_NUM; r++)
        rdesc[r].var = NULL;

    asm_head();

    for (const TAC* cur = tac_first; cur != NULL; cur = cur->next)
    {
        out_str(file_s, "\n\t# ");
        out_tac(file_s, cur);
        out_str(file_s, "\n");
        asm_code(cur);
    }
    asm_tail();
    asm_static();
}
