
// Copyright (c) 1999-2004 by Digital Mars
// All Rights Reserved
// written by Walter Bright
// www.digitalmars.com
// License for redistribution is by either the Artistic License
// in artistic.txt, or the GNU General Public License in gnu.txt.
// See the included readme.txt for details.

#include <cstddef>
#include <iostream>
#include <fstream>

#include "gen/llvm.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetMachineRegistry.h"

#include "mars.h"
#include "module.h"
#include "mtype.h"
#include "declaration.h"
#include "statement.h"
#include "enum.h"
#include "aggregate.h"
#include "init.h"
#include "attrib.h"
#include "id.h"
#include "import.h"
#include "template.h"
#include "scope.h"

#include "gen/irstate.h"
#include "gen/elem.h"
#include "gen/logger.h"
#include "gen/tollvm.h"
#include "gen/arrays.h"
#include "gen/structs.h"
#include "gen/todebug.h"
#include "gen/runtime.h"

//////////////////////////////////////////////////////////////////////////////////////////

void
Module::genobjfile()
{
    Logger::cout() << "Generating module: " << (md ? md->toChars() : toChars()) << '\n';
    LOG_SCOPE;

    // start by deleting the old object file
    deleteObjFile();

    // create a new ir state
    IRState ir;
    gIR = &ir;
    ir.dmodule = this;

    // name the module
    std::string mname(toChars());
    if (md != 0)
        mname = md->toChars();
    ir.module = new llvm::Module(mname);

    // set target stuff
    std::string target_triple(global.params.tt_arch);
    target_triple.append(global.params.tt_os);
    ir.module->setTargetTriple(target_triple);
    ir.module->setDataLayout(global.params.data_layout);

    // heavily inspired by tools/llc/llc.cpp:200-230
    const llvm::TargetMachineRegistry::Entry* targetEntry;
    std::string targetError;
    targetEntry = llvm::TargetMachineRegistry::getClosestStaticTargetForModule(*ir.module, targetError);
    assert(targetEntry && "Failed to find a static target for module");
    std::auto_ptr<llvm::TargetMachine> targetPtr(targetEntry->CtorFn(*ir.module, "")); // TODO: replace "" with features
    assert(targetPtr.get() && "Could not allocate target machine!");
    llvm::TargetMachine &targetMachine = *targetPtr.get();
    gTargetData = targetMachine.getTargetData();

    // debug info
    if (global.params.symdebug) {
        RegisterDwarfSymbols(ir.module);
        ir.dwarfCompileUnit = DtoDwarfCompileUnit(this);
    }

    // process module members
    for (int k=0; k < members->dim; k++) {
        Dsymbol* dsym = (Dsymbol*)(members->data[k]);
        assert(dsym);
        dsym->toObjFile();
    }

    // generate ModuleInfo
    genmoduleinfo();

    gTargetData = 0;

    // emit the llvm main function if necessary
    if (ir.emitMain) {
        DtoMain();
    }

    // verify the llvm
    if (!global.params.novalidate) {
        std::string verifyErr;
        Logger::println("Verifying module...");
        if (llvm::verifyModule(*ir.module,llvm::ReturnStatusAction,&verifyErr))
        {
            error("%s", verifyErr.c_str());
            fatal();
        }
        else {
            Logger::println("Verification passed!");
        }
    }

    // run passes
    // TODO

    // write bytecode
    {
        Logger::println("Writing LLVM bitcode\n");
        std::ofstream bos(bcfile->name->toChars(), std::ios::binary);
        llvm::WriteBitcodeToFile(ir.module, bos);
    }

    // disassemble ?
    if (global.params.disassemble) {
        Logger::println("Writing LLVM asm to: %s\n", llfile->name->toChars());
        std::ofstream aos(llfile->name->toChars());
        ir.module->print(aos);
    }

    delete ir.module;
    gIR = NULL;
}

/* ================================================================== */

// Put out instance of ModuleInfo for this Module

void Module::genmoduleinfo()
{
//      The layout is:
//        {
//         void **vptr;
//         monitor_t monitor;
//         char[] name;        // class name
//         ModuleInfo importedModules[];
//         ClassInfo localClasses[];
//         uint flags;         // initialization state
//         void *ctor;
//         void *dtor;
//         void *unitTest;
//        }

    if (moduleinfo) {
        Logger::println("moduleinfo");
    }
    if (vmoduleinfo) {
        Logger::println("vmoduleinfo");
    }
    if (needModuleInfo()) {
        Logger::println("**** ATTENTION: module info is needed but skipped");
    }


    /*
    Symbol *msym = toSymbol();
    unsigned offset;
    unsigned sizeof_ModuleInfo = 12 * PTRSIZE;

    //////////////////////////////////////////////

    csym->Sclass = SCglobal;
    csym->Sfl = FLdata;

//      The layout is:
//        {
//         void **vptr;
//         monitor_t monitor;
//         char[] name;        // class name
//         ModuleInfo importedModules[];
//         ClassInfo localClasses[];
//         uint flags;         // initialization state
//         void *ctor;
//         void *dtor;
//         void *unitTest;
//        }
    dt_t *dt = NULL;

    if (moduleinfo)
    dtxoff(&dt, moduleinfo->toVtblSymbol(), 0, TYnptr); // vtbl for ModuleInfo
    else
    dtdword(&dt, 0);        // BUG: should be an assert()
    dtdword(&dt, 0);            // monitor

    // name[]
    char *name = toPrettyChars();
    size_t namelen = strlen(name);
    dtdword(&dt, namelen);
    dtabytes(&dt, TYnptr, 0, namelen + 1, name);

    ClassDeclarations aclasses;
    int i;

    //printf("members->dim = %d\n", members->dim);
    for (i = 0; i < members->dim; i++)
    {
    Dsymbol *member;

    member = (Dsymbol *)members->data[i];
    //printf("\tmember '%s'\n", member->toChars());
    member->addLocalClass(&aclasses);
    }

    // importedModules[]
    int aimports_dim = aimports.dim;
    for (i = 0; i < aimports.dim; i++)
    {   Module *m = (Module *)aimports.data[i];
    if (!m->needModuleInfo())
        aimports_dim--;
    }
    dtdword(&dt, aimports_dim);
    if (aimports.dim)
    dtxoff(&dt, csym, sizeof_ModuleInfo, TYnptr);
    else
    dtdword(&dt, 0);

    // localClasses[]
    dtdword(&dt, aclasses.dim);
    if (aclasses.dim)
    dtxoff(&dt, csym, sizeof_ModuleInfo + aimports_dim * PTRSIZE, TYnptr);
    else
    dtdword(&dt, 0);

    if (needmoduleinfo)
    dtdword(&dt, 0);        // flags (4 means MIstandalone)
    else
    dtdword(&dt, 4);        // flags (4 means MIstandalone)

    if (sctor)
    dtxoff(&dt, sctor, 0, TYnptr);
    else
    dtdword(&dt, 0);

    if (sdtor)
    dtxoff(&dt, sdtor, 0, TYnptr);
    else
    dtdword(&dt, 0);

    if (stest)
    dtxoff(&dt, stest, 0, TYnptr);
    else
    dtdword(&dt, 0);

    //////////////////////////////////////////////

    for (i = 0; i < aimports.dim; i++)
    {
    Module *m;

    m = (Module *)aimports.data[i];
    if (m->needModuleInfo())
    {   Symbol *s = m->toSymbol();
        s->Sflags |= SFLweak;
        dtxoff(&dt, s, 0, TYnptr);
    }
    }

    for (i = 0; i < aclasses.dim; i++)
    {
    ClassDeclaration *cd;

    cd = (ClassDeclaration *)aclasses.data[i];
    dtxoff(&dt, cd->toSymbol(), 0, TYnptr);
    }

    csym->Sdt = dt;
#if ELFOBJ
    // Cannot be CONST because the startup code sets flag bits in it
    csym->Sseg = DATA;
#endif
    outdata(csym);

    //////////////////////////////////////////////

    obj_moduleinfo(msym);
    */
}

/* ================================================================== */

void Dsymbol::toObjFile()
{
    Logger::println("Ignoring Dsymbol::toObjFile for %s", toChars());
}

/* ================================================================== */

void Declaration::toObjFile()
{
    Logger::println("Ignoring Declaration::toObjFile for %s", toChars());
}

/* ================================================================== */

void InterfaceDeclaration::toObjFile()
{
    Logger::println("Ignoring InterfaceDeclaration::toObjFile for %s", toChars());
}

/* ================================================================== */

void StructDeclaration::toObjFile()
{
    TypeStruct* ts = (TypeStruct*)DtoDType(type);
    if (llvmType != 0)
        return;

    static int sdi = 0;
    Logger::print("StructDeclaration::toObjFile(%d): %s\n", sdi++, toChars());
    LOG_SCOPE;

    gIR->structs.push_back(IRStruct(ts));

    for (int k=0; k < members->dim; k++) {
        Dsymbol* dsym = (Dsymbol*)(members->data[k]);
        dsym->toObjFile();
    }

    Logger::println("doing struct fields");

    llvm::StructType* structtype = 0;
    std::vector<llvm::Constant*> fieldinits;

    if (gIR->topstruct().offsets.empty())
    {
        std::vector<const llvm::Type*> fieldtypes;
        Logger::println("has no fields");
        fieldtypes.push_back(llvm::Type::Int8Ty);
        fieldinits.push_back(llvm::ConstantInt::get(llvm::Type::Int8Ty, 0, false));
        structtype = llvm::StructType::get(fieldtypes);
    }
    else
    {
        Logger::println("has fields");
        std::vector<const llvm::Type*> fieldtypes;
        unsigned prevsize = (unsigned)-1;
        unsigned lastoffset = (unsigned)-1;
        const llvm::Type* fieldtype = NULL;
        llvm::Constant* fieldinit = NULL;
        size_t fieldpad = 0;
        int idx = 0;
        for (IRStruct::OffsetMap::iterator i=gIR->topstruct().offsets.begin(); i!=gIR->topstruct().offsets.end(); ++i) {
            // first iteration
            if (lastoffset == (unsigned)-1) {
                lastoffset = i->first;
                assert(lastoffset == 0);
                fieldtype = DtoType(i->second.var->type);
                fieldinit = i->second.init;
                prevsize = gTargetData->getTypeSize(fieldtype);
                i->second.var->llvmFieldIndex = idx;
            }
            // colliding offset?
            else if (lastoffset == i->first) {
                const llvm::Type* t = DtoType(i->second.var->type);
                size_t s = gTargetData->getTypeSize(t);
                if (s > prevsize) {
                    fieldpad += s - prevsize;
                    prevsize = s;
                }
                llvmHasUnions = true;
                i->second.var->llvmFieldIndex = idx;
            }
            // intersecting offset?
            else if (i->first < (lastoffset + prevsize)) {
                const llvm::Type* t = DtoType(i->second.var->type);
                size_t s = gTargetData->getTypeSize(t);
                assert((i->first + s) <= (lastoffset + prevsize)); // this holds because all types are aligned to their size
                llvmHasUnions = true;
                i->second.var->llvmFieldIndex = idx;
                i->second.var->llvmFieldIndexOffset = (i->first - lastoffset) / s;
            }
            // fresh offset
            else {
                // commit the field
                fieldtypes.push_back(fieldtype);
                fieldinits.push_back(fieldinit);
                if (fieldpad) {
                    // match up with below
                    std::vector<llvm::Constant*> vals(fieldpad, llvm::ConstantInt::get(llvm::Type::Int8Ty, 0, false));
                    llvm::Constant* c = llvm::ConstantArray::get(llvm::ArrayType::get(llvm::Type::Int8Ty, fieldpad), vals);
                    fieldtypes.push_back(c->getType());
                    fieldinits.push_back(c);
                    idx++;
                }

                idx++;

                // start new
                lastoffset = i->first;
                fieldtype = DtoType(i->second.var->type);
                fieldinit = i->second.init;
                prevsize = gTargetData->getTypeSize(fieldtype);
                i->second.var->llvmFieldIndex = idx;
                fieldpad = 0;
            }
        }
        fieldtypes.push_back(fieldtype);
        fieldinits.push_back(fieldinit);
        if (fieldpad) {
            // match up with above
            std::vector<llvm::Constant*> vals(fieldpad, llvm::ConstantInt::get(llvm::Type::Int8Ty, 0, false));
            llvm::Constant* c = llvm::ConstantArray::get(llvm::ArrayType::get(llvm::Type::Int8Ty, fieldpad), vals);
            fieldtypes.push_back(c->getType());
            fieldinits.push_back(c);
        }

        Logger::println("creating struct type");
        structtype = llvm::StructType::get(fieldtypes);
    }

    // refine abstract types for stuff like: struct S{S* next;}
    if (gIR->topstruct().recty != 0)
    {
        llvm::PATypeHolder& pa = gIR->topstruct().recty;
        llvm::cast<llvm::OpaqueType>(pa.get())->refineAbstractTypeTo(structtype);
        structtype = llvm::cast<llvm::StructType>(pa.get());
    }

    ts->llvmType = structtype;
    llvmType = structtype;

    if (parent->isModule()) {
        gIR->module->addTypeName(mangle(),ts->llvmType);
    }

    llvmUnion = new DUnion; // uses gIR->topstruct()

    // generate static data
    llvm::GlobalValue::LinkageTypes _linkage = llvm::GlobalValue::ExternalLinkage;
    llvm::Constant* _init = 0;

    // always generate the constant initalizer
    if (!zeroInit) {
        Logger::println("Not zero initialized");
        //assert(tk == gIR->gIR->topstruct()().size());
        #ifndef LLVMD_NO_LOGGER
        Logger::cout() << "struct type: " << *structtype << '\n';
        for (size_t k=0; k<fieldinits.size(); ++k) {
            Logger::cout() << "Type:" << '\n';
            Logger::cout() << *fieldinits[k]->getType() << '\n';
            Logger::cout() << "Value:" << '\n';
            Logger::cout() << *fieldinits[k] << '\n';
        }
        Logger::cout() << "Initializer printed" << '\n';
        #endif
        llvmInitZ = llvm::ConstantStruct::get(structtype,fieldinits);
    }
    else {
        Logger::println("Zero initialized");
        llvmInitZ = llvm::ConstantAggregateZero::get(structtype);
    }

    // only provide the constant initializer for the defining module
    if (getModule() == gIR->dmodule)
    {
        _init = llvmInitZ;
    }

    std::string initname("_D");
    initname.append(mangle());
    initname.append("6__initZ");
    llvm::GlobalVariable* initvar = new llvm::GlobalVariable(ts->llvmType, true, _linkage, _init, initname, gIR->module);
    ts->llvmInit = initvar;

    // generate member function definitions
    gIR->topstruct().queueFuncs = false;
    IRStruct::FuncDeclVector& mfs = gIR->topstruct().funcs;
    size_t n = mfs.size();
    for (size_t i=0; i<n; ++i) {
        mfs[i]->toObjFile();
    }

    llvmDModule = gIR->dmodule;

    gIR->structs.pop_back();

    // generate typeinfo
    if (getModule() == gIR->dmodule && llvmInternal != LLVMnotypeinfo)
        type->getTypeInfo(NULL);
}

/* ================================================================== */

static unsigned LLVM_ClassOffsetToIndex(ClassDeclaration* cd, unsigned os, unsigned& idx)
{
    // start at the bottom of the inheritance chain
    if (cd->baseClass != 0) {
        unsigned o = LLVM_ClassOffsetToIndex(cd->baseClass, os, idx);
        if (o != (unsigned)-1)
            return o;
    }

    // check this class
    unsigned i;
    for (i=0; i<cd->fields.dim; ++i) {
        VarDeclaration* vd = (VarDeclaration*)cd->fields.data[i];
        if (os == vd->offset)
            return i+idx;
    }
    idx += i;

    return (unsigned)-1;
}

void ClassDeclaration::offsetToIndex(Type* t, unsigned os, std::vector<unsigned>& result)
{
    unsigned idx = 0;
    unsigned r = LLVM_ClassOffsetToIndex(this, os, idx);
    assert(r != (unsigned)-1 && "Offset not found in any aggregate field");
    result.push_back(r+1); // vtable is 0
}

/* ================================================================== */

static void LLVM_AddBaseClassData(BaseClasses* bcs)
{
    // add base class data members first
    for (int j=0; j<bcs->dim; j++)
    {
        BaseClass* bc = (BaseClass*)(bcs->data[j]);
        assert(bc);
        Logger::println("Adding base class members of %s", bc->base->toChars());
        LOG_SCOPE;

        LLVM_AddBaseClassData(&bc->base->baseclasses);
        for (int k=0; k < bc->base->members->dim; k++) {
            Dsymbol* dsym = (Dsymbol*)(bc->base->members->data[k]);
            if (dsym->isVarDeclaration())
            {
                dsym->toObjFile();
            }
        }
    }
}

void ClassDeclaration::toObjFile()
{
    TypeClass* ts = (TypeClass*)DtoDType(type);
    if (ts->llvmType != 0 || llvmInProgress)
        return;

    llvmInProgress = true;

    static int fdi = 0;
    Logger::print("ClassDeclaration::toObjFile(%d): %s\n", fdi++, toChars());
    LOG_SCOPE;

    gIR->structs.push_back(IRStruct(ts));
    gIR->classes.push_back(this);

    // add vtable
    llvm::PATypeHolder pa = llvm::OpaqueType::get();
    const llvm::Type* vtabty = llvm::PointerType::get(pa);

    std::vector<const llvm::Type*> fieldtypes;
    fieldtypes.push_back(vtabty);

    std::vector<llvm::Constant*> fieldinits;
    fieldinits.push_back(0);

    // base classes first
    LLVM_AddBaseClassData(&baseclasses);

    // then add own members
    for (int k=0; k < members->dim; k++) {
        Dsymbol* dsym = (Dsymbol*)(members->data[k]);
        dsym->toObjFile();
    }

    // fill out fieldtypes/inits
    for (IRStruct::OffsetMap::iterator i=gIR->topstruct().offsets.begin(); i!=gIR->topstruct().offsets.end(); ++i) {
        fieldtypes.push_back(DtoType(i->second.var->type));
        fieldinits.push_back(i->second.init);
    }

    llvm::StructType* structtype = llvm::StructType::get(fieldtypes);
    // refine abstract types for stuff like: class C {C next;}
    if (gIR->topstruct().recty != 0)
    {
        llvm::PATypeHolder& pa = gIR->topstruct().recty;
        llvm::cast<llvm::OpaqueType>(pa.get())->refineAbstractTypeTo(structtype);
        structtype = llvm::cast<llvm::StructType>(pa.get());
    }

    ts->llvmType = structtype;
    llvmType = structtype;

    bool needs_definition = false;
    if (parent->isModule()) {
        gIR->module->addTypeName(mangle(),ts->llvmType);
        needs_definition = (getModule() == gIR->dmodule);
    }
    else {
        assert(0 && "class parent is not a module");
    }

    // generate vtable
    llvm::GlobalVariable* svtblVar = 0;
    std::vector<llvm::Constant*> sinits;
    std::vector<const llvm::Type*> sinits_ty;
    sinits.reserve(vtbl.dim);
    sinits_ty.reserve(vtbl.dim);

    for (int k=0; k < vtbl.dim; k++)
    {
        Dsymbol* dsym = (Dsymbol*)vtbl.data[k];
        assert(dsym);
        //Logger::cout() << "vtblsym: " << dsym->toChars() << '\n';

        if (FuncDeclaration* fd = dsym->isFuncDeclaration()) {
            fd->toObjFile();
            assert(fd->llvmValue);
            llvm::Constant* c = llvm::cast<llvm::Constant>(fd->llvmValue);
            sinits.push_back(c);
            sinits_ty.push_back(c->getType());
        }
        else if (ClassDeclaration* cd = dsym->isClassDeclaration()) {
            const llvm::Type* cty = llvm::PointerType::get(llvm::Type::Int8Ty);
            llvm::Constant* c = llvm::Constant::getNullValue(cty);
            sinits.push_back(c);
            sinits_ty.push_back(cty);
        }
        else
        assert(0);
    }

    const llvm::StructType* svtbl_ty = 0;
    if (!sinits.empty())
    {
        llvm::GlobalValue::LinkageTypes _linkage = llvm::GlobalValue::ExternalLinkage;

        std::string varname("_D");
        varname.append(mangle());
        varname.append("6__vtblZ");

        std::string styname(mangle());
        styname.append("__vtblTy");

        svtbl_ty = llvm::StructType::get(sinits_ty);
        gIR->module->addTypeName(styname, svtbl_ty);
        svtblVar = new llvm::GlobalVariable(svtbl_ty, true, _linkage, 0, varname, gIR->module);

        llvmConstVtbl = llvm::cast<llvm::ConstantStruct>(llvm::ConstantStruct::get(svtbl_ty, sinits));
        if (needs_definition)
            svtblVar->setInitializer(llvmConstVtbl);
        llvmVtbl = svtblVar;
    }

    ////////////////////////////////////////////////////////////////////////////////

    // refine for final vtable type
    llvm::cast<llvm::OpaqueType>(pa.get())->refineAbstractTypeTo(svtbl_ty);
    svtbl_ty = llvm::cast<llvm::StructType>(pa.get());
    structtype = llvm::cast<llvm::StructType>(gIR->topstruct().recty.get());
    ts->llvmType = structtype;
    llvmType = structtype;

    // generate initializer
    llvm::GlobalValue::LinkageTypes _linkage = llvm::GlobalValue::ExternalLinkage;
    llvm::Constant* _init = 0;

    // first field is always the vtable
    assert(svtblVar != 0);
    fieldinits[0] = svtblVar;

    llvmInitZ = _init = llvm::ConstantStruct::get(structtype,fieldinits);
    assert(_init);

    std::string initname("_D");
    initname.append(mangle());
    initname.append("6__initZ");
    //Logger::cout() << *_init << '\n';
    llvm::GlobalVariable* initvar = new llvm::GlobalVariable(ts->llvmType, true, _linkage, NULL, initname, gIR->module);
    ts->llvmInit = initvar;

    if (needs_definition) {
        initvar->setInitializer(_init);
        // generate member functions
        gIR->topstruct().queueFuncs = false;
        IRStruct::FuncDeclVector& mfs = gIR->topstruct().funcs;
        size_t n = mfs.size();
        for (size_t i=0; i<n; ++i) {
            mfs[i]->toObjFile();
        }
    }

    gIR->classes.pop_back();
    gIR->structs.pop_back();

    llvmInProgress = false;

    //     if (ClassDeclaration::classinfo != this)
    //         DtoClassInfo(this);
}

/******************************************
 * Get offset of base class's vtbl[] initializer from start of csym.
 * Returns ~0 if not this csym.
 */

unsigned ClassDeclaration::baseVtblOffset(BaseClass *bc)
{
  return ~0;
}

/* ================================================================== */

void VarDeclaration::toObjFile()
{
    Logger::print("VarDeclaration::toObjFile(): %s | %s\n", toChars(), type->toChars());
    LOG_SCOPE;
    llvm::Module* M = gIR->module;

    if (aliassym)
    {
        Logger::println("alias sym");
        toAlias()->toObjFile();
        return;
    }

    // global variable or magic
    if (isDataseg())
    {
        if (llvmTouched) return;
        else llvmTouched = true;

        bool _isconst = false;
        if (isConst() && (init && !init->isExpInitializer()))
            _isconst = true;

        llvm::GlobalValue::LinkageTypes _linkage;
        bool istempl = false;
        if ((storage_class & STCcomdat) || (parent && DtoIsTemplateInstance(parent))) {
            _linkage = llvm::GlobalValue::WeakLinkage;
            istempl = true;
        }
        else if (parent && parent->isFuncDeclaration())
            _linkage = llvm::GlobalValue::InternalLinkage;
        else
            _linkage = DtoLinkage(protection, storage_class);

        Type* t = DtoDType(type);

        const llvm::Type* _type = DtoType(t);
        assert(_type);

        llvm::Constant* _init = 0;
        bool _signed = !type->isunsigned();

        Logger::println("Creating global variable");
        std::string _name(mangle());

        bool emitRTstaticInit = false;

        if (!(storage_class & STCextern) && (getModule() == gIR->dmodule || istempl))
        {
            if (parent && parent->isFuncDeclaration() && init && init->isExpInitializer()) {
                _init = DtoConstInitializer(t, NULL);
                emitRTstaticInit = true;
            }
            else {
                _init = DtoConstInitializer(t, init);
            }

            //Logger::cout() << "initializer: " << *_init << '\n';
            if (_type != _init->getType()) {
                Logger::cout() << "got type '" << *_init->getType() << "' expected '" << *_type << "'\n";
                // zero initalizer
                if (_init->isNullValue())
                    _init = llvm::Constant::getNullValue(_type);
                // pointer to global constant (struct.init)
                else if (llvm::isa<llvm::GlobalVariable>(_init))
                {
                    assert(_init->getType()->getContainedType(0) == _type);
                    llvm::GlobalVariable* gv = llvm::cast<llvm::GlobalVariable>(_init);
                    assert(t->ty == Tstruct);
                    TypeStruct* ts = (TypeStruct*)t;
                    assert(ts->sym->llvmInitZ);
                    _init = ts->sym->llvmInitZ;
                }
                // array single value init
                else if (llvm::isa<llvm::ArrayType>(_type))
                {
                    _init = DtoConstStaticArray(_type, _init);
                }
                else {
                    Logger::cout() << "Unexpected initializer type: " << *_type << '\n';
                    //assert(0);
                }
            }
        }

        if (_init && _init->getType() != _type)
            _type = _init->getType();
        llvm::GlobalVariable* gvar = new llvm::GlobalVariable(_type,_isconst,_linkage,_init,_name,M);
        llvmValue = gvar;

        if (emitRTstaticInit)
            DtoLazyStaticInit(istempl, gvar, init, t);

        llvmDModule = gIR->dmodule;

        //if (storage_class & STCprivate)
        //    gvar->setVisibility(llvm::GlobalValue::ProtectedVisibility);
    }

    // inside aggregate declaration. declare a field.
    else
    {
        Logger::println("Aggregate var declaration: '%s' offset=%d", toChars(), offset);

        Type* t = DtoDType(type);
        const llvm::Type* _type = DtoType(t);

        llvm::Constant*_init = DtoConstInitializer(t, init);
        assert(_init);
        Logger::cout() << "field init is: " << *_init << " type should be " << *_type << '\n';
        if (_type != _init->getType())
        {
            if (t->ty == Tsarray)
            {
                const llvm::ArrayType* arrty = llvm::cast<llvm::ArrayType>(_type);
                uint64_t n = arrty->getNumElements();
                std::vector<llvm::Constant*> vals(n,_init);
                _init = llvm::ConstantArray::get(arrty, vals);
            }
            else if (t->ty == Tarray)
            {
                assert(llvm::isa<llvm::StructType>(_type));
                _init = llvm::ConstantAggregateZero::get(_type);
            }
            else if (t->ty == Tstruct)
            {
                const llvm::StructType* structty = llvm::cast<llvm::StructType>(_type);
                TypeStruct* ts = (TypeStruct*)t;
                assert(ts);
                assert(ts->sym);
                assert(ts->sym->llvmInitZ);
                _init = ts->sym->llvmInitZ;
            }
            else if (t->ty == Tclass)
            {
                _init = llvm::Constant::getNullValue(_type);
            }
            else {
                Logger::println("failed for type %s", type->toChars());
                assert(0);
            }
        }

        // add the field in the IRStruct
        gIR->topstruct().offsets.insert(std::make_pair(offset, IRStruct::Offset(this,_init)));
    }

    Logger::println("VarDeclaration::toObjFile is done");
}

/* ================================================================== */

void TypedefDeclaration::toObjFile()
{
    static int tdi = 0;
    Logger::print("TypedefDeclaration::toObjFile(%d): %s\n", tdi++, toChars());
    LOG_SCOPE;

    // generate typeinfo
    type->getTypeInfo(NULL);
}

/* ================================================================== */

void EnumDeclaration::toObjFile()
{
    Logger::println("Ignoring EnumDeclaration::toObjFile for %s", toChars());
}

/* ================================================================== */

void FuncDeclaration::toObjFile()
{
    if (llvmDModule) {
        assert(llvmValue != 0);
        return;
    }

    if (llvmRunTimeHack) {
        Logger::println("runtime hack func chars: %s", toChars());
        if (!llvmValue)
            llvmValue = LLVM_D_GetRuntimeFunction(gIR->module, toChars());
        return;
    }

    if (isUnitTestDeclaration()) {
        Logger::println("*** ATTENTION: ignoring unittest declaration: %s", toChars());
        return;
    }

    Type* t = DtoDType(type);
    TypeFunction* f = (TypeFunction*)t;

    bool declareOnly = false;
    if (parent)
    {
    if (TemplateInstance* tinst = parent->isTemplateInstance()) {
        TemplateDeclaration* tempdecl = tinst->tempdecl;
        if (tempdecl->llvmInternal == LLVMva_start)
        {
            Logger::println("magic va_start found");
            llvmInternal = LLVMva_start;
            declareOnly = true;
        }
        else if (tempdecl->llvmInternal == LLVMva_arg)
        {
            Logger::println("magic va_arg found");
            llvmInternal = LLVMva_arg;
            return;
        }
    }
    }

    llvm::Function* func = DtoDeclareFunction(this);

    if (declareOnly)
        return;

    if (!gIR->structs.empty() && gIR->topstruct().queueFuncs) {
        if (!llvmQueued) {
            Logger::println("queueing %s", toChars());
            gIR->topstruct().funcs.push_back(this);
            llvmQueued = true;
        }
        return; // we wait with the definition as they might invoke a virtual method and the vtable is not yet complete
    }

    // debug info
    if (global.params.symdebug) {
        llvmDwarfSubProgram = DtoDwarfSubProgram(this);
    }

    assert(f->llvmType);
    const llvm::FunctionType* functype = llvm::cast<llvm::FunctionType>(llvmValue->getType()->getContainedType(0));

    // template instances should have weak linkage
    if (parent && DtoIsTemplateInstance(parent)) {
        func->setLinkage(llvm::GlobalValue::WeakLinkage);
    }

    // only members of the current module maybe be defined
    if (getModule() == gIR->dmodule || DtoIsTemplateInstance(parent))
    {
        llvmDModule = gIR->dmodule;

        // handle static constructor / destructor
        if (isStaticCtorDeclaration() || isStaticDtorDeclaration()) {
            const llvm::ArrayType* sctor_type = llvm::ArrayType::get(llvm::PointerType::get(functype),1);
            //Logger::cout() << "static ctor type: " << *sctor_type << '\n';

            llvm::Constant* sctor_func = llvm::cast<llvm::Constant>(llvmValue);
            //Logger::cout() << "static ctor func: " << *sctor_func << '\n';

            llvm::Constant* sctor_init = llvm::ConstantArray::get(sctor_type,&sctor_func,1);

            //Logger::cout() << "static ctor init: " << *sctor_init << '\n';

            // output the llvm.global_ctors array
            const char* varname = isStaticCtorDeclaration() ? "_d_module_ctor_array" : "_d_module_dtor_array";
            llvm::GlobalVariable* sctor_arr = new llvm::GlobalVariable(sctor_type, false, llvm::GlobalValue::AppendingLinkage, sctor_init, varname, gIR->module);
        }

        // function definition
        if (fbody != 0)
        {
            gIR->functions.push_back(IRFunction(this));
            gIR->func().func = func;

            // first make absolutely sure the type is up to date
            f->llvmType = llvmValue->getType()->getContainedType(0);

            //Logger::cout() << "func type: " << *f->llvmType << '\n';

            // this handling
            if (f->llvmUsesThis) {
                Logger::println("uses this");
                if (f->llvmRetInPtr)
                    llvmThisVar = ++func->arg_begin();
                else
                    llvmThisVar = func->arg_begin();
                assert(llvmThisVar != 0);
            }

            if (isMain())
                gIR->emitMain = true;

            llvm::BasicBlock* beginbb = new llvm::BasicBlock("entry",func);
            llvm::BasicBlock* endbb = new llvm::BasicBlock("endentry",func);

            //assert(gIR->scopes.empty());
            gIR->scopes.push_back(IRScope(beginbb, endbb));

                // create alloca point
                f->llvmAllocaPoint = new llvm::BitCastInst(llvm::ConstantInt::get(llvm::Type::Int32Ty,0,false),llvm::Type::Int32Ty,"alloca point",gIR->scopebb());
                gIR->func().allocapoint = f->llvmAllocaPoint;

                // give arguments storage
                size_t n = Argument::dim(f->parameters);
                for (int i=0; i < n; ++i) {
                    Argument* arg = Argument::getNth(f->parameters, i);
                    if (arg && arg->vardecl) {
                        VarDeclaration* vd = arg->vardecl;
                        if (!vd->llvmNeedsStorage || vd->nestedref || vd->isRef() || vd->isOut() || DtoIsPassedByRef(vd->type))
                            continue;
                        llvm::Value* a = vd->llvmValue;
                        assert(a);
                        std::string s(a->getName());
                        Logger::println("giving argument '%s' storage", s.c_str());
                        s.append("_storage");
                        llvm::Value* v = new llvm::AllocaInst(a->getType(),s,f->llvmAllocaPoint);
                        gIR->ir->CreateStore(a,v);
                        vd->llvmValue = v;
                    }
                    else {
                        Logger::println("*** ATTENTION: some unknown argument: %s", arg ? arg->toChars() : 0);
                    }
                }

                // debug info
                if (global.params.symdebug) DtoDwarfFuncStart(this);

                llvm::Value* parentNested = NULL;
                if (FuncDeclaration* fd = toParent()->isFuncDeclaration()) {
                    parentNested = fd->llvmNested;
                }

                // construct nested variables struct
                if (!llvmNestedVars.empty() || parentNested) {
                    std::vector<const llvm::Type*> nestTypes;
                    int j = 0;
                    if (parentNested) {
                        nestTypes.push_back(parentNested->getType());
                        j++;
                    }
                    for (std::set<VarDeclaration*>::iterator i=llvmNestedVars.begin(); i!=llvmNestedVars.end(); ++i) {
                        VarDeclaration* vd = *i;
                        vd->llvmNestedIndex = j++;
                        if (vd->isParameter()) {
                            assert(vd->llvmValue);
                            nestTypes.push_back(vd->llvmValue->getType());
                        }
                        else {
                            nestTypes.push_back(DtoType(vd->type));
                        }
                    }
                    const llvm::StructType* nestSType = llvm::StructType::get(nestTypes);
                    Logger::cout() << "nested var struct has type:" << '\n' << *nestSType;
                    llvmNested = new llvm::AllocaInst(nestSType,"nestedvars",f->llvmAllocaPoint);
                    if (parentNested) {
                        assert(llvmThisVar);
                        llvm::Value* ptr = gIR->ir->CreateBitCast(llvmThisVar, parentNested->getType(), "tmp");
                        gIR->ir->CreateStore(ptr, DtoGEPi(llvmNested, 0,0, "tmp"));
                    }
                    for (std::set<VarDeclaration*>::iterator i=llvmNestedVars.begin(); i!=llvmNestedVars.end(); ++i) {
                        VarDeclaration* vd = *i;
                        if (vd->isParameter()) {
                            gIR->ir->CreateStore(vd->llvmValue, DtoGEPi(llvmNested, 0, vd->llvmNestedIndex, "tmp"));
                            vd->llvmValue = llvmNested;
                        }
                    }
                }

                // copy _argptr to a memory location
                if (f->linkage == LINKd && f->varargs == 1)
                {
                    llvm::Value* argptrmem = new llvm::AllocaInst(llvmArgPtr->getType(), "_argptrmem", gIR->topallocapoint());
                    new llvm::StoreInst(llvmArgPtr, argptrmem, gIR->scopebb());
                    llvmArgPtr = argptrmem;
                }

                // output function body
                fbody->toIR(gIR);

                // llvm requires all basic blocks to end with a TerminatorInst but DMD does not put a return statement
                // in automatically, so we do it here.
                if (!isMain()) {
                    if (!gIR->scopereturned()) {
                        // pass the previous block into this block
                        if (global.params.symdebug) DtoDwarfFuncEnd(this);
                        if (func->getReturnType() == llvm::Type::VoidTy) {
                            new llvm::ReturnInst(gIR->scopebb());
                        }
                        else {
                            new llvm::ReturnInst(llvm::UndefValue::get(func->getReturnType()), gIR->scopebb());
                        }
                    }
                }

                // erase alloca point
                f->llvmAllocaPoint->eraseFromParent();
                f->llvmAllocaPoint = 0;
                gIR->func().allocapoint = 0;

            gIR->scopes.pop_back();

            // get rid of the endentry block, it's never used
            assert(!func->getBasicBlockList().empty());
            func->getBasicBlockList().pop_back();

            // if the last block is empty now, it must be unreachable or it's a bug somewhere else
            // would be nice to figure out how to assert that this is correct
            llvm::BasicBlock* lastbb = &func->getBasicBlockList().back();
            if (lastbb->empty()) {
                if (lastbb->getNumUses() == 0)
                    lastbb->eraseFromParent();
                else {
                    new llvm::UnreachableInst(lastbb);
                    /*if (func->getReturnType() == llvm::Type::VoidTy) {
                        new llvm::ReturnInst(lastbb);
                    }
                    else {
                        new llvm::ReturnInst(llvm::UndefValue::get(func->getReturnType()), lastbb);
                    }*/
                }
            }

            gIR->functions.pop_back();
        }
    }
}