#include "back/tac/codegen.h"

#include <cassert>

using namespace tinylang::back::tac;

/*

  Stack Organization of TinyLang:

  +--------------------+  <- last fp
  |   return address   |  4
  +--------------------+
  | last frame pointer |  4
  +--------------------+
  |                    |
  |  local  variables  |  n * 8
  |                    |
  +--------------------+
  |                    |
  |  arguments  (max)  |  n * 8
  |                    |
  +--------------------+  <- sp, fp

  Standard Functions of TinyLang:
    mul(lhs, rhs, is_unsigned)
    div(lhs, rhs, is_unsigned)
    mod(lhs, rhs, is_unsigned)

*/

namespace {

using Asm = TinyMIPSAsm;
using Opcode = TinyMIPSOpcode;
using Reg = TinyMIPSReg;

const char *kIndent = "\t";
const char *kEpilogueLabel = "_epilogue";
const char *kMul = "_std_mul";
const char *kDiv = "_std_div";
const char *kMod = "_std_mod";

}  // namespace

void CodeGenerator::GenerateGlobalVars() {
  code_ << kIndent << ".text" << std::endl;
  for (const auto &i : entry_->vars) {
    last_var_ = nullptr;
    i->GenerateCode(*this);
    // generate 'local'
    code_ << kIndent << ".local\t" << GetVarName(last_var_->id());
    code_ << std::endl;
    // generate 'comm'
    code_ << kIndent << ".comm\t" << GetVarName(last_var_->id());
    code_ << ", 4, 4" << std::endl;
  }
}

void CodeGenerator::GenerateArrayData() {
  // put all array to data section
  code_ << kIndent << ".data" << std::endl;
  for (std::size_t i = 0; i < datas_->size(); ++i) {
    auto name = GetDataName(i);
    // generate 'align'
    code_ << kIndent << ".align\t2" << std::endl;
    // generate 'type'
    code_ << kIndent << ".type\t" << name << ", @object" << std::endl;
    // generate size info
    auto arr_size = (*datas_)[i].elem_size * (*datas_)[i].content.size();
    code_ << kIndent << ".size\t" << name << ", " << arr_size << std::endl;
    // generate data label
    code_ << name << ':' << std::endl;
    // generate content
    for (const auto &tac : (*datas_)[i].content) {
      code_ << kIndent;
      // generate prefix
      if ((*datas_)[i].elem_size == 1) {
        code_ << ".byte\t";
      }
      else if ((*datas_)[i].elem_size == 4) {
        code_ << ".word\t";
      }
      else {
        assert(false);
      }
      // generate actual data
      last_data_ = nullptr;
      last_num_ = nullptr;
      tac->GenerateCode(*this);
      if (last_data_) {
        code_ << GetDataName(last_data_->id());
      }
      else if (last_num_) {
        code_ << last_num_->num();
      }
      else {
        assert(false);
      }
      code_ << std::endl;
    }
  }
}

void CodeGenerator::GenerateFunc(const std::string &name,
                                 const FuncInfo &info) {
  // generate head
  code_ << kIndent << ".align\t2" << std::endl;
  code_ << kIndent << ".globl\t" << name << std::endl;
  code_ << kIndent << ".ent\t" << name << std::endl;
  code_ << kIndent << ".type\t" << name << ", @function" << std::endl;
  // generate labels
  code_ << name << ':' << std::endl;
  last_label_ = nullptr;
  info.label->GenerateCode(*this);
  code_ << GetLabelName(last_label_->id()) << ':' << std::endl;
  // generate frame info
  auto local_size = var_alloc_.local_area_size();
  auto args_size = var_alloc_.arg_area_size();
  auto frame_size = 8 + local_size + args_size;
  code_ << kIndent << ".frame\t$fp, " << frame_size << ", $ra" << std::endl;
  // reset state
  asm_gen_.Reset();
  // generate prologue
  GeneratePrologue(info);
  // generate body
  for (const auto &i : info.irs) {
    last_label_ = nullptr;
    i->GenerateCode(*this);
    // generate labels
    if (last_label_) {
      asm_gen_.PushLabel(GetLabelName(last_label_->id()));
    }
  }
  // generate epilogue
  GenerateEpilogue();
  // put assembly code to stream
  asm_gen_.Dump(code_, kIndent);
  // generate end
  code_ << kIndent << ".end\t" << name << std::endl;
}

void CodeGenerator::GeneratePrologue(const FuncInfo &info) {
  auto local_size = var_alloc_.local_area_size();
  auto args_size = var_alloc_.arg_area_size();
  auto frame_size = 8 + local_size + args_size;
  // initialize stack pointer
  asm_gen_.PushAsm(Opcode::ADDIU, Reg::SP, Reg::SP, -frame_size);
  // store return address and last frame pointer
  asm_gen_.PushAsm(Opcode::SW, Reg::RA, Reg::SP, frame_size - 4);
  asm_gen_.PushAsm(Opcode::SW, Reg::FP, Reg::SP, frame_size - 8);
  // set current frame pointer
  asm_gen_.PushMove(Reg::FP, Reg::SP);
  // store arguments
  for (int i = 0; i < 4 && i < info.args.size(); ++i) {
    auto arg_reg = static_cast<Reg>(static_cast<int>(Reg::A0) + i);
    asm_gen_.PushAsm(Opcode::SW, arg_reg, Reg::FP, frame_size + i * 4);
  }
}

void CodeGenerator::GenerateEpilogue() {
  auto local_size = var_alloc_.local_area_size();
  auto args_size = var_alloc_.arg_area_size();
  auto frame_size = 8 + local_size + args_size;
  // generate label
  asm_gen_.PushLabel(kEpilogueLabel);
  // restore stack pointer
  asm_gen_.PushMove(Reg::SP, Reg::FP);
  // restore return address and frame pointer
  asm_gen_.PushAsm(Opcode::LW, Reg::RA, Reg::SP, frame_size - 4);
  asm_gen_.PushAsm(Opcode::LW, Reg::FP, Reg::SP, frame_size - 8);
  // release current stack frame
  asm_gen_.PushAsm(Opcode::ADDIU, Reg::SP, Reg::SP, frame_size);
  // return to last function
  asm_gen_.PushJump(Reg::RA);
}

std::string CodeGenerator::GetVarName(std::size_t id) {
  return "_var_" + std::to_string(id);
}

std::string CodeGenerator::GetDataName(std::size_t id) {
  return "_data_" + std::to_string(id);
}

std::string CodeGenerator::GetLabelName(std::size_t id) {
  return "_label_" + std::to_string(id);
}

Reg CodeGenerator::GetValue(const TACPtr &tac) {
  // get pointer of value
  last_var_ = nullptr;
  last_data_ = nullptr;
  last_arg_get_ = nullptr;
  last_num_ = nullptr;
  tac->GenerateCode(*this);
  assert(last_var_ || last_data_ || last_arg_get_ || last_num_);
  // handle each case
  if (last_var_) {
    if (entry_->vars.find(tac) != entry_->vars.end()) {
      // load global variable
      auto name = GetVarName(last_var_->id());
      asm_gen_.PushAsm(Opcode::LUI, Reg::V1, "%hi(" + name + ")");
      asm_gen_.PushAsm(Opcode::LW, Reg::V0, Reg::V1, "%lo(" + name + ")");
      return Reg::V0;
    }
    else {
      // get position of variable
      auto pos = var_alloc_.GetPosition(tac);
      assert(pos);
      if (auto slot = std::get_if<std::size_t>(&*pos)) {
        // load from local area
        auto offset = var_alloc_.arg_area_size() + *slot * 4;
        asm_gen_.PushAsm(Opcode::LW, Reg::V0, Reg::FP, offset);
        return Reg::V0;
      }
      else {
        // just return
        return *std::get_if<Reg>(&*pos);
      }
    }
  }
  else if (last_data_) {
    // load address of label
    asm_gen_.PushLoadImm(Reg::V0, GetDataName(last_data_->id()));
    return Reg::V0;
  }
  else if (last_arg_get_) {
    auto local_size = var_alloc_.local_area_size();
    auto args_size = var_alloc_.arg_area_size();
    auto offset = 8 + local_size + args_size + last_arg_get_->pos() * 4;
    // load argument from argument area
    asm_gen_.PushAsm(Opcode::LW, Reg::V0, Reg::FP, offset);
    return Reg::V0;
  }
  else {  // last_num_
    // store number to temporary register
    asm_gen_.PushLoadImm(Reg::V0, last_num_->num());
    return Reg::V0;
  }
}

void CodeGenerator::SetValue(const TACPtr &tac, Reg value) {
  // get pointer of value
  last_var_ = nullptr;
  tac->GenerateCode(*this);
  assert(last_var_);
  // handle each case
  if (entry_->vars.find(tac) != entry_->vars.end()) {
    // store to global variable
    auto name = GetVarName(last_var_->id());
    asm_gen_.PushAsm(Opcode::LUI, Reg::V1, "%hi(" + name + ")");
    asm_gen_.PushAsm(Opcode::SW, value, Reg::V1, "%lo(" + name + ")");
  }
  else {
    // get position of variable
    auto pos = var_alloc_.GetPosition(tac);
    if (!pos) {
      // get position of argument
      auto arg_pos = var_alloc_.GetArgPosition(tac);
      assert(arg_pos);
      if (*arg_pos < 4) {
        // just move
        auto dest = static_cast<Reg>(static_cast<int>(Reg::A0) + *arg_pos);
        asm_gen_.PushMove(dest, value);
      }
      else {
        // store to stack
        auto local_size = var_alloc_.local_area_size();
        auto args_size = var_alloc_.arg_area_size();
        auto offset = 8 + local_size + args_size + *arg_pos * 4;
        asm_gen_.PushAsm(Opcode::SW, value, Reg::FP, offset);
      }
    }
    else {
      if (auto slot = std::get_if<std::size_t>(&*pos)) {
        // store to local area
        auto offset = var_alloc_.arg_area_size() + *slot * 4;
        asm_gen_.PushAsm(Opcode::SW, value, Reg::FP, offset);
      }
      else {
        // just move
        auto dest = *std::get_if<Reg>(&*pos);
        asm_gen_.PushMove(dest, value);
      }
    }
  }
}

CodeGenerator::CodeGenerator() {
  entry_ = nullptr;
  funcs_ = nullptr;
  datas_ = nullptr;
  // add avaliable registers to allocator
  var_alloc_.AddAvailableRegister(Reg::T0);
  var_alloc_.AddAvailableRegister(Reg::T1);
  var_alloc_.AddAvailableRegister(Reg::T2);
  var_alloc_.AddAvailableRegister(Reg::T3);
  var_alloc_.AddAvailableRegister(Reg::T4);
  var_alloc_.AddAvailableRegister(Reg::T5);
  var_alloc_.AddAvailableRegister(Reg::T6);
  var_alloc_.AddAvailableRegister(Reg::T7);
  var_alloc_.AddAvailableRegister(Reg::T8);
  var_alloc_.AddAvailableRegister(Reg::T9);
}

void CodeGenerator::GenerateOn(BinaryTAC &tac) {
  // get lhs & rhs
  auto lhs = GetValue(tac.lhs()), rhs = GetValue(tac.rhs());
  // generate operation
  switch (tac.op()) {
    case BinaryOp::Add: {
      asm_gen_.PushAsm(Opcode::ADDU, Reg::V0, lhs, rhs);
      break;
    }
    case BinaryOp::Sub: {
      asm_gen_.PushAsm(Opcode::SUBU, Reg::V0, lhs, rhs);
      break;
    }
    case BinaryOp::Mul: {
      asm_gen_.PushMove(Reg::A0, lhs);
      asm_gen_.PushMove(Reg::A1, rhs);
      asm_gen_.PushLoadImm(Reg::A2, 0);
      asm_gen_.PushJump(kMul);
      break;
    }
    case BinaryOp::UMul: {
      asm_gen_.PushMove(Reg::A0, lhs);
      asm_gen_.PushMove(Reg::A1, rhs);
      asm_gen_.PushLoadImm(Reg::A2, 1);
      asm_gen_.PushJump(kMul);
      break;
    }
    case BinaryOp::Div: {
      asm_gen_.PushMove(Reg::A0, lhs);
      asm_gen_.PushMove(Reg::A1, rhs);
      asm_gen_.PushLoadImm(Reg::A2, 0);
      asm_gen_.PushJump(kDiv);
      break;
    }
    case BinaryOp::UDiv: {
      asm_gen_.PushMove(Reg::A0, lhs);
      asm_gen_.PushMove(Reg::A1, rhs);
      asm_gen_.PushLoadImm(Reg::A2, 1);
      asm_gen_.PushJump(kDiv);
      break;
    }
    case BinaryOp::Mod: {
      asm_gen_.PushMove(Reg::A0, lhs);
      asm_gen_.PushMove(Reg::A1, rhs);
      asm_gen_.PushLoadImm(Reg::A2, 0);
      asm_gen_.PushJump(kMod);
      break;
    }
    case BinaryOp::UMod: {
      asm_gen_.PushMove(Reg::A0, lhs);
      asm_gen_.PushMove(Reg::A1, rhs);
      asm_gen_.PushLoadImm(Reg::A2, 1);
      asm_gen_.PushJump(kMod);
      break;
    }
    case BinaryOp::Equal: {
      asm_gen_.PushAsm(Opcode::XOR, Reg::V0, lhs, rhs);
      asm_gen_.PushLoadImm(Reg::V1, 1);
      asm_gen_.PushAsm(Opcode::SLTU, Reg::V0, Reg::V0, Reg::V1);
      break;
    }
    case BinaryOp::NotEqual: {
      asm_gen_.PushAsm(Opcode::XOR, Reg::V0, lhs, rhs);
      asm_gen_.PushAsm(Opcode::SLTU, Reg::V0, Reg::Zero, Reg::V0);
      break;
    }
    case BinaryOp::Less: {
      asm_gen_.PushAsm(Opcode::SLT, Reg::V0, lhs, rhs);
      break;
    }
    case BinaryOp::ULess: {
      asm_gen_.PushAsm(Opcode::SLTU, Reg::V0, lhs, rhs);
      break;
    }
    case BinaryOp::LessEq: {
      asm_gen_.PushAsm(Opcode::SLT, Reg::V0, rhs, lhs);
      asm_gen_.PushLoadImm(Reg::V1, 1);
      asm_gen_.PushAsm(Opcode::XOR, Reg::V0, Reg::V0, Reg::V1);
      break;
    }
    case BinaryOp::ULessEq: {
      asm_gen_.PushAsm(Opcode::SLTU, Reg::V0, rhs, lhs);
      asm_gen_.PushLoadImm(Reg::V1, 1);
      asm_gen_.PushAsm(Opcode::XOR, Reg::V0, Reg::V0, Reg::V1);
      break;
    }
    case BinaryOp::Great: {
      asm_gen_.PushAsm(Opcode::SLT, Reg::V0, rhs, lhs);
      break;
    }
    case BinaryOp::UGreat: {
      asm_gen_.PushAsm(Opcode::SLTU, Reg::V0, rhs, lhs);
      break;
    }
    case BinaryOp::GreatEq: {
      asm_gen_.PushAsm(Opcode::SLT, Reg::V0, lhs, rhs);
      asm_gen_.PushLoadImm(Reg::V1, 1);
      asm_gen_.PushAsm(Opcode::XOR, Reg::V0, Reg::V0, Reg::V1);
      break;
    }
    case BinaryOp::UGreatEq: {
      asm_gen_.PushAsm(Opcode::SLTU, Reg::V0, lhs, rhs);
      asm_gen_.PushLoadImm(Reg::V1, 1);
      asm_gen_.PushAsm(Opcode::XOR, Reg::V0, Reg::V0, Reg::V1);
      break;
    }
    case BinaryOp::And: {
      asm_gen_.PushAsm(Opcode::AND, Reg::V0, lhs, rhs);
      break;
    }
    case BinaryOp::Or: {
      asm_gen_.PushAsm(Opcode::OR, Reg::V0, lhs, rhs);
      break;
    }
    case BinaryOp::Xor: {
      asm_gen_.PushAsm(Opcode::XOR, Reg::V0, lhs, rhs);
      break;
    }
    case BinaryOp::Shl: {
      asm_gen_.PushAsm(Opcode::SLLV, Reg::V0, lhs, rhs);
      break;
    }
    case BinaryOp::AShr: {
      asm_gen_.PushAsm(Opcode::SRAV, Reg::V0, lhs, rhs);
      break;
    }
    case BinaryOp::LShr: {
      asm_gen_.PushAsm(Opcode::SRLV, Reg::V0, lhs, rhs);
      break;
    }
    default: assert(false);
  }
  // store to dest
  SetValue(tac.dest(), Reg::V0);
}

void CodeGenerator::GenerateOn(UnaryTAC &tac) {
  if (tac.op() == UnaryOp::AddressOf) {
    // get variable ref
    last_var_ = nullptr;
    tac.opr()->GenerateCode(*this);
    assert(last_var_);
    // get address of variable
    if (entry_->vars.find(tac.opr()) != entry_->vars.end()) {
      // global variable
      auto name = GetVarName(last_var_->id());
      asm_gen_.PushLoadImm(Reg::V0, name);
    }
    else {
      // get slot position of variable
      auto pos = var_alloc_.GetPosition(tac.opr());
      assert(pos);
      auto slot = std::get_if<std::size_t>(&*pos);
      assert(slot);
      // get frame offset
      auto offset = var_alloc_.arg_area_size() + *slot * 4;
      asm_gen_.PushLoadImm(Reg::V0, offset);
    }
  }
  else {
    // get operand
    auto opr = GetValue(tac.opr());
    // generate operation
    switch (tac.op()) {
      case UnaryOp::Negate: {
        asm_gen_.PushAsm(Opcode::SUBU, Reg::V0, Reg::Zero, opr);
        break;
      }
      case UnaryOp::LogicNot: {
        asm_gen_.PushLoadImm(Reg::V1, 1);
        asm_gen_.PushAsm(Opcode::SLTU, Reg::V0, opr, Reg::V1);
        break;
      }
      case UnaryOp::Not: {
        asm_gen_.PushLoadImm(Reg::V1, -1);
        asm_gen_.PushAsm(Opcode::XOR, Reg::V0, opr, Reg::V1);
        break;
      }
      case UnaryOp::SExt: {
        asm_gen_.PushAsm(Opcode::SLL, Reg::V0, opr, 24);
        asm_gen_.PushLoadImm(Reg::V1, 24);
        asm_gen_.PushAsm(Opcode::SRAV, Reg::V0, Reg::V0, Reg::V1);
        break;
      }
      case UnaryOp::ZExt: case UnaryOp::Trunc: {
        asm_gen_.PushLoadImm(Reg::V1, 0xff);
        asm_gen_.PushAsm(Opcode::AND, Reg::V0, opr, Reg::V1);
        break;
      }
      default: assert(false);
    }
  }
  // store to dest
  SetValue(tac.dest(), Reg::V0);
}

void CodeGenerator::GenerateOn(LoadTAC &tac) {
  // get address
  auto addr = GetValue(tac.addr());
  // generate load
  if (tac.size() == 1) {
    auto op = tac.is_unsigned() ? Opcode::LBU : Opcode::LB;
    asm_gen_.PushAsm(op, Reg::V0, addr, 0);
  }
  else if (tac.size() == 4) {
    asm_gen_.PushAsm(Opcode::LW, Reg::V0, addr, 0);
  }
  else {
    assert(false);
  }
  // store to dest
  SetValue(tac.dest(), Reg::V0);
}

void CodeGenerator::GenerateOn(StoreTAC &tac) {
  // get address
  auto addr = GetValue(tac.addr());
  asm_gen_.PushMove(Reg::A0, addr);
  // get value
  auto value = GetValue(tac.value());
  // generate store
  if (tac.size() == 1) {
    asm_gen_.PushAsm(Opcode::SB, value, Reg::A0, 0);
  }
  else if (tac.size() == 4) {
    asm_gen_.PushAsm(Opcode::SW, value, Reg::A0, 0);
  }
  else {
    assert(false);
  }
}

void CodeGenerator::GenerateOn(JumpTAC &tac) {
  // generate label
  last_label_ = nullptr;
  tac.dest()->GenerateCode(*this);
  // generate jump
  asm_gen_.PushJump(GetLabelName(last_label_->id()));
}

void CodeGenerator::GenerateOn(BranchTAC &tac) {
  // get condition
  auto cond = GetValue(tac.cond());
  // generate label
  last_label_ = nullptr;
  tac.dest()->GenerateCode(*this);
  // generate branch
  asm_gen_.PushBranch(cond, GetLabelName(last_label_->id()));
}

void CodeGenerator::GenerateOn(CallTAC &tac) {
  // generate label
  last_label_ = nullptr;
  tac.func()->GenerateCode(*this);
  // generate function call
  asm_gen_.PushJump(GetLabelName(last_label_->id()));
  // set return value
  SetValue(tac.dest(), Reg::V0);
}

void CodeGenerator::GenerateOn(ReturnTAC &tac) {
  // generate return value
  if (tac.value()) {
    auto value = GetValue(tac.value());
    asm_gen_.PushMove(Reg::V0, value);
  }
  // generate return
  asm_gen_.PushJump(kEpilogueLabel);
}

void CodeGenerator::GenerateOn(AssignTAC &tac) {
  auto value = GetValue(tac.value());
  SetValue(tac.var(), value);
}

void CodeGenerator::GenerateOn(VarRefTAC &tac) { last_var_ = &tac; }
void CodeGenerator::GenerateOn(DataTAC &tac) { last_data_ = &tac; }
void CodeGenerator::GenerateOn(LabelTAC &tac) { last_label_ = &tac; }
void CodeGenerator::GenerateOn(ArgGetTAC &tac) { last_arg_get_ = &tac; }
void CodeGenerator::GenerateOn(NumberTAC &tac) { last_num_ = &tac; }

void CodeGenerator::Generate() {
  // generate global variables & arrays
  GenerateGlobalVars();
  GenerateArrayData();
  // store declaration info
  for (const auto &i : *funcs_) {
    if (i.second.irs.empty()) {
      decls_.insert({i.second.label, i.first});
    }
  }
  // generate each function
  code_ << kIndent << ".text" << std::endl;
  for (auto &&i : *funcs_) {
    if (!i.second.irs.empty()) {
      // do variable allocation
      var_alloc_.Run(i.second);
      // generate current function
      GenerateFunc(i.first, i.second);
    }
  }
}

void CodeGenerator::Dump(std::ostream &os) {
  os << code_.str();
}