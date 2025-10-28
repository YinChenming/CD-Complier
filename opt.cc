#include "opt.hh"

#include <stdexcept>    // for std::runtime_error
#include <set>
#include <fstream>

#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif

using namespace opt;

static bool isLabel(int op){
    return op == TAC_LABEL;
}
static bool isTerm(int op){
    switch (op) {
        case TAC_GOTO: case TAC_IFZ: case TAC_RETURN: case TAC_ENDFUNC:
            return true;
        default: return false;
    }
}

void CFG::_init(TAC *tac_first) {
    TAC *current_tac = tac_first;

    while (current_tac) {
        if (isLabel(current_tac->op) && current_tac->next && current_tac->next->op == TAC_BEGINFUNC) {
            // 1. 找到函数符号
            SYM *func_sym = current_tac->a;
            if (!func_sym || func_sym->type != SYM_LABEL) {
                // 错误处理或跳过
                current_tac = current_tac->next;
                continue;
            }

            // 2. 找到函数体的结束点 (TAC_ENDFUNC)
            TAC *func_start = current_tac->next->next; // 函数体的第一条指令
            TAC *func_end = nullptr;
            TAC *t = func_start;
            while (t && t->op != TAC_ENDFUNC) {
                t = t->next;
            }
            func_end = t; // func_end 现在指向 TAC_ENDFUNC (或 nullptr)

            std::string func_name = func_sym->name;
            // 3. 创建 FunctionCFG 实例并构建
            auto func_cfg = std::make_unique<FunctionCFG>(func_name, func_start, func_end);

            // 4. 存储到顶层 CFG 结构
            functions_[func_name] = std::move(func_cfg);

            // 5. 移动到下一个函数/代码块
            current_tac = func_end ? func_end->next : nullptr;
        } else {
            // 不能在顶层定义除了TAC_VAR以外的三地址码
            if (!current_tac->op == TAC_VAR)
                throw std::runtime_error("cannot set global TAC except TAC_VAR!");
            current_tac = current_tac->next;
        }
    }
}

#define connect_(front, back, TYPE)\
do{\
    (front)->TYPE = (back);\
    (back)->preds_.push_back(front);\
} while (0)
#define connect(front, back, TYPE) connect_(front, back, TYPE##_)

void FunctionCFG::_init(TAC *start_tac, TAC *end_tac) {
    if (!start_tac) return;

    // 1. 预处理：识别所有 Leader，并创建标签到 Block 的映射
    std::set<TAC*> leaders;
    std::map<std::string, Block*> labeledBlocks;

    // 1.1 始终将函数的第一条指令视为 Leader
    leaders.insert(start_tac);

    // 1.2 遍历函数体，识别 Label 和 跳转指令的下一条
    for (TAC *t = start_tac; t != end_tac; t = t->next) {
        if (!t) break;
        // Leader Type 1: Label
        if (isLabel(t->op)) {
            leaders.insert(t);
        }
        // Leader Type 2: 跳转指令的下一条 (如果存在)
        if (isTerm(t->op) && t->next && t->next != end_tac) {
            leaders.insert(t->next);
        }
    }

    // 2. 创建 Block 实例
    TAC *current_tac = start_tac;
    while (current_tac && current_tac != end_tac->next) {
        if (leaders.count(current_tac)) {
            // 2.1 创建新的 Block
            blocks_.push_back(std::make_unique<Block>(blocks_.size() + 1, current_tac));
            Block *current_block = blocks_.back().get();

            // 记录标签
            if (isLabel(current_tac->op) && current_tac->a && current_tac->a->name) {
                labeledBlocks[std::string(current_tac->a->name)] = current_block;
            }

            // 2.2 确定基本块的结束指令 (end_)
            TAC *t = current_tac;
            // 继续直到遇到下一个 Leader 或者函数结束 (end_tac)
            while (t->next && t->next != end_tac->next && !leaders.count(t->next)) {
                t = t->next;
            }
            current_block->end_ = t;
            current_tac = t->next; // 移动到下一个 Leader/TAC

        } else {
            // 理论上，所有指令都应该被包含在一个块中。
            // 如果 current_tac 不是 Leader，说明它已经被包含在之前的块中了，直接移动到下一个
            // 这里的 current_tac 应该已经通过 t = t->next; 推进了。
            // 为了安全，如果 Leader 逻辑有误，这里可以推进 current_tac。
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

    // 3. 连接基本块 (Edges)
    for (size_t i = 0; i < blocks_.size(); ++i) {
        Block *block = blocks_[i].get();
        TAC *end_tac = block->end_;

        if (!end_tac) continue;

        switch (end_tac->op) {
            case TAC_GOTO: {
                if (end_tac->a && end_tac->a->name) {
                    Block *target = labeledBlocks.count(end_tac->a->name) ? labeledBlocks.at(end_tac->a->name) : &end_block_;
                    connect(block, target, fallthrough);
                    // connect_blocks(block, target);
                }
                break;
            }
            case TAC_IFZ: {
                // 1. 真分支 (IFZ) - 跳转到标签
                if (end_tac->a && end_tac->a->name) {
                    Block *target = labeledBlocks.count(end_tac->a->name) ? labeledBlocks.at(end_tac->a->name) : &end_block_;
                    connect(block, target, ifz);
                    // connect_blocks(block, target);
                }

                // 2. 假分支 (Fallthrough) - 跳转到下一个顺序块
                Block *fallthrough_target = (i < blocks_.size() - 1) ? blocks_[i+1].get() : &end_block_;
                connect(block, fallthrough_target, fallthrough);
                // connect_blocks(block, fallthrough_target);
                break;
            }
            case TAC_RETURN:
            case TAC_ENDFUNC: {
                // 退出函数
                connect(block, &end_block_, fallthrough);
                // connect_blocks(block, &end_block_);
                break;
            }
            default: { // 非跳转指令：Fallthrough 到下一个顺序块
                Block *fallthrough_target = (i < blocks_.size() - 1) ? blocks_[i+1].get() : &end_block_;
                connect(block, fallthrough_target, fallthrough);
                // connect_blocks(block, fallthrough_target);
                break;
            }
        }
    }
}
#undef connect
#undef connect_

std::string FunctionCFG::_block2dot(Block *block) {
    if (!block->begin_) {
        return "empty block, no TAC!";
    }
    char * buffer = nullptr;
    size_t size = 0;
    FILE *mem_file = open_memstream(&buffer, &size);
    int i = 1;
    for (TAC *ptac=block->begin_; ptac && ptac!=block->end_; ptac=ptac->next) {
        fprintf(mem_file, "(%d) ", i++);
        out_tac(mem_file, ptac);
        fprintf(mem_file, "\\l");
    }
    fprintf(mem_file, "(%d) ", i);
    out_tac(mem_file, block->end_);
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

    // 1. 定义所有节点 (基本块)
    for (const auto& unique_block : blocks_) {
        Block *block = unique_block.get();
        // 节点名：B<ID>
        std::string node_name = "B" + std::to_string(block->id_);

        // 节点内容：标签、ID、TAC 列表
        std::string label = "Block ID: " + std::to_string(block->id_) + "\\l";
        label += "-- TACs --\\l";
        // 实际TAC指令列表
        label += _block2dot(block);
        // DOT 格式：<节点名> [label="<标签内容>"]
        dot_str += "  " + node_name + " [label=\"" + label + "\"];\n";
    }

    // 2. 定义边 (控制流)
    for (const auto& unique_block : blocks_) {
        Block *block = unique_block.get();
        std::string source_name = "B" + std::to_string(block->id_);

        // 辅助函数：生成 DOT 边定义
        auto generate_edge = [&](Block *target, const std::string& label, const std::string& color) {
            if (!target) return; // 目标为空，跳过

            std::string dest_name = "B" + std::to_string(target->id_);

            // 特殊处理虚拟 Exit Block
            if (target->id_ == 0) {
                dest_name = "Exit";
                dot_str += "  Exit [shape=doublecircle, label=\"Function Exit\"];\n";
            }

            dot_str += "  " + source_name + " -> " + dest_name +
                    " [label=\"" + label + "\", color=\"" + color + "\"];\n";
        };

        // --- 检查 fallthrough_ ---
        if (block->fallthrough_) {
            TAC *end_tac = block->end_;
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
void CFG::to_dot(const std::filesystem::path path) const {
    std::filesystem::create_directories(path);
    for (const auto &func: functions_) {
        std::ofstream ofs(path / (func.first + ".dot"));
        if (!ofs)
            continue;
        ofs << func.second->to_dot();
        ofs.close();
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
CFG *cfg_init(TAC *tac_first) {
    try{
        return new CFG(tac_first);
    } catch(const std::exception &e) {
        return nullptr;
    }
}
EXTERNC
void cfg_free(CFG *cfg) {
    if (cfg) {
        try {
            delete cfg;
        } catch (const std::exception &e) {
            ;;;
        }
    }
}
EXTERNC
void cfg_to_dot(CFG *cfg, char *path) {
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
