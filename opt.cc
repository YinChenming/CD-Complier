#include "cfg.hh"

#include <stdexcept>    // for std::runtime_error

#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif

using namespace cfg;

EXTERNC
CFG *cfg_init(TAC *tac) {
    try{
        return new CFG(tac);
    } catch(const std::exception &e) {
        return nullptr;
    }
}
EXTERNC
void cfg_free(const CFG *cfg) {
    if (cfg) {
        try {
            delete cfg;
        } catch (const std::exception &e) {
            ;;;
        }
    }
}
EXTERNC
void cfg_to_dot(const CFG *cfg, const char *path) {
    if (!path || !cfg)
        return;
    try {
        cfg->to_dot(std::string(path));
    } catch (const std::exception &e) {
        ;;;
    }
}

EXTERNC
int run_local_optimization(CFG *cfg){
    if (!cfg) return 0;
    int opt_count = 0;
    if (cfg->opt_constants_folding()) opt_count++;
    if (cfg->opt_common_subexpresson_elimination()) opt_count++;
    return opt_count;
}
EXTERNC
int run_global_optimization(CFG *cfg){
    if (!cfg) return 0;
    return 1;
}

EXTERNC
int run_optimization(CFG *cfg){
    if (!cfg) return 0;
    int opt_count = 0;
    opt_count += run_local_optimization(cfg);
    opt_count += run_global_optimization(cfg);
    return opt_count;
}
