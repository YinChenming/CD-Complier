#ifndef OPT_HH
#define OPT_HH

#include <vector>
#include <memory>
#include <string>
#include <map>
#include <filesystem>


#ifdef __cplusplus
extern "C"{
#endif

#include "tac.h"
extern void out_tac(FILE*, TAC*);

#ifdef __cplusplus
}
#endif

namespace opt{
class Block
{
public:
    inline static constexpr int END_BLOCK_ID = 0;
    inline static constexpr int BEGIN_BLOCK_ID = -1;
    int id_=END_BLOCK_ID;
    TAC* begin_=nullptr;
    TAC* end_=nullptr;

    Block* fallthrough_=nullptr, *ifz_=nullptr;
    std::vector<Block*> preds_={};    // 前驱数组

    explicit Block(int id): id_(id) {}
    explicit Block(int id, TAC *begin): id_(id), begin_(begin) {}
    explicit Block(int id, TAC *begin, TAC *end): id_(id), begin_(begin), end_(end) {}
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
private:
    std::map<std::string, std::unique_ptr<FunctionCFG>> functions_;
    void _init(TAC * tac_first);
public:
    explicit CFG(TAC *tac_first) {
        _init(tac_first);
    }
    void to_dot(const std::filesystem::path path) const;
    std::vector<std::string> to_dot() const;
};
class FunctionCFG
{
private:
    Block begin_block_ = Block(Block::BEGIN_BLOCK_ID),
    end_block_ = Block(Block::END_BLOCK_ID);
    std::vector<std::unique_ptr<Block>> blocks_;
    std::string label_;
    void _init(TAC * tac_first, TAC * tac_last);
    static std::string _block2dot(Block *);
public:
    friend class CFG;
    Block &get_beginblock() {
        return begin_block_;
    }
    Block &get_endblock() {
        return end_block_;
    }
    explicit FunctionCFG(const std::string &label): label_(label) { }
    FunctionCFG(const std::string &label, TAC *tac_start, TAC *tac_end): label_(label) {
        if (tac_start)
            _init(tac_start, tac_end);
    }
    std::string to_dot() const;
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
