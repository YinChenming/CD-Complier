OUT_DIR_NAME = out
OUT_DIR = ./$(OUT_DIR_NAME)
CFLAGS = -I. -I$(OUT_DIR_NAME) -Wall -Wextra -Wno-unused-function
YYFLAGS = -d # -Wall -Wcounterexamples

CFLAGS += -g3
TARGETS = mini asm machine

all: $(TARGETS)

mini: main.c mini.l mini.y tac.c tac.h obj.c obj.h | $(OUT_DIR)
	lex -o $(OUT_DIR)/mini.l.c mini.l
	yacc -o $(OUT_DIR)/mini.y.c mini.y $(YYFLAGS)
	$(CC) $(CFLAGS) main.c $(OUT_DIR)/mini.l.c $(OUT_DIR)/mini.y.c tac.c obj.c -o $@

asm: asm.l asm.y opcode.h | $(OUT_DIR)
	lex -o $(OUT_DIR)/asm.l.c asm.l
	yacc -o $(OUT_DIR)/asm.y.c asm.y $(YYFLAGS)
	$(CC) $(CFLAGS) $(OUT_DIR)/asm.l.c $(OUT_DIR)/asm.y.c -o $@

machine: machine.c opcode.h | $(OUT_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(OUT_DIR):
	@mkdir $(OUT_DIR)

clean:
	@find ./ | grep -E '^\./[^.].*\.[osx]$$' | xargs rm -f
	@rm -fr $(OUT_DIR) *.l.* *.y.* *.s *.x *.o core $(TARGETS) *.dSYM

test: $(t).m
	./mini $(t).m; \
	./asm $(t).s; \
	./machine $(t).o

NEW_ASM_NAME = asm-machine
NEW_ASM_DIR = ./$(NEW_ASM_NAME)

w: YYFLAGS += -Wall -Wcounterexamples
new: CFLAGS += -DNEW_ASM
new: mini $(NEW_ASM_DIR)/asm.l $(NEW_ASM_DIR)/asm.y $(NEW_ASM_DIR)/machine.c $(NEW_ASM_DIR)/inst.h | $(OUT_DIR)
	lex -o $(OUT_DIR)/asm.l.c $(NEW_ASM_DIR)/asm.l
	yacc -o $(OUT_DIR)/asm.y.c $(NEW_ASM_DIR)/asm.y $(YYFLAGS)
	$(CC) $(CFLAGS) -I$(NEW_ASM_NAME) $(OUT_DIR)/asm.l.c $(OUT_DIR)/asm.y.c -o asm
	$(CC) $(CFLAGS) -I$(NEW_ASM_NAME) $(NEW_ASM_DIR)/machine.c -o machine


.PHONY: clean all $(TARGETS) test new w
