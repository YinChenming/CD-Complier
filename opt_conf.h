#ifndef OPT_CONFIG_H
#define OPT_CONFIG_H

#include <stdbool.h>
typedef struct LocalOptimizationConfig {
    bool ignore_constant_folding, ignore_common_subexpression_elimination;
} LocalOptimizationConfig;
typedef struct GlobalOptimizationConfig {
    bool ignore_dead_code_elimination, ignore_constant_and_copy_propagation,
        ignore_common_subexpression_elimination, ignore_loop_invariant_code_motion;
    char *dataflow_analysis_report_path;
} GlobalOptimizationConfig;

#endif // OPT_CONFIG_H
