#ifndef DATAFLOW_ANALYSIS_H
#define DATAFLOW_ANALYSIS_H

#include <map>
#include <set>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <memory>

namespace df {
    template<typename Node>
    class AbstractCFG {
    public:
        AbstractCFG() = default;
        virtual ~AbstractCFG() = default;
        [[nodiscard]] virtual bool is_exit(const Node &) const = 0;
        [[nodiscard]] virtual bool is_entry(const Node &) const = 0;
        [[nodiscard]] virtual std::vector<Node *> nodes() const = 0;
        [[nodiscard]] virtual std::vector<Node *> successors(const Node &) const = 0;
        [[nodiscard]] virtual std::vector<Node *> precursors(const Node &) const = 0;
    };

    template<typename Container>
    class DataflowResult {
    public:
        using Node = typename Container::key_type;
        using Fact = typename Container::mapped_type;
    private:
        Container in_facts_{}, out_facts_{};

    public:
        DataflowResult() = default;
        DataflowResult(const DataflowResult &df) : in_facts_(df.in_facts_), out_facts_(df.out_facts_) {}
        virtual ~DataflowResult() = default;
        [[nodiscard]] virtual Fact &get_in_fact(const Node &node) { return in_facts_[node]; }
        virtual void set_in_fact(const Node &node, const Fact &fact) { in_facts_[node] = fact; }
        [[nodiscard]] virtual Fact &get_out_fact(const Node &node) { return out_facts_[node]; }
        virtual void set_out_fact(const Node &node, const Fact &fact) { out_facts_[node] = fact; }
        DataflowResult &operator=(const DataflowResult &df_result) = default;
        virtual bool operator==(const DataflowResult &df_result) {
            return in_facts_ == df_result.in_facts_ && out_facts_ == df_result.out_facts_;
        }
    };
    template<typename Key, typename T, typename Compare = std::less<Key>,
             typename Alloc = std::allocator<std::pair<const Key, T>>>
    using TreeMapDataflowResult = DataflowResult<std::map<Key, T, Compare, Alloc>>;
    template<typename Key, typename T, typename Hash = std::hash<Key>, typename Pred = std::equal_to<Key>,
             typename Alloc = std::allocator<std::pair<const Key, T>>>
    using HashMapDataflowResult = DataflowResult<std::unordered_map<Key, T, Hash, Pred, Alloc>>;
    template<typename Node, typename Fact>
    class DataflowAnalysis {
    public:
        virtual ~DataflowAnalysis() = default;
        [[nodiscard]] virtual bool is_forward() const { return true; }
        [[nodiscard]] virtual std::unique_ptr<Fact> new_boundary_fact(const AbstractCFG<Node> &cfg) = 0;
        [[nodiscard]] virtual std::unique_ptr<Fact> new_initial_fact() const = 0;
        virtual void meet(const Fact &facts, Fact &result) const = 0;
        [[nodiscard]] virtual bool transfer_node(const Node &, Fact &in_fact, Fact &out_fact) = 0;
        DataflowAnalysis &operator=(const DataflowAnalysis &) = default;
    };
    template<typename ResultContainer>
    class Solver {
    public:
        using Node = typename ResultContainer::Node;
        using Fact = typename ResultContainer::Fact;
    private:
        DataflowAnalysis<Node, Fact> &analysis_;

    public:
        explicit Solver(DataflowAnalysis<Node, Fact> &df) : analysis_(df) {}
        Solver(const Solver &solver) : analysis_(solver.analysis_) {}
        virtual ~Solver() = default;

        [[nodiscard]] std::unique_ptr<ResultContainer> solve(const AbstractCFG<Node> &cfg) {
            auto result = initialize(cfg);
            doSolve(cfg, *result);
            return result;
        }

    private:
        [[nodiscard]] std::unique_ptr<ResultContainer> initialize(const AbstractCFG<Node> &cfg) {
            auto result = std::make_unique<ResultContainer>();
            if (analysis_.is_forward()) {
                initializeForward(cfg, *result);
                return result;
            } else {
                initializeBackward(cfg, *result);
                return result;
            }
        }
        virtual void initializeForward(const AbstractCFG<Node> &cfg, ResultContainer &df) {
            for (const Node *node: cfg.nodes()) {
                if (!cfg.is_entry(*node)) {
                    df.set_out_fact(*node, std::move(*analysis_.new_initial_fact()));
                } else {
                    df.set_out_fact(*node, std::move(*analysis_.new_boundary_fact(cfg)));
                }
                df.set_in_fact(*node, std::move(*analysis_.new_initial_fact()));
            }
        }
        virtual void initializeBackward(const AbstractCFG<Node> &cfg, ResultContainer &df) {
            for (const Node *node: cfg.nodes()) {
                if (!cfg.is_exit(*node)) {
                    df.set_in_fact(*node, std::move(*analysis_.new_initial_fact()));
                } else {
                    df.set_in_fact(*node, std::move(*analysis_.new_boundary_fact(cfg)));
                }
                df.set_out_fact(*node, std::move(*analysis_.new_initial_fact()));
            }
        }
        void doSolve(const AbstractCFG<Node> &cfg, ResultContainer &df) {
            if (analysis_.is_forward()) {
                solveForward(cfg, df);
            } else {
                solveBackward(cfg, df);
            }
        }
        virtual void solveForward(const AbstractCFG<Node> &cfg, ResultContainer &df) {
            bool changed;
            do {
                changed = false;
                for (const Node *node: cfg.nodes()) {
                    Fact in_fact = std::move(*analysis_.new_initial_fact());
                    for (auto &child: cfg.precursors(*node)) {
                        analysis_.meet(df.get_out_fact(*child), in_fact);
                    }
                    df.set_in_fact(*node, in_fact);
                    Fact &out_fact = df.get_out_fact(*node);
                    if (analysis_.transfer_node(*node, in_fact, out_fact)) {
                        changed = true;
                    }
                    df.set_out_fact(*node, out_fact);
                }
            } while (changed);
        }
        virtual void solveBackward(const AbstractCFG<Node> &cfg, ResultContainer &df) {
            bool changed;
            do {
                changed = false;
                for (const Node *node: cfg.nodes()) {
                    Fact out_fact = std::move(*analysis_.new_initial_fact());
                    for (auto &child: cfg.successors(*node)) {
                        analysis_.meet(df.get_in_fact(*child), out_fact);
                    }
                    df.set_out_fact(*node, out_fact);
                    Fact &in_fact = df.get_in_fact(*node);
                    if (analysis_.transfer_node(*node, in_fact, out_fact)) {
                        changed = true;
                    }
                    df.set_in_fact(*node, in_fact);
                }
            } while (changed);
        }
    };

    // Abstract Facts
    template<typename Container>
    class FactContainer {
    public:
        using container_type = Container;
        using Fact = typename Container::key_type;
    private:
        Container facts_{};

    public:
        FactContainer() = default;
        explicit FactContainer(const Container &facts) : facts_(facts) {}
        FactContainer(const FactContainer &facts) : facts_(facts.facts_) {}
        bool operator==(const FactContainer &facts) const { return facts_ == facts.facts_; }
        bool operator!=(const FactContainer &facts) const { return facts_ != facts.facts_; }
        FactContainer &operator=(const FactContainer &facts) = default;
        FactContainer operator|(const FactContainer &facts) const {
            Container result(facts_);
            result.insert(facts.facts_.begin(), facts.facts_.end());
            return {result};
        }
        const FactContainer &operator|=(const FactContainer &facts) {
            facts_.insert(facts.facts_.begin(), facts.facts_.end());
            return *this;
        }
        FactContainer operator&(const FactContainer &facts) const {
            Container result{};
            for (const auto &fact: facts.facts_) {
                if (facts_.find(fact) != facts_.end()) {
                    result.emplace(fact);
                }
            }
            return {result};
        }
        const FactContainer &operator&=(const FactContainer &facts) {
            for (auto &it = facts_.begin(); it != facts_.end();) {
                if (facts.facts_.find(it) == facts.facts_.end()) {
                    it = facts_.erase(it);
                } else {
                    ++it;
                }
            }
            return *this;
        }
        const FactContainer &operator+=(const Fact &fact) {
            add(fact);
            return *this;
        }
        const FactContainer &operator-=(const Fact &fact) {
            remove(fact);
            return *this;
        }

        bool add(const Fact &fact) { return facts_.emplace(fact).second; }
        bool remove(const Fact &fact) { return facts_.erase(fact); }
        bool contains(const Fact &fact) const {  return facts_.find(fact) != facts_.end(); }
        void union_(FactContainer &facts) {
            facts_.insert(facts.facts_.begin(), facts.facts_.end());
        }
        void intersect(const FactContainer &facts) {
            for (auto it = facts_.begin(); it != facts_.end();) {
                if (!facts.facts_.count(*it)) {
                    it = facts_.erase(it);
                }
            }
        }
        void clear() { facts_.clear(); }
        FactContainer copy() const { return {facts_}; }
        auto begin() -> decltype(facts_.begin()) { return facts_.begin(); }
        auto begin() const -> decltype(facts_.begin()) { return facts_.begin(); }
        auto cbegin() const -> decltype(facts_.cbegin()) { return facts_.cbegin(); }
        auto end() -> decltype(facts_.end()) { return facts_.end(); }
        auto end() const -> decltype(facts_.end()) { return facts_.end(); }
        auto cend() const -> decltype(facts_.end()) { return facts_.cend(); }
    };
    template<typename Key, typename Compare = std::less<Key>, typename Alloc = std::allocator<Key>>
    using TreeSetFactContainer = FactContainer<std::set<Key, Compare, Alloc>>;
    template<typename Key, typename Hash = std::hash<Key>, typename Pred = std::equal_to<Key>,
             typename Alloc = std::allocator<Key>>
    using HashSetFactContainer = FactContainer<std::unordered_set<Key, Hash, Pred, Alloc>>;

} // namespace df

#endif // DATAFLOW_ANALYSIS_H
