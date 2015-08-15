/*
 * S2E Selective Symbolic Execution Framework
 *
 * Copyright (c) 2015, Dependable Systems Laboratory, EPFL
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Dependable Systems Laboratory, EPFL nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE DEPENDABLE SYSTEMS LABORATORY, EPFL BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Currently maintained by:
 *    Stefan Bucur <stefan.bucur@epfl.ch>
 *
 * All contributors are listed in the S2E-AUTHORS file.
 */

#include "llvm/Pass.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/TypeBuilder.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopIterator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/ADT/PostOrderIterator.h"

#include "llvm/PassManager.h"

#include "llvm/Transforms/IPO/PassManagerBuilder.h"

using namespace llvm;


static cl::opt<bool>
BBTraceMerging("bb-trace-merging",
        cl::desc("Add prologue+epilogue for state merging."),
        cl::init(true));

static cl::opt<bool>
BBTraceLoopNesting("bb-trace-loop-nesting",
        cl::desc("Instrument loop nesting by adjusting BB topological indices"),
        cl::init(false));

namespace llvm {
void initializeBasicBlockTracePass(PassRegistry&);
}


namespace {

class BasicBlockTrace : public FunctionPass {
public:
    static char ID;

    BasicBlockTrace() : FunctionPass(ID) {
        initializeBasicBlockTracePass(*PassRegistry::getPassRegistry());
    }

    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
        AU.addRequired<LoopInfo>();
        AU.addPreserved<LoopInfo>();
        // Is the previous statement necessary if we don't change the CFG?
        AU.setPreservesCFG();
    }

    virtual bool runOnFunction(Function &F);
};

}

char BasicBlockTrace::ID = 0;

INITIALIZE_PASS_BEGIN(BasicBlockTrace, "bb-trace", "Basic block tracing",
        false, false)
INITIALIZE_PASS_DEPENDENCY(LoopInfo)
INITIALIZE_PASS_END(BasicBlockTrace, "bb-trace", "Basic block tracing",
        false, false)


////////////////////////////////////////////////////////////////////////////////

#define BBID_WIDTH         12
#define BBID_OFFSET         0
#define BBID_MASK          ((1 << BBID_WIDTH) - 1)

#define LOOPID_WIDTH        8
#define LOOPID_OFFSET      12
#define LOOPID_MASK        ((1 << LOOPID_WIDTH) - 1)

#define LOOPDEPTH_WIDTH     3
#define LOOPDEPTH_OFFSET   20
#define LOOPDEPTH_MASK     ((1 << LOOPDEPTH_WIDTH) - 1)

#define LOOPHEADER_OFFSET  23

static void getBBAsmCode(unsigned BBID, unsigned LoopID, unsigned LoopDepth,
        bool IsHeader, std::string &AsmCode) {
    AsmCode.clear();
    llvm::raw_string_ostream OS(AsmCode);

    assert(BBID < (1 << BBID_WIDTH));
    assert(LoopID < (1 << LOOPID_WIDTH));
    assert(LoopDepth < (1 << LOOPDEPTH_WIDTH));

    uint32_t BBDescriptor = ((BBID & BBID_MASK) << BBID_OFFSET) |
            ((LoopID & LOOPID_MASK) << LOOPID_OFFSET) |
            ((LoopDepth & LOOPDEPTH_MASK) << LOOPDEPTH_OFFSET) |
            ((IsHeader & 0x1) << LOOPHEADER_OFFSET);

    if (BBTraceMerging) {
        OS << "pusha\n"
              "xor %eax,%eax\n"
              "xor %ebx,%ebx\n"
              "xor %ecx,%ecx\n"
              "xor %edx,%edx\n"
              "xor %esi,%esi\n"
              "xor %edi,%edi\n"
              "xor %ebp,%ebp\n";

    // Flush the TB and force concrete mode -- for state merging.
    // We use explicit machine code instead of the jmp
    // instruction mnemonic, as sometimes the assembler
    // generates the wrong jump type, which invalidates the
    // 0x02 displacement.
        OS << ".byte 0xEB, 0x00" << '\n'; // jmp .+0x02
    }

    // Start of S2E overloaded NOP instruction
    OS << ".byte 0x0F, 0x1F\n"
          ".byte 0x84, 0x42\n";

    // The loop ID
    OS << llvm::format(".byte 0x%02X", BBDescriptor & 0xFF) << '\n';
    // The basic block S2E opcode
    OS << ".byte 0xBB\n";
    // The basic block number
    OS << llvm::format(".byte 0x%02X, 0x%02X",
            (BBDescriptor >> 8) & 0xFF,
            (BBDescriptor >> 16) & 0xFF) << '\n';

    if (BBTraceMerging) {
        OS << ".byte 0xEB, 0x00" << '\n'; // jmp .0x02
        OS << "popa\n";
    }
}


static void instrumentBB(BasicBlock *BB, std::string &AsmCode) {
    InlineAsm *AsmFunction = InlineAsm::get(
            FunctionType::get(Type::getVoidTy(BB->getContext()), false),
            AsmCode, "~{cc},~{dirflag},~{fpsr},~{flags}", true);

    CallInst::Create(AsmFunction, "", BB->getFirstInsertionPt());
}


static void traverseLoop(Loop *L, DenseMap<Loop*, int> &LoopIDs,
        int &LoopIDCounter) {
    LoopIDs.insert(std::make_pair(L, LoopIDCounter++));
    for (Loop::iterator I = L->begin(), E = L->end(); I != E; ++I) {
        traverseLoop(*I, LoopIDs, LoopIDCounter);
    }
}


static void instrumentLoop(Loop *L, DenseMap<BasicBlock*, int> &BBIDs,
        int &BBIDCounter, int &LoopIDCounter, LoopInfo *LI) {
    int LoopID = LoopIDCounter++;
    for (Loop::iterator I = L->begin(), E = L->end(); I != E; ++I) {
        instrumentLoop(*I, BBIDs, BBIDCounter, LoopIDCounter, LI);
    }

    LoopBlocksDFS DFS(L);
    DFS.perform(LI);

    for (LoopBlocksDFS::RPOIterator BI = DFS.beginRPO(), BE = DFS.endRPO();
            BI != BE; ++BI) {
        BasicBlock *BB = *BI;

        if (BBIDs.count(BB) > 0) {
            continue;
        }

        int BBID = BBIDCounter++;
        BBIDs.insert(std::make_pair(BB, BBID));

        std::string AsmCode;
        getBBAsmCode(BBID, LoopID, L->getLoopDepth(),
                L->getHeader() == BB, AsmCode);
        instrumentBB(BB, AsmCode);
    }
}


static void instrumentFunction(Function &F, LoopInfo &LI) {
    DenseMap<Loop*, int> LoopIDs;
    int BBIDCounter = 1;
    int LoopIDCounter = 1;

    for (LoopInfo::iterator I = LI.begin(), E = LI.end(); I != E; ++I) {
        traverseLoop(*I, LoopIDs, LoopIDCounter);
    }

    ReversePostOrderTraversal<Function*> RPOT(&F);
    for (ReversePostOrderTraversal<Function*>::rpo_iterator I = RPOT.begin(),
            E = RPOT.end(); I != E; ++I) {
        BasicBlock *BB = *I;
        Loop *L = LI.getLoopFor(BB);
        std::string AsmCode;

        if (LoopIDs.count(L) > 0) {
            getBBAsmCode(BBIDCounter++, LoopIDs[L], L->getLoopDepth(),
                    BB == L->getHeader(), AsmCode);
        } else {
            getBBAsmCode(BBIDCounter++, 0, 0, false, AsmCode);
        }
        instrumentBB(BB, AsmCode);
    }
}

static void instrumentLoopsInFunction(Function &F, LoopInfo &LI) {
    DenseMap<BasicBlock*, int> BBIDs;
    int BBIDCounter = 1;
    int LoopIDCounter = 1;

    // XXX: This is not the most efficient, as the DFS scan is performed once
    // for each loop.
    for (LoopInfo::iterator I = LI.begin(), E = LI.end(); I != E; ++I) {
        instrumentLoop(*I, BBIDs, BBIDCounter, LoopIDCounter, &LI);
    }

    ReversePostOrderTraversal<Function*> RPOT(&F);
    for (ReversePostOrderTraversal<Function*>::rpo_iterator I = RPOT.begin(),
            E = RPOT.end(); I != E; ++I) {
        BasicBlock *BB = *I;

        if (BBIDs.count(BB) > 0) {
            continue;
        }

        int BBID = BBIDCounter++;
        BBIDs.insert(std::make_pair(BB, BBID));

        std::string AsmCode;
        getBBAsmCode(BBID, 0, 0, false, AsmCode);
        instrumentBB(BB, AsmCode);
    }
}


// BasicBlockTrace /////////////////////////////////////////////////////////////


bool BasicBlockTrace::runOnFunction(Function &F) {
    LoopInfo &LI = getAnalysis<LoopInfo>();

    if (BBTraceLoopNesting) {
        instrumentLoopsInFunction(F, LI);
    } else {
        instrumentFunction(F, LI);
    }

    return true;
}


// Automatically enable the pass.
// http://adriansampson.net/blog/clangpass.html
static void registerBasicBlockTracePass(const PassManagerBuilder &,
                         legacy::PassManagerBase &PM) {
  PM.add(new BasicBlockTrace());
}

static RegisterStandardPasses
  RegisterLast(PassManagerBuilder::EP_OptimizerLast,
                 registerBasicBlockTracePass);

static RegisterStandardPasses
  RegisterOptLevel0(PassManagerBuilder::EP_EnabledOnOptLevel0,
                 registerBasicBlockTracePass);
