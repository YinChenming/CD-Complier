#ifndef OPT_H
#define OPT_H
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "tac.h"
typedef struct CFG CFG;
CFG *cfg_init(TAC*);
void cfg_free(CFG*);
void cfg_to_dot(CFG*, char *path);

void run_optimization();
void run_local_optimization();
void run_global_optimization();

#ifdef __cplusplus
}
#endif
#endif  // OPT_H
