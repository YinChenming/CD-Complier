OUT_DIR_NAME = out
OUT_DIR = ./$(OUT_DIR_NAME)
ASM_MACHINE_DIR_NAME = asm-machine
ASM_MACHINE_DIR = ./$(ASM_MACHINE_DIR_NAME)
CFLAGS = -I. -I$(OUT_DIR_NAME) -I$(ASM_MACHINE_DIR_NAME) -Wall -Wextra -Wno-unused-function
CXXFLAGS = -I. -I$(OUT_DIR_NAME) -I$(ASM_MACHINE_DIR_NAME) -Wall -Wextra -Wno-unused-function
YYFLAGS = -d # -Wall -Wcounterexamples

CFLAGS += -g3
CXXFLAGS += -g3 -std=c++17
TARGETS = mini asm machine

all: $(TARGETS)

mini: $(OUT_DIR)/main.o $(OUT_DIR)/mini.l.o $(OUT_DIR)/mini.y.o $(OUT_DIR)/tac.o $(OUT_DIR)/obj.o $(OUT_DIR)/opt.o $(OUT_DIR)/cfg.o $(OUT_DIR)/df.o $(OUT_DIR)/analysis.o | $(OUT_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

asm: $(ASM_MACHINE_DIR)/asm.l $(ASM_MACHINE_DIR)/asm.y $(ASM_MACHINE_DIR)/inst.h | $(OUT_DIR)
	lex -o $(OUT_DIR)/asm.l.c $(ASM_MACHINE_DIR)/asm.l
	yacc -o $(OUT_DIR)/asm.y.c $(ASM_MACHINE_DIR)/asm.y $(YYFLAGS)
	$(CC) $(CFLAGS) $(OUT_DIR)/asm.l.c $(OUT_DIR)/asm.y.c -o $@

machine: $(ASM_MACHINE_DIR)/machine.c $(ASM_MACHINE_DIR)/inst.h | $(OUT_DIR)
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
	yacc -o $(OUT_DIR)/$<.c $< $(YYFLAGS)

$(OUT_DIR)/%.l.o: $(OUT_DIR)/%.l.c $(OUT_DIR)/%.y.h
	$(CC) $(CFLAGS) $< -c -o $@

$(OUT_DIR)/%.y.o: $(OUT_DIR)/%.y.c
	$(CC) $(CFLAGS) $< -c -o $@

$(OUT_DIR)/main.o: $(OUT_DIR)/mini.y.h

test: $(t).m
	@$(MAKE) clean_dot
	@find ./ | grep -E '^\./[^.].*\.(g|txt)$$' | xargs rm -f
	./mini $(t).m > $(t)_report.txt
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
	@find ./ | grep -E '^\./[^.].*\.(o|s|x|dot|png|g|svg|txt)$$' | xargs rm -f
	@rm -fr $(OUT_DIR) *.l.* *.y.* *.s *.x *.o *.g core $(TARGETS) $(DOT_DIR) *.dSYM

w: YYFLAGS += -Wall -Wcounterexamples

testall:
	@for f in ./testcase/*.m; do \
		if [ -f "$$f" ]; then \
			name=$$(basename "$$f" .m); \
			echo "test $$name"; \
			if $(MAKE) test t="./testcase/$$name"; then \
				echo "success"; \
			else \
				echo "failed"; \
			fi; \
		fi \
	done

.PHONY: clean all $(TARGETS) test w testall dot clean_dot
