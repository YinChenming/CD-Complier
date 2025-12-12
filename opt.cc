#include "cfg.hh"

#include <algorithm>
#include <cassert>
#include <queue>
#include <set>
#include <fstream>
#include <stdexcept> // for std::runtime_error
#include <vector>

#include "analysis.hh"
#include "df.hh"

#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif

#ifdef __cplusplus
extern "C" {
#endif
#include "tac.h"
#include "opt_conf.h"
#ifdef __cplusplus
}
#endif


using namespace cfg;
using namespace df;
using namespace df::analysis;

struct WhileBlock {
    BasicBlock *header, *preheader{nullptr};
    std::set<BasicBlock *> bodies;
    std::set<BasicBlock *> exits;
    std::set<TAC *> tacs;
    explicit WhileBlock(BasicBlock *header): header(header) {}
};
static void dfs(const FunctionCFG &cfg, BasicBlock *bb, std::map<BasicBlock*, int> &visited, std::vector<std::pair<BasicBlock*, BasicBlock*>> &while_blocks) {
    if (visited.count(bb)) return;
    visited[bb] = 1;
    std::vector<BasicBlock *> children;
    if (bb->fallthrough_) children.push_back(bb->fallthrough_);
    if (bb->ifz_) children.push_back(bb->ifz_);
    for (const auto &child: children) {
        if (cfg.is_entry(*child) || cfg.is_exit(*child)) continue;
        if (!visited.count(child)) {
            // child未访问
            dfs(cfg, child, visited, while_blocks);
        } else if (visited[child] == 1 && !cfg.is_entry(*bb)) {
            // 正在访问,是前驱节点
            while_blocks.emplace_back(child/* while header */, bb/* while tail */);
        } else {
            // 访问结束,是横边
        }
    }
    visited[bb] = 2;
}
static BasicBlock *copy_basic_block(FunctionCFG &cfg, const BasicBlock &bb) {
    if (bb.begin_ == nullptr && bb.end_ == nullptr) return nullptr;
    BasicBlock &new_bb = cfg.get_new_block();
    new_bb.begin_ = new_bb.end_ = mk_tac(bb.begin_->op, bb.begin_->a, bb.begin_->b, bb.begin_->c);
    if (new_bb.begin_->op == TAC_LABEL) {
        new_bb.begin_->a = mk_label(mk_lstr(next_label++));
    }
    for (TacProxy tac{bb.begin_->next}; tac && tac != bb.end_->next; tac = tac->next) {
        if (tac->op == TAC_LABEL) continue;
        if (tac.is_declaration()) continue;
        new_bb.insert_after(mk_tac(tac->op, tac->a, tac->b, tac->c));
        // new_bb.end_->next = mk_tac(tac->op, tac->a, tac->b, tac->c);
        // new_bb.end_->next->prev = new_bb.end_.get();
        // new_bb.end_ = new_bb.end_->next;
        // new_bb.end_->next = nullptr;
    }
    return &new_bb;
}
static std::vector<WhileBlock> get_while_blocks(FunctionCFG &fcfg) {
    std::map<BasicBlock*, int> visited;
    std::vector<std::pair<BasicBlock*, BasicBlock*>> while_blocks;
    dfs(fcfg, const_cast<BasicBlock *>(&fcfg.begin_block_), visited, while_blocks);
    std::vector<WhileBlock> results;
    results.reserve(while_blocks.size());
    for (const auto &[header, tail]: while_blocks) {
        results.emplace_back(header);
        WhileBlock &while_block = results.back();
        std::queue<BasicBlock *> queue;
        queue.push(tail);
        while (!queue.empty()) {
            BasicBlock *block = queue.front();
            queue.pop();
            if (while_block.bodies.count(block)) {continue;}
            while_block.bodies.insert(block);
            if (block == header) continue;
            for (const auto &tac: *block) {
                while_block.tacs.insert(tac.get());
            }
            for (const auto &pred: block->preds_) {
                queue.push(pred);
            }
        }
        for (const auto &body: while_block.bodies) {
            if (body->fallthrough_ && while_block.bodies.find(body->fallthrough_) == while_block.bodies.end()) {
                while_block.exits.insert(body);
            }
            if (body->ifz_ && while_block.bodies.find(body->ifz_) == while_block.bodies.end()) {
                while_block.exits.insert(body);
            }
        }

        // 如果header也是exit,就改写拆分header和exit
        while (while_block.exits.count(while_block.header)) {
            auto &new_bb = *copy_basic_block(fcfg, *while_block.header);
            // 让原来的 basic block 成为进入 while 循环的入口,新的 basic block 成为 exit
            new_bb.fallthrough_ = while_block.header->fallthrough_;
            new_bb.ifz_ = while_block.header->ifz_;
            if (new_bb.fallthrough_) {
                new_bb.fallthrough_->preds_.insert(&new_bb);
            }
            if (new_bb.ifz_) {
                if (BasicBlock &ft_bb = *new_bb.fallthrough_; ft_bb.begin_->op != TAC_LABEL) {
                    ft_bb.insert_before(mk_tac(TAC_LABEL, mk_label(mk_lstr(next_label++)), nullptr, nullptr));
                    // ft_bb.begin_->prev = mk_tac(TAC_LABEL, mk_label(mk_lstr(next_label++)), NULL, NULL);
                    // ft_bb.begin_->prev->next = ft_bb.begin_.get();
                    // ft_bb.begin_ = ft_bb.begin_->prev;
                }
                BasicBlock &tmp_bb = fcfg.get_new_block();
                tmp_bb.begin_ = tmp_bb.end_ = mk_tac(TAC_GOTO, new_bb.fallthrough_->begin_->a, nullptr, nullptr);
                tmp_bb.set_fallthrough(new_bb.fallthrough_);
                // tmp_bb.fallthrough_ = new_bb.fallthrough_;
                // tmp_bb.fallthrough_->preds_.insert(&tmp_bb);
                new_bb.set_fallthrough(&tmp_bb);
                // tmp_bb.preds_.insert(&new_bb);
                // new_bb.fallthrough_ = &tmp_bb;
                // tmp_bb用于维持原有跳转逻辑,也是循环块的一部分
                while_block.bodies.insert(&tmp_bb);

                new_bb.ifz_->preds_.insert(&new_bb);
            }

            decltype(while_block.header->preds_) del_bb;
            for (auto pred: while_block.header->preds_) {
                // 让所有外部输入仍保持不变,所有循环内部的跳转均指向新的 basic block
                // 这样运行结束后,原来的 basic block 所有入边均来自循环外
                if (while_block.bodies.count(pred)) {
                    if (pred->fallthrough_ == while_block.header) {
                        pred->fallthrough_ = &new_bb;
                        if (pred->end_->op == TAC_GOTO) {
                            if (new_bb.begin_->op != TAC_LABEL) {
                                new_bb.insert_before(mk_tac(TAC_LABEL, mk_label(mk_lstr(next_label++)), nullptr, nullptr));
                                // new_bb.begin_->prev = mk_tac(TAC_LABEL, mk_label(mk_lstr(next_label++)), NULL, NULL);
                                // new_bb.begin_->prev->next = new_bb.begin_.get();
                                // new_bb.begin_ = new_bb.begin_->prev;
                            }
                            pred->end_->a = new_bb.begin_->a;
                        }
                    } else if (pred->ifz_ == while_block.header) {
                        pred->ifz_ = &new_bb;
                        if (pred->end_->op == TAC_IFZ) {
                            if (new_bb.begin_->op != TAC_LABEL) {
                                new_bb.insert_before(mk_tac(TAC_LABEL, mk_label(mk_lstr(next_label++)), nullptr, nullptr));
                                // new_bb.begin_->prev = mk_tac(TAC_LABEL, mk_label(mk_lstr(next_label++)), NULL, NULL);
                                // new_bb.begin_->prev->next = new_bb.begin_.get();
                                // new_bb.begin_ = new_bb.begin_->prev;
                            }
                            pred->end_->a = new_bb.begin_->a;
                        }
                    } else {
                        assert(0);
                    }
                    new_bb.preds_.insert(pred);
                    del_bb.insert(pred);
                }
            }
            for (auto bb: del_bb) {
                while_block.header->preds_.erase(bb);
            }

            while_block.bodies.erase(while_block.header);
            while_block.exits.erase(while_block.header);
            while_block.bodies.insert(&new_bb);
            while_block.exits.insert(&new_bb);
            if (while_block.bodies.count(while_block.header->fallthrough_)) {
                while_block.header = while_block.header->fallthrough_;
            } else if (while_block.bodies.count(while_block.header->ifz_)) {
                while_block.header = while_block.header->ifz_;
            } else {
                assert(0);
            }
        }

        // 检查是否存在preheader
        auto pre_it = while_block.header->preds_.end();
        bool valid = true;
        for (auto it = while_block.header->preds_.begin(); it != while_block.header->preds_.end();++it) {
            if (while_block.bodies.count(*it)) continue;
            if (pre_it != while_block.header->preds_.end()) valid = false;
            pre_it = it;
        }
        BasicBlock *preheader = nullptr;
        if (pre_it != while_block.header->preds_.end()) {
            preheader = *pre_it;
        }
        if (!valid || preheader == nullptr || (preheader->ifz_ && preheader->fallthrough_ && (preheader->ifz_ != while_block.header || preheader->fallthrough_ != while_block.header))) {
            // 创建一个新的preheader
            BasicBlock &new_ph = fcfg.get_new_block();
            new_ph.fallthrough_ = while_block.header;
            while_block.header->preds_.insert(&new_ph);
            new_ph.ifz_ = nullptr;
            new_ph.begin_ = new_ph.end_ = mk_tac(TAC_LABEL, mk_label(mk_lstr(next_label++)), nullptr, nullptr);

            if (pre_it != while_block.header->preds_.end()) {
                while_block.header->preds_.erase(pre_it);
                new_ph.preds_.insert(preheader);
                if (preheader) {
                    if (preheader->ifz_ == while_block.header) {
                        assert(preheader->end_->op == TAC_IFZ);
                        preheader->end_->a = new_ph.begin_->a;
                        preheader->ifz_ = &new_ph;
                    } else {
                        if (preheader->end_->op == TAC_GOTO) {
                            preheader->end_->a = new_ph.begin_->a;
                        }
                        // fallthrough 不在此处特殊处理
                        preheader->fallthrough_ = &new_ph;
                    }
                }
            }
            if (while_block.header->begin_->op != TAC_LABEL) {
                while_block.header->insert_before(mk_tac(TAC_LABEL, mk_label(mk_lstr(next_label++)), nullptr, nullptr));
                // while_block.header->begin_->prev = mk_tac(TAC_LABEL, mk_label(mk_lstr(next_label++)), NULL, NULL);
                // while_block.header->begin_->prev->next = while_block.header->begin_.get();
                // while_block.header->begin_ = while_block.header->begin_->prev;
            }
            new_ph.insert_after(mk_tac(TAC_GOTO, while_block.header->begin_->a, nullptr, nullptr));
            // new_ph.end_ = mk_tac(TAC_GOTO, while_block.header->begin_->a, nullptr, nullptr);
            // new_ph.begin_->next = new_ph.end_.get();
            // new_ph.end_->prev = new_ph.begin_.get();

            while_block.preheader = &new_ph;
        } else {
            while_block.preheader = preheader;
        }
    }
    return results;
}
static bool opt_loop_invariant_code_motion(FunctionCFG &fcfg) {
    bool changed = false;
    auto while_blocks = get_while_blocks(fcfg);
    ReachingDefinitionSolver rd_solver;
    auto rd_result = *rd_solver.solve(fcfg).release();
    LiveVariableSolver lv_solver;
    auto rv_result = *lv_solver.solve(fcfg).release();

    printf("fcfg %p while analysis:\n", &fcfg);
    // ReSharper disable once CppDFAUnusedValue
    auto opt_single_loop = [&rd_result] (WhileBlock &while_block) -> bool {
        std::unordered_map<std::string, std::pair<BasicBlock*, TAC*>> invariant_vars;
        auto is_invariant_sym = [&while_block, &invariant_vars] (const SymProxy& sym, const ReachingDefinitionFacts &facts) -> bool {
            // 1. sym 是常数
            if (sym.is_const()) return true;
            auto rd_fact = facts.to_map();
            const auto &it = rd_fact.find(sym.name());
            // 2.1 ...
            if (it == rd_fact.end()) return true;
            // 2.3 sym 只有一个定值到达此处,并且该定值是循环不变量
            if (it->second.size() == 1) {
                const auto &tac = it->second.front();
                const SymProxy tac_a(tac->a);
                if (const auto &it_var = invariant_vars.find(tac_a.name()); it_var != invariant_vars.end() && it_var->second.second == tac)
                    return true;
            }
            // 2.2 所有到达bb的定值都在循环之外
            return std::all_of(it->second.begin(), it->second.end(), [&while_block](const auto &tac) -> bool {return !while_block.tacs.count(tac);});
        };
        decltype(invariant_vars) last_vars;
        do {
            last_vars = invariant_vars;
            for (const auto &bb: while_block.bodies) {
                ReachingDefinitionFacts rd_fact {rd_result.get_in_fact(*bb)};
                for (auto tac: *bb) {
                    do {
                        if (tac.is_declaration() || !tac.has_side_effect()) break;
                        // licm **不会**对简单的赋值语句做处理!其只针对计算表达式
                        if (const SymProxy sym_b(tac->b), sym_c(tac->c); tac.has_side_effect() && sym_b.is_const() && (!sym_c||sym_c.is_const())) break;
                        if (const SymProxy sym(tac->a); tac.use_a() && !is_invariant_sym(sym, rd_fact)) break;
                        if (const SymProxy sym(tac->b); tac.use_b() && (sym.get() == tac->a || !is_invariant_sym(sym, rd_fact))) break;
                        if (const SymProxy sym(tac->c); tac.use_c() && (sym.get() == tac->a || !is_invariant_sym(sym, rd_fact))) break;
                        std::string a_name(tac->a->name);
                        // 如果在循环中还有其他定值,那么这句话和原来那句都不是循环不变量
                        if (const auto &it_var = invariant_vars.find(a_name); it_var != invariant_vars.end()) {
                            if (it_var->second.second != tac.get()) {
                                invariant_vars.erase(it_var);
                            }
                            break;
                        }
                        assert(!a_name.empty());
                        assert(tac != nullptr);
                        invariant_vars[a_name] = std::make_pair(bb, tac.get());
                    } while (false);

                    if (!tac.has_side_effect() || tac.is_declaration()) continue;
                    if (const SymProxy sym_a(tac->a); sym_a.is_temporary()) continue;
                    const std::string name(tac->a->name);
                    // kill其他definition
                    ReachingDefinitionFacts kill_facts;
                    for (auto &other_df: rd_fact) {
                        if (other_df->a == tac->a) kill_facts += other_df;
                    }
                    rd_fact -= kill_facts;
                    // gen一个新的definition
                    rd_fact += tac.get();
                }
            }
        } while (last_vars != invariant_vars);
        printf("all loop-invariant code:");
        for (const auto &[name, tac_p]: invariant_vars) {
            const TacProxy tac(tac_p.second);
            printf(" %s;", tac.to_string().c_str());
        }
        printf("\n");
        if (invariant_vars.empty()) return false;

        auto get_invariant_tac_list = [] (decltype(invariant_vars) &invariant_vars) -> auto {
            std::unordered_map<std::string, std::vector<std::string>> to_edges;
            std::unordered_map<std::string, size_t> in_degrees;
            to_edges.reserve(invariant_vars.size());
            // 构建依赖关系
            for (const auto &[name, tac_p]: invariant_vars) {
                TacProxy tac(tac_p.second);
                if (const SymProxy sym(tac->b); sym && sym.is_variable() && invariant_vars.count(sym.name())) {
                    to_edges[sym.name()].push_back(name);
                    in_degrees[name] += 1;
                }
                if (const SymProxy sym(tac->c); sym && sym.is_variable() && invariant_vars.count(sym.name())) {
                    to_edges[sym.name()].push_back(name);
                    in_degrees[name] += 1;
                }
            }
            std::vector<std::pair<BasicBlock*, TAC*>> tacs;
            std::unordered_set<std::string> next_tacs;
            std::remove_reference_t<decltype(invariant_vars)> del_vars{invariant_vars};
            for (const auto &[name, tac_p]: invariant_vars) {
                if (!in_degrees.count(name)) next_tacs.insert(name);
            }
            while (!next_tacs.empty()) {
                auto tac_it = next_tacs.begin();
                assert(tac_it != next_tacs.end());
                auto var_name {*tac_it};
                next_tacs.erase(tac_it);
                tacs.push_back(invariant_vars[var_name]);
                assert(tacs.back().second != nullptr);
                del_vars.erase(var_name);
                if (const auto it = to_edges.find(var_name); it != to_edges.end()) {
                    for (const auto &to_name: it->second) {
                        in_degrees[to_name] -= 1;
                        if (!in_degrees[to_name]) next_tacs.insert(to_name);
                    }
                }
            }
            for (const auto &[name, tac_p]: del_vars) {
                invariant_vars.erase(name);
            }
            return tacs;
        };
        TacProxy begin{nullptr}, end{nullptr};
        auto tac_list = get_invariant_tac_list(invariant_vars);
        // TODO: 将这句话的判断提前
        if (tac_list.empty()) return false;

        // 连着赋值语句一起提出循环
        std::unordered_set<std::string> need_declarations;
        for (const auto &bb: while_block.bodies) {
            for (auto tac = bb->begin_; tac && tac != bb->end_->next; tac = tac->next) {
                if (tac.is_declaration()) {
                    if (const SymProxy sym(tac->a); invariant_vars.count(sym.name())) {
                        need_declarations.insert(sym.name());
                        bb->del_tac(tac);
                        // if (tac == bb->begin_) {
                        //     bb->begin_ = tac->next;
                        // } else {
                        //     tac->prev->next = tac->next;
                        // }
                        // if (tac == bb->end_) {
                        //     bb->end_ = tac->prev;
                        // } else {
                        //     tac->next->prev = tac->prev;
                        // }
                    }
                }
            }
        }
        for (auto &[bb, tac]: tac_list) {
            // 将tac从原来的位置删除
            bb->del_tac(tac);
            tac->prev = tac->next = nullptr;
            // if (tac == bb->end_.get()) {
            //     bb->end_ = tac->prev;
            // } else {
            //     tac->next->prev = tac->prev;
            // }
            // if (tac == bb->begin_.get()) {
            //     bb->begin_ = tac->next;
            // } else {
            //     tac->prev->next = tac->next;
            // }
            // 把tac移到新的位置
            if (!begin || !end) {
                begin = end = tac;
            } else {
                end->next = tac;
                tac->prev = end.get();
                end = tac;
            }
            if (const SymProxy sym(tac->a); need_declarations.count(sym.name())) {
                TAC *decl = mk_tac(TAC_VAR, tac->a, NULL, NULL);
                decl->prev = end->prev;
                decl->next = end.get();
                if (end->prev) end->prev->next = decl;
                end->prev = decl;
                if (begin == end) begin = decl;
            }
            TacProxy tacp(tac);
            printf("block %d: move '%s' outside of while-loop\n", bb->id(), tacp.to_string().c_str());
        }
        // 把循环不变量插入头部之前
        BasicBlock &preheader = *while_block.preheader;
        if (preheader.begin_ == preheader.end_) {
            if (preheader.begin_.is_goto()) {
                preheader.begin_ = begin;
                end->next = preheader.end_.get();
                preheader.end_->prev = end.get();
            } else {
                preheader.end_->next = end.get();
                end->prev = preheader.end_.get();
                preheader.end_ = end;
            }
        } else {
            if (preheader.end_.is_goto()) {
                begin->prev = preheader.end_->prev;
                preheader.end_->prev->next = begin.get();
                end->next = preheader.end_.get();
                preheader.end_->prev = end.get();
            } else {
                begin->prev = preheader.end_.get();
                preheader.end_->next = begin.get();
                preheader.end_ = end;
            }
        }
        printf("while block %d: move to block %d\n", while_block.header->id(), preheader.id());
        return true;
    };
    for (auto &while_block: while_blocks) {
        printf("while block:\nstart at block %d, bodies:", while_block.header->id());
        for (const auto &body: while_block.bodies) {
            printf(" %d,", body->id());
        }
        printf(" exits:");
        for (const auto &while_exit: while_block.exits) {
            printf(" %d,", while_exit->id());
        }
        printf("\n");
        changed |= opt_single_loop(while_block);
    }
    return changed;
}

static bool opt_dead_code_elimination(const CFG *cfg) {
    bool changed = false;
    for (auto &fcfg_kv: cfg->functions_) {
        FunctionCFG& fcfg = *fcfg_kv.second;
        LiveVariableSolver solver;
        auto result = *solver.solve(fcfg).release();
        for (auto &bb: fcfg.blocks_) {
            const LiveVariableFacts &out_fact = result.get_out_fact(*bb);
            LiveVariableFacts facts ( out_fact);
            for (auto tac = bb->end_; tac && tac != bb->begin_->prev; tac = tac->prev) {
                if (tac.has_side_effect() && !tac.is_declaration()) {
                    const SymProxy a(tac->a);
                    if (facts.contains(a) && !(tac.is_assignment() && tac->a == tac->b)) {
                        facts -= a;
                    } else {
                        // 无用赋值,删除
                        printf("block %d: delete '%s'\n", bb->id(), tac.to_string().c_str());
                        bb->del_tac(tac);
                        // if (tac==bb->end_) {
                        //     bb->end_ = tac->prev;
                        // } else {
                        //     tac->next->prev = tac->prev;
                        // }
                        // if (tac==bb->begin_) {
                        //     bb->begin_ = tac->next;
                        // } else {
                        //     tac->prev->next = tac->next;
                        // }
                        changed = true;
                        continue;
                    }
                }
                if (tac.use_a()) {
                    const SymProxy a(tac->a);
                    if (!a.is_const())
                        facts += a;
                }
                if (tac.use_b()) {
                    // gen b
                    const SymProxy b(tac->b);
                    if (!b.is_const())
                        facts += b;
                }
                if (tac.use_c()) {
                    const SymProxy c(tac->c);
                    if (!c.is_const())
                        facts += c;
                }
            }
        }
    }
    return changed;
}

static bool opt_constant_and_copy_propagation(const CFG *cfg) {
    bool changed = false;
    for (auto &fcfg_kv: cfg->functions_) {
        FunctionCFG& fcfg = *fcfg_kv.second;
        ReachingDefinitionSolver solver;
        auto result = *solver.solve(fcfg).release();
        for (auto &bb: fcfg.blocks_) {
            auto definitions = result.get_in_fact(*bb).to_map();
            for (auto &tac: *bb) {
                if (tac.use_a()) {
                    const SymProxy a(tac->a);
                    const auto &defs = definitions.find(a.name());
                    if (defs != definitions.end() && defs->second.size() == 1) {
                        if (const TacProxy another_tac(defs->second.front()); another_tac.is_assignment()) {
                            const SymProxy another(another_tac->b);
                            if (another.is_const() || strcmp(tac->a->name, another->name)>0) {
                                printf("block %d: '%s' replace tac->a('%s') to '%s'\n", bb->id(),tac.to_string().c_str(), tac->a->name, another->name);
                                tac->a = another.get();
                                changed = true;
                            }
                        }
                    }
                }
                if (tac.use_b()) {
                    const SymProxy b(tac->b);
                    const auto &defs = definitions.find(b.name());
                    if (defs != definitions.end() && defs->second.size() == 1) {
                        if (const TacProxy another_tac(defs->second.front()); another_tac.is_assignment()) {
                            const SymProxy another(another_tac->b);
                            if (another.is_const() || strcmp(tac->b->name, another->name)>0) {
                                printf("block %d: '%s' replace tac->b('%s') to '%s'\n", bb->id(), tac.to_string().c_str(), tac->b->name, another->name);
                                tac->b = another.get();
                                changed = true;
                            }
                        }
                    }
                }
                if (tac.use_c()) {
                    const SymProxy c(tac->c);
                    const auto &defs = definitions.find(c.name());
                    if (defs != definitions.end() && defs->second.size() == 1) {
                        if (const TacProxy another_tac(defs->second.front()); another_tac.is_assignment()) {
                            const SymProxy another(another_tac->b);
                            if (another.is_const() || strcmp(tac->c->name, another->name)>0) {
                                printf("block %d: '%s' replace tac->c('%s') to '%s'\n", bb->id(), tac.to_string().c_str(), tac->c->name, another->name);
                                tac->c = another.get();
                                changed = true;
                            }
                        }
                    }
                }

                if (tac.has_side_effect() && !tac.is_declaration()) {
                    const std::string name{tac->a->name};
                    definitions.erase(name);
                    definitions[name].push_back(tac.get());
                }
            }
            // 对条件跳转结尾优化
            if (const SymProxy cond(bb->end_->b);bb->end_.is_if() && cond.is_const()) {
                // fall through
                if (cond->value && bb->ifz_) {
                    printf("block %d: always go to fallthrough!\n", bb->id());
                    BasicBlock &ifz_bb = *bb->ifz_;
                    changed = true;
                    ifz_bb.preds_.erase(bb.get());
                    bb->ifz_ = nullptr;
                    bb->end_ = bb->end_->prev;
                } else if (!cond->value && bb->fallthrough_) {
                    printf("block %d: always go to ifz!\n", bb->id());
                    BasicBlock &fallthrough_bb = *bb->fallthrough_;
                    changed = true;
                    fallthrough_bb.preds_.erase(bb.get());
                    bb->fallthrough_ = bb->ifz_;
                    bb->ifz_ = nullptr;
                    bb->end_->op = TAC_GOTO;
                    bb->end_->b = nullptr;
                }
            }
            if (bb->fallthrough_ == bb->ifz_ && bb->end_->op == TAC_IFZ) {
                bb->ifz_ = nullptr;
                bb->end_->op = TAC_GOTO;
                bb->end_->b = nullptr;
                changed = true;
            }
        }
    }
    return changed;
}
static bool opt_common_subexpression_elimination(const CFG *cfg) {
    bool changed = false;
    for (auto &fcfg_kv: cfg->functions_) {
        FunctionCFG &fcfg = *fcfg_kv.second;
        AvailableExpressionSolver ae_solver;
        auto ae_result = *ae_solver.solve(fcfg).release();
        ReachingDefinitionSolver rd_solver;
        auto rd_result = *rd_solver.solve(fcfg).release();
        for (auto &bb: fcfg.blocks_) {
            AvailableExpressionFacts ae_facts {ae_result.get_in_fact(*bb)};
            ReachingDefinitionFacts rd_facts {rd_result.get_in_fact(*bb)};
            for (auto tac = bb->begin_; tac && tac != bb->end_->next; tac = tac->next) {
                if (tac.is_computable()) {
                    if (Expression exp(tac.get()); ae_facts.contains(exp)) {
                        // 尝试寻找可以替换的表达式
                        auto rd_facts_map = rd_facts.to_map();
                        SYM *possible_sym = nullptr;
                        for (auto fact: rd_facts) {
                            if (fact->a == tac->a) continue;
                            if (const Expression fact_exp(fact); fact_exp == exp) {
                                auto it = rd_facts_map.find(fact->a->name);
                                // 如果该变量到此刻的所有定值均为该expression,我们就用该变量替换
                                if (it != rd_facts_map.end()) {
                                    auto it_rds = it->second.begin();
                                    for (;it_rds!=it->second.end(); ++it_rds) {
                                        if (const Expression rd_exp(*it_rds);!(rd_exp==exp)) break;
                                    }
                                    if (it_rds == it->second.end()) {
                                        possible_sym = fact->a;
                                        break;
                                    }
                                }
                            }
                        }
                        if (possible_sym) {
                            printf("block %d: '%s = %s' -> '%s = %s'\n", bb->id(), tac->a->name, exp.to_string().c_str(), tac->a->name, possible_sym->name);
                            tac->op = TAC_COPY;
                            tac->b = possible_sym;
                            tac->c = nullptr;
                            changed = true;
                        }
                    } else {
                        ae_facts += exp;
                    }
                }

                if (tac.has_side_effect() && !tac.is_declaration()) {
                    AvailableExpressionFacts kill;
                    for (const auto &it: ae_facts) {
                        if (it.b == tac->a || it.c == tac->a) {
                            kill += it;
                        }
                    }
                    ae_facts -= kill;

                    const SymProxy a(tac->a);
                    ReachingDefinitionFacts kill_facts;
                    for (auto fact: rd_facts) {
                        if (const SymProxy sym(fact->a); a.name() == sym.name()) {
                            kill_facts += fact;
                        }
                    }
                    rd_facts -= kill_facts;
                    rd_facts += tac.get();
                }
            }
        }

    }
    return changed;
}

EXTERNC
CFG *cfg_init(TAC *tac) {
    try{
        return new CFG(tac);
    } catch(const std::exception &e) {
        return nullptr;
    }
}
EXTERNC
void cfg_free(CFG *cfg) {
    if (cfg) {
        tac_first = cfg->to_tac().first;
        try {
            delete cfg;
        } catch (const std::exception &e) {
            ;;;
        }
    }
}
EXTERNC
void cfg_to_dot(const CFG *cfg, const char *path) {
    if (!path || !cfg)
        return;
    try {
        cfg->to_dot(std::string(path));
    } catch (const std::exception &e) {
        ;;;
    }
}

EXTERNC
int run_local_optimization(CFG *cfg, LocalOptimizationConfig conf){
    if (!cfg) return 0;
    int opt_count = 0;
    if (!conf.ignore_constant_folding) {
        if (cfg->opt_constants_folding()) opt_count++;
    }
    if (!conf.ignore_common_subexpression_elimination) {
        if (cfg->opt_common_subexpression_elimination()) opt_count++;
    }
    return opt_count;
}
EXTERNC
int run_global_optimization(CFG *cfg, GlobalOptimizationConfig conf){
    if (!cfg) return 0;
    if (conf.dataflow_analysis_report_path) {
        std::ofstream ofs(conf.dataflow_analysis_report_path, std::ios::app);

        ofs << "************ new dataflow analysis result ************" << std::endl;
        // 运行LiveVariableAnalysis
        auto rv_solver = LiveVariableSolver();
        auto &fcfg = *cfg->get_function("main");
        auto lv_result = *rv_solver.solve(fcfg);
        // printf("Live variable analysis results:\n");
        ofs << "Live variable analysis results:\n";
        for (const auto &node: fcfg.nodes()) {
            // printf("Block %d:\n", node->id());
            ofs << "Block " << node->id() << ":\n";
            const auto in_fact = lv_result.get_in_fact(*node);
            std::vector in_fact_vec(in_fact.begin(), in_fact.end());
            const auto out_fact = lv_result.get_out_fact(*node);
            std::vector out_fact_vec(out_fact.begin(), out_fact.end());
            // printf("\t in live variables:");
            ofs << "\t in live variables:";
            std::sort(in_fact_vec.begin(), in_fact_vec.end());
            std::sort(out_fact_vec.begin(), out_fact_vec.end());
            for (const auto &fact: in_fact_vec) {
                // printf(" %s", fact.name().c_str());
                ofs << " " << fact.name();
            }
            // printf("\n\tout live variables:");
            ofs << "\n\tout live variables:";
            for (const auto &fact: out_fact_vec) {
                // printf(" %s", fact.name().c_str());
                ofs << " " << fact.name();
            }
            // printf("\n");
            ofs << std::endl;
        }
        ofs << std::endl;

        auto rd_solver = ReachingDefinitionSolver();
        const auto rv_result = rd_solver.solve(fcfg);
        // printf("Reaching definitions results:\n");
        ofs << "Reaching definitions results:" << std::endl;
        for (const auto &bb: fcfg.nodes()) {
            // printf("Block %d:\n", bb->id());
            ofs << "Block " << bb->id() << ":\n";
            // printf("\t in reaching definitions:");
            ofs << "\t in reaching definitions:";
            for (const auto &tac: rv_result->get_in_fact(*bb)) {
                const TacProxy tp(tac);
                // printf(" %s;", tp.to_string().c_str());
                ofs << " " << tp.to_string() << ";";
            }
            // printf("\n\tout reaching definitions:");
            ofs << "\n\tout reaching definitions:";
            for (const auto &tac: rv_result->get_out_fact(*bb)) {
                const TacProxy tp(tac);
                // printf(" %s;", tp.to_string().c_str());
                ofs << " " << tp.to_string() << ";";
            }
            // printf("\n");
            ofs << std::endl;
        }
        ofs << std::endl;

        auto ae_solver = AvailableExpressionSolver();
        const auto rv_ae_result = ae_solver.solve(fcfg);
        // printf("Available expression solver results:\n");
        ofs << "Available expression solver results:" << std::endl;
        for (const auto &bb: fcfg.nodes()) {
            // printf("Block %d:\n", bb->id());
            ofs << "Block " << bb->id() << ":\n";
            // printf("\t in available expressions:");
            ofs << "\t in available expressions:";
            for (const auto &exp: rv_ae_result->get_in_fact(*bb)) {
                // printf(" ");
                ofs << " " << exp.to_string() << ";";
                // printf(";");
            }
            ofs << "\n\tout available expressions:";
            // printf("\n");
            // printf("\t out available expressions:");
            for (const auto &exp: rv_ae_result->get_out_fact(*bb)) {
                // printf(" ");
                ofs << " " << exp.to_string() << ";";
                // printf(";");
            }
            // printf("\n");
            ofs << std::endl;
        }

        ofs << std::endl;
        ofs.close();
    }

    int opt_count = 0;
    if (!conf.ignore_dead_code_elimination) {
        opt_count += opt_dead_code_elimination(cfg);
        cfg->remove_unnecessary_gotos_and_labels();
        // cfg->combine_fallthrough();
    }
    if (!conf.ignore_constant_and_copy_propagation) {
        opt_count += opt_constant_and_copy_propagation(cfg);
        cfg->remove_unnecessary_gotos_and_labels();
        // cfg->combine_fallthrough();
    }
    if (!conf.ignore_common_subexpression_elimination) {
        opt_count += opt_common_subexpression_elimination(cfg);
        cfg->remove_unnecessary_gotos_and_labels();
        // cfg->combine_fallthrough();
    }
    if (!conf.ignore_loop_invariant_code_motion) {
        for (const auto &it: cfg->functions_) {
            opt_count += opt_loop_invariant_code_motion(*it.second);
        }
        cfg->remove_unnecessary_gotos_and_labels();
        // cfg->combine_fallthrough();
    }
    return opt_count;
}

EXTERNC
int run_optimization(CFG *cfg, LocalOptimizationConfig local_conf, GlobalOptimizationConfig global_conf){
    if (!cfg) return 0;
    int opt_count = 0;
    opt_count += run_local_optimization(cfg, local_conf);
    opt_count += run_global_optimization(cfg, global_conf);
    opt_count += cfg->remove_unreachable_blocks();
    return opt_count;
}
