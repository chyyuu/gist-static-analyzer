//===--- ASTMatchersInternal.cpp - Structural query framework -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  Implements the base layer of the matcher framework.
//
//===----------------------------------------------------------------------===//

#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchersInternal.h"
#include "llvm/Support/ManagedStatic.h"

namespace clang {
namespace ast_matchers {
namespace internal {

void BoundNodesTreeBuilder::visitMatches(Visitor *ResultVisitor) {
  if (Bindings.empty())
    Bindings.push_back(BoundNodesMap());
  for (unsigned i = 0, e = Bindings.size(); i != e; ++i) {
    ResultVisitor->visitMatch(BoundNodes(Bindings[i]));
  }
}

namespace {

class VariadicMatcher : public DynMatcherInterface {
 public:
  VariadicMatcher(VariadicOperatorFunction Func,
                  std::vector<DynTypedMatcher> InnerMatchers)
      : Func(Func), InnerMatchers(std::move(InnerMatchers)) {}

  bool dynMatches(const ast_type_traits::DynTypedNode &DynNode,
                  ASTMatchFinder *Finder,
                  BoundNodesTreeBuilder *Builder) const override {
    return Func(DynNode, Finder, Builder, InnerMatchers);
  }

 private:
  VariadicOperatorFunction Func;
  std::vector<DynTypedMatcher> InnerMatchers;
};

class IdDynMatcher : public DynMatcherInterface {
 public:
  IdDynMatcher(StringRef ID,
               const IntrusiveRefCntPtr<DynMatcherInterface> &InnerMatcher)
      : ID(ID), InnerMatcher(InnerMatcher) {}

  bool dynMatches(const ast_type_traits::DynTypedNode &DynNode,
                  ASTMatchFinder *Finder,
                  BoundNodesTreeBuilder *Builder) const override {
    bool Result = InnerMatcher->dynMatches(DynNode, Finder, Builder);
    if (Result) Builder->setBinding(ID, DynNode);
    return Result;
  }

 private:
  const std::string ID;
  const IntrusiveRefCntPtr<DynMatcherInterface> InnerMatcher;
};

/// \brief A matcher that always returns true.
///
/// We only ever need one instance of this matcher, so we create a global one
/// and reuse it to reduce the overhead of the matcher and increase the chance
/// of cache hits.
struct TrueMatcherImpl {
  TrueMatcherImpl() : Instance(new Impl) {}
  const IntrusiveRefCntPtr<DynMatcherInterface> Instance;

  class Impl : public DynMatcherInterface {
   public:
    bool dynMatches(const ast_type_traits::DynTypedNode &, ASTMatchFinder *,
                    BoundNodesTreeBuilder *) const override {
      return true;
    }
  };
};
static llvm::ManagedStatic<TrueMatcherImpl> TrueMatcherInstance;

}  // namespace

DynTypedMatcher DynTypedMatcher::constructVariadic(
    VariadicOperatorFunction Func, std::vector<DynTypedMatcher> InnerMatchers) {
  assert(InnerMatchers.size() > 0 && "Array must not be empty.");
  assert(std::all_of(InnerMatchers.begin(), InnerMatchers.end(),
                     [&InnerMatchers](const DynTypedMatcher &M) {
           return InnerMatchers[0].SupportedKind.isSame(M.SupportedKind);
         }) &&
         "SupportedKind must match!");

  // We must relax the restrict kind here.
  // The different operators might deal differently with a mismatch.
  // Make it the same as SupportedKind, since that is the broadest type we are
  // allowed to accept.
  auto SupportedKind = InnerMatchers[0].SupportedKind;
  return DynTypedMatcher(SupportedKind, SupportedKind,
                         new VariadicMatcher(Func, std::move(InnerMatchers)));
}

DynTypedMatcher DynTypedMatcher::trueMatcher(
    ast_type_traits::ASTNodeKind NodeKind) {
  return DynTypedMatcher(NodeKind, NodeKind, TrueMatcherInstance->Instance);
}

DynTypedMatcher DynTypedMatcher::dynCastTo(
    const ast_type_traits::ASTNodeKind Kind) const {
  auto Copy = *this;
  Copy.SupportedKind = Kind;
  Copy.RestrictKind =
      ast_type_traits::ASTNodeKind::getMostDerivedType(Kind, RestrictKind);
  return Copy;
}

bool DynTypedMatcher::matches(const ast_type_traits::DynTypedNode &DynNode,
                              ASTMatchFinder *Finder,
                              BoundNodesTreeBuilder *Builder) const {
  if (RestrictKind.isBaseOf(DynNode.getNodeKind()) &&
      Implementation->dynMatches(DynNode, Finder, Builder)) {
    return true;
  }
  // Delete all bindings when a matcher does not match.
  // This prevents unexpected exposure of bound nodes in unmatches
  // branches of the match tree.
  Builder->removeBindings([](const BoundNodesMap &) { return true; });
  return false;
}

llvm::Optional<DynTypedMatcher> DynTypedMatcher::tryBind(StringRef ID) const {
  if (!AllowBind) return llvm::None;
  auto Result = *this;
  Result.Implementation = new IdDynMatcher(ID, Result.Implementation);
  return Result;
}

bool DynTypedMatcher::canConvertTo(ast_type_traits::ASTNodeKind To) const {
  const auto From = getSupportedKind();
  auto QualKind = ast_type_traits::ASTNodeKind::getFromNodeKind<QualType>();
  auto TypeKind = ast_type_traits::ASTNodeKind::getFromNodeKind<Type>();
  /// Mimic the implicit conversions of Matcher<>.
  /// - From Matcher<Type> to Matcher<QualType>
  if (From.isSame(TypeKind) && To.isSame(QualKind)) return true;
  /// - From Matcher<Base> to Matcher<Derived>
  return From.isBaseOf(To);
}

void BoundNodesTreeBuilder::addMatch(const BoundNodesTreeBuilder &Other) {
  for (unsigned i = 0, e = Other.Bindings.size(); i != e; ++i) {
    Bindings.push_back(Other.Bindings[i]);
  }
}

bool NotUnaryOperator(const ast_type_traits::DynTypedNode DynNode,
                      ASTMatchFinder *Finder, BoundNodesTreeBuilder *Builder,
                      ArrayRef<DynTypedMatcher> InnerMatchers) {
  if (InnerMatchers.size() != 1)
    return false;

  // The 'unless' matcher will always discard the result:
  // If the inner matcher doesn't match, unless returns true,
  // but the inner matcher cannot have bound anything.
  // If the inner matcher matches, the result is false, and
  // any possible binding will be discarded.
  // We still need to hand in all the bound nodes up to this
  // point so the inner matcher can depend on bound nodes,
  // and we need to actively discard the bound nodes, otherwise
  // the inner matcher will reset the bound nodes if it doesn't
  // match, but this would be inversed by 'unless'.
  BoundNodesTreeBuilder Discard(*Builder);
  return !InnerMatchers[0].matches(DynNode, Finder, &Discard);
}

bool AllOfVariadicOperator(const ast_type_traits::DynTypedNode DynNode,
                           ASTMatchFinder *Finder,
                           BoundNodesTreeBuilder *Builder,
                           ArrayRef<DynTypedMatcher> InnerMatchers) {
  // allOf leads to one matcher for each alternative in the first
  // matcher combined with each alternative in the second matcher.
  // Thus, we can reuse the same Builder.
  for (size_t i = 0, e = InnerMatchers.size(); i != e; ++i) {
    if (!InnerMatchers[i].matches(DynNode, Finder, Builder))
      return false;
  }
  return true;
}

bool EachOfVariadicOperator(const ast_type_traits::DynTypedNode DynNode,
                            ASTMatchFinder *Finder,
                            BoundNodesTreeBuilder *Builder,
                            ArrayRef<DynTypedMatcher> InnerMatchers) {
  BoundNodesTreeBuilder Result;
  bool Matched = false;
  for (size_t i = 0, e = InnerMatchers.size(); i != e; ++i) {
    BoundNodesTreeBuilder BuilderInner(*Builder);
    if (InnerMatchers[i].matches(DynNode, Finder, &BuilderInner)) {
      Matched = true;
      Result.addMatch(BuilderInner);
    }
  }
  *Builder = std::move(Result);
  return Matched;
}

bool AnyOfVariadicOperator(const ast_type_traits::DynTypedNode DynNode,
                           ASTMatchFinder *Finder,
                           BoundNodesTreeBuilder *Builder,
                           ArrayRef<DynTypedMatcher> InnerMatchers) {
  for (size_t i = 0, e = InnerMatchers.size(); i != e; ++i) {
    BoundNodesTreeBuilder Result = *Builder;
    if (InnerMatchers[i].matches(DynNode, Finder, &Result)) {
      *Builder = std::move(Result);
      return true;
    }
  }
  return false;
}

} // end namespace internal
} // end namespace ast_matchers
} // end namespace clang