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
class Block
{
    int id_=END_BLOCK_ID;
public:
    static constexpr int END_BLOCK_ID = 0;
    static constexpr int BEGIN_BLOCK_ID = -1;
    TAC* begin=nullptr;
    TAC* end=nullptr;

    Block* fallthrough_=nullptr, *ifz_=nullptr;
    std::vector<Block*> preds_={};    // 前驱数组

    explicit Block(const int id): id_(id) {}
    explicit Block(const int id, TAC *begin): id_(id), begin(begin) {}
    explicit Block(const int id, TAC *begin, TAC *end): id_(id), begin(begin), end(end) {}
    [[nodiscard]] int id() const { return id_; }
    inline static bool is_beginblock(const Block& block){
        return block.id_ == Block::BEGIN_BLOCK_ID;
    }
    inline static bool is_endblock(const Block& block){
        return block.id_ == Block::END_BLOCK_ID;
    }
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
};
class FunctionCFG
{
private:
    Block begin_block_ = Block(Block::BEGIN_BLOCK_ID),
    end_block_ = Block(Block::END_BLOCK_ID);
    std::vector<std::unique_ptr<Block>> blocks_;
    std::string label_;
    void init(TAC *start_tac, const TAC *end_tac);
    static std::string block2dot(Block *);
public:
    friend class CFG;
    Block &get_beginblock() {
        return begin_block_;
    }
    Block &get_endblock() {
        return end_block_;
    }
    explicit FunctionCFG(std::string label): label_(std::move(label)) { }
    FunctionCFG(std::string label, TAC *tac_start, const TAC *tac_end): label_(std::move(label)) {
        if (tac_start)
            init(tac_start, tac_end);
    }
    [[nodiscard]] std::string to_dot() const;
};

};


#ifdef __cplusplus
extern "C"{
#endif

void run_optimization();
void run_local_optimization();
void run_global_optimization();

#ifdef __cplusplus
};
#endif


#endif  // OPT_HH
