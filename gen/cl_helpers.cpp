#include "gen/cl_helpers.h"

#include "mars.h"
#include "root.h"
#include "rmem.h"

#include <cctype>       // isupper, tolower
#include <algorithm>
#include <utility>
#include <stdarg.h>

namespace opts {

bool FlagParser::parse(cl::Option &O, llvm::StringRef ArgName, llvm::StringRef Arg, bool &Val) {
    // Make a std::string out of it to make comparisons easier
    // (and avoid repeated conversion)
    llvm::StringRef argname = ArgName;

    typedef std::vector<std::pair<std::string, bool> >::iterator It;
    for (It I = switches.begin(), E = switches.end(); I != E; ++I) {
        llvm::StringRef name = I->first;
        if (name == argname
                || (name.size() < argname.size()
                    && argname.substr(0, name.size()) == name
                    && argname[name.size()] == '=')) {

            if (!cl::parser<bool>::parse(O, ArgName, Arg, Val)) {
                Val = (Val == I->second);
                return false;
            }
            // Invalid option value
            break;
        }
    }
    return true;
}

void FlagParser::getExtraOptionNames(llvm::SmallVectorImpl<const char*> &Names) {
    typedef std::vector<std::pair<std::string, bool> >::iterator It;
    for (It I = switches.begin() + 1, E = switches.end(); I != E; ++I) {
        Names.push_back(I->first.data());
    }
}


MultiSetter::MultiSetter(bool invert, bool* p, ...) {
    this->invert = invert;
    if (p) {
        locations.push_back(p);
        va_list va;
        va_start(va, p);
        while ((p = va_arg(va, bool*))) {
            locations.push_back(p);
        }
    }
}

void MultiSetter::operator=(bool val) {
    typedef std::vector<bool*>::iterator It;
    for (It I = locations.begin(), E = locations.end(); I != E; ++I) {
        **I = (val != invert);
    }
}


void StringsAdapter::push_back(const char* cstr) {
    if (!cstr || !*cstr)
        error("Expected argument to '-%s'", name);

    if (!*arrp)
        *arrp = new Strings;
    (*arrp)->push(mem.strdup(cstr));
}

} // namespace opts
