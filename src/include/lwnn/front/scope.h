
#pragma once

#include "common/exception.h"
#include "type.h"

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

namespace lwnn {
    namespace scope {

        class Symbol {
        public:
            virtual std::string name() const = 0;
            virtual std::string toString() const = 0;
            virtual type::Type *type() const = 0;
        };

        class SymbolTable {
            SymbolTable *parent_ = nullptr;
            std::unordered_map<std::string, Symbol*> symbols_;
            std::vector<Symbol*> orderedSymbols_;
        public:

            void setParent(SymbolTable *parent) {
                ASSERT(parent != this && "Hello? Are you trying to cause an infinite loop?");
                parent_ = parent;
            }

            /** Finds the named symbol in the current scope */
            Symbol *findSymbol(const std::string &name) const {
                auto found = symbols_.find(name);
                if (found == symbols_.end()) {
                    return nullptr;
                }
                return (*found).second;
            }

            /** Finds the named symbol in the current scope or any parent. */
            Symbol *recursiveFindSymbol(const std::string &name) const {
                SymbolTable const *current = this;
                while(current) {
                    Symbol *found = current->findSymbol(name);
                    if(found) {
                        return found;
                    }
                    current = current->parent_;
                }

                return nullptr;
            }

            void addSymbol(Symbol *variable) {
                ASSERT(symbols_.find(variable->name()) == symbols_.end()
                       && "The symbol being added must not already exist in the current scope.");

                symbols_.emplace(variable->name(), variable);
                orderedSymbols_.emplace_back(variable);
            }

            std::vector<Symbol*> symbols() const {
                std::vector<Symbol*> symbols;
                symbols.reserve(orderedSymbols_.size());
                for (Symbol *symbol : orderedSymbols_) {
                    symbols.push_back(symbol);
                }

                return symbols;
            }
        };
    }
}
