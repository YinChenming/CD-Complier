#include "cfg.hh"

#include <stdexcept>    // for std::runtime_error

#include "analysis.hh"
#include "df.hh"

#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif

using namespace cfg;
using namespace df;
using namespace df::analysis;

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
    // 运行LiveVariableAnalysis
    auto rv_solver = LiveVariableSolver();
    auto &fcfg = *cfg->get_function("main");
    auto lv_result = std::move(*rv_solver.solve(fcfg));
    printf("Live variable analysis results:\n");
    for (const auto &node: fcfg.nodes()) {
        printf("Block %d:\n", node->id());
        const auto in_fact = lv_result.get_in_fact(*node);
        std::vector in_fact_vec(in_fact.begin(), in_fact.end());
        const auto out_fact = lv_result.get_out_fact(*node);
        std::vector out_fact_vec(out_fact.begin(), out_fact.end());
        printf("\t in live variables:");
        std::sort(in_fact_vec.begin(), in_fact_vec.end());
        std::sort(out_fact_vec.begin(), out_fact_vec.end());
        for (const auto &fact: in_fact_vec) {
            printf(" %s", fact.name().c_str());
        }
        printf("\n\tout live variables:");
        for (const auto &fact: out_fact_vec) {
            printf(" %s", fact.name().c_str());
        }
        printf("\n");
    }

    auto rd_solver = ReachingDefinitionSolver();
    const auto rv_result = rd_solver.solve(fcfg);
    printf("Reaching definitions results:\n");
    for (const auto &bb: fcfg.nodes()) {
        printf("Block %d:\n", bb->id());
        printf("\t in reaching definitions:");
        for (const auto &tac: rv_result->get_in_fact(*bb)) {
            const TacProxy tp(tac);
            printf(" %s;", tp.to_string().c_str());
        }
        printf("\n\tout reaching definitions:");
        for (const auto &tac: rv_result->get_out_fact(*bb)) {
            const TacProxy tp(tac);
            printf(" %s;", tp.to_string().c_str());
        }
        printf("\n");
    }
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
