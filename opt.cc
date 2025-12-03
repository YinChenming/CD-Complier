#include "cfg.hh"

#include <stdexcept>    // for std::runtime_error
#include <algorithm>

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

static bool opt_dead_code_elimination(const CFG *cfg) {
    bool changed = false;
    for (auto &fcfg_kv: cfg->functions_) {
        FunctionCFG& fcfg = *fcfg_kv.second;
        LiveVariableSolver solver;
        auto result = *solver.solve(fcfg);
        for (auto &bb: fcfg.blocks_) {
            LiveVariableFacts facts ( result.get_out_fact(*bb));
            for (auto tac = bb->end_; tac && tac->next!=bb->begin_.get(); tac = tac->prev) {
                if (tac.has_side_effect() && !tac.is_definition()) {
                    const SymProxy a(tac->a);
                    if (facts.contains(a)) {
                        facts -= a;
                    } else {
                        // 无用赋值,删除
                        printf("block %d: delete '%s'\n", bb->id(), tac.to_string().c_str());
                        if (tac==bb->end_) {
                            bb->end_ = tac->prev;
                        } else {
                            tac->next->prev = tac->prev;
                        }
                        if (tac==bb->begin_) {
                            bb->begin_ = tac->next;
                        } else {
                            tac->prev->next = tac->next;
                        }
                        changed = true;
                        continue;
                    }
                }
                if (tac.use_a()) {
                    const SymProxy a(tac->a);
                    if (!a.is_const())
                        facts += a;
                }
                if (tac.use_b()) {
                    // gen b
                    const SymProxy b(tac->b);
                    if (!b.is_const())
                        facts += b;
                }
                if (tac.use_c()) {
                    const SymProxy c(tac->c);
                    if (!c.is_const())
                        facts += c;
                }
            }
        }
    }
    return changed;
}

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
        tac_first = cfg->to_tac().first;
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
    if (cfg->opt_common_subexpression_elimination()) opt_count++;
    return opt_count;
}
EXTERNC
int run_global_optimization(CFG *cfg){
    if (!cfg) return 0;
    // 运行LiveVariableAnalysis
    auto rv_solver = LiveVariableSolver();
    auto &fcfg = *cfg->get_function("main");
    auto lv_result = *rv_solver.solve(fcfg);
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
    int opt_count = 0;
    opt_count += opt_dead_code_elimination(cfg);
    return opt_count;
}

EXTERNC
int run_optimization(CFG *cfg){
    if (!cfg) return 0;
    int opt_count = 0;
    opt_count += run_local_optimization(cfg);
    opt_count += run_global_optimization(cfg);
    return opt_count;
}
