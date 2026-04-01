#include "system/pipeline_tracer.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <fmt/format.h>

namespace riscv {

PipelineTracer::PipelineTracer(const Config& config) : config_(config) {
    records_.reserve(config_.max_instructions);
}

void PipelineTracer::recordCommittedInstruction(const DynamicInstPtr& inst,
                                                const FlushSummary& flush_summary) {
    if (records_.size() >= config_.max_instructions) return;

    const uint64_t fetch_cycle = inst->get_fetch_cycle();
    const uint64_t retire_cycle = inst->get_retire_cycle();
    // 跳过未经过完整流水线的指令
    if (fetch_cycle == 0 && retire_cycle == 0) return;
    if (config_.start_cycle > 0 && fetch_cycle < config_.start_cycle) return;
    if (fetch_cycle > config_.end_cycle) return;

    const auto& decoded = inst->get_decoded_info();
    InstructionRecord rec;
    rec.instruction_id = inst->get_instruction_id();
    rec.pc = inst->get_pc();
    rec.disassembly = disassemble(decoded, inst->get_pc());
    rec.fetch_cycle = fetch_cycle;
    rec.decode_cycle = inst->get_decode_cycle();
    rec.issue_cycle = inst->get_issue_cycle();
    rec.execute_cycle = inst->get_execute_cycle();
    rec.complete_cycle = inst->get_complete_cycle();
    rec.retire_cycle = inst->get_retire_cycle();
    rec.is_branch = (decoded.opcode == Opcode::BRANCH ||
                     decoded.opcode == Opcode::JAL ||
                     decoded.opcode == Opcode::JALR);
    rec.branch_mispredicted = false;
    if (rec.is_branch && inst->has_predicted_next_pc()) {
        uint64_t fallthrough = inst->get_pc() + (decoded.is_compressed ? 2ULL : 4ULL);
        uint64_t actual_next = inst->is_jump() ? inst->get_jump_target() : fallthrough;
        rec.branch_mispredicted = (inst->get_predicted_next_pc() != actual_next);
    }
    rec.is_load = (decoded.opcode == Opcode::LOAD || decoded.opcode == Opcode::LOAD_FP);
    rec.is_store = (decoded.opcode == Opcode::STORE || decoded.opcode == Opcode::STORE_FP);
    rec.has_exception = inst->has_exception();
    rec.caused_flush = flush_summary.triggered;
    rec.flush_reason = flush_summary.reason ? flush_summary.reason : "";
    rec.flushed_rob_entries = flush_summary.flushed_rob_entries;
    rec.redirect_pc = flush_summary.redirect_pc;
    rec.has_redirect_pc = flush_summary.has_redirect_pc;
    rec.fetch_buffer_dropped = flush_summary.fetch_buffer_dropped;

    records_.push_back(std::move(rec));
}

const char* PipelineTracer::regName(RegNum reg) {
    static const char* names[] = {
        "zero","ra","sp","gp","tp","t0","t1","t2",
        "s0","s1","a0","a1","a2","a3","a4","a5",
        "a6","a7","s2","s3","s4","s5","s6","s7",
        "s8","s9","s10","s11","t3","t4","t5","t6"
    };
    return (reg < 32) ? names[reg] : "??";
}

std::string PipelineTracer::disassemble(const DecodedInstruction& decoded, uint64_t pc) {
    auto f3 = static_cast<uint8_t>(decoded.funct3);
    auto f7 = static_cast<uint8_t>(decoded.funct7);
    std::string name = "???";

    switch (decoded.opcode) {
    case Opcode::LUI:
        return fmt::format("lui {},{:#x}", regName(decoded.rd), static_cast<uint32_t>(decoded.imm) >> 12);
    case Opcode::AUIPC:
        return fmt::format("auipc {},{:#x}", regName(decoded.rd), static_cast<uint32_t>(decoded.imm) >> 12);
    case Opcode::JAL:
        return fmt::format("jal {},0x{:x}", regName(decoded.rd), pc + decoded.imm);
    case Opcode::JALR:
        return fmt::format("jalr {},{}({})", regName(decoded.rd), decoded.imm, regName(decoded.rs1));

    case Opcode::BRANCH: {
        const char* bn[] = {"beq","bne","??","??","blt","bge","bltu","bgeu"};
        name = (f3 < 8) ? bn[f3] : "b??";
        return fmt::format("{} {},{},0x{:x}", name, regName(decoded.rs1), regName(decoded.rs2), pc + decoded.imm);
    }

    case Opcode::LOAD: {
        const char* ln[] = {"lb","lh","lw","ld","lbu","lhu","lwu","??"};
        name = (f3 < 8) ? ln[f3] : "l??";
        return fmt::format("{} {},{}({})", name, regName(decoded.rd), decoded.imm, regName(decoded.rs1));
    }
    case Opcode::LOAD_FP:
        name = (f3 == 2) ? "flw" : "fld";
        return fmt::format("{} f{},{}({})", name, decoded.rd, decoded.imm, regName(decoded.rs1));

    case Opcode::STORE: {
        const char* sn[] = {"sb","sh","sw","sd"};
        name = (f3 < 4) ? sn[f3] : "s??";
        return fmt::format("{} {},{}({})", name, regName(decoded.rs2), decoded.imm, regName(decoded.rs1));
    }
    case Opcode::STORE_FP:
        name = (f3 == 2) ? "fsw" : "fsd";
        return fmt::format("{} f{},{}({})", name, decoded.rs2, decoded.imm, regName(decoded.rs1));

    case Opcode::OP_IMM: {
        const char* in[] = {"addi","slli","slti","sltiu","xori","srli","ori","andi"};
        name = (f3 < 8) ? in[f3] : "??";
        if (f3 == 5 && f7 == 0x20) name = "srai";
        if (f3 == 0 && decoded.rs1 == 0 && decoded.rd != 0) name = "li";
        if (f3 == 0 && decoded.imm == 0 && decoded.rd != 0 && decoded.rs1 != 0) name = "mv";
        return fmt::format("{} {},{},{}", name, regName(decoded.rd), regName(decoded.rs1), decoded.imm);
    }
    case Opcode::OP_IMM_32: {
        const char* in[] = {"addiw","slliw","??","??","??","srliw","??","??"};
        name = (f3 < 8) ? in[f3] : "??";
        if (f3 == 5 && f7 == 0x20) name = "sraiw";
        return fmt::format("{} {},{},{}", name, regName(decoded.rd), regName(decoded.rs1), decoded.imm);
    }

    case Opcode::OP: {
        if (f7 == 0x01) {
            const char* mn[] = {"mul","mulh","mulhsu","mulhu","div","divu","rem","remu"};
            name = (f3 < 8) ? mn[f3] : "m??";
        } else {
            const char* rn[] = {"add","sll","slt","sltu","xor","srl","or","and"};
            name = (f3 < 8) ? rn[f3] : "??";
            if (f3 == 0 && f7 == 0x20) name = "sub";
            if (f3 == 5 && f7 == 0x20) name = "sra";
        }
        return fmt::format("{} {},{},{}", name, regName(decoded.rd), regName(decoded.rs1), regName(decoded.rs2));
    }
    case Opcode::OP_32: {
        if (f7 == 0x01) {
            const char* mn[] = {"mulw","??","??","??","divw","divuw","remw","remuw"};
            name = (f3 < 8) ? mn[f3] : "??";
        } else {
            const char* rn[] = {"addw","sllw","??","??","??","srlw","??","??"};
            name = (f3 < 8) ? rn[f3] : "??";
            if (f3 == 0 && f7 == 0x20) name = "subw";
            if (f3 == 5 && f7 == 0x20) name = "sraw";
        }
        return fmt::format("{} {},{},{}", name, regName(decoded.rd), regName(decoded.rs1), regName(decoded.rs2));
    }

    case Opcode::SYSTEM: {
        if (f3 == 0) {
            uint32_t imm_val = static_cast<uint32_t>(decoded.imm) & 0xFFF;
            if (imm_val == 0) return "ecall";
            if (imm_val == 1) return "ebreak";
            if (imm_val == 0x302) return "mret";
            if (imm_val == 0x105) return "wfi";
            return fmt::format("system {:#x}", imm_val);
        }
        const char* cn[] = {"??","csrrw","csrrs","csrrc","??","csrrwi","csrrsi","csrrci"};
        name = (f3 < 8) ? cn[f3] : "csr??";
        return fmt::format("{} {},{:#x},{}", name, regName(decoded.rd),
                           static_cast<uint32_t>(decoded.imm) & 0xFFF, regName(decoded.rs1));
    }
    case Opcode::MISC_MEM:
        return (f3 == 1) ? "fence.i" : "fence";

    case Opcode::AMO:
        return fmt::format("amo.{} {},{},{}", (f3 == 2 ? "w" : "d"),
                           regName(decoded.rd), regName(decoded.rs2), regName(decoded.rs1));

    case Opcode::OP_FP:
    case Opcode::FMADD:
    case Opcode::FMSUB:
    case Opcode::FNMADD:
    case Opcode::FNMSUB:
        return fmt::format("fp_op f{},f{},f{}", decoded.rd, decoded.rs1, decoded.rs2);

    default:
        return fmt::format("??? (op={:#04x})", static_cast<uint8_t>(decoded.opcode));
    }
}

// ======================== 文本格式 ========================

bool PipelineTracer::generateText(const std::string& output_path) const {
    if (records_.empty()) return false;

    std::ofstream out(output_path);
    if (!out.is_open()) return false;

    // 表头
    out << fmt::format("{:>6} {:>10} {:<30} {:>5} {:>5} {:>5} {:>5} {:>5} {:>5}  {}\n",
                       "ID", "PC", "Instruction", "F", "D", "I", "E", "W", "C", "Flags");
    out << std::string(110, '-') << "\n";

    for (const auto& r : records_) {
        std::string flags;
        if (r.branch_mispredicted) flags += "MISP ";
        if (r.is_load) flags += "LD ";
        if (r.is_store) flags += "ST ";
        if (r.has_exception) flags += "EXC ";
        if (r.caused_flush) flags += "FLUSH ";

        // 显示各阶段周期和持续时间
        out << fmt::format("{:>6} {:>08x}   {:<30} {:>5} {:>5} {:>5} {:>5} {:>5} {:>5}  {}\n",
                           r.instruction_id, r.pc, r.disassembly,
                           r.fetch_cycle, r.decode_cycle, r.issue_cycle,
                           r.execute_cycle, r.complete_cycle, r.retire_cycle,
                           flags);
        if (r.caused_flush) {
            std::string flush_detail = fmt::format("reason={} rob={} fb={}",
                                                   r.flush_reason,
                                                   r.flushed_rob_entries,
                                                   r.fetch_buffer_dropped);
            if (r.has_redirect_pc) {
                flush_detail += fmt::format(" redirect=0x{:x}", r.redirect_pc);
            }
            out << fmt::format("{:>6} {:>10}   {:<30} {:>5} {:>5} {:>5} {:>5} {:>5} {:>5}  {}\n",
                               "", "", "[flush-summary]",
                               "", "", "", "", "", "",
                               flush_detail);
        }
    }

    out.close();
    return true;
}

// ======================== HTML 生成 ========================

bool PipelineTracer::generateHTML(const std::string& output_path) const {
    if (records_.empty()) return false;

    std::ofstream out(output_path);
    if (!out.is_open()) return false;

    // 计算周期范围
    uint64_t min_cycle = UINT64_MAX, max_cycle = 0;
    for (const auto& r : records_) {
        min_cycle = std::min(min_cycle, r.fetch_cycle);
        max_cycle = std::max(max_cycle, r.retire_cycle);
    }
    const uint64_t total_cycles = max_cycle - min_cycle + 1;

    out << R"(<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="UTF-8">
<title>RISC-V Pipeline View</title>
<style>
* { margin: 0; padding: 0; box-sizing: border-box; }
html, body { height: 100%; overflow: hidden; }
body { font-family: 'Menlo', 'Consolas', monospace; font-size: 12px; background: #1e1e2e; color: #cdd6f4; display: flex; flex-direction: column; }
.header { padding: 12px 16px; background: #181825; border-bottom: 1px solid #313244; flex-shrink: 0; z-index: 100; }
.header h1 { font-size: 14px; color: #89b4fa; }
.header .info { font-size: 11px; color: #6c7086; margin-top: 4px; }
.legend { display: flex; gap: 12px; margin-top: 6px; flex-wrap: wrap; }
.legend span { display: flex; align-items: center; gap: 4px; font-size: 11px; }
.legend .box { width: 14px; height: 14px; border-radius: 2px; display: inline-block; }
.hint { font-size: 10px; color: #585b70; margin-top: 4px; }
.container { flex: 1; overflow: auto; }
table { border-collapse: collapse; white-space: nowrap; }
th, td { padding: 0; height: 22px; text-align: center; }
th { position: sticky; top: 0; background: #181825; z-index: 10; color: #a6adc8; font-weight: normal; font-size: 10px; border-bottom: 1px solid #313244; min-width: 26px; }
th.label-col { text-align: left; padding: 0 8px; min-width: 280px; position: sticky; left: 0; z-index: 20; background: #181825; }
td.label-col { text-align: left; padding: 0 8px; font-size: 11px; position: sticky; left: 0; z-index: 5; background: #1e1e2e; border-right: 1px solid #313244; }
td.label-col:hover { background: #313244; }
.pc { color: #6c7086; }
.dis { color: #cdd6f4; }
.cell { width: 26px; height: 22px; }
.F { background: #89b4fa; color: #1e1e2e; font-weight: bold; }
.D { background: #a6e3a1; color: #1e1e2e; font-weight: bold; }
.I { background: #f9e2af; color: #1e1e2e; font-weight: bold; }
.E { background: #fab387; color: #1e1e2e; font-weight: bold; }
.W { background: #cba6f7; color: #1e1e2e; font-weight: bold; }
.C { background: #f38ba8; color: #1e1e2e; font-weight: bold; }
.stall { background: #45475a; color: #6c7086; }
.mispredict td.label-col .dis { color: #f38ba8; }
.exception td.label-col .dis { color: #fab387; }
.flush td.label-col { box-shadow: inset 3px 0 0 #f9e2af; }
.meta { color: #7f849c; font-size: 10px; margin-left: 8px; }
.meta.flush { color: #f9e2af; }
tr:hover td { opacity: 0.9; }
tr:hover td.label-col { background: #313244 !important; }
</style>
</head>
<body>
<div class="header">
  <h1>RISC-V OOO Pipeline View</h1>
  <div class="info">)";

    out << fmt::format("指令数: {} | 周期范围: {}-{} ({} cycles)", records_.size(), min_cycle, max_cycle, total_cycles);

    out << R"(</div>
  <div class="legend">
    <span><div class="box F"></div>Fetch</span>
    <span><div class="box D"></div>Decode</span>
    <span><div class="box I"></div>Issue</span>
    <span><div class="box E"></div>Execute</span>
    <span><div class="box W"></div>Writeback</span>
    <span><div class="box C"></div>Commit</span>
    <span><div class="box stall"></div>Stage duration (not necessarily stall)</span>
  </div>
  <div class="hint">Scroll: mouse wheel = vertical, Shift+wheel = horizontal, or drag the scrollbar. Only committed instructions are shown.</div>
</div>
<div class="container">
<table>
<thead><tr><th class="label-col">Instruction</th>
)";

    // 周期列头（显示周期数，但限制列数避免过大）
    const uint64_t display_cycles = std::min(total_cycles, static_cast<uint64_t>(500));
    for (uint64_t c = 0; c < display_cycles; c++) {
        uint64_t cycle = min_cycle + c;
        if (c % 5 == 0) {
            out << fmt::format("<th>{}</th>", cycle);
        } else {
            out << "<th></th>";
        }
    }
    out << "</tr></thead>\n<tbody>\n";

    // 每条指令一行
    for (const auto& rec : records_) {
        if (rec.fetch_cycle > min_cycle + display_cycles) break;

        std::string row_class;
        if (rec.has_exception) {
            row_class = " class=\"exception\"";
        } else if (rec.branch_mispredicted) {
            row_class = " class=\"mispredict\"";
        }
        if (rec.caused_flush) {
            row_class = row_class.empty() ? " class=\"flush\"" : row_class.substr(0, row_class.size() - 1) + " flush\"";
        }

        std::string label_html = fmt::format("<span class=\"pc\">{:08x}</span> <span class=\"dis\">{}</span>",
                                             rec.pc, rec.disassembly);
        if (rec.caused_flush) {
            std::string flush_meta = fmt::format("flush={} rob={} fb={}",
                                                 rec.flush_reason,
                                                 rec.flushed_rob_entries,
                                                 rec.fetch_buffer_dropped);
            if (rec.has_redirect_pc) {
                flush_meta += fmt::format(" redirect=0x{:x}", rec.redirect_pc);
            }
            label_html += fmt::format("<span class=\"meta flush\">{}</span>", flush_meta);
        }

        out << fmt::format("<tr{}><td class=\"label-col\">{}</td>",
                           row_class, label_html);

        // 各阶段的周期区间
        struct StageRange { uint64_t start; uint64_t end; const char* label; const char* cls; };
        std::vector<StageRange> stages;
        stages.push_back({rec.fetch_cycle, rec.decode_cycle, "F", "F"});
        stages.push_back({rec.decode_cycle, rec.issue_cycle, "D", "D"});
        stages.push_back({rec.issue_cycle, rec.execute_cycle, "I", "I"});
        stages.push_back({rec.execute_cycle, rec.complete_cycle, "E", "E"});
        stages.push_back({rec.complete_cycle, rec.retire_cycle, "W", "W"});
        stages.push_back({rec.retire_cycle, rec.retire_cycle + 1, "C", "C"});

        for (uint64_t c = 0; c < display_cycles; c++) {
            uint64_t cycle = min_cycle + c;
            bool found = false;
            for (const auto& s : stages) {
                if (cycle >= s.start && cycle < s.end) {
                    // 阶段首周期显示标签，后续周期显示等待
                    if (cycle == s.start) {
                        out << fmt::format("<td class=\"cell {}\">{}</td>", s.cls, s.label);
                    } else {
                        out << fmt::format("<td class=\"cell stall\">&middot;</td>");
                    }
                    found = true;
                    break;
                }
            }
            if (!found) {
                out << "<td class=\"cell\"></td>";
            }
        }

        out << "</tr>\n";
    }

    out << R"(</tbody></table></div>
<script>
(function() {
    var c = document.querySelector('.container');
    // Shift+wheel = vertical scroll (browser default in .container)
    // Plain wheel = horizontal scroll (most useful for wide pipeline view)
    c.addEventListener('wheel', function(e) {
        if (!e.shiftKey && Math.abs(e.deltaY) > Math.abs(e.deltaX)) {
            c.scrollLeft += e.deltaY;
            e.preventDefault();
        }
    }, {passive: false});

    // Middle-mouse drag to pan
    var dragging = false, startX = 0, startY = 0, scrollX0 = 0, scrollY0 = 0;
    c.addEventListener('mousedown', function(e) {
        if (e.button === 1 || (e.button === 0 && e.altKey)) {
            dragging = true; startX = e.clientX; startY = e.clientY;
            scrollX0 = c.scrollLeft; scrollY0 = c.scrollTop;
            c.style.cursor = 'grabbing'; e.preventDefault();
        }
    });
    window.addEventListener('mousemove', function(e) {
        if (!dragging) return;
        c.scrollLeft = scrollX0 - (e.clientX - startX);
        c.scrollTop = scrollY0 - (e.clientY - startY);
    });
    window.addEventListener('mouseup', function() {
        if (dragging) { dragging = false; c.style.cursor = ''; }
    });
})();
</script>
</body></html>)";

    out.close();
    return true;
}

} // namespace riscv
