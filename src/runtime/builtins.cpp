#include "runtime/builtins.h"

#include <iostream>
#include <sstream>
#include <common/exception.h>

namespace anode { namespace runtime {

unsigned int AssertPassCount;

extern "C" {
    void assert_pass() {
        AssertPassCount++;
    }

    void assert_fail(char *filename, unsigned int lineNo) {
        std::stringstream out;
        out << filename << ":" << lineNo << ": " << "ASSERTION FAILED" << std::endl;
        throw exception::AnodeAssertionFailedException(out.str());
        //std::abort();
    }

    uint64_t anode_malloc(unsigned int size) {
        return (uint64_t)GC_malloc(size);
    }
}

std::unordered_map<std::string, symbolptr_t> getBuiltins() {

    std::unordered_map<std::string, symbolptr_t> builtins {
        { "__assert_failed__", reinterpret_cast<symbolptr_t>(assert_fail) },
        { "__assert_passed__", reinterpret_cast<symbolptr_t>(assert_pass) },
        { "__malloc__", reinterpret_cast<symbolptr_t>(anode_malloc) },

    };

    return builtins;
}

}}