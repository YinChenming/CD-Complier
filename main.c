#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "obj.h"
#include "opt.h"
#include "tac.h"

// mini.y.h must be at the la
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

    TAC *cur;
    for (cur = tac_first; cur != NULL; cur = cur->next) {
        out_str(file_x, "%p\t", cur);
        out_tac(file_x, cur);
        out_str(file_x, "\n");
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2)
        error("usage: %s filename\n", argv[0]);

    char *input = argv[1];
    if (input[strlen(input) - 1] != 'm')
        error("%s does not end with .m\n", input);

    if (freopen(input, "r", stdin) == NULL)
        error("open %s failed\n", input);

    char *output = strdup(input);

    output[strlen(output) - 1] = 'x';
    if ((file_x = fopen(output, "w")) == NULL)
        error("open %s failed\n", output);

    output[strlen(output) - 1] = 's';
    if ((file_s = fopen(output, "w")) == NULL)
        error("open %s failed\n", output);

    tac_init();
    yyparse();
    CFG *cfg = cfg_init(tac_first);
    cfg_to_dot(cfg, "dot/initial/");
    LocalOptimizationConfig local_conf;
    GlobalOptimizationConfig global_conf = {
        // .ignore_common_subexpression_elimination=true,
        .dataflow_analysis_report_path=strdup(output),
    };
    global_conf.dataflow_analysis_report_path[strlen(output)-1] = 'g';
    const int max_loop = 10;
    for (int i=1; i<=max_loop; i++) {
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
