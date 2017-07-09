#include "execute.h"
#include "compile.h"

#include <map>
#include <stack>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Mangler.h"

#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/DynamicLibrary.h"

#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/RuntimeDyld.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"

#include "llvm/IR/LegacyPassManager.h"

#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/IRTransformLayer.h"
#include "llvm/ExecutionEngine/Orc/LambdaResolver.h"
#include "llvm/ExecutionEngine/Orc/IndirectionUtils.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"

#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"

#pragma GCC diagnostic pop

namespace lwnn {
    namespace execute {

        //The llvm::orc::createResolver(...) version of this doesn't seem to work for some reason...
        template<typename DylibLookupFtorT, typename ExternalLookupFtorT>
        std::unique_ptr<llvm::orc::LambdaResolver<DylibLookupFtorT, ExternalLookupFtorT>>
        createLambdaResolver2(DylibLookupFtorT DylibLookupFtor, ExternalLookupFtorT ExternalLookupFtor) {
            typedef llvm::orc::LambdaResolver<DylibLookupFtorT, ExternalLookupFtorT> LR;
            return std::make_unique<LR>(DylibLookupFtor, ExternalLookupFtor);
        }

        std::unique_ptr<llvm::Module> irgenAndTakeOwnership(ast::Function &FnAST, const std::string &Suffix);

        /** This class originally taken from:
         * https://github.com/llvm-mirror/llvm/tree/master/examples/Kaleidoscope/BuildingAJIT/Chapter4
         * http://llvm.org/docs/tutorial/BuildingAJIT4.html
         */
        class SimpleJIT {
        private:
            std::unique_ptr<llvm::TargetMachine> TM;
            const llvm::DataLayout DL;
            llvm::orc::RTDyldObjectLinkingLayer ObjectLayer;
            llvm::orc::IRCompileLayer<decltype(ObjectLayer), llvm::orc::SimpleCompiler> CompileLayer;

            using OptimizeFunction =
            std::function<std::shared_ptr<llvm::Module>(std::shared_ptr<llvm::Module>)>;

            llvm::orc::IRTransformLayer<decltype(CompileLayer), OptimizeFunction> OptimizeLayer;

            std::unique_ptr<llvm::orc::JITCompileCallbackManager> CompileCallbackMgr;
            std::unique_ptr<llvm::orc::IndirectStubsManager> IndirectStubsMgr;

        public:
            using ModuleHandle = decltype(OptimizeLayer)::ModuleHandleT;

            SimpleJIT()
                : TM(llvm::EngineBuilder().selectTarget()),
                  DL(TM->createDataLayout()),
                  ObjectLayer([]() { return std::make_shared<llvm::SectionMemoryManager>(); }),
                  CompileLayer(ObjectLayer, llvm::orc::SimpleCompiler(*TM)),
                  OptimizeLayer(CompileLayer,
                                [this](std::shared_ptr<llvm::Module> M) {
                                    return optimizeModule(std::move(M));
                                }),
                  CompileCallbackMgr(
                      llvm::orc::createLocalCompileCallbackManager(TM->getTargetTriple(), 0)) {
                auto IndirectStubsMgrBuilder =
                    llvm::orc::createLocalIndirectStubsManagerBuilder(TM->getTargetTriple());
                IndirectStubsMgr = IndirectStubsMgrBuilder();
                llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr);
            }

            llvm::TargetMachine *getTargetMachine() { return TM.get(); }

            ModuleHandle addModule(std::unique_ptr<llvm::Module> M) {
                // Build our symbol resolver:
                // Lambda 1: Look back into the JIT itself to find symbols that are part of
                //           the same "logical dylib".
                // Lambda 2: Search for external symbols in the host process.
                auto Resolver = createLambdaResolver2(
                    [&](const std::string &Name) {
                        if (auto Sym = IndirectStubsMgr->findStub(Name, false))
                            return Sym;
                        if (auto Sym = OptimizeLayer.findSymbol(Name, false))
                            return Sym;
                        return llvm::JITSymbol(nullptr);
                    },
                    [](const std::string &Name) {
                        if (auto SymAddr =
                            llvm::RTDyldMemoryManager::getSymbolAddressInProcess(Name))
                            return llvm::JITSymbol(SymAddr, llvm::JITSymbolFlags::Exported);
                        return llvm::JITSymbol(nullptr);
                    });

                // Add the set to the JIT with the resolver we created above and a newly
                // created SectionMemoryManager.
                return cantFail(OptimizeLayer.addModule(std::move(M),
                                                        std::move(Resolver)));
            }

            llvm::Error addFunctionAST(std::unique_ptr<ast::Function> FnAST) {
                // Create a CompileCallback - this is the re-entry point into the compiler
                // for functions that haven't been compiled yet.
                auto CCInfo = CompileCallbackMgr->getCompileCallback();

                // Create an indirect stub. This serves as the functions "canonical
                // definition" - an unchanging (constant address) entry point to the
                // function implementation.
                // Initially we point the stub's function-pointer at the compile callback
                // that we just created. In the compile action for the callback (see below)
                // we will update the stub's function pointer to point at the function
                // implementation that we just implemented.
                if (auto Err = IndirectStubsMgr->createStub(mangle(FnAST->name()),
                                                            CCInfo.getAddress(),
                                                            llvm::JITSymbolFlags::Exported))
                    return Err;

                // Move ownership of FnAST to a shared pointer - C++11 lambdas don't support
                // capture-by-move, which is be required for unique_ptr.
                auto SharedFnAST = std::shared_ptr<ast::Function>(std::move(FnAST));

                // Set the action to compile our AST. This lambda will be run if/when
                // execution hits the compile callback (via the stub).
                //
                // The steps to compile are:
                // (1) IRGen the function.
                // (2) Add the IR module to the JIT to make it executable like any other
                //     module.
                // (3) Use findSymbol to get the address of the compiled function.
                // (4) Update the stub pointer to point at the implementation so that
                ///    subsequent calls go directly to it and bypass the compiler.
                // (5) Return the address of the implementation: this lambda will actually
                //     be run inside an attempted call to the function, and we need to
                //     continue on to the implementation to complete the attempted call.
                //     The JIT runtime (the resolver block) will use the return address of
                //     this function as the address to continue at once it has reset the
                //     CPU state to what it was immediately before the call.
                CCInfo.setCompileAction(
                    [this, SharedFnAST]() {
                        auto M = irgenAndTakeOwnership(*SharedFnAST, "$impl");
                        addModule(std::move(M));
                        auto Sym = findSymbol(SharedFnAST->name() + "$impl");
                        assert(Sym && "Couldn't find compiled function?");
                        llvm::JITTargetAddress SymAddr = cantFail(Sym.getAddress());
                        if (auto Err =
                            IndirectStubsMgr->updatePointer(mangle(SharedFnAST->name()),
                                                            SymAddr)) {
                            logAllUnhandledErrors(std::move(Err), llvm::errs(),
                                                  "Error updating function pointer: ");
                            exit(1);
                        }

                        return SymAddr;
                    });

                return llvm::Error::success();
            }

            llvm::JITSymbol findSymbol(const std::string Name) {
                return OptimizeLayer.findSymbol(mangle(Name), true);
            }

            void removeModule(ModuleHandle H) {
                cantFail(OptimizeLayer.removeModule(H));
            }

        private:
            std::string mangle(const std::string &Name) {
                std::string MangledName;
                llvm::raw_string_ostream MangledNameStream(MangledName);
                llvm::Mangler::getNameWithPrefix(MangledNameStream, Name, DL);
                return MangledNameStream.str();
            }

            std::shared_ptr<llvm::Module> optimizeModule(std::shared_ptr<llvm::Module> M) {
                // Create a function pass manager.
                auto FPM = llvm::make_unique<llvm::legacy::FunctionPassManager>(M.get());

                // Add some optimizations.
                FPM->add(llvm::createInstructionCombiningPass());
                FPM->add(llvm::createReassociatePass());
                FPM->add(llvm::createGVNPass());
                FPM->add(llvm::createCFGSimplificationPass());
                FPM->doInitialization();

                // Run the optimizations over all functions in the module being added to
                // the JIT.
                for (auto &F : *M)
                    FPM->run(F);

                return M;
            }
        }; //SimpleJIT

        class ExecutionContextImpl : public ExecutionContext {
            //TODO:  determine if definition order (destruction order) is still significant, and if so
            //update this note to say so.
            llvm::LLVMContext context_;
            std::unique_ptr<SimpleJIT> jit_ = std::make_unique<SimpleJIT>();

        public:

            uint64_t getSymbolAddress(const std::string &name) override {
                llvm::JITSymbol symbol = jit_->findSymbol(name);

                if (!symbol)
                    return 0;

                uint64_t retval = cantFail(symbol.getAddress());
                return retval;
            }

            void addModule(const ast::Module *module) override {
                std::unique_ptr<llvm::Module> llvmModule = lwnn::compile::generateCode(module, &context_,
                                                                                       jit_->getTargetMachine());

                llvmModule->print(llvm::outs(), nullptr);

                jit_->addModule(move(llvmModule));
            }

            //TODO:  addFunction(std::unique_ptr<ast::Function> func);
            //TODO:  executeExpr(ast::Expr *expr);
            //TODO:  addGlobalVariable(std::string name, int size); (or somesuch)
        }; //ExecutionContextImpl

        std::unique_ptr<ExecutionContext> createExecutionContext() {
            return std::make_unique<ExecutionContextImpl>();
        }

        void initializeJitCompiler() {
            llvm::InitializeNativeTarget();
            llvm::InitializeNativeTargetAsmPrinter();
            llvm::InitializeNativeTargetAsmParser();
        }
    } //namespace execute
} //namespace float
