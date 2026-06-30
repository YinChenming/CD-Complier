#include "cfg.hh"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <list>
#include <set>
#include <sstream>
#include <stack>
#include <stdexcept> // for std::runtime_error

using namespace cfg;

static bool is_label(const int op){
    return op == TAC_LABEL;
}
static bool is_term(const int op){
    switch (op) {
        case TAC_GOTO: case TAC_IFZ: case TAC_RETURN: case TAC_ENDFUNC:
            return true;
        default: return false;
    }
}

void CFG::init(const TAC *tac) {
    const TAC *current_tac = tac;

    while (current_tac) {
        if (is_label(current_tac->op) && current_tac->next && current_tac->next->op == TAC_BEGINFUNC) {
            const SYM *func_sym = current_tac->a;
            if (!func_sym || func_sym->type != SYM_LABEL) {
                current_tac = current_tac->next;
                continue;
            }

            TAC *func_start = current_tac->next->next; // 函数体的第一条指令
            TAC *func_end = nullptr;
            TAC *t = func_start;
            while (t && t->op != TAC_ENDFUNC) {
                t = t->next;
            }
            func_end = t; // func_end 现在指向 TAC_ENDFUNC (或 nullptr)

            std::string func_name = func_sym->name;
            auto func_cfg = std::make_unique<FunctionCFG>(func_name, func_start, func_end);

            functions_[func_name] = std::move(func_cfg);

            current_tac = func_end ? func_end->next : nullptr;
        } else if (current_tac->op == TAC_VAR){
            if (current_tac->a)
                global_vars_.push_back(current_tac->a);
            current_tac = current_tac->next;
        } else {
            // 不能在顶层定义除了TAC_VAR以外的三地址码
            throw std::runtime_error("cannot set global TAC except TAC_VAR!");
        }
    }
}

#define CONNECT_(front, back, TYPE)\
do{\
    (front)->TYPE = (back);\
    (back)->preds_.insert(front);\
} while (0)
#define CONNECT(front, back, TYPE) CONNECT_(front, back, TYPE##_)

void FunctionCFG::init(TAC *start_tac, const TAC *end_tac) {
    if (!start_tac) return;

    std::set<TAC*> leaders;
    std::map<std::string, BasicBlock*> labeled_blocks;

    // 第一条指令是Leader
    leaders.insert(start_tac);

    for (TAC *t = start_tac; t != end_tac; t = t->next) {
        if (!t) break;
        // 跳转指令的目标(label)是block header
        if (is_label(t->op)) {
            leaders.insert(t);
        }
        // 跳转指令的下一条是block header
        if (is_term(t->op) && t->next && t->next != end_tac) {
            leaders.insert(t->next);
        }
    }

    TAC *current_tac = start_tac;
    while (current_tac && current_tac != end_tac->next) {
        if (leaders.count(current_tac)) {
            BasicBlock *current_block = &get_new_block();
            current_block->begin_ = current_tac;

            if (is_label(current_tac->op) && current_tac->a && current_tac->a->name) {
                labeled_blocks[std::string(current_tac->a->name)] = current_block;
            }

            TAC *t = current_tac;
            // block的结尾是下一个block header的前一条或结束指令
            while (t->next && t->next != end_tac->next && !leaders.count(t->next)) {
                t = t->next;
            }
            current_block->end_ = t;
            current_tac = t->next; // 移动到下一个 Leader/TAC
        } else {
            // ???
            current_tac = current_tac->next;
        }
    }

    // 设置 Entry Block
    if (!blocks_.empty()) {
        begin_block_.fallthrough_ = blocks_.front().get();
        blocks_.front()->preds_.insert(&begin_block_);
    } else {
        // 空函数直接退出!!!
        begin_block_.fallthrough_ = &end_block_;
        return;
    }

    for (size_t i = 0; i < blocks_.size(); ++i) {
        BasicBlock *block = blocks_[i].get();
        const auto block_end = block->end_;

        if (!block_end) continue;

        switch (block_end->op) {
            case TAC_GOTO: {
                if (block_end->a && block_end->a->name) {
                    BasicBlock *target = labeled_blocks.count(block_end->a->name) ? labeled_blocks.at(block_end->a->name) : &end_block_;
                    CONNECT(block, target, fallthrough);
                }
                break;
            }
            case TAC_IFZ: {
                // 1. 真分支 (IFZ) - 跳转到标签
                if (block_end->a && block_end->a->name) {
                    BasicBlock *target = labeled_blocks.count(block_end->a->name) ? labeled_blocks.at(block_end->a->name) : &end_block_;
                    CONNECT(block, target, ifz);
                }

                // 2. 假分支 (Fallthrough) - 跳转到下一个顺序块
                BasicBlock *fallthrough_target = (i < blocks_.size() - 1) ? blocks_[i+1].get() : &end_block_;
                CONNECT(block, fallthrough_target, fallthrough);
                break;
            }
            case TAC_RETURN:
            case TAC_ENDFUNC: {
                // 退出函数
                CONNECT(block, &end_block_, fallthrough);
                break;
            }
            default: { // 非跳转指令：fallthrough 到下一个顺序块
                BasicBlock *fallthrough_target = (i < blocks_.size() - 1) ? blocks_[i+1].get() : &end_block_;
                CONNECT(block, fallthrough_target, fallthrough);
                break;
            }
        }
    }
}
#undef CONNECT
#undef CONNECT_

std::string TacProxy::to_string() const {
    if (tac_ == nullptr) return "";
    char *buffer = nullptr;
    size_t size = 0;
    FILE *mem_file = open_memstream(&buffer, &size);
    out_tac(mem_file, tac_);
    fclose(mem_file);
    auto result = std::string(buffer, size);
    free(buffer);
    return result;
}


std::string FunctionCFG::block2dot(BasicBlock *block) {
    if (!block->begin_) {
        return "empty block, no TAC!";
    }
    char * buffer = nullptr;
    size_t size = 0;
    FILE *mem_file = open_memstream(&buffer, &size);
    int i = 1;
    for (auto ptac=block->begin_; ptac && ptac.get() != block->end_.get(); ptac=ptac->next) {
        fprintf(mem_file, "(%d) ", i++);
        out_tac(mem_file, ptac.get());
        fprintf(mem_file, "\\l");
    }
    fprintf(mem_file, "(%d) ", i);
    out_tac(mem_file, block->end_.get());
    fprintf(mem_file, "\\l");

    // 不能在close之前读取buffer!!!
    fclose(mem_file);
    auto result = std::string(buffer, size);
    free(buffer);
    return result;
}

std::string FunctionCFG::to_dot() const {
    auto dot_str = std::string("digraph ") + label_ + " {\n";
    dot_str += "  node [shape=box, fontname=\"Monospace\"];\n";

    if (label_ == "main")
        dot_str += "B0 [shape=doublecircle, label=\"Program Entry\"];\n";
    else
        dot_str += "B0 [shape=doublecircle, label=\"Function Entry\"];\n";
    // 1. 定义所有节点 (基本块)
    for (const auto& unique_block : blocks_) {
        BasicBlock *block = unique_block.get();
        // 节点名：B<ID>
        std::string node_name = "B" + std::to_string(block->id());

        // 节点内容：标签、ID、TAC 列表
        std::string label = "Block ID: " + std::to_string(block->id()) + "\\l";
        label += "-- TACs --\\l";
        // 实际TAC指令列表
        label += block2dot(block);
        // DOT 格式：<节点名> [label="<标签内容>"]
        dot_str += "  " + node_name + " [label=\"";
        dot_str.append(label);
        dot_str += "\"];\n";
    }

    // 2. 定义边 (控制流)
    dot_str += "\n  B0 -> B1 [label=\"Entry\", color=\"green\"];\n"; // Entry 边
    for (const auto& unique_block : blocks_) {
        BasicBlock *block = unique_block.get();
        std::string source_name = "B" + std::to_string(block->id());

        // 辅助函数：生成 DOT 边定义
        auto generate_edge = [&](BasicBlock *target, const std::string& label, const std::string& color) {
            if (!target) return; // 目标为空，跳过

            std::string dest_name = "B" + std::to_string(target->id());

            // 特殊处理虚拟 Exit Block
            if (target->id() == 0) {
                dest_name = "Exit";
                if (label_ == "main")
                    dot_str += "  Exit [shape=doublecircle, label=\"Program Exit\"];\n";
                else
                    dot_str += "  Exit [shape=doublecircle, label=\"Function Exit\"];\n";
            }

            dot_str += "  " + source_name + " -> ";
            dot_str += dest_name + " [label=\"";
            dot_str += label + "\", color=\"";
            dot_str += color + "\"];\n";
        };

        // --- 检查 fallthrough_ ---
        if (block->fallthrough_) {
            auto &end_tac = block->end_;
            std::string edge_label = "Fall-Through";
            std::string color = "black";

            if (end_tac && end_tac->op == TAC_GOTO) {
                edge_label = "GOTO";
                color = "purple";
            } else if (end_tac && (end_tac->op == TAC_RETURN || end_tac->op == TAC_ENDFUNC)) {
                edge_label = "Return/End";
                color = "red";
            } else if (end_tac && end_tac->op == TAC_IFZ) {
                // 如果是 IFZ，fallthrough_ 是假分支
                edge_label = "False/Seq";
                color = "blue";
            }

            generate_edge(block->fallthrough_, edge_label, color);
        }

        // --- 检查 ifz_ (仅用于 TAC_IFZ) ---
        if (block->ifz_) {
            if (block->end_ && block->end_->op == TAC_IFZ) {
                // IFZ 的目标是真分支 (条件满足时跳转)
                generate_edge(block->ifz_, "True/Jump", "darkgreen");
            }
        }
    }

    dot_str += "}\n";
    return dot_str;
}

std::string CFG::global_vars_to_dot() const {
    std::ostringstream result;
    result << ("digraph main {\n  node [shape=box, fontname=\"Monospace\"];\n  Globals [label=\"-- Global Variables-- \\l");
    int i=1;
    for (const auto& sym: global_vars_) {
        result << "(" << (i++) << ") var " << sym->name << "\\l";
    }
    result << "\"];\n}";
    return result.str();
}
void CFG::to_dot(const std::filesystem::path &path) const {
    std::filesystem::create_directories(path);
    for (const auto &func: functions_) {
        std::ofstream ofs(path / (func.first + ".dot"));
        if (!ofs)
            continue;
        ofs << func.second->to_dot();
        // ofs在局部变量作用域析构后会自动close
    }
    if (!global_vars_.empty()) {
        std::ofstream ofs(path / "globalvars.dot");
        if (!ofs)
            return;
        ofs << global_vars_to_dot();
    }
}
std::vector<std::string> CFG::to_dot() const {
    std::vector<std::string> result(functions_.size());
    for (const auto &func: functions_) {
        result.push_back(func.second->to_dot());
    }
    return result;
}

std::pair<TAC*, TAC*> CFG::to_tac() {
    TAC *begin = nullptr, *end = nullptr;
    for (const auto &sym: global_vars_) {
        if (begin == nullptr || end == nullptr) {
            begin = end = mk_tac(TAC_VAR, sym, NULL, NULL);
            continue;
        }
        end->next = mk_tac(TAC_VAR, sym, NULL, NULL);
        end->next->prev = end;
        end = end->next;
    }
    for (const auto &func: functions_) {
        const auto result = func.second->to_tac();
        TAC *func_tac = mk_tac(TAC_LABEL, mk_label((char*)func.first.c_str()), NULL, NULL);
        func_tac->next = mk_tac(TAC_BEGINFUNC, NULL, NULL, NULL);
        func_tac->next->prev = func_tac;
        func_tac->next->next = result.first;
        result.first->prev = func_tac->next;

        if (begin == nullptr || end == nullptr) {
            begin = func_tac;
            end = result.second;
            continue;
        }
        end->next = func_tac;
        func_tac->prev = end;
        end = result.second;
    }
    return {begin, end};
}
std::pair<TAC*, TAC*> FunctionCFG::to_tac() {
    TAC *begin = nullptr, *end = nullptr;
    std::unordered_set<BasicBlock*> visited;
    std::stack<BasicBlock*> stack;
    if (begin_block_.ifz_) stack.push(begin_block_.ifz_);
    if (begin_block_.fallthrough_) stack.push(begin_block_.fallthrough_);
    BasicBlock *last_bb = nullptr;
    while (!stack.empty()) {
        BasicBlock *block = stack.top();
        stack.pop();
        if (is_entry(*block) || is_exit(*block)) continue;
        if (visited.count(block)) continue;
        visited.insert(block);
        if (last_bb && end && end->op == TAC_GOTO && block->begin_.is_label() && end->a == block->begin_->a) {
            // 删除无效的goto
            last_bb->del_tac(last_bb->end_);
            end = end->prev;
        }

        if (!begin || !end) {
            begin = block->begin_.get();
            begin->prev = nullptr;
            end = block->end_.get();
            end->next = nullptr;
        } else {
            end->next = block->begin_.get();
            block->begin_->prev = end;
            end = block->end_.get();
            end->next = nullptr;
        }
        while (block->ifz_) {
            stack.push(block->ifz_);
            if (visited.count(block->fallthrough_)) {
                // if (block->fallthrough_->begin_->prev && block->fallthrough_->begin_->prev->op == TAC_IFZ) {
                    if (block->fallthrough_->begin_->op != TAC_LABEL) {
                        block->fallthrough_->insert_before(mk_tac(TAC_LABEL, mk_label(mk_lstr()), nullptr, nullptr));
                    }
                    if (begin == block->fallthrough_->begin_->next) {
                        begin = block->fallthrough_->begin_.get();
                    }
                    if (block->end_->op == TAC_GOTO) {
                        block->end_->a = block->fallthrough_->begin_->a;
                    } else {
                        block->insert_after(mk_tac(TAC_GOTO, block->fallthrough_->begin_->a, nullptr, nullptr));
                    }
                    end = block->end_.get();
                    last_bb = block;
                // } else {
                //     end = block->begin_->prev;
                //     assert(end == last_bb->end_.get());
                //     BasicBlock *pred_bb = nullptr;
                //     for (auto &pred: block->fallthrough_->preds_) {
                //         if (pred->end_.get() == block->fallthrough_->begin_->prev) {
                //             pred_bb = pred;
                //             break;
                //         }
                //     }
                //     assert(pred_bb);
                //     if (!block->fallthrough_->begin_.is_label()) {
                //         block->fallthrough_->insert_before(mk_tac(TAC_LABEL, mk_label(mk_lstr()), nullptr, nullptr));
                //     }
                //     if (!pred_bb->end_.is_goto()) {
                //         pred_bb->insert_after(mk_tac(TAC_GOTO, block->fallthrough_->begin_->a, nullptr, nullptr));
                //         if (pred_bb->end_->prev == end) end = pred_bb->end_.get();
                //     }
                //     if (!block->begin_.is_label()) {
                //         block->insert_before(mk_tac(TAC_LABEL, mk_label(mk_lstr()), nullptr, nullptr));
                //         if (begin == block->begin_->next) begin = block->begin_.get();
                //     }
                //     for (auto &pred: block->preds_) {
                //         if (!visited.count(pred)) continue;
                //         if (!pred->end_.is_goto() || pred->end_->a != block->begin_->a) {
                //             pred->insert_after(mk_tac(TAC_GOTO, block->begin_->a, nullptr, nullptr));
                //             if (pred->end_->prev == end) end = pred->end_.get();
                //         }
                //     }
                //     block->begin_->prev = block->fallthrough_->begin_->prev;
                //     if (block->begin_->prev) {
                //         block->begin_->prev->next = block->begin_.get();
                //     }
                //     block->end_->next = block->fallthrough_->begin_.get();
                //     block->fallthrough_->begin_->prev = block->end_.get();
                //     if (begin == block->fallthrough_->begin_.get()) {
                //         begin =  block->begin_.get();
                //     }
                //     visited.insert(block);
                // }
                break;
            }
            block = block->fallthrough_;
            visited.insert(block);
            end->next = block->begin_.get();
            block->begin_->prev = end;
            end->next->prev = end;
            end = block->end_.get();
        }
        if (block->ifz_) continue;
        last_bb = block;
        if (!block->fallthrough_ || is_exit(*block->fallthrough_)) continue;
        if (visited.count(block->fallthrough_)) {
            if (block->end_->op != TAC_GOTO || block->end_->a != block->fallthrough_->begin_->a) {
                BasicBlock *ft_bb = block->fallthrough_;
                if (!ft_bb->begin_.is_label()) {
                    ft_bb->insert_before(mk_tac(TAC_LABEL, mk_label(mk_lstr()), nullptr, nullptr));
                    // TAC *label_tac = mk_tac(TAC_LABEL, mk_label(mk_lstr()), nullptr, nullptr);
                    // label_tac->prev = ft_bb->begin_->prev;
                    // label_tac->next = ft_bb->begin_.get();
                    // if (label_tac->prev) {
                    //     label_tac->prev->next = label_tac;
                    // }
                    // ft_bb->begin_->prev = label_tac;
                    // if (ft_bb->begin_ == begin) {
                    //     begin = label_tac;
                    // }
                    // ft_bb->begin_ = label_tac;
                }
                if (begin == ft_bb->begin_->next) {
                    begin = ft_bb->begin_.get();
                }
                if (block->end_->op == TAC_GOTO) {
                    block->end_->a = ft_bb->begin_->a;
                } else {
                    block->insert_after(mk_tac(TAC_GOTO, ft_bb->begin_->a, nullptr, nullptr));
                }
                end = block->end_.get();
            }
            continue;
        }
        if (block->end_->op == TAC_GOTO) {
            BasicBlock *ft_bb = block->fallthrough_;
            if (!ft_bb->begin_.is_label()) {
                ft_bb->insert_before(mk_tac(TAC_LABEL, mk_label(mk_lstr()), nullptr, nullptr));
            }
            if (begin == ft_bb->begin_->next) {
                begin = ft_bb->begin_.get();
            }
            block->end_->a = ft_bb->begin_->a;
        }
        stack.push(block->fallthrough_);
    }
    // 删除无效的label
    for (auto &bb: blocks_) {
        bool need_label = false;
        for (const auto &pred: bb->preds_) {
            if (pred->end_.is_goto()) {
                if (pred->end_->op == TAC_GOTO) {
                    need_label = true;
                } else if ((pred->end_->op == TAC_IFZ && pred->ifz_ == bb.get()) || pred->end_->next != bb->begin_.get()) {
                    need_label = true;
                }
            }
        }
        if (!need_label && bb->begin_.is_label()) {
            if (bb->begin_ == begin && begin) {
                begin = begin->next;
            }
            if (bb->begin_ == end && end) {
                end = end->next;
            }
            bb->del_tac(bb->begin_);
        }
    }

    // kill declarations
    for (TacProxy tac{begin}; tac && begin && end && tac.get() != end->next; tac = tac->next) {
        if (!tac.is_declaration()) continue;
        if (tac.get() == begin) begin = begin->next;
        if (tac->prev) tac->prev->next = tac->next;
        if (tac->next) tac->next->prev = tac->prev;
    }
    std::unordered_set<SYM *> has_declared;
    for (TacProxy tac{begin}; tac && begin && end && tac.get() != end->next; tac = tac->next) {
        if (const SymProxy sym{tac->a}; sym && sym.is_variable() && !sym.is_global_variable() && !has_declared.count(sym.get())) {
            TAC *decl = mk_tac(TAC_VAR, sym.get(), nullptr, nullptr);
            decl->prev = tac->prev;
            decl->next = tac.get();
            if (tac.get() == begin) begin = decl;
            if (tac->prev) tac->prev->next = decl;
            tac->prev = decl;
            has_declared.insert(sym.get());
        }

        if (const SymProxy sym{tac->b}; sym && sym.is_variable() && !sym.is_global_variable() && !has_declared.count(sym.get())) {
            TAC *decl = mk_tac(TAC_VAR, sym.get(), nullptr, nullptr);
            decl->prev = tac->prev;
            decl->next = tac.get();
            if (tac.get() == begin) begin = decl;
            if (tac->prev) tac->prev->next = decl;
            tac->prev = decl;
            has_declared.insert(sym.get());
        }

        if (const SymProxy sym{tac->c}; sym && sym.is_variable() && !sym.is_global_variable() && !has_declared.count(sym.get())) {
            TAC *decl = mk_tac(TAC_VAR, sym.get(), nullptr, nullptr);
            decl->prev = tac->prev;
            decl->next = tac.get();
            if (tac.get() == begin) begin = decl;
            if (tac->prev) tac->prev->next = decl;
            tac->prev = decl;
            has_declared.insert(sym.get());
        }
    }

    return {begin, end};
}

bool CFG::opt_constants_folding() const {
    bool optimize = false;
    for (auto const &[key, func]: functions_) {
        optimize |= func->opt_constants_folding();
    }
    return optimize;
}
bool FunctionCFG::opt_constants_folding() const {
    bool optimized = false;
    for (auto const& block: blocks_) {
        optimized |= block->opt_constants_folding();
    }
    return optimized;
}
bool BasicBlock::opt_constants_folding() const {
    bool optimized = false;
    for (auto t = begin_; t && t.get() != end_->next; t = t->next) {
        if (t->op >= TAC_MIN_CALC && t->op <= TAC_MAX_CALC) {
            if (t->b && t->b->type == SYM_CONST &&
                (t->op == TAC_NEG || (t->c && t->c->type == SYM_CONST))) {
                const int val_a = t->b->value, val_b = t->c ? t->c->value : 0;
                int result = 0;
                switch (t->op) {
                    case TAC_ADD:
                        result = val_a + val_b; break;
                    case TAC_SUB:
                        result = val_a - val_b; break;
                    case TAC_MUL:
                        result = val_a * val_b; break;
                    case TAC_DIV:
                        if (val_b != 0) {
                            result = val_a / val_b;
                        } else {
                            printf("Error: division by zero!\n");
                            continue; // 避免除以零
                        }
                        break;
                    case TAC_EQ:
                        result = val_a == val_b; break;
                    case TAC_NE:
                        result = val_a != val_b; break;
                    case TAC_LT:
                        result = val_a < val_b; break;
                    case TAC_LE:
                        result = val_a <= val_b; break;
                    case TAC_GT:
                        result = val_a > val_b; break;
                    case TAC_GE:
                        result = val_a >= val_b; break;
                    case TAC_NEG:
                        result = -val_a; break;
                    default:
                        continue; // 不支持的操作
                }
                // 创建新的常量符号
                SYM *const_sym = mk_const(result, SYM_VAL_DEFAULT);

                // 修改当前TAC为赋值操作
                t->op = TAC_COPY;
                t->b = const_sym; // 赋值的常量
                t->c = nullptr;

                optimized = true;
            }
        }
    }
    return optimized;
}

bool CFG::opt_common_subexpression_elimination() const {
    bool result = false;
    for (const auto &fcfg: functions_) {
        result |= fcfg.second->opt_common_subexpression_elimination();
    }
    return result;
}
bool FunctionCFG::opt_common_subexpression_elimination() const {
    bool result = false;
    for (const auto &bb: blocks_) {
        result |= bb->opt_common_subexpression_elimination();
    }
    return result;
}
bool BasicBlock::opt_common_subexpression_elimination() const {
    bool result = false;
    auto is_same_tac = [] (const TAC *t1, const TAC *t2) -> bool{
        return t1->op == t2->op && t1->b == t2->b && t1->c == t2->c;
    };
    std::list<TAC *> assignments;
    for (auto tac = begin_; tac && tac.get() != end_->next; tac = tac->next) {
        if (tac.has_memory_effect()) {
            assignments.clear();
        }
        if (tac->op >= TAC_MIN_CALC && tac->op <= TAC_MAX_CALC) {
            bool flag = true;
            for (const auto &assignment: assignments) {
                if (is_same_tac(static_cast<TAC*>(assignment), tac.get())) {
                    tac->op = TAC_COPY;
                    tac->b = assignment->a;
                    tac->c = nullptr;
                    flag = false;
                    result = true;
                    break;
                }
            }
            if (flag) {
                assignments.push_back(tac.get());
            }
        }
        if ((tac->op >= TAC_MIN_CALC && tac->op <= TAC_MAX_CALC)|| tac->op == TAC_COPY) {
            // tac->a被修改
            assignments.remove_if([&] (const TAC *t) -> bool {
                return (t->b == tac->a || t->c == tac->a);
            });
        }
    }
    return result;
}

bool CFG::remove_unreachable_blocks() const {
    bool result = false;
    for (auto &fcfg_kv: functions_) {
        result |= fcfg_kv.second->remove_unreachable_blocks();
    }
    return result;
}
bool FunctionCFG::remove_unreachable_blocks() {
    bool result = false;
    auto it = blocks_.begin();
    while (it != blocks_.end()) {
        std::unique_ptr<BasicBlock> &bb = *it;
        if (bb->preds_.empty()) {
            // unreachable bb!
            if (bb->fallthrough_) {
                BasicBlock &child = *bb->fallthrough_;
                child.preds_.erase(bb.get());
            }
            if (bb->ifz_) {
                BasicBlock &child = *bb->ifz_;
                child.preds_.erase(bb.get());
            }
            blocks_.erase(it);
            result = true;
        } else {
            ++it;
        }
    }
    return result;
}
void CFG::remove_unnecessary_gotos_and_labels() const {
    for (const auto &fcfg: functions_) {
        fcfg.second->remove_unnecessary_gotos_and_labels();
    }
}
void FunctionCFG::remove_unnecessary_gotos_and_labels() {
    delete_empty_block();

    std::unordered_set<BasicBlock*> visited;
    std::stack<BasicBlock*> stack;
    if (begin_block_.ifz_) stack.push(begin_block_.ifz_);
    if (begin_block_.fallthrough_) stack.push(begin_block_.fallthrough_);
    BasicBlock *last_bb = nullptr;
    while (!stack.empty()) {
        BasicBlock *block = stack.top();
        stack.pop();
        if (is_entry(*block) || is_exit(*block)) continue;
        if (visited.count(block)) continue;
        visited.insert(block);
        if (last_bb && last_bb->end_->op == TAC_GOTO && block->begin_.is_label() && last_bb->end_->a == block->begin_->a) {
            // 删除无效的goto
            if (block->fallthrough_ != last_bb && block->ifz_ != last_bb) {
                last_bb->del_tac(last_bb->end_);
                if (!last_bb->begin_ || !last_bb->end_) {
                    block->preds_.erase(last_bb);
                    for (auto &pred: last_bb->preds_) {
                        if (pred->fallthrough_ == last_bb) {
                            // set_fallthrough 会改变last_bb->preds_
                            pred->fallthrough_ = block;
                            block->preds_.insert(pred);
                        }
                        else assert(0);
                    }
                    for (auto it = blocks_.begin(); it != blocks_.end();) {
                        if (it->get() == last_bb) it = blocks_.erase(it);
                        else ++it;
                    }
                }
            }
        }
        while (block->ifz_ && !is_exit(*block)) {
            stack.push(block->ifz_);

            last_bb = block;
            block = block->fallthrough_;
        }
        if (!block->fallthrough_ || is_exit(*block->fallthrough_)) continue;
        last_bb = block;
        if (visited.count(block->fallthrough_)) {
            if (block->end_->op != TAC_GOTO || block->end_->a != block->fallthrough_->begin_->a) {
                BasicBlock *ft_bb = block->fallthrough_;
                if (!ft_bb->begin_.is_label()) {
                    TAC *label_tac = mk_tac(TAC_LABEL, mk_label(mk_lstr()), nullptr, nullptr);
                    label_tac->prev = ft_bb->begin_->prev;
                    label_tac->next = ft_bb->begin_.get();
                    if (label_tac->prev) {
                        label_tac->prev->next = label_tac;
                    }
                    ft_bb->begin_->prev = label_tac;
                    ft_bb->begin_ = label_tac;
                }
                if (block->end_->op == TAC_GOTO) {
                    block->end_->a = ft_bb->begin_->a;
                } else {
                    block->end_->next = mk_tac(TAC_GOTO, ft_bb->begin_->a, nullptr, nullptr);
                    block->end_->next->prev = block->end_.get();
                    block->end_ = block->end_->next;
                }

            }
            continue;
        }
        stack.push(block->fallthrough_);
    }
    delete_empty_block();
    // 删除无效的label
    for (auto &bb: blocks_) {
        bool need_label = false;
        for (const auto &pred: bb->preds_) {
            if (pred->end_.is_goto()) {
                if (pred->end_->op == TAC_GOTO) {
                    need_label = true;
                } else if (pred->end_->op == TAC_IFZ && pred->ifz_ == bb.get()) {
                    need_label = true;
                }
            }
        }
        if (!need_label && bb->begin_.is_label()) {
            bb->del_tac(bb->begin_);
        }
    }
    delete_empty_block();
}
void FunctionCFG::delete_empty_block() {
    // 对已经为空的block进行处理
    std::set<BasicBlock*> del_bbs;
    std::stack<BasicBlock*>stack;
    std::unordered_set<BasicBlock*> visited;
    if (begin_block_.ifz_) stack.push(begin_block_.ifz_);
    if (begin_block_.fallthrough_) stack.push(begin_block_.fallthrough_);
    while (!stack.empty()) {
        auto bb = stack.top();
        stack.pop();
        if (visited.count(bb) || is_entry(*bb) || is_exit(*bb)) continue;
        visited.insert(bb);
        if (!bb->begin_ || !bb->end_) {
            del_bbs.insert(bb);
            auto pred_it = bb->preds_.begin();
            if (pred_it == bb->preds_.end()) {
                continue;
            }
            auto &pred = **pred_it;
            bb->fallthrough_->preds_.erase(bb);
            if (pred.fallthrough_ == bb) {
                pred.set_fallthrough(bb->fallthrough_);
            } else {
                pred.set_ifz(bb->fallthrough_);
            }
        }
        if (bb->fallthrough_) stack.push(bb->fallthrough_);
        if (bb->ifz_) stack.push(bb->ifz_);
    }
    for (auto it = blocks_.begin(); it != blocks_.end();) {
        if (del_bbs.count(it->get())) {
            printf("block %d: empty, remove!\n", (*it)->id());
            it = blocks_.erase(it);
        } else {
            assert((*it)->begin_);
            assert((*it)->end_);
            ++it;
        }
    }
}
void CFG::combine_fallthrough() const {
    for (auto &[name, fcfg]: functions_) {
        fcfg->combine_fallthrough();
    }
}
void FunctionCFG::combine_fallthrough() {
    delete_empty_block();

    std::stack<BasicBlock*> stack;
    std::unordered_set<BasicBlock*> visited;
    if (begin_block_.fallthrough_) stack.push(begin_block_.fallthrough_);
    if (begin_block_.ifz_) stack.push(begin_block_.ifz_);
    while (!stack.empty()) {
        auto bb = stack.top();
        stack.pop();
        if (is_entry(*bb) || is_exit(*bb)) continue;
        if (visited.count(bb)) continue;
        visited.insert(bb);

        while (!bb->ifz_ && bb->fallthrough_ && !is_exit(*bb->fallthrough_) && bb->fallthrough_->preds_.size() == 1) {
            // 如果当前block有且只有一个后继,并且后继也只有当前block一个前驱,就可以安全地合并两个block
            BasicBlock &ft_bb = *bb->fallthrough_;
            printf("block %d: combine with block %d\n", bb->id(), ft_bb.id());
            if (ft_bb.begin_.is_label()) {
                ft_bb.del_tac(ft_bb.begin_);
            }
            if (bb->end_.is_goto()) {
                bb->del_tac(bb->end_);
            }
            if (ft_bb.fallthrough_) ft_bb.fallthrough_->preds_.erase(&ft_bb);
            if (ft_bb.ifz_) ft_bb.ifz_->preds_.erase(&ft_bb);
            bb->set_fallthrough(ft_bb.fallthrough_);
            bb->set_ifz(ft_bb.ifz_);

            if (ft_bb.begin_ && ft_bb.end_) {
                if (bb->end_ && bb->begin_) bb->end_->next = ft_bb.begin_.get();
                else {
                    bb->begin_ = ft_bb.begin_;
                }
                ft_bb.begin_->prev = bb->end_.get();
                bb->end_ = ft_bb.end_;
            }
            for (auto it = blocks_.begin(); it != blocks_.end();) {
                if (it->get() == &ft_bb) {
                    it = blocks_.erase(it);
                    break;
                } else {
                    ++it;
                }
            }
            bb = bb->fallthrough_;
        }
        if (bb->fallthrough_) stack.push(bb->fallthrough_);
        if (bb->ifz_) stack.push(bb->ifz_);
    }
}
