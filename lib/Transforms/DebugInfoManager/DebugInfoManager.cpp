#include "llvm/Pass.h"
#include "llvm/Module.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/PassManager.h"
#include "llvm/Instructions.h"
#include "llvm/DebugInfo.h"
#include <llvm/Constants.h>
#include <llvm/InlineAsm.h>
#include <llvm/Intrinsics.h>

#include <sstream>
#include <iostream>
#include "DebugInfoManager.h"

using namespace llvm;
using namespace std;

static cl::opt<string> TargetFileName("target-file-name",
       cl::desc("The file name that contains the target instruction"),
       cl::init(""));
static cl::opt<string> TargetFunctionName("target-function-name",
       cl::desc("The function where the instruction lies"),
       cl::init(""));
static cl::opt<string> TargetLineNumber("target-line-number",
       cl::desc("The line numbers of the target instructions"),
       cl::init(""));
static cl::opt<bool> Debug("debug-debug-info-manager",
       cl::desc("Print debugging statements for debug info manager"),
       cl::init(false));
static cl::opt<bool> FindMultBBCode("find-mult-bb",
       cl::desc("Find and count the lines of source code that have multi-basic-block LLVM implementations"),
       cl::init(false));


DebugInfoManager::DebugInfoManager() : ModulePass(ID) {
  if (FindMultBBCode)
    ;
  else if (TargetLineNumber == "" || TargetFileName == "" || TargetFunctionName == "") {
    errs() << "\nYou need to provide the file name, function name, and "
           << "the line number to retrieve the LLVM IR instruction!!!" << "\n\n";
              exit(1);
  }
}


const char* DebugInfoManager::getPassName() const {
  return "DebugInfoLocator";
}


void DebugInfoManager::getAnalysisUsage(AnalysisUsage &au) const {
  // We don't modify the program and preserve all the passes
  au.setPreservesAll();
}


void DebugInfoManager::printDebugInfo(Instruction& instr) {
  MDNode *N = instr.getMetadata("dbg");
  DILocation Loc(N);
  unsigned lineNumber = Loc.getLineNumber();
  StringRef fileName = Loc.getFilename();
  StringRef directory = Loc.getDirectory();
  errs() << "\t\t" << directory << "/" << fileName<< " : " << lineNumber << "\n ";
}

set<string>& DebugInfoManager::split(const string &s, char delim, 
                                        set<string> &elems) {
  stringstream ss(s);
  string item;
  while (getline(ss, item, delim)) {
    elems.insert(item);
  }
  return elems;
}


set<string> DebugInfoManager::split(const string &s, char delim) {
  set<string> elems;
  split(s, delim, elems);
  return elems;
}

// TODO: We should cache the results once they are computed for a given binary.
bool DebugInfoManager::runOnModule(Module& m) {
  for (Module::iterator fi = m.begin(), fe = m.end(); fi != fe; ++fi) {
    if (Debug)
      errs() << "Function: " << fi->getName() << "\n";
    
    // Process line numbers and put them in a set
    set<string> strLineNumbers = split(TargetLineNumber, ':');
    set<int> intLineNumbers;
    for(auto str : strLineNumbers) {
      intLineNumbers.insert(std::stoi(str));
    }   

    for (Function::iterator bi = fi->begin(), be = fi->end(); bi != be; ++bi) {
      // TODO: Improve this comparison by getting mangled names from the elf debug information
      if(fi->getName().find(StringRef(TargetFunctionName)) != StringRef::npos)
        for (BasicBlock::iterator ii = bi->begin(), ie = bi->end(); ii != ie; ++ii) { 
          if (MDNode *N = ii->getMetadata("dbg")) {
            DILocation Loc(N);
            unsigned lineNumber = Loc.getLineNumber();
            if(intLineNumbers.find(lineNumber) != intLineNumbers.end()) {
              StringRef fileName = Loc.getFilename();
              // Currently only use calls and loads as the potential target instructions
              if ((fileName.find(StringRef(TargetFileName))) != StringRef::npos) {
                errs() << *ii << "\n";
                if (isa<LoadInst>(*ii)) {
                  targetInstructions.push_back(&(*ii));
                  targetFunctions.push_back(&(*fi));
                  targetOperands.push_back(ii->getOperand(0));                  
                } else if (CallInst* CI = dyn_cast<CallInst>(&(*ii))) {
                  Function* f = dyn_cast<Function>(CI->getOperand(0));
                  Value* v = NULL;
                  if (f && f->getIntrinsicID() == Intrinsic::not_intrinsic)
                    v = CI->getOperand(0);
                  else if (MDNode* node = dyn_cast<MDNode>(CI->getOperand(0))) {
                    v = node->getOperand(0);
                    if (TargetFileName != "htscore.c")
                      errs() << "Not httrack, this is new!" << "\n";
                  }
                  
                  if(v) {
                    targetInstructions.push_back(&(*ii));
                    targetFunctions.push_back(&(*fi));
                    targetOperands.push_back(v);
                  }
                }
              }
            }
          }
        }
     }
   }
  return false;
}

char DebugInfoManager::ID = 0;

static RegisterPass<DebugInfoManager> X("debug-info-manager", "Debug information locator",
                                         true /* Only looks at CFG */,
                                         true /* Analysis Pass */);
