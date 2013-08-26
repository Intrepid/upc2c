#include <clang/Tooling/Tooling.h>
#include <clang/Sema/SemaConsumer.h>
#include <clang/Sema/Scope.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <llvm/Support/raw_ostream.h>
#include <string>
#include "../../lib/Sema/TreeTransform.h"

using namespace clang;
using namespace clang::tooling;
using llvm::APInt;

namespace {

  struct UPCRDecls {
    FunctionDecl * upcr_notify;
    FunctionDecl * upcr_wait;
    FunctionDecl * upcr_barrier;
    FunctionDecl * UPCR_BEGIN_FUNCTION;
    FunctionDecl * UPCRT_STARTUP_PSHALLOC;
    FunctionDecl * upcr_startup_pshalloc;
    FunctionDecl * UPCR_GET_PSHARED;
    FunctionDecl * UPCR_PUT_PSHARED;
    QualType upcr_shared_ptr_t;
    QualType upcr_startup_pshalloc_t;
    SourceLocation FakeLocation;
    explicit UPCRDecls(ASTContext& Context) {
      SourceManager& SourceMgr = Context.getSourceManager();
      FakeLocation = SourceMgr.getLocForStartOfFile(SourceMgr.getMainFileID());

      // types
      upcr_shared_ptr_t = CreateTypedefType(Context, "upcr_shared_ptr_t");
      upcr_startup_pshalloc_t = CreateTypedefType(Context, "upcr_startup_pshalloc_t");

      // upcr_notify
      {
	QualType argTypes[] = { Context.IntTy, Context.IntTy };
	upcr_notify = CreateFunction(Context, "upcr_notify", Context.VoidTy, argTypes, 2);
      }
      // upcr_wait
      {
	QualType argTypes[] = { Context.IntTy, Context.IntTy };
	upcr_wait = CreateFunction(Context, "upcr_wait", Context.VoidTy, argTypes, 2);
      }
      // upcr_barrier
      {
	QualType argTypes[] = { Context.IntTy, Context.IntTy };
	upcr_barrier = CreateFunction(Context, "upcr_barrier", Context.VoidTy, argTypes, 2);
      }
      // UPCR_GET_PSHARED
      {
	QualType argTypes[] = { Context.VoidPtrTy, upcr_shared_ptr_t, Context.IntTy, Context.IntTy };
	UPCR_GET_PSHARED = CreateFunction(Context, "UPCR_GET_PSHARED", Context.VoidTy, argTypes, sizeof(argTypes)/sizeof(argTypes[0]));
      }
      // UPCR_PUT_PSHARED
      {
	QualType argTypes[] = { upcr_shared_ptr_t, Context.VoidPtrTy, Context.IntTy, Context.IntTy };
	UPCR_PUT_PSHARED = CreateFunction(Context, "UPCR_PUT_PSHARED", Context.VoidTy, argTypes, sizeof(argTypes)/sizeof(argTypes[0]));
      }
      // UPCR_BEGIN_FUNCTION
      {
	UPCR_BEGIN_FUNCTION = CreateFunction(Context, "UPCR_BEGIN_FUNCTION", Context.VoidTy, NULL, 0);
      }
      // UPCRT_STARTUP_PSHALLOC
      {
	QualType argTypes[] = { upcr_shared_ptr_t, Context.IntTy, Context.IntTy, Context.IntTy, Context.IntTy, Context. getPointerType(Context.getConstType(Context.CharTy)) };
	UPCRT_STARTUP_PSHALLOC = CreateFunction(Context, "UPCRT_STARTUP_PSHALLOC", upcr_startup_pshalloc_t, argTypes, sizeof(argTypes)/sizeof(argTypes[0]));
      }
      // upcr_startup_pshalloc
      {
	QualType argTypes[] = { Context.getPointerType(upcr_startup_pshalloc_t), Context.IntTy };
	upcr_startup_pshalloc = CreateFunction(Context, "upcr_startup_pshalloc", Context.VoidTy, argTypes, sizeof(argTypes)/sizeof(argTypes[0]));
      }

    }
    FunctionDecl *CreateFunction(ASTContext& Context, StringRef name, QualType RetType, QualType * argTypes, int numArgs) {
      DeclContext *DC = Context.getTranslationUnitDecl();
      QualType Ty = Context.getFunctionType(RetType, argTypes, numArgs, FunctionProtoType::ExtProtoInfo());
      FunctionDecl *Result = FunctionDecl::Create(Context, DC, FakeLocation, FakeLocation, DeclarationName(&Context.Idents.get(name)), Ty, Context.getTrivialTypeSourceInfo(Ty));
      llvm::SmallVector<ParmVarDecl *, 4> Params;
      for(int i = 0; i < numArgs; ++i) {
	Params.push_back(ParmVarDecl::Create(Context, Result, SourceLocation(), SourceLocation(), 0, argTypes[i], 0, SC_None, SC_None, 0));
	Params[i]->setScopeInfo(0, i);
      }
      Result->setParams(Params);
      return Result;
    }
    QualType CreateTypedefType(ASTContext& Context, StringRef name) {
      DeclContext *DC = Context.getTranslationUnitDecl();
      RecordDecl *Result = RecordDecl::Create(Context, TTK_Struct, DC,
					      SourceLocation(), SourceLocation(),
					      0);
      Result->startDefinition();
      Result->completeDefinition();
      TypedefDecl *Typedef = TypedefDecl::Create(Context, DC, SourceLocation(), SourceLocation(), &Context.Idents.get(name), Context.getTrivialTypeSourceInfo(Context.getRecordType(Result)));
      return Context.getTypedefType(Typedef);
    }
  };

  class RemoveUPCTransform : public clang::TreeTransform<RemoveUPCTransform> {
  public:
    RemoveUPCTransform(Sema& S, UPCRDecls* D) : TreeTransform(S), Decls(D) {
    }
    bool AlwaysRebuild() { return true; }
    // TreeTransform ignores AlwayRebuild for literals
    ExprResult TransformIntegerLiteral(IntegerLiteral *E) {
      return IntegerLiteral::Create(SemaRef.Context, E->getValue(), E->getType(), E->getLocation());
    }
    ExprResult BuildUPCRCall(FunctionDecl *FD, std::vector<Expr*>& args) {
      ExprResult Fn = SemaRef.BuildDeclRefExpr(FD, FD->getType(), VK_LValue, SourceLocation());
      return SemaRef.BuildResolvedCallExpr(Fn.get(), FD, SourceLocation(), args.data(), args.size(), SourceLocation());
    }
    StmtResult TransformUPCNotifyStmt(UPCNotifyStmt *S) {
      Expr *ID = S->getIdValue();
      std::vector<Expr*> args;
      if(ID) {
	args.push_back(TransformExpr(ID).get());
	args.push_back(IntegerLiteral::Create(
	  SemaRef.Context, APInt(32, 0), SemaRef.Context.IntTy, SourceLocation()));
      } else {
	args.push_back(IntegerLiteral::Create(
	  SemaRef.Context, APInt(32, 0), SemaRef.Context.IntTy, SourceLocation()));
	args.push_back(IntegerLiteral::Create(
	  SemaRef.Context, APInt(32, 1), SemaRef.Context.IntTy, SourceLocation()));
      }
      Stmt *result = BuildUPCRCall(Decls->upcr_notify, args).get();
      return SemaRef.Owned(result);
    }
    StmtResult TransformUPCWaitStmt(UPCWaitStmt *S) {
      Expr *ID = S->getIdValue();
      std::vector<Expr*> args;
      if(ID) {
	args.push_back(TransformExpr(ID).get());
	args.push_back(IntegerLiteral::Create(
	  SemaRef.Context, APInt(32, 0), SemaRef.Context.IntTy, SourceLocation()));
      } else {
	args.push_back(IntegerLiteral::Create(
	  SemaRef.Context, APInt(32, 0), SemaRef.Context.IntTy, SourceLocation()));
	args.push_back(IntegerLiteral::Create(
	  SemaRef.Context, APInt(32, 1), SemaRef.Context.IntTy, SourceLocation()));
      }
      Stmt *result = BuildUPCRCall(Decls->upcr_wait, args).get();
      return SemaRef.Owned(result);
    }
    StmtResult TransformUPCBarrierStmt(UPCBarrierStmt *S) {
      Expr *ID = S->getIdValue();
      std::vector<Expr*> args;
      if(ID) {
	args.push_back(TransformExpr(ID).get());
	args.push_back(IntegerLiteral::Create(
	  SemaRef.Context, APInt(32, 0), SemaRef.Context.IntTy, SourceLocation()));
      } else {
	args.push_back(IntegerLiteral::Create(
	  SemaRef.Context, APInt(32, 0), SemaRef.Context.IntTy, SourceLocation()));
	args.push_back(IntegerLiteral::Create(
	  SemaRef.Context, APInt(32, 1), SemaRef.Context.IntTy, SourceLocation()));
      }
      Stmt *result = BuildUPCRCall(Decls->upcr_barrier, args).get();
      return SemaRef.Owned(result);
    }
    ExprResult TransformImplicitCastExpr(ImplicitCastExpr *E) {
      if(E->getCastKind() == CK_LValueToRValue && E->getSubExpr()->getType().getQualifiers().hasShared()) {
	int SizeTypeSize = SemaRef.Context.getTypeSize(SemaRef.Context.getSizeType());
	// Handle pointer-to-shared reads
	// Ptr should have type upcr_shared_ptr_t
	Qualifiers Quals = E->getSubExpr()->getType().getQualifiers();
	QualType ResultType = TransformType(E->getType());
	VarDecl *TmpVar = CreateTmpVar(ResultType);
	// FIXME: Handle other layout qualifiers
	std::vector<Expr*> args;
	args.push_back(SemaRef.CreateBuiltinUnaryOp(SourceLocation(), UO_AddrOf, SemaRef.BuildDeclRefExpr(TmpVar, ResultType, VK_LValue, SourceLocation()).get()).get());
	args.push_back(TransformExpr(E->getSubExpr()).get());
	// offset
	args.push_back(IntegerLiteral::Create(SemaRef.Context, APInt(SizeTypeSize, 0), SemaRef.Context.getSizeType(), SourceLocation()));
	// size
	args.push_back(IntegerLiteral::Create(SemaRef.Context, APInt(SizeTypeSize, SemaRef.Context.getTypeSizeInChars(E->getType()).getQuantity()), SemaRef.Context.getSizeType(), SourceLocation()));
	Expr *Load = BuildUPCRCall(Decls->UPCR_GET_PSHARED, args).get();
	return SemaRef.ActOnParenExpr(SourceLocation(), SourceLocation(), SemaRef.CreateBuiltinBinOp(SourceLocation(), BO_Comma, Load, SemaRef.BuildDeclRefExpr(TmpVar, ResultType, VK_LValue, SourceLocation()).get()).get());
      } else {
	// Otherwise use the default transform
	return TreeTransform::TransformImplicitCastExpr(E);
      }
    }
    ExprResult TransformBinaryOperator(BinaryOperator *E) {
      // Catch assignment to shared variables
      if(E->getOpcode() == BO_Assign && E->getLHS()->getType().getQualifiers().hasShared()) {
	int SizeTypeSize = SemaRef.Context.getTypeSize(SemaRef.Context.getSizeType());
	Expr *LHS = TransformExpr(E->getLHS()).get();
	Expr *RHS = TransformExpr(E->getRHS()).get();
	VarDecl *TmpVar = CreateTmpVar(RHS->getType());
	Expr *SetTmp = SemaRef.CreateBuiltinBinOp(SourceLocation(), BO_Assign, SemaRef.BuildDeclRefExpr(TmpVar, RHS->getType(), VK_LValue, SourceLocation()).get(), RHS).get();
	std::vector<Expr*> args;
	args.push_back(LHS);
	args.push_back(SemaRef.CreateBuiltinUnaryOp(SourceLocation(), UO_AddrOf, SemaRef.BuildDeclRefExpr(TmpVar, RHS->getType(), VK_LValue, SourceLocation()).get()).get());
	// offset
	args.push_back(IntegerLiteral::Create(SemaRef.Context, APInt(SizeTypeSize, 0), SemaRef.Context.getSizeType(), SourceLocation()));
	// size
	args.push_back(IntegerLiteral::Create(SemaRef.Context, APInt(SizeTypeSize, SemaRef.Context.getTypeSizeInChars(RHS->getType()).getQuantity()), SemaRef.Context.getSizeType(), SourceLocation()));
	Expr *Store = BuildUPCRCall(Decls->UPCR_PUT_PSHARED, args).get();
	return SemaRef.ActOnParenExpr(SourceLocation(), SourceLocation(), SemaRef.CreateBuiltinBinOp(SourceLocation(), BO_Comma, SetTmp, Store).get());
      } else {
	// Otherwise use the default transform
	return TreeTransform::TransformBinaryOperator(E);
      }
    }
    VarDecl *CreateTmpVar(QualType Ty) {
      int ID = static_cast<int>(LocalTemps.size());
      std::string name = (llvm::Twine("_bupc_spilld") + llvm::Twine(ID)).str();
      VarDecl *TmpVar = VarDecl::Create(SemaRef.Context, SemaRef.getFunctionLevelDeclContext(), SourceLocation(), SourceLocation(), &SemaRef.Context.Idents.get(name), Ty, SemaRef.Context.getTrivialTypeSourceInfo(Ty), SC_Auto, SC_None);
      LocalTemps.push_back(TmpVar);
      return TmpVar;
    }
    Decl *TransformDecl(SourceLocation Loc, Decl *D) {
      return TreeTransform::TransformDecl(Loc, D);
    }
    Decl *TransformDefinition(SourceLocation Loc, Decl *D) {
      return TransformDeclaration(D, SemaRef.CurContext);
    }
    Decl *TransformDeclaration(Decl *D, DeclContext *DC) {
      Decl *Result = TransformDeclarationImpl(D, DC);
      transformedLocalDecl(D, Result);
      return Result;
    }
    Decl *TransformDeclarationImpl(Decl *D, DeclContext *DC) {
      if(TranslationUnitDecl *TUD = dyn_cast<TranslationUnitDecl>(D)) {
	return TransformTranslationUnitDecl(TUD);
      } else if(FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
	FunctionDecl *result = FunctionDecl::Create(SemaRef.Context, DC, FD->getLocStart(),
				    FD->getNameInfo(), TransformType(FD->getType()),
				    TransformType(FD->getTypeSourceInfo()),
				    FD->getStorageClass(), FD->getStorageClassAsWritten(),
				    FD->isInlineSpecified(), FD->hasWrittenPrototype(),
				    FD->isConstexpr());
	if(FD->doesThisDeclarationHaveABody()) {
	  SemaRef.ActOnStartOfFunctionDef(0, result);
	  Sema::SynthesizedFunctionScope Scope(SemaRef, result);
	  Stmt *UserBody = TransformStmt(FD->getBody()).get();
	  llvm::SmallVector<Stmt*, 8> Body;
	  {
	    std::vector<Expr*> args;
	    Body.push_back(BuildUPCRCall(Decls->UPCR_BEGIN_FUNCTION, args).get());
	  }
	  // Insert all the temporary variables that we created
	  for(std::vector<VarDecl*>::const_iterator iter = LocalTemps.begin(), end = LocalTemps.end(); iter != end; ++iter) {
	    Decl *decl_arr[] = { *iter };
	    Body.push_back(SemaRef.ActOnDeclStmt(Sema::DeclGroupPtrTy::make(DeclGroupRef::Create(SemaRef.Context, decl_arr, 1)), SourceLocation(), SourceLocation()).get());
	  }
	  LocalTemps.clear();
	  // Insert the user code
	  Body.push_back(UserBody);
	  SemaRef.ActOnFinishFunctionBody(result, SemaRef.ActOnCompoundStmt(SourceLocation(), SourceLocation(), Body, false).get());
	}
	return result;
      } else if(VarDecl *VD = dyn_cast<VarDecl>(D)) {
	if(VD->getType().getQualifiers().hasShared()) {
	  VarDecl *result = VarDecl::Create(SemaRef.Context, DC, VD->getLocStart(),
					    VD->getLocation(), VD->getIdentifier(),
					    Decls->upcr_shared_ptr_t, SemaRef.Context.getTrivialTypeSourceInfo(Decls->upcr_shared_ptr_t), VD->getStorageClass(), VD->getStorageClassAsWritten());
	  SharedGlobals.push_back(std::make_pair(result, VD));
	  return result;
	} else {
	  VarDecl *result = VarDecl::Create(SemaRef.Context, DC, VD->getLocStart(), VD->getLocation(), VD->getIdentifier(),
					    TransformType(VD->getType()), TransformType(VD->getTypeSourceInfo()),
					    VD->getStorageClass(), VD->getStorageClassAsWritten());
	  if(Expr *Init = VD->getInit()) {
	    result->setInit(TransformExpr(Init).get());
	  }
	  return result;
	}
      } else if(RecordDecl *RD = dyn_cast<RecordDecl>(D)) {
	return RecordDecl::Create(SemaRef.Context, RD->getTagKind(), DC,
				  RD->getLocStart(), RD->getLocation(),
				  RD->getIdentifier(), cast_or_null<RecordDecl>(TransformDecl(SourceLocation(), RD->getPreviousDecl())));
      } else if(TypedefDecl *TD = dyn_cast<TypedefDecl>(D)) {
	return TypedefDecl::Create(SemaRef.Context, DC, TD->getLocStart(), TD->getLocation(), TD->getIdentifier(), TransformType(TD->getTypeSourceInfo()));
      } else {
	assert(!"Unknown Decl");
      }
    }
    Decl *TransformTranslationUnitDecl(TranslationUnitDecl *D) {
      TranslationUnitDecl *result = SemaRef.Context.getTranslationUnitDecl();
      Scope CurScope(0, Scope::DeclScope, SemaRef.getDiagnostics());
      SemaRef.setCurScope(&CurScope);
      SemaRef.PushDeclContext(&CurScope, result);
      CopyDeclContext(D, result);
      result->addDecl(GetInitialization());
      SemaRef.setCurScope(0);
      return result;
    }
    UPCRDecls *Decls;
    void CopyDeclContext(DeclContext *Src, DeclContext *Dst) {
      for(DeclContext::decl_iterator iter = Src->decls_begin(),
          end = Src->decls_end(); iter != end; ++iter) {
	Dst->addDecl(TransformDeclaration(*iter, Dst));
      }
    }
    std::vector<VarDecl*> LocalTemps;
    // The shared variables that need to be initialized
    // all must have type upcr_shared_ptr_t
    // first = upcr_shared_ptr_t, second = original declaration
    // This must be called at the end of the transformation
    // after all variables with static storage duration
    // have been processed
    typedef std::vector<std::pair<VarDecl*, VarDecl*> > SharedGlobalsType;
    std::vector<std::pair<VarDecl*, VarDecl*> > SharedGlobals;
    FunctionDecl* GetInitialization() {
      // FIXME: randomize (?) the name
      FunctionDecl *Result = Decls->CreateFunction(SemaRef.Context, "UPCRI_ALLOC_test", SemaRef.Context.VoidTy, 0, 0);
      SemaRef.ActOnStartOfFunctionDef(0, Result);
      Sema::SynthesizedFunctionScope Scope(SemaRef, Result);
      StmtResult Body;
      {
	Sema::CompoundScopeRAII BodyScope(SemaRef);
	SmallVector<Stmt*, 8> Statements;
	{
	  std::vector<Expr*> args;
	  Statements.push_back(BuildUPCRCall(Decls->UPCR_BEGIN_FUNCTION, args).get());
	}
	int SizeTypeSize = SemaRef.Context.getTypeSize(SemaRef.Context.getSizeType());
	QualType _bupc_pinfo_type = SemaRef.Context.getIncompleteArrayType(Decls->upcr_startup_pshalloc_t, ArrayType::Normal, 0);
	VarDecl *_bupc_pinfo = VarDecl::Create(SemaRef.Context, Result, SourceLocation(), SourceLocation(),
					       &SemaRef.Context.Idents.get("_bupc_pinfo"), _bupc_pinfo_type, SemaRef.Context.getTrivialTypeSourceInfo(_bupc_pinfo_type), SC_Auto, SC_None);
	SmallVector<Expr*, 8> Initializers;
	for(SharedGlobalsType::const_iterator iter = SharedGlobals.begin(), end = SharedGlobals.end();
	    iter != end; ++iter) {
	  std::vector<Expr*> args;
	  args.push_back(SemaRef.BuildDeclRefExpr(iter->first, Decls->upcr_shared_ptr_t, VK_LValue, SourceLocation()).get());
	  int LayoutQualifier = iter->second->getType().getQualifiers().getLayoutQualifier();
	  llvm::APInt ArrayDimension(SizeTypeSize, 1);
	  bool hasThread = false;
	  QualType ElemTy = iter->second->getType().getCanonicalType();
	  while(const ArrayType *AT = dyn_cast<ArrayType>(ElemTy.getTypePtr())) {
	    if(const ConstantArrayType *CAT = dyn_cast<ConstantArrayType>(AT)) {
	      ArrayDimension *= CAT->getSize();
	    } else if(const UPCThreadArrayType *TAT = dyn_cast<UPCThreadArrayType>(AT)) {
	      if(TAT->getThread()) {
		hasThread = true;
	      }
	      ArrayDimension *= TAT->getSize();
	    } else {
	      assert(!"Other array types should not syntax check");
	    }
	    ElemTy = AT->getElementType();
	  }
	  int ElementSize = SemaRef.Context.getTypeSize(ElemTy);
	  if(LayoutQualifier == 0) {
	    // FIXME:
	  } else {
	    args.push_back(IntegerLiteral::Create(SemaRef.Context, llvm::APInt(SizeTypeSize, LayoutQualifier * ElementSize), SemaRef.Context.getSizeType(), SourceLocation()));
	    args.push_back(IntegerLiteral::Create(SemaRef.Context, (ArrayDimension + LayoutQualifier - 1).udiv(llvm::APInt(SizeTypeSize, LayoutQualifier)), SemaRef.Context.getSizeType(), SourceLocation()));
	    args.push_back(IntegerLiteral::Create(SemaRef.Context, llvm::APInt(SizeTypeSize, hasThread), SemaRef.Context.getSizeType(), SourceLocation()));
	    args.push_back(IntegerLiteral::Create(SemaRef.Context, llvm::APInt(SizeTypeSize, ElementSize), SemaRef.Context.getSizeType(), SourceLocation()));
	    // FIXME: encode the correct mangled type
	    args.push_back(StringLiteral::Create(SemaRef.Context, "", StringLiteral::Ascii, false, SemaRef.Context.getPointerType(SemaRef.Context.getConstType(SemaRef.Context.CharTy)), SourceLocation()));
	    Initializers.push_back(BuildUPCRCall(Decls->UPCRT_STARTUP_PSHALLOC, args).get());
	  }
	}
	// InitializerList semantics vary depending on whether the SourceLocations are valid.
	SemaRef.AddInitializerToDecl(_bupc_pinfo, SemaRef.ActOnInitList(Decls->FakeLocation, Initializers, Decls->FakeLocation).get(), false, false);
	Decl *_bupc_pinfo_arr[] = { _bupc_pinfo };
	Statements.push_back(SemaRef.ActOnDeclStmt(Sema::DeclGroupPtrTy::make(DeclGroupRef::Create(SemaRef.Context, _bupc_pinfo_arr, 1)), SourceLocation(), SourceLocation()).get());
	{
	  std::vector<Expr*> args;
	  args.push_back(SemaRef.BuildDeclRefExpr(_bupc_pinfo, _bupc_pinfo_type, VK_LValue, SourceLocation()).get());
	  args.push_back(IntegerLiteral::Create(SemaRef.Context, llvm::APInt(SizeTypeSize, SharedGlobals.size()), SemaRef.Context.getSizeType(), SourceLocation()));
	  Statements.push_back(BuildUPCRCall(Decls->upcr_startup_pshalloc, args).get());
	}
	Body = SemaRef.ActOnCompoundStmt(SourceLocation(), SourceLocation(), Statements, false);
      }
      SemaRef.ActOnFinishFunctionBody(Result, Body.get());
      return Result;
    }
  };

  class RemoveUPCConsumer : public clang::SemaConsumer {
  public:
    RemoveUPCConsumer() : filename("upc.c") {}
    virtual void HandleTranslationUnit(clang::ASTContext &Context) {
      TranslationUnitDecl *top = Context.getTranslationUnitDecl();
      // Copy the ASTContext and Sema
      LangOptions LangOpts = Context.getLangOpts();
      ASTContext newContext(LangOpts, Context.getSourceManager(), &Context.getTargetInfo(),
			    Context.Idents, Context.Selectors, Context.BuiltinInfo,
			    Context.getTypes().size());
      ASTConsumer nullConsumer;
      UPCRDecls Decls(newContext);
      Sema newSema(S->getPreprocessor(), newContext, nullConsumer);
      RemoveUPCTransform Trans(newSema, &Decls);
      Decl *Result = Trans.TransformTranslationUnitDecl(top);
      std::string error;
      llvm::raw_fd_ostream OS(filename.c_str(), error);
      Result->print(OS);
    }
    void InitializeSema(Sema& SemaRef) { S = &SemaRef; }
    void ForgetSema() { S = 0; }
  private:
    Sema *S;
    std::string filename;
  };

  class RemoveUPCAction : public clang::ASTFrontendAction {
  public:
    virtual clang::ASTConsumer *CreateASTConsumer(clang::CompilerInstance &Compiler, llvm::StringRef InFile) {
      return new RemoveUPCConsumer();
    }
  };

}

int main(int argc, const char ** argv) {
  std::vector<std::string> options(argv, argv + argc);
  FileManager Files((FileSystemOptions()));
  ToolInvocation tool(options, new RemoveUPCAction, &Files);
  tool.run();
}
