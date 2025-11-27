#include "analysis.hh"

using namespace df;
using namespace analysis;
using namespace cfg;

std::unique_ptr<LiveVariableFacts> LiveVariableAnalysis::new_boundary_fact(const AbstractCFG<BasicBlock> &) {
    return new_initial_fact();
}
std::unique_ptr<LiveVariableFacts> LiveVariableAnalysis::new_initial_fact() const {
    return std::make_unique<LiveVariableFacts>();
}
void LiveVariableAnalysis::meet(const LiveVariableFacts &facts, LiveVariableFacts &result) const {
    result |= facts;
}
bool LiveVariableAnalysis::transfer_node(const BasicBlock &bb, LiveVariableFacts &in_fact,
                                         /* const */ LiveVariableFacts &out_fact) {
    // 先kill再gen
    auto new_in_fact {out_fact};
    for (auto tac = bb.end_; tac && tac->next!=bb.begin_.get(); tac=TacProxy(tac->prev)) {
    // for (const auto &tac: bb) {
        if (tac.has_side_effect()) {
            // kill a
            new_in_fact -= SymProxy(tac->a);
        }
        if (tac.use_a()) {
            const SymProxy a(tac->a);
            if (!a.is_const())
                new_in_fact += a;
        }
        if (tac.use_b()) {
            // gen b
            const SymProxy b(tac->b);
            if (!b.is_const())
                new_in_fact += b;
        }
        if (tac.use_c()) {
            const SymProxy c(tac->c);
            if (!c.is_const())
                new_in_fact += c;
        }
    }
    const auto changed = new_in_fact != in_fact;
    in_fact = new_in_fact;
    return changed;
}

void ReachingDefinitionAnalysis::insertDefinition(TAC *tac) {
    if (!tac) return;
    const TacProxy tp(tac);
    if (!tp.has_side_effect()) return;
    if (tp.is_definition()) return;
    const std::string name(tp->a->name);
    value2gen_.try_emplace(name);
    value2gen_[name].insert(tac);
}
std::vector<TAC *> ReachingDefinitionAnalysis::getDefinitions(const std::string &name) const {
    const auto it = value2gen_.find(name);
    if (it == value2gen_.end()) {
        return {};
    }
    return {it->second.cbegin(), it->second.cend()};
}
void ReachingDefinitionAnalysis::init(const AbstractCFG<BasicBlock> &cfg) {
    for (const auto &bb: cfg.nodes()) {
        for (const auto &tac: *bb) {
            if (!tac || !tac.has_side_effect() || tac.is_definition()) continue;
            const std::string name(tac->a->name);
            value2gen_.try_emplace(name);
            value2gen_[name].insert(tac.get());
        }
    }
}
std::unique_ptr<ReachingDefinitionFacts> ReachingDefinitionAnalysis::new_boundary_fact(const AbstractCFG<BasicBlock> &cfg) {
    value2gen_.clear();
    init(cfg);
    return std::make_unique<ReachingDefinitionFacts>();
}
std::unique_ptr<ReachingDefinitionFacts> ReachingDefinitionAnalysis::new_initial_fact() const {
    return std::make_unique<ReachingDefinitionFacts>();
}
void ReachingDefinitionAnalysis::meet(const ReachingDefinitionFacts &facts, ReachingDefinitionFacts &result) const {
    result |= facts;
}
bool ReachingDefinitionAnalysis::transfer_node(const BasicBlock &bb, ReachingDefinitionFacts &in_fact, ReachingDefinitionFacts &out_fact) {
    ReachingDefinitionFacts new_out_fact{in_fact};
    for (const auto &tac: bb) {
        if (!tac.has_side_effect() || tac.is_definition()) continue;
        const SymProxy sym_a(tac->a);
        if (sym_a.is_temporary()) continue;
        const std::string name(tac->a->name);
        // kill其他definition
        for (const auto &other_df: getDefinitions(name)) {
            new_out_fact -= other_df;
        }
        // gen一个新的definition
        new_out_fact += tac.get();
    }
    const bool changed = new_out_fact != out_fact;
    out_fact = new_out_fact;
    return changed;
}
