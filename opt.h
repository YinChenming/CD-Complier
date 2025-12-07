#ifndef OPT_H
#define OPT_H
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "tac.h"
#include "opt_conf.h"
typedef struct CFG CFG;
CFG *cfg_init(TAC*);
void cfg_free(const CFG*);
void cfg_to_dot(const CFG*, const char *path);

int run_optimization(CFG*, LocalOptimizationConfig, GlobalOptimizationConfig);
int run_local_optimization(CFG*, LocalOptimizationConfig);
int run_global_optimization(CFG*, GlobalOptimizationConfig);

#ifdef __cplusplus
}
#endif
#endif  // OPT_H
