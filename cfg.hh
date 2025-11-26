#ifndef CONTROL_FLOW_GRAPH_H
#define CONTROL_FLOW_GRAPH_H

#include <vector>
#include <string>
#include <memory>
#include <filesystem>
#include <map>
#include <unordered_map>
#include <cstring>

extern "C" {
#include "tac.h"
}

namespace cfg{
template<typename Node>
class AbstractCFG{
public:
    AbstractCFG(){};
    virtual ~AbstractCFG() = default;
    [[nodiscard]] virtual bool is_exit(const Node&) const = 0;
    [[nodiscard]] virtual bool is_entry(const Node&) const = 0;
    [[nodiscard]] virtual const std::vector<Node*>&nodes() const = 0;
    [[nodiscard]] virtual const std::vector<Node*>&successors(const Node&) const = 0;
    [[nodiscard]] virtual const std::vector<Node*>&precursors(const Node&) const = 0;
};
// 强兼struct tac类型
// 应当注意,这里tac_可能为nullptr,nullptr也可能隐式转换成TacProxy对象
class TacProxy {
    TAC *tac_ = nullptr;
    void init_() {
        if (is_binocular() && is_exchangeable()) {
            // 确保如果可交换的话让第一个参数的名字小于第二个参数
            if (strcmp(tac_->b->name, tac_->c->name)>0) {
                std::swap(tac_->b, tac_->c);
            }
        }
    }
public:
    // 强兼struct tac*,支持隐式转成代理类
    // no explicit
    TacProxy(TAC *tac): tac_(tac) {init_();}
    TAC *operator->() const {
        return tac_;
    }
    const bool operator<(const TAC *tac) const {
        if (!tac_ && !tac) return false;
        if (!tac_ && tac) return true;
        if (tac_ && !tac) return false;
        if (tac_->op < tac->op) return true;
        if (tac_->a != tac->a) {
            if (strcmp(tac_->a->name, tac->a->name)<0) return true;
        }
        if (tac_->b != tac->b) {
            if (strcmp(tac_->b->name, tac->b->name)<0) return true;
        }
        if (tac_->c != tac->c) {
            if (strcmp(tac_->c->name, tac->c->name)<0) return true;
        }
        return false;
    }
    const bool operator<(const TacProxy &tac) const {
        return *this<tac.tac_;
    }
    const bool operator>=(const TAC *tac) const {
        return !(*this<tac);
    }
    const bool operator>=(const TacProxy &tac) const {
        return !(*this<tac.tac_);
    }
    const bool operator==(const TAC *tac) const {
        if (!tac_ && !tac) return true;
        if (!tac_ || !tac) return false;
        if (tac_->op != tac->op) return false;
        if (tac_->a != tac->a && (!tac_->a || !tac->a || strcmp(tac_->a->name, tac->a->name)!=0)) return false;
        if (tac_->b != tac->b && (!tac_->b || !tac->b || strcmp(tac_->b->name, tac->b->name)!=0)) return false;
        if (tac_->c != tac->c && (!tac_->c || !tac->c || strcmp(tac_->c->name, tac->c->name)!=0)) return false;
        return true;
    }
    const bool operator==(const TacProxy &tac) const {
        return *this == tac.tac_;
    }
    const bool operator!=(const TAC *tac) const {
        return !(*this==tac);
    }
    const bool operator!=(const TacProxy &tac) const {
        return !(*this==tac.tac_);
    }
    const bool operator<=(const TAC *tac) const {
        return *this==tac || *this<tac;
    }
    const bool operator<=(const TacProxy &tac) const {
        return *this==tac.tac_ || *this<tac.tac_;
    }
    const bool operator>(const TAC *tac) const {
        return !(*this<=tac);
    }
    const bool operator>(const TacProxy &tac) const {
        return !(*this<=tac.tac_);
    }
    explicit operator bool() const {
        return tac_;
    }
    // 强兼struct tac*,可以隐式将代理类转回struct tac*
    // 不要加explicit
    operator TAC*() const {
        return tac_;
    }
    bool is_computable() const {
        if (!tac_) return false;
        if (tac_->op >= TAC_MIN_CALC && tac_->op <= TAC_MAX_CALC) return true;
        return false;
    }
    bool is_exchangeable() const {
        if (!tac_) return false;
        if (tac_->op == TAC_ADD || tac_->op == TAC_MUL) return true;
        if (tac_->op == TAC_EQ || tac_->op == TAC_NE) return true;
        return false;
    }
    bool is_monocular() const {
        if (!tac_) return false;
        return !tac_->c && tac_->a && tac_->b;
    }
    bool is_binocular() const {
        if (!tac_) return false;
        return tac_->c && tac_->b && tac_->a;
    }
    bool has_side_effect() const {
        if (!tac_) return false;
        if (tac_->op == TAC_COPY) return true;
        if (tac_->op == TAC_INPUT) return true;
        if (is_computable()) return true;
        return false;
    }
    TAC *get() {
        return tac_;
    }
    void set(TAC *tac) {
        tac_ = tac;
    }

    struct Hash {
        const size_t operator()(const TacProxy &tac) const {
            if (!tac) return -1;
            auto hash = std::hash<int>()(tac.tac_->op);
            if (tac.tac_->a) hash ^= std::hash<std::string>()(tac.tac_->a->name);
            if (tac.tac_->b) hash ^= std::hash<std::string>()(tac.tac_->b->name);
            if (tac.tac_->c) hash ^= std::hash<std::string>()(tac.tac_->c->name);
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
    SymProxy(): sym_(nullptr), has_set_name_(false) {}
    // no explicit
    SymProxy(SYM *sym): sym_(sym), has_set_name_(sym!=nullptr), name_(sym && sym->name ? sym->name : "") {}
    SYM *operator->() const {
        return sym_;
    }
    const bool operator==(const SYM *sym) const {
        if (sym_ == sym) return true;
        if (!sym_ || !sym) return false;
        return sym_->type == sym->type && strcmp(sym_->name, sym->name) == 0;
    }
    const bool operator==(const SymProxy &sym) const {return *this==sym.sym_;}
    const bool operator!=(const SYM *sym) const {
        if (sym_ == sym) return false;
        if (!sym_ || !sym) return false;
        return sym_->type != sym->type || strcmp(sym_->name, sym->name) != 0;
    }
    const bool operator!=(const SymProxy &sym) const {return *this!=sym.sym_;}
    const bool operator<(const SYM *sym) const {
        if (!sym_ && !sym) return false;
        if (!sym_ && sym) return true;
        if (sym_ && !sym) return false;
        return strcmp(sym_->name, sym->name) < 0;
    }
    const bool operator<(const SymProxy &sym) const {return *this<sym.sym_;}
    const bool operator<=(const SYM *sym) const {
        if (!sym_ && !sym) return true;
        if (!sym_ && sym) return true;
        if (sym_ && !sym) return false;
        return strcmp(sym_->name, sym->name) <= 0;
    }
    const bool operator<=(const SymProxy &sym) const {return *this<=sym.sym_;}
    const bool operator>(const SYM *sym) const {
        if (!sym_ && !sym) return false;
        if (!sym_ && sym) return false;
        if (sym_ && !sym) return true;
        return strcmp(sym_->name, sym->name) > 0;
    }
    const bool operator>(const SymProxy &sym) const {return *this>sym.sym_;}
    const bool operator>=(const SYM *sym) const {
        if (!sym_ && !sym) return true;
        if (!sym_ && sym) return false;
        if (sym_ && !sym) return true;
        return strcmp(sym_->name, sym->name) >= 0;
    }
    const bool operator>=(const SymProxy &sym) const {return *this>=sym.sym_;}
    explicit operator bool() const {
        return sym_;
    }
    operator SYM*() const {
        return sym_;
    }

    SYM *get() const {
        return sym_;
    }
    void set(SYM *sym) {
        sym_ = sym;
        if (sym_ != nullptr) {
            name_ = std::string(sym_->name);
            has_set_name_ = true;
        } else {
            has_set_name_ = false;
        }
    }
    const std::string &name() const {
        if (has_set_name_) return name_;
        return "";
    }

    struct Hash {
        const size_t operator()(const SymProxy &sym) const {
            if (!sym) return -1;
            return std::hash<int>()(sym->type) ^ std::hash<int>()(sym->scope) ^ std::hash<std::string>()(sym.name());
        }
    };
};
class BasicBlock
{
    int id_=END_BLOCK_ID;
public:
    static constexpr int END_BLOCK_ID = 0;
    static constexpr int BEGIN_BLOCK_ID = -1;
    TacProxy begin_=nullptr;
    TacProxy end_=nullptr;

    BasicBlock* fallthrough_=nullptr, *ifz_=nullptr;
    std::vector<BasicBlock*> preds_={};    // 前驱数组

    explicit BasicBlock(const int id): id_(id) {}
    explicit BasicBlock(const int id, TAC *begin): id_(id), begin_(begin) {}
    explicit BasicBlock(const int id, TAC *begin, TAC *end): id_(id), begin_(begin), end_(end) {}
    ~BasicBlock() = default;
    [[nodiscard]] int id() const { return id_; }
    static bool is_beginblock(const BasicBlock& block){
        return block.id_ == BEGIN_BLOCK_ID;
    }
    static bool is_endblock(const BasicBlock& block){
        return block.id_ == END_BLOCK_ID;
    }
    [[nodiscard]] bool opt_constants_folding() const;
    [[nodiscard]] bool opt_common_subexpression_elimination() const;
};

class FunctionCFG;
class CFG
{
    std::map<std::string, std::unique_ptr<FunctionCFG>> functions_;
    std::vector<SYM *> global_vars_;
    void init(const TAC *tac);
public:
    explicit CFG(const TAC *tac) {
        init(tac);
    }
    [[nodiscard]] std::string global_vars_to_dot() const;
    void to_dot(const std::filesystem::path &path) const;
    [[nodiscard]] std::vector<std::string> to_dot() const;
    [[nodiscard]] bool opt_constants_folding() const;
    [[nodiscard]] bool opt_common_subexpresson_elimination() const;
};

class FunctionCFG: public AbstractCFG<BasicBlock>
{
    class FunctionCFGIterator {
        FunctionCFG *cfg_ = nullptr;
        size_t index_ = -1;
    public:
        FunctionCFGIterator(): cfg_(nullptr), index_(-1) {}
        FunctionCFGIterator(const FunctionCFGIterator& other): cfg_(other.cfg_), index_(other.index_) {}
        FunctionCFGIterator(FunctionCFG *cfg, size_t i): cfg_(cfg) {
            if (cfg==nullptr) {
                cfg_ = nullptr;
                index_ = -1;
                return;
            }
            if (i<cfg_->blocks_.size()) {
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
        FunctionCFGIterator  operator++(int) {
            auto cfg = cfg_;
            auto index = index_;
            ++*this;
            return FunctionCFGIterator(cfg, index);
        }
        bool operator==(const FunctionCFGIterator &other) {
            return cfg_ == other.cfg_ && index_ == other.index_;
        }
        BasicBlock *operator->() {
            if (!cfg_ || index_ == -1)
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
    BasicBlock &get_beginblock() {
        return begin_block_;
    }
    BasicBlock &get_endblock() {
        return end_block_;
    }
    explicit FunctionCFG(std::string label): label_(std::move(label)) { }
    ~FunctionCFG() override = default;
    FunctionCFG(std::string label, TAC *tac_start, const TAC *tac_end): label_(std::move(label)) {
        if (tac_start)
            init(tac_start, tac_end);
    }
    // implement AbstractCFG
    [[nodiscard]] bool is_entry(const BasicBlock &bb) const override {
        return BasicBlock::is_beginblock(bb);
    }
    [[nodiscard]] bool is_exit(const BasicBlock &bb) const override {
        return BasicBlock::is_endblock(bb);
    }
    [[nodiscard]] const std::vector<BasicBlock*> &nodes() const override {
        std::vector<BasicBlock*> result(blocks_.size());
        for (size_t i=0; i<blocks_.size(); ++i) {
            result[i] = blocks_[i].get();
        }
        return result;
    }
    [[nodiscard]] const std::vector<BasicBlock*>&successors(const BasicBlock& bb) const override {
        std::vector<BasicBlock*> succ = {bb.fallthrough_};
        if (bb.ifz_) {
            succ.push_back(bb.ifz_);
        }
        return succ;
    }
    [[nodiscard]] const std::vector<BasicBlock*>&precursors(const BasicBlock& bb) const override {
        return bb.preds_;
    }

    // for(auto &it: function_cfg)
    FunctionCFGIterator begin() {
        return {this, 0};
    }
    FunctionCFGIterator end() {
        return {};
    }
    [[nodiscard]] std::string to_dot() const;
    [[nodiscard]] bool opt_constants_folding() const;
    [[nodiscard]] bool opt_common_subexpression_elimination() const;
};

}

#endif  // CONTROL_FLOW_GRAPH_H