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
        [[nodiscard]] std::unique_ptr<LiveVariableFacts> new_initial_fact(const AbstractCFG<BasicBlock> &) const override;
        void meet(const LiveVariableFacts &facts, LiveVariableFacts &result) const override;
        [[nodiscard]] bool transfer_node(const BasicBlock &, LiveVariableFacts &in_fact, LiveVariableFacts &out_fact) override;
    };
    // const LiveVariableAnalysis liveVariableAnalysis = {};
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
        [[nodiscard]] std::vector<TAC *> getDefinitions(const std::string &name) const;
    public:
        void init(const AbstractCFG<BasicBlock> &cfg);
        [[nodiscard]] bool is_forward() const override { return true; }
        [[nodiscard]] std::unique_ptr<ReachingDefinitionFacts> new_boundary_fact(const AbstractCFG<BasicBlock> &cfg) override;
        [[nodiscard]] std::unique_ptr<ReachingDefinitionFacts> new_initial_fact(const AbstractCFG<BasicBlock> &) const override;
        void meet(const ReachingDefinitionFacts &facts, ReachingDefinitionFacts &result) const override;
        [[nodiscard]] bool transfer_node(const BasicBlock &, ReachingDefinitionFacts &in_fact, ReachingDefinitionFacts &out_fact) override;
    };
    class ReachingDefinitionSolver final : public Solver<HashMapDataflowResult<BasicBlock, ReachingDefinitionFacts, BasicBlock::Hash>> {
        inline static ReachingDefinitionAnalysis liveVariableAnalysis_{};
    public:
        ReachingDefinitionSolver() : Solver(liveVariableAnalysis_) {}
    };

    struct Expression {
        int op;
        SYM *b=nullptr, *c=nullptr;
        explicit Expression(const TAC *tac): op(tac->op), b(tac->b), c(tac->c) {}
        explicit Expression(int op, SYM *b, SYM *c) : op(op), b(b), c(c) {}
        bool operator==(const Expression &other) const {
            return op == other.op && b == other.b && c == other.c;
        }
    };
    struct HashExpression {
        size_t operator() (const Expression &exp) const {
            return std::hash<int>()(exp.op) ^ std::hash<SYM *>()(exp.b) ^ std::hash<SYM *>()(exp.c);
        }
    };
    using AvailableExpressionFacts = HashSetFactContainer<Expression, HashExpression>;
    class AvailableExpressionAnalysis final : public DataflowAnalysis<BasicBlock, AvailableExpressionFacts> {
    public:
        [[nodiscard]] bool is_forward() const override { return true; }
        [[nodiscard]] std::unique_ptr<AvailableExpressionFacts> new_boundary_fact(const AbstractCFG<BasicBlock> &cfg) override;
        [[nodiscard]] std::unique_ptr<AvailableExpressionFacts> new_initial_fact(const AbstractCFG<BasicBlock> &cfg) const override;
        void meet(const AvailableExpressionFacts &facts, AvailableExpressionFacts &result) const override;
        [[nodiscard]] bool transfer_node(const BasicBlock &, AvailableExpressionFacts &in_fact, AvailableExpressionFacts &out_fact) override;
    };
    class AvailableExpressionSolver final : public Solver<HashMapDataflowResult<BasicBlock, AvailableExpressionFacts, BasicBlock::Hash>> {
        inline static AvailableExpressionAnalysis liveVariableAnalysis_{};
    public:
        AvailableExpressionSolver() : Solver(liveVariableAnalysis_) {}
    };
}

#endif // FINAL_ASSIGNMENT_ANALYSIS_HH
