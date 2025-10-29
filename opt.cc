#include "opt.hh"

#include <stdexcept>    // for std::runtime_error
#include <set>
#include <fstream>
#include <sstream>

#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif

using namespace opt;

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
    (back)->preds_.push_back(front);\
} while (0)
#define CONNECT(front, back, TYPE) CONNECT_(front, back, TYPE##_)

void FunctionCFG::init(TAC *start_tac, const TAC *end_tac) {
    if (!start_tac) return;

    std::set<TAC*> leaders;
    std::map<std::string, Block*> labeled_blocks;

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
            blocks_.push_back(std::make_unique<Block>(blocks_.size() + 1, current_tac));
            Block *current_block = blocks_.back().get();

            if (is_label(current_tac->op) && current_tac->a && current_tac->a->name) {
                labeled_blocks[std::string(current_tac->a->name)] = current_block;
            }

            TAC *t = current_tac;
            // block的结尾是下一个block header的前一条或结束指令
            while (t->next && t->next != end_tac->next && !leaders.count(t->next)) {
                t = t->next;
            }
            current_block->end = t;
            current_tac = t->next; // 移动到下一个 Leader/TAC
        } else {
            // ???
            current_tac = current_tac->next;
        }
    }

    // 设置 Entry Block
    if (!blocks_.empty()) {
        begin_block_.fallthrough_ = blocks_.front().get();
    } else {
        // 空函数直接退出!!!
        begin_block_.fallthrough_ = &end_block_;
        return;
    }

    for (size_t i = 0; i < blocks_.size(); ++i) {
        Block *block = blocks_[i].get();
        const TAC *block_end = block->end;

        if (!block_end) continue;

        switch (block_end->op) {
            case TAC_GOTO: {
                if (block_end->a && block_end->a->name) {
                    Block *target = labeled_blocks.count(block_end->a->name) ? labeled_blocks.at(block_end->a->name) : &end_block_;
                    CONNECT(block, target, fallthrough);
                }
                break;
            }
            case TAC_IFZ: {
                // 1. 真分支 (IFZ) - 跳转到标签
                if (block_end->a && block_end->a->name) {
                    Block *target = labeled_blocks.count(block_end->a->name) ? labeled_blocks.at(block_end->a->name) : &end_block_;
                    CONNECT(block, target, ifz);
                }

                // 2. 假分支 (Fallthrough) - 跳转到下一个顺序块
                Block *fallthrough_target = (i < blocks_.size() - 1) ? blocks_[i+1].get() : &end_block_;
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
                Block *fallthrough_target = (i < blocks_.size() - 1) ? blocks_[i+1].get() : &end_block_;
                CONNECT(block, fallthrough_target, fallthrough);
                break;
            }
        }
    }
}
#undef CONNECT
#undef CONNECT_

std::string FunctionCFG::block2dot(Block *block) {
    if (!block->begin) {
        return "empty block, no TAC!";
    }
    char * buffer = nullptr;
    size_t size = 0;
    FILE *mem_file = open_memstream(&buffer, &size);
    int i = 1;
    for (TAC *ptac=block->begin; ptac && ptac!=block->end; ptac=ptac->next) {
        fprintf(mem_file, "(%d) ", i++);
        out_tac(mem_file, ptac);
        fprintf(mem_file, "\\l");
    }
    fprintf(mem_file, "(%d) ", i);
    out_tac(mem_file, block->end);
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
        Block *block = unique_block.get();
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
        Block *block = unique_block.get();
        std::string source_name = "B" + std::to_string(block->id());

        // 辅助函数：生成 DOT 边定义
        auto generate_edge = [&](Block *target, const std::string& label, const std::string& color) {
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
            TAC *end_tac = block->end;
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
            if (block->end && block->end->op == TAC_IFZ) {
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

EXTERNC
CFG *cfg_init(TAC *tac) {
    try{
        return new CFG(tac);
    } catch(const std::exception &e) {
        return nullptr;
    }
}
EXTERNC
void cfg_free(const CFG *cfg) {
    if (cfg) {
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
void run_optimization(){
    run_local_optimization();
    run_global_optimization();
}
EXTERNC
void run_local_optimization(){
    ;;;
}
EXTERNC
void run_global_optimization(){
    ;;;
}
