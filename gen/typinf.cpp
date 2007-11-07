

// Copyright (c) 1999-2004 by Digital Mars
// All Rights Reserved
// written by Walter Bright
// www.digitalmars.com
// License for redistribution is by either the Artistic License
// in artistic.txt, or the GNU General Public License in gnu.txt.
// See the included readme.txt for details.

#include <cstdio>
#include <cassert>

#include "gen/llvm.h"

#include "mars.h"
#include "module.h"
#include "mtype.h"
#include "scope.h"
#include "init.h"
#include "expression.h"
#include "attrib.h"
#include "declaration.h"
#include "template.h"
#include "id.h"
#include "enum.h"
#include "import.h"
#include "aggregate.h"

#include "gen/irstate.h"
#include "gen/logger.h"
#include "gen/runtime.h"
#include "gen/tollvm.h"
#include "gen/arrays.h"

/*******************************************
 * Get a canonicalized form of the TypeInfo for use with the internal
 * runtime library routines. Canonicalized in that static arrays are
 * represented as dynamic arrays, enums are represented by their
 * underlying type, etc. This reduces the number of TypeInfo's needed,
 * so we can use the custom internal ones more.
 */

Expression *Type::getInternalTypeInfo(Scope *sc)
{   TypeInfoDeclaration *tid;
    Expression *e;
    Type *t;
    static TypeInfoDeclaration *internalTI[TMAX];

    //printf("Type::getInternalTypeInfo() %s\n", toChars());
    t = toBasetype();
    switch (t->ty)
    {
    case Tsarray:
        t = t->next->arrayOf(); // convert to corresponding dynamic array type
        break;

    case Tclass:
        if (((TypeClass *)t)->sym->isInterfaceDeclaration())
        break;
        goto Linternal;

    case Tarray:
        if (t->next->ty != Tclass)
        break;
        goto Linternal;

    case Tfunction:
    case Tdelegate:
    case Tpointer:
    Linternal:
        tid = internalTI[t->ty];
        if (!tid)
        {   tid = new TypeInfoDeclaration(t, 1);
        internalTI[t->ty] = tid;
        }
        e = new VarExp(0, tid);
        //e = e->addressOf(sc);
        e->type = tid->type;    // do this so we don't get redundant dereference
        return e;

    default:
        break;
    }
    //printf("\tcalling getTypeInfo() %s\n", t->toChars());
    return t->getTypeInfo(sc);
}


/****************************************************
 * Get the exact TypeInfo.
 */

Expression *Type::getTypeInfo(Scope *sc)
{
    Expression *e;
    Type *t;

    //printf("Type::getTypeInfo() %p, %s\n", this, toChars());
    t = merge();    // do this since not all Type's are merge'd
    if (!t->vtinfo)
    {   t->vtinfo = t->getTypeInfoDeclaration();
    assert(t->vtinfo);

    /* If this has a custom implementation in std/typeinfo, then
     * do not generate a COMDAT for it.
     */
    if (!t->builtinTypeInfo())
    {   // Generate COMDAT
        if (sc)         // if in semantic() pass
        {   // Find module that will go all the way to an object file
        Module *m = sc->module->importedFrom;
        m->members->push(t->vtinfo);
        }
        else            // if in obj generation pass
        {
        t->vtinfo->toObjFile();
        }
    }
    }
    e = new VarExp(0, t->vtinfo);
    //e = e->addressOf(sc);
    e->type = t->vtinfo->type;      // do this so we don't get redundant dereference
    return e;
}

enum RET TypeFunction::retStyle()
{
    return RETstack;
}

TypeInfoDeclaration *Type::getTypeInfoDeclaration()
{
    //printf("Type::getTypeInfoDeclaration() %s\n", toChars());
    return new TypeInfoDeclaration(this, 0);
}

TypeInfoDeclaration *TypeTypedef::getTypeInfoDeclaration()
{
    return new TypeInfoTypedefDeclaration(this);
}

TypeInfoDeclaration *TypePointer::getTypeInfoDeclaration()
{
    return new TypeInfoPointerDeclaration(this);
}

TypeInfoDeclaration *TypeDArray::getTypeInfoDeclaration()
{
    return new TypeInfoArrayDeclaration(this);
}

TypeInfoDeclaration *TypeSArray::getTypeInfoDeclaration()
{
    return new TypeInfoStaticArrayDeclaration(this);
}

TypeInfoDeclaration *TypeAArray::getTypeInfoDeclaration()
{
    return new TypeInfoAssociativeArrayDeclaration(this);
}

TypeInfoDeclaration *TypeStruct::getTypeInfoDeclaration()
{
    return new TypeInfoStructDeclaration(this);
}

TypeInfoDeclaration *TypeClass::getTypeInfoDeclaration()
{
    if (sym->isInterfaceDeclaration())
    return new TypeInfoInterfaceDeclaration(this);
    else
    return new TypeInfoClassDeclaration(this);
}

TypeInfoDeclaration *TypeEnum::getTypeInfoDeclaration()
{
    return new TypeInfoEnumDeclaration(this);
}

TypeInfoDeclaration *TypeFunction::getTypeInfoDeclaration()
{
    return new TypeInfoFunctionDeclaration(this);
}

TypeInfoDeclaration *TypeDelegate::getTypeInfoDeclaration()
{
    return new TypeInfoDelegateDeclaration(this);
}

TypeInfoDeclaration *TypeTuple::getTypeInfoDeclaration()
{
    return new TypeInfoTupleDeclaration(this);
}


/* ========================================================================= */

/* These decide if there's an instance for them already in std.typeinfo,
 * because then the compiler doesn't need to build one.
 */

int Type::builtinTypeInfo()
{
    return 0;
}

int TypeBasic::builtinTypeInfo()
{
    return 1;
}

int TypeDArray::builtinTypeInfo()
{
    return next->isTypeBasic() != NULL;
}

/* ========================================================================= */

/***************************************
 * Create a static array of TypeInfo references
 * corresponding to an array of Expression's.
 * Used to supply hidden _arguments[] value for variadic D functions.
 */

Expression *createTypeInfoArray(Scope *sc, Expression *exps[], int dim)
{
    assert(0);
    return NULL;
}

/* ========================================================================= */

//////////////////////////////////////////////////////////////////////////////
//                             MAGIC   PLACE
//////////////////////////////////////////////////////////////////////////////

void TypeInfoDeclaration::toObjFile()
{
    if (llvmTouched) return;
    else llvmTouched = true;

    Logger::println("TypeInfoDeclaration::toObjFile()");
    LOG_SCOPE;

    Logger::println("type = '%s'", tinfo->toChars());
    Logger::println("typeinfo mangle: %s", mangle());

    // this is a declaration of a builtin __initZ var
    if (tinfo->builtinTypeInfo()) {
        llvmValue = LLVM_D_GetRuntimeGlobal(gIR->module, mangle());
        assert(llvmValue);
        Logger::cout() << "Got typeinfo var:" << '\n' << *llvmValue << '\n';
    }
    // custom typedef
    else {
        toDt(NULL);
    }
}

/* ========================================================================= */

void TypeInfoDeclaration::toDt(dt_t **pdt)
{
    assert(0 && "TypeInfoDeclaration");
}

/* ========================================================================= */

void TypeInfoTypedefDeclaration::toDt(dt_t **pdt)
{
    Logger::println("TypeInfoTypedefDeclaration::toDt() %s", toChars());
    LOG_SCOPE;

    ClassDeclaration* base = Type::typeinfotypedef;
    base->toObjFile();

    llvm::Constant* initZ = base->llvmInitZ;
    assert(initZ);
    const llvm::StructType* stype = llvm::cast<llvm::StructType>(initZ->getType());

    std::vector<llvm::Constant*> sinits;
    sinits.push_back(initZ->getOperand(0));

    assert(tinfo->ty == Ttypedef);
    TypeTypedef *tc = (TypeTypedef *)tinfo;
    TypedefDeclaration *sd = tc->sym;

    // TypeInfo base
    //const llvm::PointerType* basept = llvm::cast<llvm::PointerType>(initZ->getOperand(1)->getType());
    //sinits.push_back(llvm::ConstantPointerNull::get(basept));
    Logger::println("generating base typeinfo");
    //sd->basetype = sd->basetype->merge();
    sd->basetype->getTypeInfo(NULL);        // generate vtinfo
    assert(sd->basetype->vtinfo);
    if (!sd->basetype->vtinfo->llvmValue)
        sd->basetype->vtinfo->toObjFile();
    assert(llvm::isa<llvm::Constant>(sd->basetype->vtinfo->llvmValue));
    llvm::Constant* castbase = llvm::cast<llvm::Constant>(sd->basetype->vtinfo->llvmValue);
    castbase = llvm::ConstantExpr::getBitCast(castbase, initZ->getOperand(1)->getType());
    sinits.push_back(castbase);

    // char[] name
    char *name = sd->toPrettyChars();
    sinits.push_back(DtoConstString(name));
    assert(sinits.back()->getType() == initZ->getOperand(2)->getType());

    // void[] init
    const llvm::PointerType* initpt = llvm::PointerType::get(llvm::Type::Int8Ty);
    if (tinfo->isZeroInit() || !sd->init) // 0 initializer, or the same as the base type
    {
        sinits.push_back(DtoConstSlice(DtoConstSize_t(0), llvm::ConstantPointerNull::get(initpt)));
    }
    else
    {
        llvm::Constant* ci = DtoConstInitializer(sd->basetype, sd->init);
        std::string ciname(sd->mangle());
        ciname.append("__init");
        llvm::GlobalVariable* civar = new llvm::GlobalVariable(DtoType(sd->basetype),true,llvm::GlobalValue::InternalLinkage,ci,ciname,gIR->module);
        llvm::Constant* cicast = llvm::ConstantExpr::getBitCast(civar, initpt);
        size_t cisize = gTargetData->getTypeSize(DtoType(sd->basetype));
        sinits.push_back(DtoConstSlice(DtoConstSize_t(cisize), cicast));
    }

    // create the symbol
    llvm::Constant* tiInit = llvm::ConstantStruct::get(stype, sinits);
    llvm::GlobalVariable* gvar = new llvm::GlobalVariable(stype,true,llvm::GlobalValue::WeakLinkage,tiInit,toChars(),gIR->module);

    llvmValue = gvar;
}

/* ========================================================================= */

void TypeInfoEnumDeclaration::toDt(dt_t **pdt)
{
    Logger::println("TypeInfoTypedefDeclaration::toDt() %s", toChars());
    LOG_SCOPE;

    ClassDeclaration* base = Type::typeinfoenum;
    base->toObjFile();

    llvm::Constant* initZ = base->llvmInitZ;
    assert(initZ);
    const llvm::StructType* stype = llvm::cast<llvm::StructType>(initZ->getType());

    std::vector<llvm::Constant*> sinits;
    sinits.push_back(initZ->getOperand(0));

    assert(tinfo->ty == Tenum);
    TypeEnum *tc = (TypeEnum *)tinfo;
    EnumDeclaration *sd = tc->sym;

    // TypeInfo base
    //const llvm::PointerType* basept = llvm::cast<llvm::PointerType>(initZ->getOperand(1)->getType());
    //sinits.push_back(llvm::ConstantPointerNull::get(basept));
    Logger::println("generating base typeinfo");
    //sd->basetype = sd->basetype->merge();
    sd->memtype->getTypeInfo(NULL);        // generate vtinfo
    assert(sd->memtype->vtinfo);
    if (!sd->memtype->vtinfo->llvmValue)
        sd->memtype->vtinfo->toObjFile();
    assert(llvm::isa<llvm::Constant>(sd->memtype->vtinfo->llvmValue));
    llvm::Constant* castbase = llvm::cast<llvm::Constant>(sd->memtype->vtinfo->llvmValue);
    castbase = llvm::ConstantExpr::getBitCast(castbase, initZ->getOperand(1)->getType());
    sinits.push_back(castbase);

    // char[] name
    char *name = sd->toPrettyChars();
    sinits.push_back(DtoConstString(name));
    assert(sinits.back()->getType() == initZ->getOperand(2)->getType());

    // void[] init
    const llvm::PointerType* initpt = llvm::PointerType::get(llvm::Type::Int8Ty);
    if (tinfo->isZeroInit() || !sd->defaultval) // 0 initializer, or the same as the base type
    {
        sinits.push_back(DtoConstSlice(DtoConstSize_t(0), llvm::ConstantPointerNull::get(initpt)));
    }
    else
    {
        const llvm::Type* memty = DtoType(sd->memtype);
        llvm::Constant* ci = llvm::ConstantInt::get(memty, sd->defaultval, !sd->memtype->isunsigned());
        std::string ciname(sd->mangle());
        ciname.append("__init");
        llvm::GlobalVariable* civar = new llvm::GlobalVariable(memty,true,llvm::GlobalValue::InternalLinkage,ci,ciname,gIR->module);
        llvm::Constant* cicast = llvm::ConstantExpr::getBitCast(civar, initpt);
        size_t cisize = gTargetData->getTypeSize(memty);
        sinits.push_back(DtoConstSlice(DtoConstSize_t(cisize), cicast));
    }

    // create the symbol
    llvm::Constant* tiInit = llvm::ConstantStruct::get(stype, sinits);
    llvm::GlobalVariable* gvar = new llvm::GlobalVariable(stype,true,llvm::GlobalValue::WeakLinkage,tiInit,toChars(),gIR->module);

    llvmValue = gvar;
}

/* ========================================================================= */

static llvm::Constant* LLVM_D_Create_TypeInfoBase(Type* basetype, TypeInfoDeclaration* tid, ClassDeclaration* cd)
{
    ClassDeclaration* base = cd;
    base->toObjFile();

    llvm::Constant* initZ = base->llvmInitZ;
    assert(initZ);
    const llvm::StructType* stype = llvm::cast<llvm::StructType>(initZ->getType());

    std::vector<llvm::Constant*> sinits;
    sinits.push_back(initZ->getOperand(0));

    // TypeInfo base
    Logger::println("generating base typeinfo");
    basetype->getTypeInfo(NULL);
    assert(basetype->vtinfo);
    if (!basetype->vtinfo->llvmValue)
        basetype->vtinfo->toObjFile();
    assert(llvm::isa<llvm::Constant>(basetype->vtinfo->llvmValue));
    llvm::Constant* castbase = llvm::cast<llvm::Constant>(basetype->vtinfo->llvmValue);
    castbase = llvm::ConstantExpr::getBitCast(castbase, initZ->getOperand(1)->getType());
    sinits.push_back(castbase);

    // create the symbol
    llvm::Constant* tiInit = llvm::ConstantStruct::get(stype, sinits);
    llvm::GlobalVariable* gvar = new llvm::GlobalVariable(stype,true,llvm::GlobalValue::WeakLinkage,tiInit,tid->toChars(),gIR->module);

    tid->llvmValue = gvar;
}

/* ========================================================================= */

void TypeInfoPointerDeclaration::toDt(dt_t **pdt)
{
    Logger::println("TypeInfoPointerDeclaration::toDt() %s", toChars());
    LOG_SCOPE;

    assert(tinfo->ty == Tpointer);
    TypePointer *tc = (TypePointer *)tinfo;

    LLVM_D_Create_TypeInfoBase(tc->next, this, Type::typeinfopointer);
}

/* ========================================================================= */

void TypeInfoArrayDeclaration::toDt(dt_t **pdt)
{
    Logger::println("TypeInfoArrayDeclaration::toDt() %s", toChars());
    LOG_SCOPE;

    assert(tinfo->ty == Tarray);
    TypeDArray *tc = (TypeDArray *)tinfo;

    LLVM_D_Create_TypeInfoBase(tc->next, this, Type::typeinfoarray);
}

/* ========================================================================= */

void TypeInfoStaticArrayDeclaration::toDt(dt_t **pdt)
{
    assert(0 && "TypeInfoStaticArrayDeclaration");

    /*
    //printf("TypeInfoStaticArrayDeclaration::toDt()\n");
    dtxoff(pdt, Type::typeinfostaticarray->toVtblSymbol(), 0, TYnptr); // vtbl for TypeInfo_StaticArray
    dtdword(pdt, 0);                // monitor

    assert(tinfo->ty == Tsarray);

    TypeSArray *tc = (TypeSArray *)tinfo;

    tc->next->getTypeInfo(NULL);
    dtxoff(pdt, tc->next->vtinfo->toSymbol(), 0, TYnptr); // TypeInfo for array of type

    dtdword(pdt, tc->dim->toInteger());     // length
    */
}

/* ========================================================================= */

void TypeInfoAssociativeArrayDeclaration::toDt(dt_t **pdt)
{
    assert(0 && "TypeInfoAssociativeArrayDeclaration");

    /*
    //printf("TypeInfoAssociativeArrayDeclaration::toDt()\n");
    dtxoff(pdt, Type::typeinfoassociativearray->toVtblSymbol(), 0, TYnptr); // vtbl for TypeInfo_AssociativeArray
    dtdword(pdt, 0);                // monitor

    assert(tinfo->ty == Taarray);

    TypeAArray *tc = (TypeAArray *)tinfo;

    tc->next->getTypeInfo(NULL);
    dtxoff(pdt, tc->next->vtinfo->toSymbol(), 0, TYnptr); // TypeInfo for array of type

    tc->index->getTypeInfo(NULL);
    dtxoff(pdt, tc->index->vtinfo->toSymbol(), 0, TYnptr); // TypeInfo for array of type
    */
}

/* ========================================================================= */

void TypeInfoFunctionDeclaration::toDt(dt_t **pdt)
{
    Logger::println("TypeInfoFunctionDeclaration::toDt() %s", toChars());
    LOG_SCOPE;

    assert(tinfo->ty == Tfunction);
    TypeFunction *tc = (TypeFunction *)tinfo;

    LLVM_D_Create_TypeInfoBase(tc->next, this, Type::typeinfofunction);
}

/* ========================================================================= */

void TypeInfoDelegateDeclaration::toDt(dt_t **pdt)
{
    Logger::println("TypeInfoDelegateDeclaration::toDt() %s", toChars());
    LOG_SCOPE;

    assert(tinfo->ty == Tdelegate);
    TypeDelegate *tc = (TypeDelegate *)tinfo;

    LLVM_D_Create_TypeInfoBase(tc->next->next, this, Type::typeinfodelegate);
}

/* ========================================================================= */

void TypeInfoStructDeclaration::toDt(dt_t **pdt)
{
    Logger::println("TypeInfoStructDeclaration::toDt() %s", toChars());
    LOG_SCOPE;

    assert(tinfo->ty == Tstruct);
    TypeStruct *tc = (TypeStruct *)tinfo;
    StructDeclaration *sd = tc->sym;
    sd->toObjFile();

    ClassDeclaration* base = Type::typeinfostruct;
    base->toObjFile();

    const llvm::StructType* stype = llvm::cast<llvm::StructType>(base->llvmType);

    std::vector<llvm::Constant*> sinits;
    sinits.push_back(base->llvmVtbl);

    // char[] name
    char *name = sd->toPrettyChars();
    sinits.push_back(DtoConstString(name));
    Logger::println("************** A");
    assert(sinits.back()->getType() == stype->getElementType(1));

    // void[] init
    const llvm::PointerType* initpt = llvm::PointerType::get(llvm::Type::Int8Ty);
    if (sd->zeroInit) // 0 initializer, or the same as the base type
    {
        sinits.push_back(DtoConstSlice(DtoConstSize_t(0), llvm::ConstantPointerNull::get(initpt)));
    }
    else
    {
        assert(sd->llvmInitZ);
        size_t cisize = gTargetData->getTypeSize(tc->llvmType);
        llvm::Constant* cicast = llvm::ConstantExpr::getBitCast(tc->llvmInit, initpt);
        sinits.push_back(DtoConstSlice(DtoConstSize_t(cisize), cicast));
    }

    // toX functions ground work
    FuncDeclaration *fd;
    FuncDeclaration *fdx;
    TypeFunction *tf;
    Type *ta;
    Dsymbol *s;

    static TypeFunction *tftohash;
    static TypeFunction *tftostring;

    if (!tftohash)
    {
    Scope sc;

    tftohash = new TypeFunction(NULL, Type::thash_t, 0, LINKd);
    tftohash = (TypeFunction *)tftohash->semantic(0, &sc);

    tftostring = new TypeFunction(NULL, Type::tchar->arrayOf(), 0, LINKd);
    tftostring = (TypeFunction *)tftostring->semantic(0, &sc);
    }

    TypeFunction *tfeqptr;
    {
    Scope sc;
    Arguments *arguments = new Arguments;
    Argument *arg = new Argument(STCin, tc->pointerTo(), NULL, NULL);

    arguments->push(arg);
    tfeqptr = new TypeFunction(arguments, Type::tint32, 0, LINKd);
    tfeqptr = (TypeFunction *)tfeqptr->semantic(0, &sc);
    }

#if 0
    TypeFunction *tfeq;
    {
    Scope sc;
    Array *arguments = new Array;
    Argument *arg = new Argument(In, tc, NULL, NULL);

    arguments->push(arg);
    tfeq = new TypeFunction(arguments, Type::tint32, 0, LINKd);
    tfeq = (TypeFunction *)tfeq->semantic(0, &sc);
    }
#endif

    Logger::println("************** B");
    const llvm::PointerType* ptty = llvm::cast<llvm::PointerType>(stype->getElementType(3));

    s = search_function(sd, Id::tohash);
    fdx = s ? s->isFuncDeclaration() : NULL;
    if (fdx)
    {
        fd = fdx->overloadExactMatch(tftohash);
        if (fd) {
            assert(fd->llvmValue != 0);
            llvm::Constant* c = llvm::cast_or_null<llvm::Constant>(fd->llvmValue);
            assert(c);
            c = llvm::ConstantExpr::getBitCast(c, ptty);
            sinits.push_back(c);
        }
        else {
            //fdx->error("must be declared as extern (D) uint toHash()");
            sinits.push_back(llvm::ConstantPointerNull::get(ptty));
        }
    }
    else {
        sinits.push_back(llvm::ConstantPointerNull::get(ptty));
    }

    s = search_function(sd, Id::eq);
    fdx = s ? s->isFuncDeclaration() : NULL;
    for (int i = 0; i < 2; i++)
    {
        Logger::println("************** C %d", i);
        ptty = llvm::cast<llvm::PointerType>(stype->getElementType(4+i));
        if (fdx)
        {
            fd = fdx->overloadExactMatch(tfeqptr);
            if (fd) {
                assert(fd->llvmValue != 0);
                llvm::Constant* c = llvm::cast_or_null<llvm::Constant>(fd->llvmValue);
                assert(c);
                c = llvm::ConstantExpr::getBitCast(c, ptty);
                sinits.push_back(c);
            }
            else {
                //fdx->error("must be declared as extern (D) int %s(%s*)", fdx->toChars(), sd->toChars());
                sinits.push_back(llvm::ConstantPointerNull::get(ptty));
            }
        }
        else {
            sinits.push_back(llvm::ConstantPointerNull::get(ptty));
        }

        s = search_function(sd, Id::cmp);
        fdx = s ? s->isFuncDeclaration() : NULL;
    }

    Logger::println("************** D");
    ptty = llvm::cast<llvm::PointerType>(stype->getElementType(6));
    s = search_function(sd, Id::tostring);
    fdx = s ? s->isFuncDeclaration() : NULL;
    if (fdx)
    {
        fd = fdx->overloadExactMatch(tftostring);
        if (fd) {
            assert(fd->llvmValue != 0);
            llvm::Constant* c = llvm::cast_or_null<llvm::Constant>(fd->llvmValue);
            assert(c);
            c = llvm::ConstantExpr::getBitCast(c, ptty);
            sinits.push_back(c);
        }
        else {
            //fdx->error("must be declared as extern (D) char[] toString()");
            sinits.push_back(llvm::ConstantPointerNull::get(ptty));
        }
    }
    else {
        sinits.push_back(llvm::ConstantPointerNull::get(ptty));
    }

    // uint m_flags;
    sinits.push_back(DtoConstUint(tc->hasPointers()));

    // create the symbol
    llvm::Constant* tiInit = llvm::ConstantStruct::get(stype, sinits);
    llvm::GlobalVariable* gvar = new llvm::GlobalVariable(stype,true,llvm::GlobalValue::WeakLinkage,tiInit,toChars(),gIR->module);

    llvmValue = gvar;
}

/* ========================================================================= */

void TypeInfoClassDeclaration::toDt(dt_t **pdt)
{
    assert(0 && "TypeInfoClassDeclaration");

    /*
    //printf("TypeInfoClassDeclaration::toDt() %s\n", tinfo->toChars());
    dtxoff(pdt, Type::typeinfoclass->toVtblSymbol(), 0, TYnptr); // vtbl for TypeInfoClass
    dtdword(pdt, 0);                // monitor

    assert(tinfo->ty == Tclass);

    TypeClass *tc = (TypeClass *)tinfo;
    Symbol *s;

    if (!tc->sym->vclassinfo)
    tc->sym->vclassinfo = new ClassInfoDeclaration(tc->sym);
    s = tc->sym->vclassinfo->toSymbol();
    dtxoff(pdt, s, 0, TYnptr);      // ClassInfo for tinfo
    */
}

/* ========================================================================= */

void TypeInfoInterfaceDeclaration::toDt(dt_t **pdt)
{
    assert(0 && "TypeInfoInterfaceDeclaration");

    /*
    //printf("TypeInfoInterfaceDeclaration::toDt() %s\n", tinfo->toChars());
    dtxoff(pdt, Type::typeinfointerface->toVtblSymbol(), 0, TYnptr); // vtbl for TypeInfoInterface
    dtdword(pdt, 0);                // monitor

    assert(tinfo->ty == Tclass);

    TypeClass *tc = (TypeClass *)tinfo;
    Symbol *s;

    if (!tc->sym->vclassinfo)
    tc->sym->vclassinfo = new ClassInfoDeclaration(tc->sym);
    s = tc->sym->vclassinfo->toSymbol();
    dtxoff(pdt, s, 0, TYnptr);      // ClassInfo for tinfo
    */
}

/* ========================================================================= */

void TypeInfoTupleDeclaration::toDt(dt_t **pdt)
{
    assert(0 && "TypeInfoTupleDeclaration");

    /*
    //printf("TypeInfoTupleDeclaration::toDt() %s\n", tinfo->toChars());
    dtxoff(pdt, Type::typeinfotypelist->toVtblSymbol(), 0, TYnptr); // vtbl for TypeInfoInterface
    dtdword(pdt, 0);                // monitor

    assert(tinfo->ty == Ttuple);

    TypeTuple *tu = (TypeTuple *)tinfo;

    size_t dim = tu->arguments->dim;
    dtdword(pdt, dim);              // elements.length

    dt_t *d = NULL;
    for (size_t i = 0; i < dim; i++)
    {   Argument *arg = (Argument *)tu->arguments->data[i];
    Expression *e = arg->type->getTypeInfo(NULL);
    e = e->optimize(WANTvalue);
    e->toDt(&d);
    }

    Symbol *s;
    s = static_sym();
    s->Sdt = d;
    outdata(s);

    dtxoff(pdt, s, 0, TYnptr);          // elements.ptr
    */
}

/* ========================================================================= */
/* ========================================================================= */
/*                           CLASS INFO STUFF                                */
/* ========================================================================= */
/* ========================================================================= */

void DtoClassInfo(ClassDeclaration* cd)
{
//     The layout is:
//        {
//         void **vptr;
//         monitor_t monitor;
//         byte[] initializer;     // static initialization data
//         char[] name;        // class name
//         void *[] vtbl;
//         Interface[] interfaces;
//         ClassInfo *base;        // base class
//         void *destructor;
//         void *invariant;        // class invariant
//         uint flags;
//         void *deallocator;
//         OffsetTypeInfo[] offTi;
//         void *defaultConstructor;
//        }

    // holds the list of initializers for llvm
    std::vector<llvm::Constant*> inits;

    ClassDeclaration* cinfo = ClassDeclaration::classinfo;
    assert(cinfo);
    Logger::println("cinfo toObj");
    cinfo->toObjFile();
    
    Logger::println("cinfo toObj done");
    assert(cinfo->type->ty == Tclass);
    TypeClass* tc = (TypeClass*)cinfo->type;
    //assert(tc->llvmInit);
    //assert(cinfo->llvmInitZ);

    cinfo = ClassDeclaration::classinfo;
    assert(cinfo->llvmInitZ);

    /*
    llvm::Constant* c;

    // own vtable
    c = cinfo->llvmInitZ->getOperand(0);
    assert(c);
    inits.push_back(c);

    // monitor
    // TODO no monitors yet

    // initializer
    c = cinfo->llvmInitZ->getOperand(1);
    inits.push_back(c);

    // class name
    // from dmd
    char *name = cd->ident->toChars();
    size_t namelen = strlen(name);
    if (!(namelen > 9 && memcmp(name, "TypeInfo_", 9) == 0))
    {
        name = cd->toPrettyChars();
        namelen = strlen(name);
    }
    c = DtoConstString(name);
    inits.push_back(c);

    // vtbl array
    c = cinfo->llvmInitZ->getOperand(3);
    inits.push_back(c);

    // interfaces array
    c = cinfo->llvmInitZ->getOperand(4);
    inits.push_back(c);

    // base classinfo
    c = cinfo->llvmInitZ->getOperand(5);
    inits.push_back(c);

    // destructor
    c = cinfo->llvmInitZ->getOperand(5);
    inits.push_back(c);

    // invariant
    c = cinfo->llvmInitZ->getOperand(6);
    inits.push_back(c);

    // flags
    c = cinfo->llvmInitZ->getOperand(7);
    inits.push_back(c);

    // allocator
    c = cinfo->llvmInitZ->getOperand(8);
    inits.push_back(c);

    // offset typeinfo
    c = cinfo->llvmInitZ->getOperand(9);
    inits.push_back(c);

    // default constructor
    c = cinfo->llvmInitZ->getOperand(10);
    inits.push_back(c);

    // build the initializer
    const llvm::StructType* st = llvm::cast<llvm::StructType>(cinfo->llvmInitZ->getType());
    llvm::Constant* finalinit = llvm::ConstantStruct::get(st, inits);
    Logger::cout() << "built the classinfo initializer:\n" << *finalinit <<'\n';

    assert(0);
    */
}













