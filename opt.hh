#ifndef OPT_HH
#define OPT_HH

#include <utility>
#include <vector>
#include <memory>
#include <string>
#include <map>
#include <filesystem>


#ifdef __cplusplus
extern "C"{
#endif

#include "tac.h"

#ifdef __cplusplus
}
#endif

namespace opt{
class BasicBlock
{
    int id_=END_BLOCK_ID;
public:
    static constexpr int END_BLOCK_ID = 0;
    static constexpr int BEGIN_BLOCK_ID = -1;
    TAC* begin_=nullptr;
    TAC* end_=nullptr;

    BasicBlock* fallthrough_=nullptr, *ifz_=nullptr;
    std::vector<BasicBlock*> preds_={};    // 前驱数组

    explicit BasicBlock(const int id): id_(id) {}
    explicit BasicBlock(const int id, TAC *begin): id_(id), begin_(begin) {}
    explicit BasicBlock(const int id, TAC *begin, TAC *end): id_(id), begin_(begin), end_(end) {}
    [[nodiscard]] int id() const { return id_; }
    static bool is_beginblock(const BasicBlock& block){
        return block.id_ == BEGIN_BLOCK_ID;
    }
    static bool is_endblock(const BasicBlock& block){
        return block.id_ == END_BLOCK_ID;
    }
    [[nodiscard]] bool opt_constants_folding() const;
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
};
class FunctionCFG
{
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
    FunctionCFG(std::string label, TAC *tac_start, const TAC *tac_end): label_(std::move(label)) {
        if (tac_start)
            init(tac_start, tac_end);
    }
    [[nodiscard]] std::string to_dot() const;
    [[nodiscard]] bool opt_constants_folding() const;
};

}

#endif  // OPT_HH
