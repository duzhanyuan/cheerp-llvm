//===-- CheerpWastWriter.cpp - The Cheerp JavaScript generator ------------===//
//
//                     Cheerp: The C++ compiler for the Web
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
// Copyright 2017 Leaning Technologies
//
//===----------------------------------------------------------------------===//

#include <algorithm>

#include "Relooper.h"
#include "llvm/Cheerp/NameGenerator.h"
#include "llvm/Cheerp/Writer.h"
#include "llvm/Cheerp/WastWriter.h"

using namespace cheerp;
using namespace llvm;
using namespace std;

enum BLOCK_TYPE { WHILE1 = 0, DO, SWITCH, CASE, IF };

class BlockType
{
public:
	BLOCK_TYPE type;
	uint32_t depth;

	BlockType(BLOCK_TYPE bt, uint32_t depth = 0)
	 :
		type(bt),
		depth(depth)
	{}
};

BlockType* findSwitchBlockType(std::vector<BlockType>& blocks)
{
	for (size_t i = blocks.size(); i;)
	{
		BlockType* block = &blocks[--i];
		if (block->type == SWITCH)
			return block;
	}

	llvm_unreachable("switch render block not found");
}

class CheerpWastRenderInterface: public RenderInterface
{
private:
	CheerpWastWriter* writer;
	std::vector<BlockType> blockTypes;
	uint32_t labelLocal;
	void renderCondition(const BasicBlock* B, int branchId);
	void indent();
public:
	const BasicBlock* lastDepth0Block;
	CheerpWastRenderInterface(CheerpWastWriter* w, uint32_t labelLocal)
	 :
		writer(w),
		labelLocal(labelLocal),
		lastDepth0Block(nullptr)
	{ }
	void renderBlock(const void* privateBlock);
	void renderLabelForSwitch(int labelId);
	void renderSwitchOnLabel(IdShapeMap& idShapeMap);
	void renderCaseOnLabel(int labelId);
	void renderSwitchBlockBegin(const void* privateBranchVar, BlockBranchMap& branchesOut);
	void renderCaseBlockBegin(const void* privateBlock, int branchId);
	void renderDefaultBlockBegin();
	void renderIfBlockBegin(const void* privateBlock, int branchId, bool first);
	void renderIfBlockBegin(const void* privateBlock, const vector<int>& branchId, bool first);
	void renderElseBlockBegin();
	void renderBlockEnd();
	void renderBlockPrologue(const void* privateBlockTo, const void* privateBlockFrom);
	bool hasBlockPrologue(const void* privateBlockTo, const void* privateBlockFrom) const;
	void renderWhileBlockBegin();
	void renderWhileBlockBegin(int labelId);
	void renderDoBlockBegin();
	void renderDoBlockBegin(int labelId);
	void renderDoBlockEnd();
	void renderBreak();
	void renderBreak(int labelId);
	void renderContinue();
	void renderContinue(int labelId);
	void renderLabel(int labelId);
	void renderIfOnLabel(int labelId, bool first);
};

void CheerpWastRenderInterface::renderBlock(const void* privateBlock)
{
	const BasicBlock* bb=(const BasicBlock*)privateBlock;
	if (blockTypes.empty())
		lastDepth0Block = bb;
	else
		lastDepth0Block = nullptr;
	writer->compileBB(*bb);
}

void CheerpWastRenderInterface::indent()
{
	for(uint32_t i=0;i<blockTypes.size();i++)
		writer->stream << "  ";
}

void CheerpWastRenderInterface::renderCondition(const BasicBlock* bb, int branchId)
{
	const TerminatorInst* term=bb->getTerminator();

	if(isa<BranchInst>(term))
	{
		const BranchInst* bi=cast<BranchInst>(term);
		assert(bi->isConditional());
		//The second branch is the default
		assert(branchId==0);
		writer->compileOperand(bi->getCondition());
	}
	else if(isa<SwitchInst>(term))
	{
		const SwitchInst* si=cast<SwitchInst>(term);
		assert(branchId > 0);
		SwitchInst::ConstCaseIt it=si->case_begin();
		for(int i=1;i<branchId;i++)
			++it;
		const BasicBlock* dest=it.getCaseSuccessor();
		writer->compileOperand(si->getCondition());
		writer->stream << '\n';
		writer->compileOperand(it.getCaseValue());
		writer->stream << "\ni32.eq";
		//We found the destination, there may be more cases for the same
		//destination though
		for(++it;it!=si->case_end();++it)
		{
			if(it.getCaseSuccessor()==dest)
			{
				//Also add this condition
				writer->stream << '\n';
				writer->compileOperand(si->getCondition());
				writer->stream << '\n';
				writer->compileOperand(it.getCaseValue());
				writer->stream << "\ni32.eq\ni32.or";
			}
		}
	}
	else
	{
		term->dump();
		llvm::report_fatal_error("Unsupported code found, please report a bug", false);
	}
}

void CheerpWastRenderInterface::renderLabelForSwitch(int labelId)
{
assert(false);
#if 0
	writer->stream << 'L' << labelId << ':';
#endif
}

void CheerpWastRenderInterface::renderSwitchOnLabel(IdShapeMap& idShapeMap)
{
	int64_t max = std::numeric_limits<int64_t>::min();
	int64_t min = std::numeric_limits<int64_t>::max();
	for (auto iter = idShapeMap.begin(); iter != idShapeMap.end(); iter++)
	{
		int64_t curr = iter->first;
		max = std::max(max, curr);
		min = std::min(min, curr);
	}

	// There should be at least one case.
	uint32_t depth = max - min + 1;
	assert(depth >= 1);

	// Fill the jump table. By default, jump to the first block. This block
	// will do nothing.
	std::vector<uint32_t> table;
	table.assign(depth, 0);
	uint32_t blockIndex = 1;

	for (auto iter = idShapeMap.begin(); iter != idShapeMap.end(); iter++) {
		table.at(iter->first - min) = blockIndex;
		blockIndex++;
	}

	for (uint32_t i = 0; i < idShapeMap.size() + 1; i++)
		writer->stream << "block\n";

	// Wrap the br_table instruction in its own block
	writer->stream << "block\n";
	writer->stream << "get_local " << labelLocal;
	if (min != 0)
	{
		writer->stream << "\ni32.const " << min;
		writer->stream << "\ni32.sub";
	}
	writer->stream << "\nbr_table";

	for (auto label : table)
		writer->stream << " " << label;
	writer->stream << " 0\n";

	writer->stream << "\nend\n";

	// The first block does not do anything, and breaks out of the switch.
	writer->stream << "br " << idShapeMap.size() << "\n";
	writer->stream << "\nend\n";

	blockTypes.emplace_back(SWITCH, idShapeMap.size());
}

void CheerpWastRenderInterface::renderCaseOnLabel(int labelId)
{
	BlockType prevBlock = blockTypes.back();
	assert(prevBlock.type == SWITCH || prevBlock.type == CASE);
	assert(findSwitchBlockType(blockTypes)->depth > 0);

	blockTypes.emplace_back(CASE);
}

uint32_t findBlockInBranchesOutMap(const BasicBlock* dest, BlockBranchMap& branchesOut)
{
	int i = 0;
	for (auto it = branchesOut.begin(); it != branchesOut.end(); ++it) {
		if ((BasicBlock*)it->first->privateBlock == dest)
			return i;
		// Do not count the default block. The default block will be rendered
		// at the end by relooper.
		if (it->second->branchId == -1)
			continue;
		i++;
	}

	llvm_unreachable("destination not found in branches out");
}

void CheerpWastRenderInterface::renderSwitchBlockBegin(const void* privateSwitchInst, BlockBranchMap& branchesOut)
{
	const SwitchInst* si = static_cast<const SwitchInst*>(privateSwitchInst);

	assert(si->getNumCases());

	int64_t max = std::numeric_limits<int64_t>::min();
	int64_t min = std::numeric_limits<int64_t>::max();
	for (auto& c: si->cases())
	{
		int64_t curr = c.getCaseValue()->getSExtValue();
		max = std::max(max, curr);
		min = std::min(min, curr);
	}

	// There should be at least one default case and zero or more cases.
	uint32_t depth = (max - min + 1) + 1;
	assert(depth >= 1);

	// Fill the jump table.
	std::vector<int32_t> table;
	table.assign(depth, -1);

	std::unordered_map<const llvm::BasicBlock*, uint32_t> blockIndexMap;
	uint32_t caseBlocks = 0;

	for (auto it : si->cases())
	{
		const BasicBlock* dest = it.getCaseSuccessor();
		const auto& found = blockIndexMap.find(dest);

		if (found == blockIndexMap.end())
		{
			// Use the block index from the Relooper branches list. Otherwise,
			// it is possible that the Relooper branches list does not match
			// with the order of the LLVM Basic Blocks.
			uint32_t blockIndex = findBlockInBranchesOutMap(dest, branchesOut);
			blockIndexMap.emplace(dest, blockIndex);
			table.at(it.getCaseValue()->getSExtValue() - min) = blockIndex;

			// Add cases that have the same destination
			auto it_next = it;
			for (++it_next; it_next != si->case_end(); ++it_next)
			{
				if (it_next.getCaseSuccessor() != dest)
					continue;

				table.at(it_next.getCaseValue()->getSExtValue() - min) = blockIndex;
			}

			caseBlocks++;
		}
	}

	// Elements that are not set, will jump to the default block.
	std::replace(table.begin(), table.end(), -1, (int32_t)caseBlocks);

	// Print the case blocks and the default block.
	for (uint32_t i = 0; i < caseBlocks + 1; i++)
		writer->stream << "block\n";

	// Wrap the br_table instruction in its own block.
	writer->stream << "block\n";
	writer->compileOperand(si->getCondition());
	if (min != 0)
	{
		writer->stream << "\ni32.const " << min;
		writer->stream << "\ni32.sub";
	}
	writer->stream << "\nbr_table";

	// Print the case labels and the default label.
	for (auto label : table)
		writer->stream << " " << label;
	writer->stream << " " << caseBlocks << "\n";

	writer->stream << "end\n";

	blockTypes.emplace_back(SWITCH, caseBlocks + 1);
}

void CheerpWastRenderInterface::renderCaseBlockBegin(const void* privateBlock, int branchId)
{
	BlockType prevBlock = blockTypes.back();
	assert(prevBlock.type == SWITCH || prevBlock.type == CASE);
	assert(findSwitchBlockType(blockTypes)->depth > 0);

	blockTypes.emplace_back(CASE);
}

void CheerpWastRenderInterface::renderDefaultBlockBegin()
{
	renderCaseBlockBegin(nullptr, 0);
}

void CheerpWastRenderInterface::renderIfBlockBegin(const void* privateBlock, int branchId, bool first)
{
	const BasicBlock* bb=(const BasicBlock*)privateBlock;
	if(!first)
	{
		indent();
		writer->stream << "else\n";
	}
	// The condition goes first
	renderCondition(bb, branchId);
	writer->stream << '\n';
	indent();
	writer->stream << "if\n";
	if(first)
	{
		blockTypes.emplace_back(IF);
	}
	else
	{
		assert(blockTypes.back().type == IF);
		blockTypes.back().depth += 1;
	}
}

void CheerpWastRenderInterface::renderIfBlockBegin(const void* privateBlock, const std::vector<int>& skipBranchIds, bool first)
{
	const BasicBlock* bb=(const BasicBlock*)privateBlock;
	if(!first)
	{
		indent();
		writer->stream << "else\n";
	}
	// The condition goes first
	for(uint32_t i=0;i<skipBranchIds.size();i++)
	{
		if(i!=0)
		{
assert(false);
#if 0
			writer->stream << "||";
#endif
		}
		renderCondition(bb, skipBranchIds[i]);
		writer->stream << '\n';
	}
	// Invert result
	writer->stream << "i32.const 1\n";
	writer->stream << "i32.xor\n";
	indent();
	writer->stream << "if\n";

	if(first)
	{
		blockTypes.emplace_back(IF);
	}
	else
	{
		assert(blockTypes.back().type == IF);
		blockTypes.back().depth += 1;
	}
}

void CheerpWastRenderInterface::renderElseBlockBegin()
{
	assert(!blockTypes.empty());
	assert(blockTypes.back().type == IF);

	indent();
	writer->stream << "else\n";
}

void CheerpWastRenderInterface::renderBlockEnd()
{
	assert(!blockTypes.empty());
	BlockType block = blockTypes.back();
	blockTypes.pop_back();

	if(block.type == WHILE1)
	{
		// TODO: Why do we even need to fake value
		writer->stream << "i32.const 0\n";
		writer->stream << "br 1\n";
		writer->stream << "end\n";
		writer->stream << "end\n";
	}
	else if (block.type == CASE)
	{
		writer->stream << "end\n";
		BlockType* switchBlock = findSwitchBlockType(blockTypes);
		assert(switchBlock->depth > 0);
		switchBlock->depth--;
	}
	else if(block.type == IF)
	{
		for(uint32_t i = 0; i < block.depth + 1; i++)
		{
			indent();
			writer->stream << "end\n";
		}
	}
	else if (block.type == SWITCH)
	{
		assert(block.depth == 0);
	}
	else
	{
		assert(false);
	}
}

void CheerpWastRenderInterface::renderBlockPrologue(const void* privateBlockTo, const void* privateBlockFrom)
{
	const BasicBlock* bbTo=(const BasicBlock*)privateBlockTo;
	const BasicBlock* bbFrom=(const BasicBlock*)privateBlockFrom;
	writer->compilePHIOfBlockFromOtherBlock(bbTo, bbFrom);
}

bool CheerpWastRenderInterface::hasBlockPrologue(const void* privateBlockTo, const void* privateBlockFrom) const
{
	const BasicBlock* to=(const BasicBlock*)privateBlockTo;
	const BasicBlock* from=(const BasicBlock*)privateBlockFrom;

	if (to->getFirstNonPHI()==&to->front())
		return false;

	// We can avoid assignment from the same register if no pointer kind
	// conversion is required
	return writer->needsPointerKindConversionForBlocks(to, from);
}

void CheerpWastRenderInterface::renderWhileBlockBegin()
{
	// Wrap a block in a loop so that:
	// br 1 -> break
	// br 2 -> continue
	indent();
	writer->stream << "loop\n";
	indent();
	writer->stream << "block\n";
	blockTypes.emplace_back(WHILE1);
}

void CheerpWastRenderInterface::renderWhileBlockBegin(int blockLabel)
{
	// Wrap a block in a loop so that:
	// br 1 -> break
	// br 2 -> continue
	indent();
	writer->stream << "loop $c" << blockLabel << "\n";
	indent();
	writer->stream << "block $" << blockLabel << "\n";
	blockTypes.emplace_back(WHILE1);
}

void CheerpWastRenderInterface::renderDoBlockBegin()
{
	indent();
	writer->stream << "block\n";
	blockTypes.emplace_back(DO);
}

void CheerpWastRenderInterface::renderDoBlockBegin(int blockLabel)
{
	indent();
	writer->stream << "block $" << blockLabel << "\n";
	blockTypes.emplace_back(DO);
}

void CheerpWastRenderInterface::renderDoBlockEnd()
{
	assert(!blockTypes.empty());
	assert(blockTypes.back().type == DO);
	blockTypes.pop_back();

	indent();
	writer->stream << "end\n";
}

void CheerpWastRenderInterface::renderBreak()
{
	BlockType block = blockTypes.back();
	if (block.type == CASE)
	{
		BlockType* switchBlock = findSwitchBlockType(blockTypes);
		assert(switchBlock->depth > 0);
		writer->stream << "br " << (switchBlock->depth - 1) << "\n";
	}
	else
	{
		// Find the last loop's block
		uint32_t breakIndex = 0;
		for (uint32_t i = 0; i < blockTypes.size(); i++)
		{
			BLOCK_TYPE bt = blockTypes[blockTypes.size() - i - 1].type;
			if (bt == DO || bt == WHILE1)
				break;

			breakIndex += blockTypes[blockTypes.size() - i - 1].depth + 1;
		}
		writer->stream << "br " << breakIndex << "\n";
	}
}

void CheerpWastRenderInterface::renderBreak(int labelId)
{
	// DO blocks only have one label
	// WHILE1 blocks have the "block" without a prefix
	writer->stream << "br $" << labelId << '\n';
}

void CheerpWastRenderInterface::renderContinue()
{
	// Find the last loop's block
	uint32_t breakIndex = 0;
	for (uint32_t i = 0; i < blockTypes.size(); i++)
	{
		BLOCK_TYPE bt = blockTypes[blockTypes.size() - i - 1].type;
		if (bt == DO || bt == WHILE1)
			break;

		breakIndex += blockTypes[blockTypes.size() - i - 1].depth + 1;
	}
	breakIndex += 1;
	writer->stream << "br " << breakIndex << "\n";
}

void CheerpWastRenderInterface::renderContinue(int labelId)
{
	writer->stream << "br $c" << labelId << '\n';
}

void CheerpWastRenderInterface::renderLabel(int labelId)
{
	writer->stream << "i32.const " << labelId << '\n';
	writer->stream << "set_local " << labelLocal << '\n';
}

void CheerpWastRenderInterface::renderIfOnLabel(int labelId, bool first)
{
	// TODO: Use first to optimize dispatch
	writer->stream << "i32.const " << labelId << '\n';
	writer->stream << "get_local " << labelLocal << '\n';
	writer->stream << "i32.eq\n";
	indent();
	writer->stream << "if\n";
	blockTypes.emplace_back(IF);
}

bool CheerpWastWriter::needsPointerKindConversion(const Instruction* phi, const Value* incoming)
{
	const Instruction* incomingInst=dyn_cast<Instruction>(incoming);
	if(!incomingInst)
		return true;
	return isInlineable(*incomingInst, PA) ||
		registerize.getRegisterId(phi)!=registerize.getRegisterId(incomingInst);
}


bool CheerpWastWriter::needsPointerKindConversionForBlocks(const BasicBlock* to, const BasicBlock* from)
{
	class PHIHandler: public EndOfBlockPHIHandler
	{
	public:
		PHIHandler(CheerpWastWriter& w):EndOfBlockPHIHandler(w.PA),needsPointerKindConversion(false),writer(w)
		{
		}
		~PHIHandler()
		{
		}
		bool needsPointerKindConversion;
	private:
		CheerpWastWriter& writer;
		void handleRecursivePHIDependency(const Instruction* incoming) override
		{
		}
		void handlePHI(const Instruction* phi, const Value* incoming) override
		{
			needsPointerKindConversion |= writer.needsPointerKindConversion(phi, incoming);
		}
	};

	auto handler = PHIHandler(*this);
	handler.runOnEdge(registerize, from, to);
	return handler.needsPointerKindConversion;
}

void CheerpWastWriter::compilePHIOfBlockFromOtherBlock(const BasicBlock* to, const BasicBlock* from)
{
	class WriterPHIHandler: public EndOfBlockPHIHandler
	{
	public:
		WriterPHIHandler(CheerpWastWriter& w, const BasicBlock* f, const BasicBlock* t):EndOfBlockPHIHandler(w.PA),writer(w),fromBB(f),toBB(t)
		{
		}
		~WriterPHIHandler()
		{
		}
	private:
		CheerpWastWriter& writer;
		const BasicBlock* fromBB;
		const BasicBlock* toBB;
		void handleRecursivePHIDependency(const Instruction* incoming) override
		{
			assert(incoming);
			writer.stream << "get_local " << (1 + writer.currentFun->arg_size() + writer.registerize.getRegisterId(incoming)) << '\n';
			writer.stream << "set_local " << (1 + writer.currentFun->arg_size() + writer.registerize.getRegisterIdForEdge(incoming, fromBB, toBB)) << '\n';
		}
		void handlePHI(const Instruction* phi, const Value* incoming) override
		{
			// We can avoid assignment from the same register if no pointer kind conversion is required
			if(!writer.needsPointerKindConversion(phi, incoming))
				return;
			// 1) Put the value on the stack
			writer.registerize.setEdgeContext(fromBB, toBB);
			writer.compileOperand(incoming);
			writer.registerize.clearEdgeContext();
			// 2) Save the value in the phi
			writer.stream << "\nset_local " << (1 + writer.currentFun->arg_size() + writer.registerize.getRegisterId(phi)) << '\n';
		}
	};
	WriterPHIHandler(*this, from, to).runOnEdge(registerize, from, to);
}

const char* CheerpWastWriter::getTypeString(Type* t)
{
	if(t->isIntegerTy() || t->isPointerTy())
		return "i32";
	else if(t->isFloatTy())
		return "f32";
	else if(t->isDoubleTy())
		return "f64";
	else
	{
		llvm::errs() << "Unsupported type " << *t << "\n";
		llvm_unreachable("Unsuppored type");
	}
}

void CheerpWastWriter::compileGEP(const llvm::User* gep_inst)
{
	WastGepWriter gepWriter(*this);
	const llvm::Value *p = linearHelper.compileGEP(gep_inst, &gepWriter);
	compileOperand(p);
	if(!gepWriter.first)
		stream << "\ni32.add";
}

void CheerpWastWriter::compileSignedInteger(const llvm::Value* v, bool forComparison)
{
	uint32_t shiftAmount = 32-v->getType()->getIntegerBitWidth();
	if(const ConstantInt* C = dyn_cast<ConstantInt>(v))
	{
		if(forComparison)
			stream << "i32.const " << (C->getSExtValue() << shiftAmount) << '\n';
		else
			stream << "i32.const " << C->getSExtValue() << '\n';
		return;
	}

	compileOperand(v);
	stream << '\n';

	if (shiftAmount == 0)
		return;

	if (forComparison)
	{
		// When comparing two signed values we can avoid the right shift
		stream << "i32.const " << shiftAmount << '\n';
		stream << "i32.shl\n";
	}
	else
	{
		stream << "i32.const " << shiftAmount << '\n';
		stream << "i32.shl\n";
		stream << "i32.const " << shiftAmount << '\n';
		stream << "i32.shr_s\n";
	}
}

void CheerpWastWriter::compileUnsignedInteger(const llvm::Value* v)
{
	if(const ConstantInt* C = dyn_cast<ConstantInt>(v))
	{
		stream << "i32.const " << C->getZExtValue() << '\n';
		return;
	}

	compileOperand(v);
	stream << '\n';

	uint32_t initialSize = v->getType()->getIntegerBitWidth();
	if(initialSize != 32)
	{
		stream << "i32.const " << getMaskForBitWidth(initialSize) << '\n';
		stream << "i32.and\n";
	}
}

void CheerpWastWriter::compileConstantExpr(const ConstantExpr* ce)
{
	switch(ce->getOpcode())
	{
		case Instruction::GetElementPtr:
		{
			compileGEP(ce);
			break;
		}
		case Instruction::BitCast:
		{
			assert(ce->getOperand(0)->getType()->isPointerTy());
			compileOperand(ce->getOperand(0));
			break;
		}
		case Instruction::IntToPtr:
		{
			compileOperand(ce->getOperand(0));
			break;
		}
		case Instruction::ICmp:
		{
			CmpInst::Predicate p = (CmpInst::Predicate)ce->getPredicate();
			compileOperand(ce->getOperand(0));
			stream << '\n';
			compileOperand(ce->getOperand(1));
			stream << '\n';
			stream << getTypeString(ce->getOperand(0)->getType())
				<< '.' << getIntegerPredicate(p);
			break;
		}
		case Instruction::PtrToInt:
		{
			compileOperand(ce->getOperand(0));
			break;
		}
#if 0
		case Instruction::Select:
		{
			compileSelect(ce, ce->getOperand(0), ce->getOperand(1), ce->getOperand(2), HIGHEST);
			break;
		}
		case Instruction::Sub:
		{
			compileSubtraction(ce->getOperand(0), ce->getOperand(1), HIGHEST);
			break;
		}
#endif
		default:
			stream << "undefined";
			llvm::errs() << "warning: Unsupported constant expr " << ce->getOpcodeName() << '\n';
	}
}

void CheerpWastWriter::compileConstant(const Constant* c)
{
	if(const ConstantExpr* CE = dyn_cast<ConstantExpr>(c))
	{
		compileConstantExpr(CE);
	}
	else if(const ConstantInt* i=dyn_cast<ConstantInt>(c))
	{
		stream << getTypeString(i->getType()) << ".const ";
		if(i->getBitWidth()==32)
			stream << i->getSExtValue();
		else
			stream << i->getZExtValue();
	}
	else if(const ConstantFP* f=dyn_cast<ConstantFP>(c))
	{
		stream << getTypeString(f->getType()) << ".const ";
		if(f->getValueAPF().isInfinity())
		{
			if(f->getValueAPF().isNegative())
				stream << '-';
			stream << "infinity";
		}
		else if(f->getValueAPF().isNaN())
		{
			stream << "nan";
		}
		else
		{
			APFloat apf = f->getValueAPF();
			char buf[40];
			// TODO: Figure out the right amount of hexdigits
			unsigned charCount = apf.convertToHexString(buf, f->getType()->isFloatTy() ? 8 : 16, false, APFloat::roundingMode::rmNearestTiesToEven);
			assert(charCount < 40);
			stream << buf;
		}
	}
	else if(const GlobalVariable* GV = dyn_cast<GlobalVariable>(c))
	{
		stream << "i32.const " << linearHelper.getGlobalVariableAddress(GV);
	}
	else if(isa<ConstantPointerNull>(c))
	{
		stream << "i32.const 0";
	}
	else if(isa<Function>(c))
	{
		const Function* F = cast<Function>(c);
		if (globalDeps.functionAddresses().count(F) &&
			globalDeps.functionTables().count(F->getFunctionType()))
		{
			int offset = globalDeps.functionAddresses().at(F);
			const auto& functionTable = globalDeps.functionTables().at(F->getFunctionType());
			int functionTableOffset = functionTableOffsets.at(functionTable.name);
			stream << "i32.const " << functionTableOffset + offset;
		}
		else
		{
			c->dump();
			assert(false);
		}
	}
	else if (isa<UndefValue>(c))
	{
		stream << "i32.const 0";
	}
	else
	{
		c->dump();
		assert(false);
	}
}

void CheerpWastWriter::compileOperand(const llvm::Value* v)
{
	if(const Constant* c=dyn_cast<Constant>(v))
		compileConstant(c);
	else if(const Instruction* it=dyn_cast<Instruction>(v))
	{
		if(isInlineable(*it, PA))
			compileInstruction(*it);
		else
			stream << "get_local " << (1 + currentFun->arg_size() + registerize.getRegisterId(it));
	}
	else if(const Argument* arg=dyn_cast<Argument>(v))
	{
		stream << "get_local " << arg->getArgNo();
	}
	else
	{
		v->dump();
		assert(false);
	}
}

const char* CheerpWastWriter::getIntegerPredicate(llvm::CmpInst::Predicate p)
{
	switch(p)
	{
		case CmpInst::ICMP_EQ:
			return "eq";
		case CmpInst::ICMP_NE:
			return "ne";
		case CmpInst::ICMP_SGE:
			return "ge_s";
		case CmpInst::ICMP_SGT:
			return "gt_s";
		case CmpInst::ICMP_SLE:
			return "le_s";
		case CmpInst::ICMP_SLT:
			return "lt_s";
		case CmpInst::ICMP_UGE:
			return "ge_u";
		case CmpInst::ICMP_UGT:
			return "gt_u";
		case CmpInst::ICMP_ULE:
			return "le_u";
		case CmpInst::ICMP_ULT:
			return "lt_u";
		default:
			llvm::errs() << "Handle predicate " << p << "\n";
			break;
	}
	return "";
}

void CheerpWastWriter::compileDowncast(ImmutableCallSite callV)
{
	assert(callV.arg_size() == 2);
	assert(callV.getCalledFunction()->getIntrinsicID() == Intrinsic::cheerp_downcast);

	const Value* src = callV.getArgument(0);
	const Value* offset = callV.getArgument(1);

	Type* t = src->getType()->getPointerElementType();

	compileOperand(src);
	stream << '\n';

	if(!TypeSupport::isClientType(t) &&
			(!isa<ConstantInt>(offset) || !cast<ConstantInt>(offset)->isNullValue()))
	{
		compileOperand(offset);
		stream << '\n';
		stream << "i32.add\n";
	}
}

bool CheerpWastWriter::compileInstruction(const Instruction& I)
{
	switch(I.getOpcode())
	{
		case Instruction::Alloca:
		{
			const AllocaInst* ai = cast<AllocaInst>(&I);
			Type* allocTy = ai->getAllocatedType();
			// TODO: There is another method that includes the alignment
			uint32_t size = targetData.getTypeAllocSize(allocTy);
			uint32_t alignment = TypeSupport::getAlignmentAsmJS(targetData, allocTy);
			assert (!ai->isArrayAllocation());
			assert((alignment & (alignment-1)) == 0 && "alignment must be power of 2");
			// We grow the stack down for now
			// 1) Push the current stack pointer
			stream << "get_global " << stackTopGlobal << '\n';
			// 2) Push the allocation size
			stream << "i32.const " << size << '\n';
			// 3) Substract the size
			stream << "i32.sub\n";
			// 3.1) Optionally align the stack down
			if(size % alignment)
			{
				stream << "i32.const " << uint32_t(0-alignment) << '\n';
				stream << "i32.and\n";
			}
			// 4) Write the location to the local, but preserve the value
			stream << "tee_local " << (1 + currentFun->arg_size() + registerize.getRegisterId(&I)) << '\n';
			// 5) Save the new stack position
			stream << "set_global " << stackTopGlobal << '\n';
			return true;
		}
		case Instruction::Add:
		{
			compileOperand(I.getOperand(0));
			stream << '\n';
			compileOperand(I.getOperand(1));
			stream << '\n';
			stream << getTypeString(I.getType()) << ".add";
			break;
		}
		case Instruction::And:
		{
			compileOperand(I.getOperand(0));
			stream << '\n';
			compileOperand(I.getOperand(1));
			stream << '\n';
			stream << getTypeString(I.getType()) << ".and";
			break;
		}
		case Instruction::AShr:
		{
			compileOperand(I.getOperand(0));
			stream << '\n';
			compileOperand(I.getOperand(1));
			stream << '\n';
			stream << getTypeString(I.getType()) << ".shr_s";
			break;
		}
		case Instruction::BitCast:
		{
			assert(I.getType()->isPointerTy());
			compileOperand(I.getOperand(0));
			break;
		}
		case Instruction::Br:
			break;
		case Instruction::VAArg:
		{
			const VAArgInst& vi=cast<VAArgInst>(I);

			// Load the current argument
			compileOperand(vi.getPointerOperand());
			stream << "\n";
			stream << "i32.load\n";
			stream << getTypeString(vi.getType()) << ".load\n";

			// Move varargs pointer to next argument
			compileOperand(vi.getPointerOperand());
			stream << "\n";
			compileOperand(vi.getPointerOperand());
			stream << "\n";
			stream << "i32.load\n";
			stream << "i32.const 8\n";
			stream << "i32.add\n";
			stream << "i32.store\n";
			break;
		}
		case Instruction::Call:
		{
			const CallInst& ci = cast<CallInst>(I);
			const Function * calledFunc = ci.getCalledFunction();
			const Value * calledValue = ci.getCalledValue();
			const PointerType* pTy = cast<PointerType>(calledValue->getType());
			const FunctionType* fTy = cast<FunctionType>(pTy->getElementType());
			assert(!ci.isInlineAsm());

			if (calledFunc)
			{
				switch (calledFunc->getIntrinsicID())
				{
					case Intrinsic::trap:
					{
						stream << "unreachable ;; trap\n";
						return true;
					}
					case Intrinsic::vastart:
					{
						compileOperand(ci.getOperand(0));
						stream << '\n';
						uint32_t numArgs = I.getParent()->getParent()->arg_size();
						stream << "get_local " << numArgs << "\n";
						stream << "i32.store\n";
						return true;
					}
					case Intrinsic::invariant_start:
					case Intrinsic::vaend:
					{
						// Do nothing.
						return true;
					}
					case Intrinsic::cheerp_downcast:
					{
						compileDowncast(&ci);
						return false;
					}
					case Intrinsic::cheerp_downcast_current:
					{
						compileOperand(ci.getOperand(0));
						return false;
					}
					case Intrinsic::cheerp_cast_user:
					{
						if(ci.use_empty())
							return true;

						compileOperand(ci.getOperand(0));
						return false;
					}
					case Intrinsic::flt_rounds:
					{
						// Rounding mode 1: nearest
						stream << "i32.const 1\n";
						return false;
					}
					case Intrinsic::ctlz:
					{
						compileOperand(ci.getOperand(0));
						stream << '\n';
						stream << "i32.clz\n";
						return false;
					}
					default:
					{
						unsigned intrinsic = calledFunc->getIntrinsicID();
						if (intrinsic != Intrinsic::not_intrinsic)
							ci.dump();
						assert(intrinsic == Intrinsic::not_intrinsic);
					}
					break;
				}
			}

			// Calling convention for variadic arguments in Wast mode:
			// arguments are pushed into the stack in the reverse order
			// in which they appear.
			if (fTy->isVarArg())
			{
				size_t n = ci.getNumArgOperands();
				size_t arg_size = fTy->getNumParams();
				size_t i = 0;
				for (auto op = ci.op_begin() + n - 1;
						op != ci.op_begin() + arg_size - 1; op--)
				{
					i++;
					stream << "get_global " << stackTopGlobal << '\n';
					stream << "i32.const 8\n";
					stream << "i32.sub\n";
					// TODO: use 'tee_global' when it's available?
					stream << "set_global " << stackTopGlobal << '\n';
					stream << "get_global " << stackTopGlobal << '\n';
					compileOperand(op->get());
					stream << "\n";
					stream << getTypeString(op->get()->getType()) << ".store\n";
				}
			}

			for (auto op = ci.op_begin();
					op != ci.op_begin() + fTy->getNumParams(); ++op)
			{
				compileOperand(op->get());
				stream << '\n';
			}

			if (calledFunc)
			{
				if (functionIds.count(calledFunc))
				{
					stream << "call " << functionIds[calledFunc];
				}
				else
				{
					// TODO implement ffi calls to the browser side.
					stream << "unreachable ;; unknown call \""
						<< calledFunc->getName() << "\"\n";
					return true;
				}
			}
			else
			{
				if (globalDeps.functionTables().count(fTy))
				{
					const auto& table = globalDeps.functionTables().at(fTy);
					compileOperand(calledValue);
					stream << '\n';
					stream << "call_indirect $vt_" << table.name;
				}
				else
				{
					// TODO implement ffi calls to the browser side.
					stream << "unreachable ;; unknown indirect call\n";
					return true;
				}
			}

			if(ci.getType()->isVoidTy())
			{
				stream << '\n';
				return true;
			}
			break;
		}
		case Instruction::FAdd:
		{
			compileOperand(I.getOperand(0));
			stream << '\n';
			compileOperand(I.getOperand(1));
			stream << '\n';
			stream << getTypeString(I.getType()) << ".add";
			break;
		}
		case Instruction::FCmp:
		{
			const CmpInst& ci = cast<CmpInst>(I);
			compileOperand(ci.getOperand(0));
			stream << '\n';
			compileOperand(ci.getOperand(1));
			stream << '\n';
			stream << getTypeString(ci.getOperand(0)->getType()) << '.';
			switch(ci.getPredicate())
			{
				// TODO: Handle ordered vs unordered
				case CmpInst::FCMP_UEQ:
				case CmpInst::FCMP_OEQ:
					stream << "eq";
					break;
				case CmpInst::FCMP_UNE:
				case CmpInst::FCMP_ONE:
					stream << "ne";
					break;
				case CmpInst::FCMP_ULT:
				case CmpInst::FCMP_OLT:
					stream << "lt";
					break;
				case CmpInst::FCMP_OGT:
				case CmpInst::FCMP_UGT:
					stream << "gt";
					break;
				case CmpInst::FCMP_ULE:
				case CmpInst::FCMP_OLE:
					stream << "le";
					break;
				case CmpInst::FCMP_UGE:
				case CmpInst::FCMP_OGE:
					stream << "ge";
					break;
				default:
					llvm::errs() << "Handle predicate for " << ci << "\n";
					break;
			}
			break;
		}
		case Instruction::FDiv:
		{
			compileOperand(I.getOperand(0));
			stream << '\n';
			compileOperand(I.getOperand(1));
			stream << '\n';
			stream << getTypeString(I.getType()) << ".div";
			break;
		}
		case Instruction::FRem:
		{
			// No FRem in wasm, implement manually
			// frem x, y -> fsub (x, fmul( ftrunc ( fdiv (x, y) ), y ) )
			compileOperand(I.getOperand(0));
			stream << '\n';
			compileOperand(I.getOperand(0));
			stream << '\n';
			compileOperand(I.getOperand(1));
			stream << '\n';
			stream << getTypeString(I.getType()) << ".div\n";
			stream << getTypeString(I.getType()) << ".trunc\n";
			compileOperand(I.getOperand(1));
			stream << '\n';
			stream << getTypeString(I.getType()) << ".mul\n";
			stream << getTypeString(I.getType()) << ".sub";
			break;
		}
		case Instruction::FMul:
		{
			compileOperand(I.getOperand(0));
			stream << '\n';
			compileOperand(I.getOperand(1));
			stream << '\n';
			stream << getTypeString(I.getType()) << ".mul";
			break;
		}
		case Instruction::FSub:
		{
			compileOperand(I.getOperand(0));
			stream << '\n';
			compileOperand(I.getOperand(1));
			stream << '\n';
			stream << getTypeString(I.getType()) << ".sub";
			break;
		}
		case Instruction::GetElementPtr:
		{
			compileGEP(&I);
			break;
		}
		case Instruction::ICmp:
		{
			const CmpInst& ci = cast<CmpInst>(I);
			CmpInst::Predicate p = (CmpInst::Predicate)ci.getPredicate();
			if(ci.getOperand(0)->getType()->isPointerTy())
			{
				compileOperand(ci.getOperand(0));
				stream << '\n';
				compileOperand(ci.getOperand(1));
				stream << '\n';
			}
			else if(CmpInst::isSigned(p))
			{
				compileSignedInteger(ci.getOperand(0), true);
				stream << '\n';
				compileSignedInteger(ci.getOperand(1), true);
				stream << '\n';
			}
			else if (CmpInst::isUnsigned(p) || !I.getOperand(0)->getType()->isIntegerTy(32))
			{
				compileUnsignedInteger(ci.getOperand(0));
				stream << '\n';
				compileUnsignedInteger(ci.getOperand(1));
				stream << '\n';
			}
			else
			{
				compileSignedInteger(ci.getOperand(0), true);
				stream << '\n';
				compileSignedInteger(ci.getOperand(1), true);
				stream << '\n';
			}
			stream << getTypeString(ci.getOperand(0)->getType()) << '.' << getIntegerPredicate(ci.getPredicate());
			break;
		}
		case Instruction::Load:
		{
			const LoadInst& li = cast<LoadInst>(I);
			const Value* ptrOp=li.getPointerOperand();
			// 1) The pointer
			compileOperand(ptrOp);
			stream << '\n';
			// 2) Load
			stream << getTypeString(li.getType()) << ".load";
			if(li.getType()->isIntegerTy())
			{
				uint32_t bitWidth = li.getType()->getIntegerBitWidth();
				if(bitWidth == 1)
					bitWidth = 8;
				if(bitWidth < 32)
				{
					assert(bitWidth == 8 || bitWidth == 16);
					// Currently assume unsigned, like Cheerp. We may optimize this be looking at a following sext or zext instruction.
					stream << bitWidth << "_u";
				}
			}
			break;
		}
		case Instruction::LShr:
		{
			compileOperand(I.getOperand(0));
			stream << '\n';
			compileOperand(I.getOperand(1));
			stream << '\n';
			stream << getTypeString(I.getType()) << ".shr_u";
			break;
		}
		case Instruction::Mul:
		{
			compileOperand(I.getOperand(0));
			stream << '\n';
			compileOperand(I.getOperand(1));
			stream << '\n';
			stream << getTypeString(I.getType()) << ".mul";
			break;
		}
		case Instruction::Or:
		{
			compileOperand(I.getOperand(0));
			stream << '\n';
			compileOperand(I.getOperand(1));
			stream << '\n';
			stream << getTypeString(I.getType()) << ".or";
			break;
		}
		case Instruction::PtrToInt:
		{
			compileOperand(I.getOperand(0));
			break;
		}
		case Instruction::Shl:
		{
			compileOperand(I.getOperand(0));
			stream << '\n';
			compileOperand(I.getOperand(1));
			stream << '\n';
			stream << getTypeString(I.getType()) << ".shl";
			break;
		}
		case Instruction::Store:
		{
			const StoreInst& si = cast<StoreInst>(I);
			const Value* ptrOp=si.getPointerOperand();
			const Value* valOp=si.getValueOperand();
			// 1) The pointer
			compileOperand(ptrOp);
			stream << '\n';
			// 2) The value
			compileOperand(valOp);
			stream << '\n';
			// 3) Store
			stream << getTypeString(valOp->getType()) << ".store";
			// When storing values with size less than 32-bit we need to truncate them
			if(valOp->getType()->isIntegerTy())
			{
				uint32_t bitWidth = valOp->getType()->getIntegerBitWidth();
				if(bitWidth == 1)
					bitWidth = 8;
				if(bitWidth < 32)
				{
					assert(bitWidth == 8 || bitWidth == 16);
					stream << bitWidth;
				}
			}
			stream << '\n';
			break;
		}
		case Instruction::Sub:
		{
			compileOperand(I.getOperand(0));
			stream << '\n';
			compileOperand(I.getOperand(1));
			stream << '\n';
			stream << getTypeString(I.getType()) << ".sub";
			break;
		}
		case Instruction::Switch:
			break;
		case Instruction::Trunc:
		{
			// TODO: We need to mask the value
			compileOperand(I.getOperand(0));
			break;
		}
		case Instruction::Ret:
		{
			const ReturnInst& ri = cast<ReturnInst>(I);
			Value* retVal = ri.getReturnValue();
			if(retVal)
			{
				compileOperand(I.getOperand(0));
				stream << '\n';
			}
			// Restore old stack
			stream << "get_local " << currentFun->arg_size() << "\n";
			stream << "set_global " << stackTopGlobal << '\n';
			stream << "return\n";
			break;
		}
		case Instruction::SDiv:
		case Instruction::UDiv:
		{
			compileOperand(I.getOperand(0));
			stream << '\n';
			compileOperand(I.getOperand(1));
			stream << '\n';
			stream << getTypeString(I.getType()) << ".div_"
				<< (I.getOpcode() == Instruction::SDiv ? 's' : 'u');
			break;
		}
		case Instruction::SRem:
		case Instruction::URem:
		{
			compileOperand(I.getOperand(0));
			stream << '\n';
			compileOperand(I.getOperand(1));
			stream << '\n';
			stream << getTypeString(I.getType()) << ".rem_"
				<< (I.getOpcode() == Instruction::SRem ? 's' : 'u');
			break;
		}
		case Instruction::Select:
		{
			const SelectInst& si = cast<SelectInst>(I);
			compileOperand(si.getTrueValue());
			stream << '\n';
			compileOperand(si.getFalseValue());
			stream << '\n';
			compileOperand(si.getCondition());
			stream << '\n';
			stream << "select";
			break;
		}
		case Instruction::SExt:
		{
			uint32_t bitWidth = I.getOperand(0)->getType()->getIntegerBitWidth();
			compileOperand(I.getOperand(0));
			stream << "\ni32.const " << (32-bitWidth) << '\n';
			stream << "i32.shl\n";
			stream << "i32.const " << (32-bitWidth) << '\n';
			stream << "i32.shr_s";
			break;
		}
		case Instruction::FPToSI:
		{
			compileOperand(I.getOperand(0));
			stream << '\n' << getTypeString(I.getType()) << ".trunc_s/" << getTypeString(I.getOperand(0)->getType());
			break;
		}
		case Instruction::FPToUI:
		{
			compileOperand(I.getOperand(0));
			stream << '\n' << getTypeString(I.getType()) << ".trunc_u/" << getTypeString(I.getOperand(0)->getType());
			break;
		}
		case Instruction::SIToFP:
		{
			compileOperand(I.getOperand(0));
			uint32_t bitWidth = I.getOperand(0)->getType()->getIntegerBitWidth();
			if(bitWidth != 32)
			{
				// Sign extend
				stream << "\ni32.const " << (32-bitWidth) << '\n';
				stream << "i32.shl\n";
				stream << "i32.const " << (32-bitWidth) << '\n';
				stream << "i32.shr_s";
			}
			stream << '\n' << getTypeString(I.getType()) << ".convert_s/" << getTypeString(I.getOperand(0)->getType());
			break;
		}
		case Instruction::UIToFP:
		{
			compileOperand(I.getOperand(0));
			uint32_t bitWidth = I.getOperand(0)->getType()->getIntegerBitWidth();
			if(bitWidth != 32)
			{
				stream << "\ni32.const " << getMaskForBitWidth(bitWidth);
			}
			stream << '\n' << getTypeString(I.getType()) << ".convert_u/" << getTypeString(I.getOperand(0)->getType());
			break;
		}
		case Instruction::FPTrunc:
		{
			assert(I.getType()->isFloatTy());
			assert(I.getOperand(0)->getType()->isDoubleTy());
			compileOperand(I.getOperand(0));
			stream << '\n' << getTypeString(I.getType()) << ".demote/" << getTypeString(I.getOperand(0)->getType());
			break;
		}
		case Instruction::FPExt:
		{
			assert(I.getType()->isDoubleTy());
			assert(I.getOperand(0)->getType()->isFloatTy());
			compileOperand(I.getOperand(0));
			stream << '\n' << getTypeString(I.getType()) << ".promote/" << getTypeString(I.getOperand(0)->getType());
			break;
		}
		case Instruction::Xor:
		{
			compileOperand(I.getOperand(0));
			stream << '\n';
			compileOperand(I.getOperand(1));
			stream << '\n';
			stream << getTypeString(I.getType()) << ".xor";
			break;
		}
		case Instruction::ZExt:
		{
			uint32_t bitWidth = I.getOperand(0)->getType()->getIntegerBitWidth();
			compileOperand(I.getOperand(0));
			stream << "\ni32.const " << getMaskForBitWidth(bitWidth) << '\n';
			stream << "i32.and";
			break;
		}
		case Instruction::Unreachable:
		{
			stream << "unreachable\n";
			break;
		}
		default:
		{
			I.dump();
			llvm::errs() << "\tImplement inst " << I.getOpcodeName() << '\n';
		}
	}
	return false;
}

void CheerpWastWriter::compileBB(const BasicBlock& BB)
{
	BasicBlock::const_iterator I=BB.begin();
	BasicBlock::const_iterator IE=BB.end();
	for(;I!=IE;++I)
	{
		if(isInlineable(*I, PA))
			continue;
		if(I->getOpcode()==Instruction::PHI) //Phys are manually handled
			continue;
		if(const IntrinsicInst* II=dyn_cast<IntrinsicInst>(&(*I)))
		{
			//Skip some kind of intrinsics
			if(II->getIntrinsicID()==Intrinsic::lifetime_start ||
				II->getIntrinsicID()==Intrinsic::lifetime_end ||
				II->getIntrinsicID()==Intrinsic::dbg_declare ||
				II->getIntrinsicID()==Intrinsic::dbg_value)
			{
				continue;
			}
		}

		// Display file and line markers in WAST for debugging purposes
		const llvm::DebugLoc& debugLoc = I->getDebugLoc();
		if (!debugLoc.isUnknown()) {
			MDNode* file = debugLoc.getScope(Ctx);
			assert(file);
			assert(file->getNumOperands()>=2);
			MDNode* fileNamePath = cast<MDNode>(file->getOperand(1));
			assert(fileNamePath->getNumOperands()==2);
			StringRef fileName = cast<MDString>(fileNamePath->getOperand(0))->getString();
			uint32_t currentLine = debugLoc.getLine();
			stream << ";; " << fileName << ":" << currentLine << "\n";
		}

		if(I->isTerminator() || !I->use_empty() || I->mayHaveSideEffects())
		{
			if(!compileInstruction(*I) && !I->getType()->isVoidTy())
			{
				if(I->use_empty())
					stream << "\ndrop\n";
				else
					stream << "\nset_local " << (1 + currentFun->arg_size() + registerize.getRegisterId(I)) << '\n';
			}
		}
	}
}

void CheerpWastWriter::compileMethodLocals(const Function& F, bool needsLabel)
{
	const std::vector<Registerize::RegisterInfo>& regsInfo = registerize.getRegistersForFunction(&F);
	// The first local after ther params stores the previous stack address
	stream << "(local i32";
	// Emit the registers, careful as the registerize id is offset by the number of args
	for(const Registerize::RegisterInfo& regInfo: regsInfo)
	{
		stream << ' ';
		assert(regInfo.regKind != Registerize::OBJECT);
		assert(!regInfo.needsSecondaryName);
		switch(regInfo.regKind)
		{
			case Registerize::DOUBLE:
				stream << "f64";
				break;
			case Registerize::FLOAT:
				stream << "f32";
				break;
			case Registerize::INTEGER:
				stream << "i32";
				break;
			default:
				assert(false);
		}
	}
	// If needed, label is the very last local
	if(needsLabel)
		stream << " i32";
	stream << ")\n";
}

void CheerpWastWriter::compileMethodParams(const Function& F)
{
	uint32_t numArgs = F.arg_size();
	if(numArgs)
	{
		stream << "(param";
		llvm::FunctionType* FTy = F.getFunctionType();
		for(uint32_t i = 0; i < numArgs; i++)
			stream << ' ' << getTypeString(FTy->getParamType(i));
		stream << ')';
	}
}

void CheerpWastWriter::compileMethodResult(const Function& F)
{
	if(!F.getReturnType()->isVoidTy())
		stream << "(result " << getTypeString(F.getReturnType()) << ')';
}

void CheerpWastWriter::compileMethod(const Function& F)
{
	currentFun = &F;
	stream << "(func";
	stream << " $" << F.getName();
	// TODO: We should not export them all
	stream << " (export \"" << NameGenerator::filterLLVMName(F.getName(),NameGenerator::NAME_FILTER_MODE::GLOBAL) << "\")";
	uint32_t numArgs = F.arg_size();
	compileMethodParams(F);
	compileMethodResult(F);
	stream << '\n';
	const llvm::BasicBlock* lastDepth0Block = nullptr;
	if(F.size() == 1)
	{
		compileMethodLocals(F, false);
		// TODO: Only save the stack address if required
		stream << "get_global " << stackTopGlobal << '\n';
		stream << "set_local " << numArgs << "\n";
		compileBB(*F.begin());
		lastDepth0Block = &(*F.begin());
	}
	else
	{
		Relooper* rl = CheerpWriter::runRelooperOnFunction(F);
		compileMethodLocals(F, rl->needsLabel());
		// TODO: Only save the stack address if required
		stream << "get_global " << stackTopGlobal << '\n';
		stream << "set_local " << numArgs << "\n";
		uint32_t numArgs = F.arg_size();
		const std::vector<Registerize::RegisterInfo>& regsInfo = registerize.getRegistersForFunction(&F);
		uint32_t numRegs = regsInfo.size();
		// label is the very last local
		CheerpWastRenderInterface ri(this, 1+numArgs+numRegs);
		rl->Render(&ri);
		lastDepth0Block = ri.lastDepth0Block;
	}
	// A function has to terminate with a return instruction
	if(!lastDepth0Block || !isa<ReturnInst>(lastDepth0Block->getTerminator()))
	{
		// Add a fake return
		if(!F.getReturnType()->isVoidTy())
			stream << getTypeString(F.getReturnType()) << ".const 0\n";
		stream << "return\n";
	}
	stream << ")\n";
}

void CheerpWastWriter::compileImport(const Function& F)
{
	assert(useWastLoader);
	stream << "(func (import \"imports\" \"";
	stream << NameGenerator::filterLLVMName(F.getName(),NameGenerator::NAME_FILTER_MODE::GLOBAL);
	stream << "\")";
	uint32_t numArgs = F.arg_size();
	if(numArgs)
	{
		stream << "(param";
		llvm::FunctionType* FTy = F.getFunctionType();
		for(uint32_t i = 0; i < numArgs; i++)
			stream << ' ' << getTypeString(FTy->getParamType(i));
		stream << ')';
	}
	if(!F.getReturnType()->isVoidTy())
		stream << "(result " << getTypeString(F.getReturnType()) << ')';
	stream << ")\n";
}

void CheerpWastWriter::compileDataSection()
{
	for ( const GlobalVariable & GV : module.getGlobalList() )
	{
		if (GV.getSection() != StringRef("asmjs"))
			continue;
		if (GV.hasInitializer())
		{
			const Constant* init = GV.getInitializer();
			Type* ty = init->getType();
			// If the initializer is a function, skip it
			if (ty->isPointerTy() && ty->getPointerElementType()->isFunctionTy())
				continue;
			// The offset into memory, which is the address
			stream << "(data (i32.const " << linearHelper.getGlobalVariableAddress(&GV) << ") \"";
			WastBytesWriter bytesWriter(stream, functionTableOffsets);
			linearHelper.compileConstantAsBytes(init,/* asmjs */ true, &bytesWriter);
			stream << "\")\n";
		}
	}
}

void CheerpWastWriter::makeWast()
{
	// First run, assign required Ids to functions and globals
	if (useWastLoader) {
		for ( const Function * F : globalDeps.asmJSImports() )
		{
			functionIds.insert(std::make_pair(F, functionIds.size()));
		}
	}
	for ( const Function & F : module.getFunctionList() )
	{
		if (!F.empty() && F.getSection() == StringRef("asmjs"))
		{
			functionIds.insert(std::make_pair(&F, functionIds.size()));
		}
	}

	// Emit S-expressions for the module
	stream << "(module\n";

	// Second run, actually compile the code (imports needs to be before everything)
	if (useWastLoader) {
		for ( const Function * F : globalDeps.asmJSImports() )
		{
			compileImport(*F);
		}
	}

	// Define function type variables
	for (const auto& table : globalDeps.functionTables())
	{
		stream << "(type " << "$vt_" << table.second.name << " (func ";
		const llvm::Function& F = *table.second.functions[0];
		compileMethodParams(F);
		compileMethodResult(F);
		stream << "))\n";
	}

	// Define 'table' with functions
	if (!globalDeps.functionTables().empty())
		stream << "(table anyfunc (elem";

	uint32_t functionTableOffset = 0;
	for (const auto& table : globalDeps.functionTables()) {
		for (const auto& F : table.second.functions) {
			stream << " $" << F->getName();
		}
		functionTableOffsets.insert(std::make_pair(StringRef(table.second.name),
												   functionTableOffset));
		functionTableOffset += table.second.functions.size();
	}

	if (!globalDeps.functionTables().empty())
		stream << "))\n";

	// Define the memory for the module (these should be parameter, they are min and max in WasmPage units)
	uint32_t minMemory = 1;
	uint32_t maxMemory = 2;
	stream << "(memory (export \"memory\") " << minMemory << ' ' << maxMemory << ")\n";

	// Assign globals in the module, these are used for codegen they are not part of the user program
	stackTopGlobal = usedGlobals++;
	// Start the stack from the end of default memory
	stream << "(global (mut i32) (i32.const " << (minMemory*WasmPage) << "))\n";

	// Experimental entry point for wast code
	llvm::Function* wastStart = module.getFunction("_Z9wastStartv");
	if(wastStart && globalDeps.constructors().empty())
	{
		assert(functionIds.count(wastStart));
		stream << "(start " << functionIds[wastStart] << ")\n";
	}
	else if (!globalDeps.constructors().empty() && !useWastLoader)
	{
		stream << "(start " << functionIds.size() << ")\n";
	}

	for ( const GlobalVariable & GV : module.getGlobalList() )
	{
		if (GV.getSection() != StringRef("asmjs"))
			continue;
		linearHelper.addGlobalVariable(&GV);
	}

	// Second run, actually compile the code
	for ( const Function & F : module.getFunctionList() )
	{
		if (!F.empty() && F.getSection() == StringRef("asmjs"))
		{
			compileMethod(F);
		}
	}

	// Construct an anonymous function that calls the global constructors.
	if (!globalDeps.constructors().empty() && !useWastLoader)
	{
		stream << "(func\n";
		for (const Function* F : globalDeps.constructors())
		{
			if (F->getSection() == StringRef("asmjs"))
				stream << "call " << functionIds.at(F) << "\n";
		}

		if (wastStart)
			stream << "call " << functionIds.at(wastStart) << "\n";

		stream << ")\n";
	}

	compileDataSection();
	
	stream << ')';
}

void CheerpWastWriter::WastBytesWriter::addByte(uint8_t byte)
{
	char buf[4];
	snprintf(buf, 4, "\\%02x", byte);
	stream << buf;
}

uint32_t CheerpWastWriter::WastBytesWriter::getFunctionTableOffset(llvm::StringRef tableName)
{
	return functionTableOffsets.at(tableName);
}

void CheerpWastWriter::WastGepWriter::addValue(const llvm::Value* v, uint32_t size)
{
	writer.compileOperand(v);
	writer.stream << '\n';
	if(size != 1)
	{
		writer.stream << "i32.const " << size << '\n';
		writer.stream << "i32.mul\n";
	}
	if(!first)
		writer.stream << "i32.add\n";
	first = false;
}

void CheerpWastWriter::WastGepWriter::addConst(uint32_t v)
{
	assert(v);
	writer.stream << "i32.const " << v << '\n';
	if(!first)
		writer.stream << "i32.add\n";
	first = false;
}
