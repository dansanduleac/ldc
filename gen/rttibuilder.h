#ifndef __LDC_GEN_RTTIBUILDER_H__
#define __LDC_GEN_RTTIBUILDER_H__

#include "llvm/Constant.h"
#include "llvm/ADT/SmallVector.h"

struct ClassDeclaration;
struct AggregateDeclaration;
struct TypeClass;
struct Type;

struct IrStruct;

struct RTTIBuilder
{
    AggregateDeclaration* base;
    TypeClass* basetype;
    IrStruct* baseir;

    // 10 is enough for any D1 TypeInfo
    // 14 is enough for any D1 ClassInfo
    llvm::SmallVector<llvm::Constant*, 14> inits;

    RTTIBuilder(AggregateDeclaration* base_class);

    void push(llvm::Constant* C);
    void push_null(Type* T);
    void push_null_vp();
    void push_null_void_array();
    void push_uint(unsigned u);
    void push_size(uint64_t s);
    void push_string(const char* str);
    void push_typeinfo(Type* t);
    void push_classinfo(ClassDeclaration* cd);
    void push_rt_info(Expression *rtInfo, bool hasPointers);

    /// pushes the function pointer or a null void* if it cannot.
    void push_funcptr(FuncDeclaration* fd, Type* castto = NULL);

    /// pushes the array slice given.
    void push_array(uint64_t dim, llvm::Constant * ptr);

    /// pushes void[] slice, dim is used directly, ptr is cast to void* .
    void push_void_array(uint64_t dim, llvm::Constant* ptr);

    /// pushes void[] slice with data.
    /// CI is the constant initializer the array should point to, the length
    /// and ptr are resolved automatically
    void push_void_array(llvm::Constant* CI, Type* valtype, Dsymbol* mangle_sym);

    /// pushes valtype[] slice with data.
    /// CI is the constant initializer that .ptr should point to
    /// dim is .length member directly
    /// valtype provides the D element type, .ptr is cast to valtype->pointerTo()
    /// mangle_sym provides the mangle prefix for the symbol generated.
    void push_array(llvm::Constant* CI, uint64_t dim, Type* valtype, Dsymbol* mangle_sym);

    /// Creates the initializer constant and assigns it to the global.
    void finalize(IrGlobal* tid);
    void finalize(LLType* type, LLValue* value);

    /// Creates the initializer constant and assigns it to the global.
    llvm::Constant* get_constant(LLStructType *initType);
};

#endif
