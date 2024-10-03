#include <gtest/gtest.h>
#include <Compiler/Resolver.h>

namespace {

using namespace clice;

std::vector<const char*> compileArgs = {
    "clang++",
    "-std=c++20",
    "main.cpp",
    "-resource-dir",
    "/home/ykiko/C++/clice2/build/lib/clang/20",
};

struct Visitor : public clang::RecursiveASTVisitor<Visitor> {
    clang::QualType result;
    std::unique_ptr<ParsedAST> parsedAST;

    Visitor(const char* code) : parsedAST(ParsedAST::build("main.cpp", code, compileArgs)) {}

    bool VisitTypeAliasDecl(clang::TypeAliasDecl* decl) {
        if(decl->getName() == "result") {
            auto type = decl->getUnderlyingType();
            {
                clang::Sema::CodeSynthesisContext context;
                context.Kind = clang::Sema::CodeSynthesisContext::TemplateInstantiation;
                context.Entity = decl;
                context.TemplateArgs = nullptr;
                parsedAST->sema.pushCodeSynthesisContext(context);
            }
            auto resolver = TemplateResolver(parsedAST->sema);
            result = resolver.resolve(type);
        }
        return true;
    }

    clang::QualType test() {
        auto decl = parsedAST->context.getTranslationUnitDecl();
        TraverseDecl(decl);
        return result;
    }
};

void match(clang::QualType type, std::string name, std::initializer_list<std::string> args) {
    auto TST = type->getAs<clang::TemplateSpecializationType>();
    ASSERT_TRUE(TST);
    ASSERT_EQ(TST->getTemplateName().getAsTemplateDecl()->getName(), name);

    auto template_args = TST->template_arguments();
    ASSERT_EQ(template_args.size(), args.size());

    auto iter = args.begin();
    for(auto arg: template_args) {
        auto T = llvm::dyn_cast<clang::TemplateTypeParmType>(arg.getAsType());
        ASSERT_TRUE(T);
        ASSERT_EQ(T->getDecl()->getName(), *iter);
        ++iter;
    }
}

TEST(DependentNameResolver, single_level_dependent_name) {
    const char* code = R"(
template <typename ...Ts>
struct type_list {};

template <typename T>
struct A {
    using type = type_list<T>;
};

template <typename X>
struct test {
    using result = typename A<X>::type;
};

)";
    Visitor visitor(code);
    auto result = visitor.test();
    match(result, "type_list", {"X"});
}

TEST(DependentNameResolver, multi_level_dependent_name) {
    const char* code = R"(
template <typename ...Ts>
struct type_list {};

template <typename T1>
struct A {
    using type = type_list<T1>;
};

template <typename T2>
struct B {
    using type = typename A<T2>::type;
};

template <typename T3>
struct C {
    using type = typename B<T3>::type;
};

template <typename X>
struct test {
    using result = typename C<X>::type;
};
)";
    Visitor visitor(code);
    clang::QualType result = visitor.test();

    match(result, "type_list", {"X"});
}

TEST(DependentNameResolver, dependent_dependent_dependent_name) {
    const char* code = R"(
 template <typename ...Ts>
 struct type_list {};

 template <typename T1>
 struct A {
     using self = A<T1>;
     using type = type_list<T1>;
 };

 template <typename X>
 struct test {
     using result = typename A<X>::self::self::self::self::self::type;
 };
)";
    Visitor visitor(code);
    clang::QualType result = visitor.test();

    match(result, "type_list", {"X"});
}

TEST(DependentNameResolver, alias_dependent_name) {
    const char* code = R"(
 template <typename ...Ts>
 struct type_list {};

 template <typename T1>
 struct A {
     using type = type_list<T1>;
 };

 template <typename T2>
 struct B {
     using base = A<T2>;
     using type = typename base::type;
 };

 template <typename X>
 struct test {
     using result = typename B<X>::type;
 };
)";
    Visitor visitor(code);
    clang::QualType result = visitor.test();
    match(result, "type_list", {"X"});
}

TEST(DependentNameResolver, alias_template_dependent_name) {
    const char* code = R"(
 template <typename ...Ts>
 struct type_list {};

 template <typename T1>
 struct A {
     using type = T1;
 };

 template <typename T2>
 struct B {
     using base = A<T2>;
     using type = type_list<typename base::type>;
 };

 template <typename X>
 struct test {
     using result = typename B<X>::type;
 };
)";
    Visitor visitor(code);
    clang::QualType result = visitor.test();
    match(result, "type_list", {"X"});
}

TEST(DependentNameResolver, template_alias_dependent_name) {
    const char* code = R"(
 template <typename ...Ts>
 struct type_list {};

 template <typename T1>
 struct A {
     using type = type_list<T1>;
 };

 template <typename T2>
 using B = A<T2>;

 template <typename X>
 struct test {
     using result = typename B<X>::type;
 };
)";
    Visitor visitor(code);
    clang::QualType result = visitor.test();
    match(result, "type_list", {"X"});
}

TEST(DependentNameResolver, dependent_member_template) {
    const char* code = R"(
 template <typename... Ts>
 struct type_list {};

 template <typename T1, typename U1>
 struct A {
     using type = type_list<T1, U1>;
 };

 template <typename T2>
 struct B {
     template <typename U2>
     using type = typename A<T2, U2>::type;
 };

 template <typename X, typename Y>
 struct test {
     using result = typename B<X>::template type<Y>;
 };
)";
    Visitor visitor(code);
    clang::QualType result = visitor.test();
    match(result, "type_list", {"X", "Y"});
}

TEST(DependentNameResolver, dependent_member_class_template) {
    const char* code = R"(
 template <typename... Ts>
 struct type_list {};

 template <typename T1>
 struct A {
     template <typename U1>
     struct B {
         using type = type_list<T1, U1>;
     };
 };

 template <typename X, typename Y>
 struct test {
     using result = typename A<X>::template B<Y>::type;
 };
)";

    Visitor visitor(code);
    clang::QualType result = visitor.test();
    match(result, "type_list", {"X", "Y"});
}

TEST(DependentNameResolver, dependent_partial_name) {
    const char* code = R"(
 template <typename... Ts>
 struct type_list {};

 template <typename T1>
 struct A {};

 template <typename U2>
 struct B {};

 template <typename U2, template <typename...> typename HKT>
 struct B<HKT<U2>> {
     using type = type_list<U2>;
 };

 template <typename X>
 struct test {
     using result = typename B<A<X>>::type;
 };
)";

    Visitor visitor(code);
    clang::QualType result = visitor.test();
    result.dump();
}

TEST(DependentNameResolver, dependent_base_name) {
    const char* code = R"(
 template <typename... Ts>
 struct type_list {};

 template <typename T1>
 struct A {
     using type = type_list<T1>;
 };

 template <typename U2>
 struct B : A<U2> {};

 template <typename X>
 struct test {
     using result = typename B<X>::type;
 };
)";

    Visitor visitor(code);
    auto result = visitor.test();

    match(result, "type_list", {"X"});
}

TEST(DependentNameResolver, dependent_base_name_2) {
    const char* code = R"(
 template <typename... Ts>
 struct type_list {};

 template <typename T1>
 struct A {
     using type = type_list<T1>;
 };

 template <typename X>
 struct test {
    using base = typename A<X>::type;
    using result = base;
 };
)";

    Visitor visitor(code);
    auto result = visitor.test();
    llvm::outs() << "------------------------------------------\n";
    result.dump();
    match(result, "type_list", {"X"});
}

TEST(DependentNameResolver, std_vector) {
    const char* code = R"(
   #include <vector>

   template <typename T>
   struct A {};

   template <typename X>
   struct test {
       using result = typename std::vector<X>::reference;
   };
)";

    Visitor visitor(code);
    clang::QualType result = visitor.test();
    llvm::outs() << "result is: { " << result.getAsString() << " }\n";
}

TEST(DependentNameResolver, std_list) {
    const char* code = R"(
  #include <list>

  template <typename T>
  struct A {};

  template <typename X>
  struct test {
      using result = typename std::list<A<X>>::reference;
  };
)";

    Visitor visitor(code);
    clang::QualType result = visitor.test();
    llvm::outs() << "result is: { " << result.getAsString() << " }\n";
}

}  // namespace

