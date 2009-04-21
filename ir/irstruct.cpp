#include "gen/llvm.h"

#include "mtype.h"
#include "aggregate.h"
#include "declaration.h"
#include "init.h"

#include "gen/irstate.h"
#include "gen/tollvm.h"
#include "gen/logger.h"
#include "gen/llvmhelpers.h"
#include "gen/utils.h"

#include "ir/irstruct.h"
#include "ir/irtypeclass.h"

#include <algorithm>

//////////////////////////////////////////////////////////////////////////////

IrStruct::IrStruct(AggregateDeclaration* aggr)
:   diCompositeType(NULL)
{
    aggrdecl = aggr;

    type = aggr->type;

    packed = (type->ty == Tstruct)
        ? type->alignsize() == 1
        : false;

    // above still need to be looked at

    init = NULL;
    constInit = NULL;

    vtbl = NULL;
    constVtbl = NULL;
    classInfo = NULL;
    constClassInfo = NULL;

    classInterfacesArray = NULL;
}

//////////////////////////////////////////////////////////////////////////////

LLGlobalVariable * IrStruct::getInitSymbol()
{
    if (init)
        return init;

    // create the initZ symbol
    std::string initname("_D");
    initname.append(aggrdecl->mangle());
    initname.append("6__initZ");

    llvm::GlobalValue::LinkageTypes _linkage = DtoExternalLinkage(aggrdecl);

    init = new llvm::GlobalVariable(
        type->irtype->getPA().get(), true, _linkage, NULL, initname, gIR->module);

    return init;
}

//////////////////////////////////////////////////////////////////////////////

llvm::Constant * IrStruct::getDefaultInit()
{
    if (constInit)
        return constInit;

    if (type->ty == Tstruct)
    {
        constInit = createStructDefaultInitializer();
    }
    else
    {
        constInit = createClassDefaultInitializer();
    }

    return constInit;
}

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

// helper function that returns the static default initializer of a variable
LLConstant* get_default_initializer(VarDeclaration* vd, Initializer* init)
{
    if (init)
    {
        return DtoConstInitializer(init->loc, vd->type, init);
    }
    else if (vd->init)
    {
        return DtoConstInitializer(vd->init->loc, vd->type, vd->init);
    }
    else
    {
        return DtoConstExpInit(vd->loc, vd->type, vd->type->defaultInit(vd->loc));
    }
}

// helper function that adds zero bytes to a vector of constants
size_t add_zeros(std::vector<llvm::Constant*>& constants, size_t diff)
{
    size_t n = constants.size();
    while (diff)
    {
        if (global.params.is64bit && diff % 8 == 0)
        {
            constants.push_back(llvm::Constant::getNullValue(llvm::Type::Int64Ty));
            diff -= 8;
        }
        else if (diff % 4 == 0)
        {
            constants.push_back(llvm::Constant::getNullValue(llvm::Type::Int32Ty));
            diff -= 4;
        }
        else if (diff % 2 == 0)
        {
            constants.push_back(llvm::Constant::getNullValue(llvm::Type::Int16Ty));
            diff -= 2;
        }
        else
        {
            constants.push_back(llvm::Constant::getNullValue(llvm::Type::Int8Ty));
            diff -= 1;
        }
    }
    return constants.size() - n;
}

// Matches the way the type is built in IrTypeStruct
// maybe look at unifying the interface.

LLConstant * IrStruct::createStructDefaultInitializer()
{
    IF_LOG Logger::println("Building default initializer for %s", aggrdecl->toPrettyChars());
    LOG_SCOPE;

    assert(type->ty == Tstruct && "cannot build struct default initializer for non struct type");

    // start at offset zero
    size_t offset = 0;

    // vector of constants
    std::vector<llvm::Constant*> constants;

    // go through fields
    ArrayIter<VarDeclaration> it(aggrdecl->fields);
    for (; !it.done(); it.next())
    {
        VarDeclaration* vd = it.get();

        if (vd->offset < offset)
        {
            IF_LOG Logger::println("skipping field: %s %s (+%u)", vd->type->toChars(), vd->toChars(), vd->offset);
            continue;
        }

        IF_LOG Logger::println("using field: %s %s (+%u)", vd->type->toChars(), vd->toChars(), vd->offset);

        // get next aligned offset for this field
        size_t alignedoffset = offset;
        if (!packed)
        {
            size_t alignsize = vd->type->alignsize();
            alignedoffset = (offset + alignsize - 1) & ~(alignsize - 1);
        }

        // insert explicit padding?
        if (alignedoffset < vd->offset)
        {
            add_zeros(constants, vd->offset - alignedoffset);
        }

        // add initializer
        constants.push_back(get_default_initializer(vd, NULL));

        // advance offset to right past this field
        offset = vd->offset + vd->type->size();
    }

    // tail padding?
    if (offset < aggrdecl->structsize)
    {
        add_zeros(constants, aggrdecl->structsize - offset);
    }

    // build constant struct
    llvm::Constant* definit = llvm::ConstantStruct::get(constants, packed);
#if 0
    IF_LOG Logger::cout() << "final default initializer: " << *definit << std::endl;
#endif

    // sanity check
    if (definit->getType() != type->irtype->get())
    {
        assert(0 && "default initializer type does not match the default struct type");
    }

    return definit;
}

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

// yet another rewrite of the notorious StructInitializer.

typedef std::pair<VarDeclaration*, llvm::Constant*> VCPair;

bool struct_init_data_sort(const VCPair& a, const VCPair& b)
{
    return (a.first && b.first)
        ? a.first->offset < b.first->offset
        : false;
}

// this time a bit more inspired by the DMD code.

LLConstant * IrStruct::createStructInitializer(StructInitializer * si)
{
    IF_LOG Logger::println("Building StructInitializer of type %s", si->ad->toPrettyChars());
    LOG_SCOPE;

    // sanity check
    assert(si->ad == aggrdecl && "struct type mismatch");
    assert(si->vars.dim == si->value.dim && "inconsistent StructInitializer");

    // array of things to build
    llvm::SmallVector<VCPair, 16> data(aggrdecl->fields.dim);

    // start by creating a map from initializer indices to field indices.
    // I have no fucking clue why dmd represents it like this :/
    size_t n = si->vars.dim;
    LLSmallVector<int, 16> datamap(n, 0);
    for (size_t i = 0; i < n; i++)
    {
        for (size_t j = 0; 1; j++)
        {
            assert(j < aggrdecl->fields.dim);
            if (aggrdecl->fields.data[j] == si->vars.data[i])
            {
                datamap[i] = j;
                break;
            }
        }
    }

    // fill in explicit initializers
    n = si->vars.dim;
    for (size_t i = 0; i < n; i++)
    {
        VarDeclaration* vd = (VarDeclaration*)si->vars.data[i];
        Initializer* ini = (Initializer*)si->value.data[i];

        size_t idx = datamap[i];

        if (data[idx].first != NULL)
        {
            Loc l = ini ? ini->loc : si->loc;
            error(l, "duplicate initialization of %s", vd->toChars());
            continue;
        }

        IF_LOG Logger::println("Explicit initializer: %s @+%u", vd->toChars(), vd->offset);
        LOG_SCOPE;

        data[idx].first = vd;
        data[idx].second = get_default_initializer(vd, ini);
    }

    // fill in implicit initializers
    n = data.size();
    for (size_t i = 0; i < n; i++)
    {
        VarDeclaration* vd = data[i].first;
        if (vd)
            continue;

        vd = (VarDeclaration*)aggrdecl->fields.data[i];

        unsigned vd_begin = vd->offset;
        unsigned vd_end = vd_begin + vd->type->size();

        // make sure it doesn't overlap any explicit initializers.
        VarDeclarationIter it(aggrdecl->fields);
        bool overlaps = false;
        size_t j = 0;
        for (; it.more(); it.next(), j++)
        {
            if (i == j || !data[j].first)
                continue;

            unsigned f_begin = it->offset;
            unsigned f_end = f_begin + it->type->size();

            if (vd_begin >= f_end || vd_end <= f_begin)
                continue;

            overlaps = true;
            break;
        }
        // add if no overlap found
        if (!overlaps)
        {
            IF_LOG Logger::println("Implicit initializer: %s @+%u", vd->toChars(), vd->offset);
            LOG_SCOPE;

            data[i].first = vd;
            data[i].second = get_default_initializer(vd, NULL);
        }
    }

    // stop if there were errors
    if (global.errors)
    {
        fatal();
    }

    // sort data array by offset
    std::sort(data.begin(), data.end(), struct_init_data_sort);

    // build array of constants and make sure explicit zero padding is inserted when necessary.
    size_t offset = 0;
    std::vector<llvm::Constant*> constants;
    constants.reserve(n);

    for (size_t i = 0; i < n; i++)
    {
        VarDeclaration* vd = data[i].first;
        if (vd == NULL)
            continue;

        // get next aligned offset for this field
        size_t alignedoffset = offset;
        if (!packed)
        {
            size_t alignsize = vd->type->alignsize();
            alignedoffset = (offset + alignsize - 1) & ~(alignsize - 1);
        }

        // insert explicit padding?
        if (alignedoffset < vd->offset)
        {
            size_t diff = vd->offset - alignedoffset;
            IF_LOG Logger::println("adding %zu bytes zero padding", diff);
            add_zeros(constants, diff);
        }

        IF_LOG Logger::println("adding field %s", vd->toChars());

        constants.push_back(data[i].second);
        offset = vd->offset + vd->type->size();
    }

    // tail padding?
    if (offset < aggrdecl->structsize)
    {
        size_t diff = aggrdecl->structsize - offset;
        IF_LOG Logger::println("adding %zu bytes zero padding", diff);
        add_zeros(constants, diff);
    }

    // build constant
    assert(!constants.empty());
    llvm::Constant* c = llvm::ConstantStruct::get(&constants[0], constants.size(), packed);
    IF_LOG Logger::cout() << "final struct initializer: " << *c << std::endl;
    return c;
}
