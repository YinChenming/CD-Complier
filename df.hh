#ifndef DATAFLOW_ANALYSIS_H
#define DATAFLOW_ANALYSIS_H

#include <map>

#include "cfg.hh"

namespace df {
template<typename Container>
class DataflowResult {
    using Node = Container::key_type;
    using Fact = Container::mapped_type;

    Container in_facts_{}, out_facts_{};
public:
    DataflowResult() {}
    DataflowResult(const DataflowResult &df): in_facts_(df.in_facts_), out_facts_(df.out_facts_) {}
    ~DataflowResult() = default;
    [[nodiscard]] virtual Fact &get_in_fact(const Node &node) {
        return in_facts_[node];
    }
    virtual void set_in_fact(const Node &node, Fact &fact) {
        in_facts_[node] = fact;
    }
    [[nodiscard]] virtual Fact &get_out_fact(const Node &node) {
        return out_facts_[node];
    }
    virtual void set_out_fact(const Node &node, Fact &fact) {
        out_facts_[node] = fact;
    }
    virtual DataflowResult &operator=(const DataflowResult &df_result) {
        in_facts_ = df_result.in_facts_;
        out_facts_ = df_result.out_facts_;
        return *this;
    }
    virtual bool operator==(const DataflowResult &df_result) {
        return in_facts_ == df_result.in_facts_ && out_facts_ == df_result.out_facts_
    }
};
template <typename Key, typename T, typename Compare = std::less<Key>,
          typename Alloc = std::allocator<std::pair<const Key, T>>>
using TreeMapDataflowResult = DataflowResult<std::map<Key, T, Compare, Alloc>>;
template <typename Key, typename T,
          typename Hash = std::hash<Key>,
          typename Pred = std::equal_to<Key>,
          typename Alloc = std::allocator<std::pair<const Key, T>>>
using HashMapDataflowResult = DataflowResult<std::unordered_map<Key, T, Hash, Pred, Alloc>>;
template<typename Node, typename Fact>
class DataflowAnalysis {
public:
    virtual ~DataflowAnalysis() = default;
    virtual bool isForward() const = 0;
    virtual std::unique_ptr<Fact> new_boundary_fact(const AbstractCFG<Node>&) const = 0;
    virtual std::unique_ptr<Fact> new_initial_fact() const = 0;
    virtual void meet(Fact&, Fact&) const = 0;
    virtual bool transfer_node(const Node&, Fact &in_fact, Fact &out_fact) const = 0;
    virtual DataflowAnalysis &operator=(const DataflowAnalysis&)=default;
};
template<typename Node, typename Fact>
class Solver {
    DataflowAnalysis<Node, Fact> analysis_{};
public:
    Solver() {}
    explicit Solver(const DataflowAnalysis<Node, Fact>& df): analysis_(df) {}
    Solver(const Solver& solver): analysis_(solver.analysis_) {}
    ~Solver() = default;

    [[nodiscard]] std::unique_ptr<DataflowResult<Node, Fact>> solve(AbstractCFG<Node>&cfg) {
        auto result = initialize(cfg);
        doSolve(cfg, *result);
        return result;
    }
private:
    [[nodiscard]] std::unique_ptr<DataflowResult<Node, Fact>> initialize(AbstractCFG<Node>& cfg) {
        auto result = std::make_unique<DataflowResult<Node, Fact>>();
        if (analysis_.isForward()) {
            return initializeForward(cfg, *result);
        } else {
            return initializeBackward(cfg, *result);
        }
    }
    virtual void initializeForward(AbstractCFG<Node>&cfg, DataflowResult<Node, Fact>&df) {
        for (const Node *node: cfg.nodes()) {
            if (!cfg.is_entry(*node)) {
                df.set_out_fact(*node, analysis_.new_boundary_fact(*node));
            } else {
                df.set_out_fact(*node, analysis_.new_initial_fact());
            }
            df.set_in_fact(*node, analysis_.new_initial_fact());
        }
    }
    virtual void initializeBackward(AbstractCFG<Node>&cfg, DataflowResult<Node, Fact>&df) {
        for (const Node *node: cfg.nodes()) {
            if (!cfg.is_exit(*node)) {
                df.set_in_fact(*node, analysis_.new_initial_fact());
            } else {
                df.set_in_fact(*node, analysis_.new_boundary_fact(*node));
            }
            df.set_out_fact(*node, analysis_.new_initial_fact());
        }
    }
    void doSolve(AbstractCFG<Node> &cfg, DataflowResult<Node, Fact>&df) {
        if (analysis_.isForward()) {
            solveForward(cfg, df);
        } else {
            solveBackward(cfg, df);
        }
    }
    virtual void solveForward(AbstractCFG<Node>&cfg, DataflowResult<Node, Fact>&df) const {
        bool changed;
        do {
            changed = false;
            for (const Node *node: cfg.nodes()) {
                Fact in_fact = analysis_.new_initial_fact();
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
    virtual void solveBackward(AbstractCFG<Node>&cfg, DataflowResult<Node, Fact>&df) const {
        bool changed;
        do {
            changed = false;
            for (const Node *node: cfg.nodes()) {
                Fact out_fact = analysis_.new_initial_fact();
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
    using Fact = Container::key_type;
    Container facts_{};
public:
    FactContainer() {}
    explicit FactContainer(const Container &facts): facts_(facts) {}
    FactContainer(const FactContainer &facts): facts_(facts.facts_) {}
    const bool operator==(const FactContainer &facts) const {
        return facts_ == facts.facts_;
    }
    const bool operator!=(const FactContainer &facts) const {
        return facts_ != facts.facts_;
    }
    const FactContainer& operator=(const FactContainer &facts) {
        facts_.clear();
        facts_.insert(facts.facts_.begin(), facts.facts_.end());
        return *this;
    }
    const FactContainer operator|(const FactContainer &facts) const {
        Container result(facts_);
        result.insert(facts.facts_.begin(), facts.facts_.end());
        return {result};
    }
    const FactContainer &operator|=(const FactContainer &facts) {
        facts_.insert(facts.facts_.begin(), facts.facts_.end());
        return *this;
    }
    const FactContainer operator&(const FactContainer &facts) const {
        Container result();
        for (const auto&fact: facts.facts_) {
            if (facts_.find(fact)!=facts_.end()) {
                result.emplace(fact);
            }
        }
        return {result};
    }
    const FactContainer& operator&=(const FactContainer &facts) {
        for (auto &it=facts_.begin(); it!=facts_.end();) {
            if (facts.facts_.find(it) == facts.facts_.end()) {
                it = facts_.erase(it);
            } else {
                ++it;
            }
        }
        return *this;
    }
    const FactContainer& operator+=(const Fact &fact) {
        add(fact);
        return *this;
    }
    const FactContainer& operator-=(const Fact &fact) {
        remove(fact);
        return *this;
    }
    bool add(const Fact &fact) {
        return facts_.emplace(fact).second;
    }
    bool remove(const Fact &fact) {
        return facts_.erase(fact);
    }
    void clear() {
        facts_.clear();
    }
    FactContainer copy() const {
        return {facts_};
    }
    auto begin() -> decltype(facts_.begin()) {
        return facts_.begin();
    }
    auto begin() const -> decltype(facts_.begin()) {
        return facts_.begin();
    }
    auto cbegin() const -> decltype(facts_.cbegin()) {
        return facts_.cbegin();
    }
    auto end() -> decltype(facts_.end()) {
        return facts_.end();
    }
    auto end() const -> decltype(facts_.end()) {
        return facts_.end();
    }
    auto cend() const -> decltype(facts_.end()) {
        return facts_.cend();
    }
};
template <typename Key,
          typename Compare = std::less<Key>,
          typename Alloc = std::allocator<Key>>
using TreeSetFactContainer = FactContainer<std::set<Key, Compare, Alloc>>;
template <typename Key,
          typename Hash = std::hash<Key>,
          typename Pred = std::equal_to<Key>,
          typename Alloc = std::allocator<Key>>
using HashSetFactContainer = FactContainer<std::unordered_set<Key, Hash, Pred, Alloc>>;

}

#endif  // DATAFLOW_ANALYSYS_H