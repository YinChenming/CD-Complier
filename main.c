#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "obj.h"
#include "opt.h"
#include "tac.h"
#include "mini.y.h"

int yyparse();
FILE *file_x, *file_s;

void error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    exit(0);
}

void tac_list() {
    out_str(file_x, "\n# tac list\n\n");
    print_structs(file_x);
    for (TAC *cur = tac_first; cur != NULL; cur = cur->next) {
        out_str(file_x, "%p\t", cur);
        out_tac(file_x, cur);
        out_str(file_x, "\n");
    }
}
static int str2int(const char *str, unsigned int *result) {
    *result = 0;
    for (const char *cp = str; *cp != '\0'; cp++) {
        if ('0' <= *cp && *cp <= '9') {
            *result = *result * 10 + (*cp - '0');
        } else {
            return cp - str + 1;
        }
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2)
        error("usage: %s filename\n", argv[0]);

    LocalOptimizationConfig local_conf = {

    };
    GlobalOptimizationConfig global_conf = {
        // .ignore_common_subexpression_elimination=true,
    };

    char *input = argv[1], *arg;
    size_t input_len;
    unsigned int max_loop = 10;


    for (int i=1; i < argc; i++) {
        arg = argv[i];
        if (arg[0] == '\0') error("missing argument: source code\n");
        if (arg[0] != '-') {
            input = arg;
            continue;
        }
        if (((arg[1] == 'h' || arg[1] == 'H') && arg[2] == '\0') || !strcmp(arg, "--help")) {
            printf("Usage: %s [options] file\n", argv[0]);
            printf("Options:\n");
            printf("\t-t <n>\tset maxinum number for optimization.\n");
            printf("\t-nCBE\tdisable optimization of common subexpression elimination.\n");
            printf("\t-nCF\tdisable optimization of constant folding.\n");
            printf("\t-nCP\tdisable optimization of constant and copy propagation.\n");
            printf("\t-nDCE\tdisable optimization of dead code elimination.\n");
            printf("\t-nLICM\tdisable optimization of loop invariant code motion.\n");
            return 0;
        }
        if (!strcmp(arg, "-v") || !strcmp(arg, "--version")) {
#ifdef NEW_ASM
            printf("mini (optimized for new virtual machine) 1.0.0\n");
#else
            printf("mini (optimized for old virtual machine) 1.0.0\n");
#endif
            return 0;
        }
        if (!strcmp(arg, "-nCBE")) {
            global_conf.ignore_common_subexpression_elimination = true;
            local_conf.ignore_common_subexpression_elimination = true;
            continue;
        }
        if (!strcmp(arg, "-nDCE")) {
            global_conf.ignore_dead_code_elimination = true;
            continue;
        }
        if (!strcmp(arg, "-nCP")) {
            global_conf.ignore_constant_and_copy_propagation = true;
            continue;
        }
        if (!strcmp(arg, "-nLICM")) {
            global_conf.ignore_loop_invariant_code_motion = true;
            continue;
        }
        if (!strcmp(arg, "-nCF")) {
            local_conf.ignore_constant_folding = true;
            continue;
        }
        if (!strcmp(arg, "-t")) {
            if (i == argc-1) {
                error("missing argument for -t flag.\n");
            }
            int result = str2int(argv[++i], &max_loop);
            if (result) {
                error("unknown character '%c'.\n", argv[i][result-1]);
            }
            continue;
        }
        error("unknown option '%s', see 'mini --help' or 'mini -h'.\n");
    }
    input_len = strlen(input);

    if (input_len < 3 || input[input_len - 1] != 'm' || input[input_len-2] != '.')
        error("%s does not end with .m\n", input);

    if (freopen(input, "r", stdin) == NULL)
        error("open %s failed.\n", input);

    char *output = strdup(input);

    output[input_len - 1] = 'x';
    if ((file_x = fopen(output, "w")) == NULL)
        error("open %s failed.\n", output);

    output[input_len - 1] = 's';
    if ((file_s = fopen(output, "w")) == NULL)
        error("open %s failed.\n", output);

    tac_init();
    yyparse();
    CFG *cfg = cfg_init(tac_first);
    cfg_to_dot(cfg, "dot/initial/");
    global_conf.dataflow_analysis_report_path = strdup(output);
    global_conf.dataflow_analysis_report_path[input_len - 1] = 'g';

    for (unsigned int i=1; i<=max_loop; i++) {
        int changed_num = run_optimization(cfg, local_conf, global_conf);
        printf("%d times, optimized %d\n", i, changed_num);
        if (!changed_num) break;
    }
    cfg_to_dot(cfg, "dot/");
    cfg_free(cfg);

    tac_list();
    tac_obj();

    fclose(file_s);
    fclose(file_x);

    return 0;
}
