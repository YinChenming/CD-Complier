#ifndef FINAL_ASSIGNMENT_ANALYSIS_HH
#define FINAL_ASSIGNMENT_ANALYSIS_HH

#include "df.hh"
#include "cfg.hh"

namespace df::analysis {
    using namespace df;
    using namespace cfg;
    using LiveVariableFacts = HashSetFactContainer<SymProxy, SymProxy::Hash>;
    class LiveVariableAnalysis final : public DataflowAnalysis<BasicBlock, LiveVariableFacts> {
    public:
        [[nodiscard]] bool is_forward() const override { return false; }
        [[nodiscard]] std::unique_ptr<LiveVariableFacts> new_boundary_fact(const AbstractCFG<BasicBlock> &) override;
        [[nodiscard]] std::unique_ptr<LiveVariableFacts> new_initial_fact() const override;
        void meet(const LiveVariableFacts &facts, LiveVariableFacts &result) const override;
        [[nodiscard]] bool transfer_node(const BasicBlock &, LiveVariableFacts &in_fact, LiveVariableFacts &out_fact) override;
    };
    const LiveVariableAnalysis liveVariableAnalysis = {};
    class LiveVariableSolver final
        : public Solver<HashMapDataflowResult<BasicBlock, LiveVariableFacts, BasicBlock::Hash>> {
        inline static LiveVariableAnalysis liveVariableAnalysis_{};
    public:
        LiveVariableSolver() : Solver(liveVariableAnalysis_) {}
    };

    struct HashTacPointer {
        size_t operator() (const TAC * tac) const {
            return std::hash<size_t>()(reinterpret_cast<size_t>(tac));
        }
    };
    using ReachingDefinitionFacts = HashSetFactContainer<TAC *, HashTacPointer>;
    class ReachingDefinitionAnalysis final : public DataflowAnalysis<BasicBlock, ReachingDefinitionFacts> {
        std::map<std::string, std::unordered_set<TAC *, HashTacPointer>> value2gen_;
        void insertDefinition(TAC *tac);
        std::vector<TAC *> getDefinitions(const std::string &name) const;
    public:
        void init(const AbstractCFG<BasicBlock> &cfg);
        [[nodiscard]] bool is_forward() const override { return true; }
        [[nodiscard]] std::unique_ptr<ReachingDefinitionFacts> new_boundary_fact(const AbstractCFG<BasicBlock> &cfg) override;
        [[nodiscard]] std::unique_ptr<ReachingDefinitionFacts> new_initial_fact() const override;
        void meet(const ReachingDefinitionFacts &facts, ReachingDefinitionFacts &result) const override;
        [[nodiscard]] bool transfer_node(const BasicBlock &, ReachingDefinitionFacts &in_fact, ReachingDefinitionFacts &out_fact) override;
    };
    class ReachingDefinitionSolver final : public Solver<HashMapDataflowResult<BasicBlock, ReachingDefinitionFacts, BasicBlock::Hash>> {
        inline static ReachingDefinitionAnalysis liveVariableAnalysis_{};
    public:
        ReachingDefinitionSolver() : Solver(liveVariableAnalysis_) {}
    };

    using AvailableExpressionFacts = HashSetFactContainer<TAC *, HashTacPointer>;

}

#endif // FINAL_ASSIGNMENT_ANALYSIS_HH
