#ifndef CONTROL_FLOW_GRAPH_H
#define CONTROL_FLOW_GRAPH_H

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include "tac.h"
}

namespace cfg {
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
    // 强兼struct tac类型
    // 应当注意,这里tac_可能为nullptr,nullptr也可能隐式转换成TacProxy对象
    class TacProxy {
        TAC *tac_ = nullptr;
        void init_() const {
            if (is_binocular() && is_exchangeable()) {
                // 确保如果可交换的话让第一个参数的名字小于第二个参数
                if (strcmp(tac_->b->name, tac_->c->name) > 0) {
                    std::swap(tac_->b, tac_->c);
                }
            }
        }

    public:
        TacProxy() = default;
        // 强兼struct tac*,支持隐式转成代理类
        explicit TacProxy(TAC *tac) : tac_(tac) { init_(); }
        TAC *operator->() const { return tac_; }
        TAC &operator*() const { return *tac_; }
        bool operator<(const TAC *tac) const {
            if (!tac_ && !tac)
                return false;
            if (!tac_ && tac)
                return true;
            if (tac_ && !tac)
                return false;
            if (tac_->op < tac->op)
                return true;
            if (tac_->a != tac->a) {
                if (strcmp(tac_->a->name, tac->a->name) < 0)
                    return true;
            }
            if (tac_->b != tac->b) {
                if (strcmp(tac_->b->name, tac->b->name) < 0)
                    return true;
            }
            if (tac_->c != tac->c) {
                if (strcmp(tac_->c->name, tac->c->name) < 0)
                    return true;
            }
            return false;
        }
        bool operator<(const TacProxy &tac) const { return operator<(tac.tac_); }
        bool operator>=(const TAC *tac) const { return !(*this < tac); }
        bool operator>=(const TacProxy &tac) const { return operator>=(tac.tac_); }
        bool operator==(const TAC *tac) const {
            if (!tac_ && !tac)
                return true;
            if (!tac_ || !tac)
                return false;
            if (tac_->op != tac->op)
                return false;
            if (tac_->a != tac->a && (!tac_->a || !tac->a || strcmp(tac_->a->name, tac->a->name) != 0))
                return false;
            if (tac_->b != tac->b && (!tac_->b || !tac->b || strcmp(tac_->b->name, tac->b->name) != 0))
                return false;
            if (tac_->c != tac->c && (!tac_->c || !tac->c || strcmp(tac_->c->name, tac->c->name) != 0))
                return false;
            return true;
        }
        bool operator==(const TacProxy &tac) const { return operator==(tac.tac_); }
        bool operator!=(const TAC *tac) const { return !(*this == tac); }
        bool operator!=(const TacProxy &tac) const { return operator!=(tac.tac_); }
        bool operator<=(const TAC *tac) const { return *this == tac || *this < tac; }
        bool operator<=(const TacProxy &tac) const { return operator<=(tac.tac_); }
        bool operator>(const TAC *tac) const { return !(*this <= tac); }
        bool operator>(const TacProxy &tac) const { return operator>(tac.tac_); }
        explicit operator bool() const { return tac_; }
        // 强兼struct tac*,可以隐式将代理类转回struct tac*
        explicit operator TAC *() const { return tac_; }
        const TacProxy &operator=(TAC *tac) {
            tac_ = tac;
            return *this;
        }
        [[nodiscard]] bool is_computable() const {
            if (!tac_)
                return false;
            if (tac_->op >= TAC_MIN_CALC && tac_->op <= TAC_MAX_CALC)
                return true;
            return false;
        }
        [[nodiscard]] bool is_exchangeable() const {
            if (!tac_)
                return false;
            if (tac_->op == TAC_ADD || tac_->op == TAC_MUL)
                return true;
            if (tac_->op == TAC_EQ || tac_->op == TAC_NE)
                return true;
            return false;
        }
        [[nodiscard]] bool is_monocular() const {
            if (!tac_)
                return false;
            return !tac_->c && tac_->a && tac_->b;
        }
        [[nodiscard]] bool is_binocular() const {
            if (!tac_)
                return false;
            return tac_->c && tac_->b && tac_->a;
        }
        [[nodiscard]] bool is_definition() const {
            return tac_ && tac_->op == TAC_VAR;
        }
        [[nodiscard]] bool has_side_effect() const {
            if (!tac_ || !tac_->a)
                return false;
            if (tac_->op == TAC_VAR) return true;
            if (tac_->op == TAC_COPY) return true;
            if (tac_->op == TAC_INPUT) return true;
            if (is_computable()) return true;
            return false;
        }
        [[nodiscard]] bool use_a() const {
            if (!tac_ || !tac_->a) return false;
            if (tac_->op == TAC_OUTPUT) return true;
            return false;
        }
        [[nodiscard]] bool use_b() const {
            if (!tac_ || !tac_->b) return false;
            if (tac_->op >= TAC_MIN_CALC && tac_->op <= TAC_MAX_CALC) return true;
            if (tac_->op == TAC_COPY) return true;
            if (tac_->op == TAC_OUTPUT) return true;
            if (tac_->op == TAC_FORMAL) return true;
            if (tac_->op == TAC_RETURN) return true;
            if (tac_->op == TAC_IFZ) return true;
            return false;
        }
        [[nodiscard]] bool use_c() const {
            if (!tac_ || !tac_->c) return false;
            if (tac_->op >= TAC_MIN_CALC && tac_->op < TAC_MAX_CALC) return true;
            return false;
        }
        [[nodiscard]] TAC *get() const { return tac_; }
        void set(TAC *tac) { tac_ = tac; }
        std::string to_string() const;

        struct Hash {
            size_t operator()(const TacProxy &tac) const {
                if (!tac)
                    return -1;
                auto hash = std::hash<int>()(tac.tac_->op);
                if (tac.tac_->a)
                    hash ^= std::hash<std::string>()(tac.tac_->a->name);
                if (tac.tac_->b)
                    hash ^= std::hash<std::string>()(tac.tac_->b->name);
                if (tac.tac_->c)
                    hash ^= std::hash<std::string>()(tac.tac_->c->name);
                return hash;
            }
        };
    };
    // 强兼struct sym类型
    // 同样,这里sym_也可能是nullptr
    class SymProxy {
        SYM *sym_ = nullptr;
        std::string name_{};
        bool has_set_name_ = false;

    public:
        SymProxy() = default;
        explicit SymProxy(SYM *sym) : sym_(sym), name_(sym && sym->name ? sym->name : ""), has_set_name_(sym != nullptr) {}
        SYM *operator->() const { return sym_; }
        bool operator==(const SYM *sym) const {
            if (sym_ == sym)
                return true;
            if (!sym_ || !sym)
                return false;
            return sym_->type == sym->type && strcmp(sym_->name, sym->name) == 0;
        }
        bool operator==(const SymProxy &sym) const { return operator==(sym.sym_); }
        bool operator!=(const SYM *sym) const {
            if (sym_ == sym)
                return false;
            if (!sym_ || !sym)
                return false;
            return sym_->type != sym->type || strcmp(sym_->name, sym->name) != 0;
        }
        bool operator!=(const SymProxy &sym) const { return operator!=(sym.sym_); }
        bool operator<(const SYM *sym) const {
            if (!sym_ && !sym)
                return false;
            if (!sym_ && sym)
                return true;
            if (sym_ && !sym)
                return false;
            return strcmp(sym_->name, sym->name) < 0;
        }
        bool operator<(const SymProxy &sym) const { return operator<(sym.sym_); }
        bool operator<=(const SYM *sym) const {
            if (!sym_ && !sym)
                return true;
            if (!sym_ && sym)
                return true;
            if (sym_ && !sym)
                return false;
            return strcmp(sym_->name, sym->name) <= 0;
        }
        bool operator<=(const SymProxy &sym) const { return operator<=(sym.sym_); }
        bool operator>(const SYM *sym) const {
            if (!sym_ && !sym)
                return false;
            if (!sym_ && sym)
                return false;
            if (sym_ && !sym)
                return true;
            return strcmp(sym_->name, sym->name) > 0;
        }
        bool operator>(const SymProxy &sym) const { return operator>(sym.sym_); }
        bool operator>=(const SYM *sym) const {
            if (!sym_ && !sym)
                return true;
            if (!sym_ && sym)
                return false;
            if (sym_ && !sym)
                return true;
            return strcmp(sym_->name, sym->name) >= 0;
        }
        bool operator>=(const SymProxy &sym) const { return operator>=(sym.sym_); }
        explicit operator bool() const { return sym_; }
        explicit operator SYM *() const { return sym_; }

        [[nodiscard]] SYM *get() const { return sym_; }
        void set(SYM *sym) {
            sym_ = sym;
            if (sym_ != nullptr) {
                name_ = std::string(sym_->name);
                has_set_name_ = true;
            } else {
                has_set_name_ = false;
            }
        }
        [[nodiscard]] const std::string &name() const {
            if (has_set_name_)
                return name_;
            return "";
        }

        [[nodiscard]] bool is_const() const {
            return sym_ && (sym_->type != SYM_VAR);
        }
        [[nodiscard]] bool is_variable() const {
            return sym_ && sym_->type == SYM_VAR;
        }
        [[nodiscard]] bool is_temporary() const {
            return sym_ && sym_->name && sym_->name[0] == '$' && sym_->name[1] == 't';
        }

        struct Hash {
            size_t operator()(const SymProxy &sym) const {
                if (!sym)
                    return -1;
                return std::hash<int>()(sym->type) ^ std::hash<int>()(sym->scope) ^
                       std::hash<std::string>()(sym.name());
            }
        };
    };
    class BasicBlock {
        int id_ = END_BLOCK_ID;

    public:
        struct TacIterator {
            TacProxy tac_{nullptr};
            TacIterator() = default;
            TacIterator(const TacIterator &) = default;
            explicit TacIterator(const TacProxy &tac) : tac_(tac) {}
            explicit TacIterator(TAC *tac) : tac_(tac) {}
            TAC *operator->() const {
                if (!tac_) return nullptr;
                return tac_.operator->();
            }
            TacProxy &operator*() {
                return tac_;
            }
            bool operator==(const TAC *tac) const {
                return tac_ == tac;
            }
            bool operator==(const TacProxy &tac) const {
                return tac_ == tac;
            }
            bool operator==(const TacIterator &tac) const {
                return operator==(tac.tac_);
            }
            bool operator!=(const TAC *tac) const {
                return tac_ != tac;
            }
            bool operator!=(const TacProxy &tac) const {
                return tac_ != tac;
            }
            bool operator!=(const TacIterator &tac) const {
                return !operator==(tac);
            }
            const TacIterator &operator++() {
                if (!tac_) return *this;
                tac_ = tac_->next;
                return *this;
            }
            TacIterator operator++(int) {
                const auto tmp = *this;
                operator++();
                return tmp;
            }
            TAC *get() const {
                if (!tac_) return nullptr;
                return tac_.get();
            }
        };
        static constexpr int END_BLOCK_ID = 0;
        static constexpr int BEGIN_BLOCK_ID = -1;
        TacProxy begin_{nullptr};
        TacProxy end_{nullptr};

        BasicBlock *fallthrough_ = nullptr, *ifz_ = nullptr;
        std::vector<BasicBlock *> preds_ = {}; // 前驱数组

        explicit BasicBlock(const int id) : id_(id) {}
        explicit BasicBlock(const int id, TAC *begin) : id_(id), begin_(begin) {}
        explicit BasicBlock(const int id, TAC *begin, TAC *end) : id_(id), begin_(begin), end_(end) {}
        ~BasicBlock() = default;
        [[nodiscard]] int id() const { return id_; }
        bool operator==(const BasicBlock &block) const {
            return id_ == block.id_;
        }
        bool operator!=(const BasicBlock &block) const {
            return id_ != block.id_;
        }
        static bool is_entry(const BasicBlock &block) { return block.id_ == BEGIN_BLOCK_ID; }
        static bool is_exit(const BasicBlock &block) { return block.id_ == END_BLOCK_ID; }
        [[nodiscard]] bool opt_constants_folding() const;
        [[nodiscard]] bool opt_common_subexpression_elimination() const;

        TacIterator begin() const {
            return TacIterator(begin_);
        }
        TacIterator end() const {
            if (end_) return TacIterator(end_->next);
            else return TacIterator();
        }

        struct Hash {
            size_t operator()(const BasicBlock &block) const {
                return std::hash<int>()(block.id_);
            }
        };
    };

    class FunctionCFG;
    class CFG {
        std::map<std::string, std::unique_ptr<FunctionCFG>> functions_;
        std::vector<SYM *> global_vars_;
        void init(const TAC *tac);

    public:
        explicit CFG(const TAC *tac) { init(tac); }
        [[nodiscard]] FunctionCFG *get_function(const std::string &name) const { auto it = functions_.find(name); if (it == functions_.end()) return nullptr; return it->second.get(); }
        [[nodiscard]] std::string global_vars_to_dot() const;
        void to_dot(const std::filesystem::path &path) const;
        [[nodiscard]] std::vector<std::string> to_dot() const;
        [[nodiscard]] bool opt_constants_folding() const;
        [[nodiscard]] bool opt_common_subexpresson_elimination() const;
    };

    class FunctionCFG : public AbstractCFG<BasicBlock> {
        class FunctionCFGIterator {
            FunctionCFG *cfg_ = nullptr;
            size_t index_ = -1;

        public:
            FunctionCFGIterator() = default;
            FunctionCFGIterator(const FunctionCFGIterator &other) = default;
            FunctionCFGIterator(FunctionCFG *cfg, size_t i) : cfg_(cfg) {
                if (cfg == nullptr) {
                    cfg_ = nullptr;
                    index_ = static_cast<size_t>(-1);
                    return;
                }
                if (i < cfg_->blocks_.size()) {
                    index_ = i;
                } else {
                    cfg_ = nullptr;
                }
            }
            FunctionCFGIterator &operator++() {
                index_++;
                if (index_ >= cfg_->blocks_.size()) {
                    index_ = -1;
                    cfg_ = nullptr;
                }
                return *this;
            }
            FunctionCFGIterator operator++(int) {
                auto cfg = cfg_;
                auto index = index_;
                ++*this;
                return {cfg, index};
            }
            bool operator==(const FunctionCFGIterator &other) const { return cfg_ == other.cfg_ && index_ == other.index_; }
            BasicBlock *operator->() const {
                if (!cfg_ || index_ == static_cast<size_t>(-1))
                    return nullptr;
                return cfg_->blocks_[index_].get();
            }
        };

        BasicBlock begin_block_ = BasicBlock(BasicBlock::BEGIN_BLOCK_ID),
                   end_block_ = BasicBlock(BasicBlock::END_BLOCK_ID);
        std::vector<std::unique_ptr<BasicBlock>> blocks_;
        std::string label_;
        void init(TAC *start_tac, const TAC *end_tac);
        static std::string block2dot(BasicBlock *);

    public:
        friend class CFG;
        BasicBlock &get_entry() { return begin_block_; }
        BasicBlock &get_exit() { return end_block_; }
        explicit FunctionCFG(std::string label) : label_(std::move(label)) {}
        ~FunctionCFG() override = default;
        FunctionCFG(std::string label, TAC *tac_start, const TAC *tac_end) : label_(std::move(label)) {
            if (tac_start)
                init(tac_start, tac_end);
        }
        // implement AbstractCFG
        [[nodiscard]] bool is_entry(const BasicBlock &bb) const override { return BasicBlock::is_entry(bb); }
        [[nodiscard]] bool is_exit(const BasicBlock &bb) const override { return BasicBlock::is_exit(bb); }
        [[nodiscard]] std::vector<BasicBlock *> nodes() const override {
            std::vector<BasicBlock *> result(blocks_.size());
            for (size_t i = 0; i < blocks_.size(); ++i) {
                result[i] = blocks_[i].get();
            }
            return result;
        }
        [[nodiscard]] std::vector<BasicBlock *> successors(const BasicBlock &bb) const override {
            std::vector succ{bb.fallthrough_};
            if (bb.ifz_) {
                succ.push_back(bb.ifz_);
            }
            return succ;
        }
        [[nodiscard]] std::vector<BasicBlock *> precursors(const BasicBlock &bb) const override {
            return bb.preds_;
        }

        // for(auto &it: function_cfg)
        FunctionCFGIterator begin() { return {this, 0}; }
        // ReSharper disable once CppMemberFunctionMayBeStatic
        FunctionCFGIterator end() { return {}; } // NOLINT(*-convert-member-functions-to-static)
        [[nodiscard]] std::string to_dot() const;
        [[nodiscard]] bool opt_constants_folding() const;
        [[nodiscard]] bool opt_common_subexpression_elimination() const;
    };

} // namespace cfg

#endif // CONTROL_FLOW_GRAPH_H
