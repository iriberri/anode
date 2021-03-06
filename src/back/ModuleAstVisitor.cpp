
#include "back/compile.h"
#include "front/ast.h"
#include "emit.h"
#include "CompileContext.h"
#include "CompileAstVisitor.h"
#include "CreateStructsAstVisitor.h"
#include "llvm.h"
#include "GlobalVariableAstVisitor.h"


using namespace anode::ast;

/**
 * The best way I have found to figure out what instructions to use with LLVM is to
 * write some c++ that does the equivalent of what want to do and compile it here: http://ellcc.org/demo/index.cgi
 * then examine the IR that's generated.
 */

namespace anode { namespace back {

void createLlvmStructsForClasses(ast::Module *anodeModule, CompileContext &cc) {
    CreateStructsAstVisitor visitor{cc};
    anodeModule->accept(&visitor);
}

class ModuleAstVisitor : public CompileAstVisitor {
    llvm::TargetMachine &targetMachine_;

    llvm::Function *initFunc_ = nullptr;
    int resultExprStmtCount_ = 0;
    llvm::Function *resultFunc_ = nullptr;
    llvm::Value *executionContextPtrValue_ = nullptr;
    scope::SymbolTable *globalScope_ = nullptr;

public:
    ModuleAstVisitor(CompileContext &cc, llvm::TargetMachine &targetMachine)
        : CompileAstVisitor{cc},
          targetMachine_{targetMachine} {}

    bool visitingClassDefinition(ClassDefinition *) override {
        return false;
    }

    bool visitingFuncDefStmt(FuncDefStmt *) override {
        return false;
    }

    bool visitingExprStmt(ExprStmt *expr) override {

        // Skip globally scoped CompoundExpr and allow it's children to be visited
        auto maybeCompoundExpr = dynamic_cast<ast::CompoundExpr*>(expr);
        if(maybeCompoundExpr) {
            if(maybeCompoundExpr->scope() == globalScope_)
                return true;
        }

        llvm::Value *llvmValue = emitExpr(expr, cc());

        if (!expr->type()->isPrimitive()) {
            //eventually, we'll call toString() or somesuch.
            return false;
        }

        if (llvmValue && !llvmValue->getType()->isVoidTy()) {
            resultExprStmtCount_++;
            std::string variableName = string::format("result_%d", resultExprStmtCount_);
            llvm::AllocaInst *resultVar = cc().irBuilder().CreateAlloca(llvmValue->getType(), nullptr, variableName);

            cc().irBuilder().CreateStore(llvmValue, resultVar);

            std::string bitcastVariableName = string::format("bitcasted_%d", resultExprStmtCount_);
            auto bitcasted = cc().irBuilder().CreateBitCast(resultVar, llvm::Type::getInt64PtrTy(cc().llvmContext()));

            std::vector<llvm::Value *> args{
                //ExecutionContext pointer
                executionContextPtrValue_,
                //PrimitiveType,
                llvm::ConstantInt::get(cc().llvmContext(), llvm::APInt(32, (uint64_t) expr->type()->primitiveType(), true)),
                //Pointer to value.
                bitcasted
            };
            cc().irBuilder().CreateCall(resultFunc_, args);
        }

        return false;
    }

    bool visitingModule(Module *module) override {
        globalScope_ = module->scope();

        cc().llvmModule().setDataLayout(targetMachine_.createDataLayout());

        declareResultFunction();

        startModuleInitFunc(module);

        declareExecutionContextGlobal();

        createLlvmStructsForClasses(module, cc());

        emitGlobals(module, cc());

        emitFuncDefs(module, cc());

        return true;
    }

    void visitedModule(Module *) override {
        cc().irBuilder().CreateRetVoid();
        llvm::raw_ostream &os = llvm::errs();
        if (llvm::verifyModule(cc().llvmModule(), &os)) {
            std::cerr << "Module dump: \n";
            std::cerr.flush();
#ifdef ANODE_DEBUG
            cc().llvmModule().dump();
#endif
            ASSERT_FAIL("Failed LLVM module verification.");
        }
    }

private:
    void startModuleInitFunc(const Module *module) {
        auto initFuncRetType = llvm::Type::getVoidTy(cc().llvmContext());
        initFunc_ = llvm::cast<llvm::Function>(
            cc().llvmModule().getOrInsertFunction(module->name() + MODULE_INIT_SUFFIX, initFuncRetType));

        initFunc_->setCallingConv(llvm::CallingConv::C);
        auto initFuncBlock = llvm::BasicBlock::Create(cc().llvmContext(), "begin", initFunc_);

        cc().irBuilder().SetInsertPoint(initFuncBlock);
    }

    void declareResultFunction() {
        resultFunc_ = llvm::cast<llvm::Function>(cc().llvmModule().getOrInsertFunction(
            RECEIVE_RESULT_FUNC_NAME,
            llvm::Type::getVoidTy(cc().llvmContext()),        //Return type
            llvm::Type::getInt64PtrTy(cc().llvmContext()),    //Pointer to ExecutionContext
            llvm::Type::getInt32Ty(cc().llvmContext()),       //type::PrimitiveType
            llvm::Type::getInt64PtrTy(cc().llvmContext())));  //Pointer to value


        auto paramItr = resultFunc_->arg_begin();
        llvm::Value *executionContext = paramItr++;
        executionContext->setName("executionContext");

        llvm::Value *primitiveType = paramItr++;
        primitiveType->setName("primitiveType");

        llvm::Value *valuePtr = paramItr;
        valuePtr->setName("valuePtr");
    }

    void declareExecutionContextGlobal() {
        cc().llvmModule().getOrInsertGlobal(EXECUTION_CONTEXT_GLOBAL_NAME, llvm::Type::getInt64Ty(cc().llvmContext()));
        llvm::GlobalVariable *globalVar = cc().llvmModule().getNamedGlobal(EXECUTION_CONTEXT_GLOBAL_NAME);
        globalVar->setLinkage(llvm::GlobalValue::ExternalLinkage);
        globalVar->setAlignment(ALIGNMENT);

        executionContextPtrValue_ = globalVar;
    }
};

std::unique_ptr<llvm::Module> emitModule(
    anode::ast::Module *module,
    anode::back::TypeMap &typeMap,
    llvm::LLVMContext &llvmContext,
    llvm::TargetMachine *targetMachine
) {

    std::unique_ptr<llvm::Module> llvmModule = std::make_unique<llvm::Module>(module->name(), llvmContext);
    llvm::IRBuilder<> irBuilder{llvmContext};

    CompileContext cc{llvmContext, *llvmModule.get(), irBuilder, typeMap};
    ModuleAstVisitor visitor{cc, *targetMachine};

    module->accept(&visitor);
    return llvmModule;
}

}}
