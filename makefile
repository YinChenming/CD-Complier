OUT_DIR_NAME = out
OUT_DIR = ./$(OUT_DIR_NAME)
CFLAGS = -I. -I$(OUT_DIR_NAME) -Wall -Wextra

CFLAGS += -g3
TARGETS = mini asm machine

all: $(TARGETS)

mini: main.c mini.l mini.y tac.c tac.h obj.c obj.h | $(OUT_DIR)
	lex -o $(OUT_DIR)/mini.l.c mini.l
	yacc -d -o $(OUT_DIR)/mini.y.c mini.y
	$(CC) $(CFLAGS) main.c $(OUT_DIR)/mini.l.c $(OUT_DIR)/mini.y.c tac.c obj.c -o $@

asm: asm.l asm.y opcode.h | $(OUT_DIR)
	lex -o $(OUT_DIR)/asm.l.c asm.l
	yacc -d -o $(OUT_DIR)/asm.y.c asm.y
	$(CC) $(CFLAGS) $(OUT_DIR)/asm.l.c $(OUT_DIR)/asm.y.c -o $@

machine: machine.c opcode.h | $(OUT_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(OUT_DIR):
	@mkdir $(OUT_DIR) -v -p

clean:
	@rm -fr $(OUT_DIR) *.l.* *.y.* *.s *.x *.o core $(TARGETS)

test:
	./mini test.m; \
	./asm test.s; \
	./machine test.o

.PHONY: clean all $(TARGETS) test
