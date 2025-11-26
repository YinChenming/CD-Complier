OUT_DIR_NAME = out
OUT_DIR = ./$(OUT_DIR_NAME)
CFLAGS = -I. -I$(OUT_DIR_NAME) -Wall -Wextra
CXXFLAGS = -I. -I$(OUT_DIR_NAME) -Wall -Wextra

CFLAGS += -g3
CXXFLAGS += -g3 -std=c++17
TARGETS = mini asm machine

all: $(TARGETS)

mini: $(OUT_DIR)/main.o $(OUT_DIR)/mini.l.o $(OUT_DIR)/mini.y.o $(OUT_DIR)/tac.o $(OUT_DIR)/obj.o $(OUT_DIR)/opt.o $(OUT_DIR)/cfg.o $(OUT_DIR)/df.o | $(OUT_DIR)
# 	lex -o $(OUT_DIR)/mini.l.c mini.l
# 	yacc -d -o $(OUT_DIR)/mini.y.c mini.y
# 	$(CC) $(CFLAGS) main.c $(OUT_DIR)/mini.l.c $(OUT_DIR)/mini.y.c tac.c obj.c -o $@
	$(CXX) $(CXXFLAGS) $^ -o $@

asm: asm.l asm.y opcode.h | $(OUT_DIR)
	lex -o $(OUT_DIR)/asm.l.c asm.l
	yacc -d -o $(OUT_DIR)/asm.y.c asm.y
	$(CC) $(CFLAGS) $(OUT_DIR)/asm.l.c $(OUT_DIR)/asm.y.c -o $@

machine: machine.c opcode.h | $(OUT_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(OUT_DIR):
	@mkdir $(OUT_DIR)

$(OUT_DIR)/%.o: %.c | $(OUT_DIR)
	$(CC) $(CFLAGS) $< -c -o $@

$(OUT_DIR)/%.o: %.cc | $(OUT_DIR)
	$(CXX) $(CXXFLAGS) $< -c -o $@

$(OUT_DIR)/%.l.c: %.l | $(OUT_DIR)
	lex -o $@ $<

$(OUT_DIR)/%.y.c $(OUT_DIR)/%.y.h: %.y | $(OUT_DIR)
	yacc -d -o $(OUT_DIR)/$<.c $<

$(OUT_DIR)/%.l.o: $(OUT_DIR)/%.l.c $(OUT_DIR)/%.y.h
	$(CC) $(CFLAGS) $< -c -o $@

$(OUT_DIR)/%.y.o: $(OUT_DIR)/%.y.c
	$(CC) $(CFLAGS) $< -c -o $@

$(OUT_DIR)/main.o: $(OUT_DIR)/mini.y.h

test: $(t).m
	@$(MAKE) clean_dot
	./mini $(t).m
	$(MAKE) dot; \
	./asm $(t).s; \
	./machine $(t).o

DOT_DIR_NAME = dot
DOT_DIR = ./$(DOT_DIR_NAME)
DOT_FILES := $(wildcard $(DOT_DIR)/*.dot)
SVG_FILES := $(patsubst %.dot,%.svg,$(DOT_FILES))

clean_dot:
	rm -rf $(DOT_DIR)

dot: $(SVG_FILES)

$(DOT_DIR)/%.svg: $(DOT_DIR)/%.dot | $(DOT_DIR)/
	dot -Tsvg -o $@ $<

$(DOT_DIR)/:
	@mkdir $(DOT_DIR)

clean:
	@find ./ | grep -E '^\./[^.].*\.(o|s|x|dot|png)$$' | xargs rm -f
	@rm -fr $(OUT_DIR) *.l.* *.y.* *.s *.x *.o core $(TARGETS) $(DOT_DIR)

NEW_ASM_NAME = asm-machine
NEW_ASM_DIR = ./$(NEW_ASM_NAME)

new: CFLAGS += -DNEW_ASM
new: CXXFLAGS += -DNEW_ASM
new: mini $(NEW_ASM_DIR)/asm.l $(NEW_ASM_DIR)/asm.y $(NEW_ASM_DIR)/machine.c $(NEW_ASM_DIR)/inst.h | $(OUT_DIR)
	lex -o $(OUT_DIR)/asm.l.c $(NEW_ASM_DIR)/asm.l
	yacc -d -o $(OUT_DIR)/asm.y.c $(NEW_ASM_DIR)/asm.y
	$(CC) $(CFLAGS) -I$(NEW_ASM_NAME) $(OUT_DIR)/asm.l.c $(OUT_DIR)/asm.y.c -o asm
	$(CC) $(CFLAGS) -I$(NEW_ASM_NAME) $(NEW_ASM_DIR)/machine.c -o machine


.PHONY: clean all $(TARGETS) test new dot clean_dot
