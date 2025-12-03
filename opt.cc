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
            const LiveVariableFacts &out_fact = result.get_out_fact(*bb);
            LiveVariableFacts facts ( out_fact);
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

static bool opt_constant_and_copy_propagation(const CFG *cfg) {
    bool changed = false;
    auto reaching_definition_to_map = [] (const ReachingDefinitionFacts &facts) -> std::unordered_map<std::string, std::vector<TAC*>> {
        std::unordered_map<std::string, std::vector<TAC*>> result;
        for (auto &tac: facts) {
            result[std::string(tac->a->name)].push_back(tac);
        }
        return result;
    };
    for (auto &fcfg_kv: cfg->functions_) {
        FunctionCFG& fcfg = *fcfg_kv.second;
        ReachingDefinitionSolver solver;
        auto result = *solver.solve(fcfg);
        for (auto &bb: fcfg.blocks_) {
            auto definitions = reaching_definition_to_map(result.get_in_fact(*bb));
            for (auto &tac: *bb) {
                if (tac.use_a()) {
                    const SymProxy a(tac->a);
                    const auto &defs = definitions.find(a.name());
                    if (defs != definitions.end() && defs->second.size() == 1) {
                        if (const TacProxy another_tac(defs->second.front()); another_tac.is_assignment()) {
                            const SymProxy another(another_tac->b);
                            if (another.is_const() || strcmp(tac->a->name, another->name)>0) {
                                printf("block %d: a replace '%s' to '%s'\n", bb->id(), tac->a->name, another->name);
                                tac->a = another.get();
                                changed = true;
                            }
                        }
                    }
                }

                if (tac.use_b()) {
                    const SymProxy b(tac->b);
                    const auto &defs = definitions.find(b.name());
                    if (defs != definitions.end() && defs->second.size() == 1) {
                        if (const TacProxy another_tac(defs->second.front()); another_tac.is_assignment()) {
                            const SymProxy another(another_tac->b);
                            if (another.is_const() || strcmp(tac->b->name, another->name)>0) {
                                printf("block %d: b replace '%s' to '%s'\n", bb->id(), tac->b->name, another->name);
                                tac->b = another.get();
                                changed = true;
                            }
                        }
                    }
                }

                if (tac.use_c()) {
                    const SymProxy c(tac->c);
                    const auto &defs = definitions.find(c.name());
                    if (defs != definitions.end() && defs->second.size() == 1) {
                        if (const TacProxy another_tac(defs->second.front()); another_tac.is_assignment()) {
                            const SymProxy another(another_tac->b);
                            if (another.is_const() || strcmp(tac->c->name, another->name)>0) {
                                printf("block %d: c replace '%s' to '%s'\n", bb->id(), tac->c->name, another->name);
                                tac->c = another.get();
                                changed = true;
                            }
                        }
                    }
                }

                if (tac.has_side_effect() && !tac.is_definition()) {
                    definitions[std::string(tac->a->name)].push_back(tac.get());
                }
            }
            // 对条件跳转结尾优化
            if (const SymProxy cond(bb->end_->b);bb->end_.is_if() && cond.is_const()) {
                // fall through
                if (cond->value && bb->ifz_) {
                    printf("block %d: always go to fallthrough!\n", bb->id());
                    BasicBlock &ifz_bb = *bb->ifz_;
                    changed = true;
                    ifz_bb.preds_.erase(std::remove(ifz_bb.preds_.begin(), ifz_bb.preds_.end(), bb.get()), ifz_bb.preds_.end());
                    bb->ifz_ = nullptr;
                    bb->end_ = bb->end_->prev;
                } else if (!cond->value && bb->fallthrough_) {
                    printf("block %d: always go to ifz!\n", bb->id());
                    BasicBlock &fallthrough_bb = *bb->fallthrough_;
                    changed = true;
                    fallthrough_bb.preds_.erase(std::remove(fallthrough_bb.preds_.begin(), fallthrough_bb.preds_.end(), bb.get()), fallthrough_bb.preds_.end());
                    bb->fallthrough_ = bb->ifz_;
                    bb->ifz_ = nullptr;
                    bb->end_->op = TAC_GOTO;
                    bb->end_->b = NULL;
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
    opt_count += opt_constant_and_copy_propagation(cfg);
    return opt_count;
}

EXTERNC
int run_optimization(CFG *cfg){
    if (!cfg) return 0;
    int opt_count = 0;
    opt_count += run_local_optimization(cfg);
    opt_count += run_global_optimization(cfg);
    opt_count += cfg->remove_unreachable_blocks();
    return opt_count;
}
