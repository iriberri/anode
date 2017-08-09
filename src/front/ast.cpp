
#include "front/ast.h"


namespace lwnn {
    namespace ast {
        unsigned long astNodesDestroyedCount = 0;

        std::string to_string(StmtKind kind) {
            switch (kind) {
                case StmtKind::FunctionDeclStmt:
                    return "FunctionDeclStmt";
                case StmtKind::ReturnStmt:
                    return "ReturnStmt";
                case StmtKind::ExprStmt:
                    return "ExprStmt";
                default:
                    ASSERT_FAIL("Unhandled StmtKind");
            }
        }

        std::string to_string(ExprKind kind) {
            switch (kind) {
                case ExprKind::CompoundExpr:
                    return "CompoundExpr";
                case ExprKind::VariableDeclExpr:
                    return "VariableDeclExpr";
                case ExprKind::LiteralInt32Expr:
                    return "LiteralInt32Expr";
                case ExprKind::LiteralFloatExpr:
                    return "LiteralFloatExpr";
                case ExprKind::VariableRefExpr:
                    return "VariableRefExpr";
                case ExprKind::BinaryExpr:
                    return "BinaryExpr";
                case ExprKind::ConditionalExpr:
                    return "ConditionalExpr";
                default:
                    ASSERT_FAIL("Unhandled ExprKind");
            }
        }

        std::string to_string(BinaryOperationKind type) {
            switch (type) {
                case BinaryOperationKind::Assign:
                    return "=";
                case BinaryOperationKind::Add:
                    return "+";
                case BinaryOperationKind::Sub:
                    return "-";
                case BinaryOperationKind::Mul:
                    return "*";
                case BinaryOperationKind::Div:
                    return "/";
                case BinaryOperationKind::Eq:
                    return "==";
                case BinaryOperationKind::NotEq:
                    return "!=";
                case BinaryOperationKind::LogicalAnd:
                    return "&&";
                case BinaryOperationKind::LogicalOr:
                    return "||";
                case BinaryOperationKind::GreaterThan:
                    return ">";
                case BinaryOperationKind::GreaterThanOrEqual:
                    return ">=";
                case BinaryOperationKind::LessThan:
                    return "<";
                case BinaryOperationKind::LessThanOrEqual:
                    return "<=";
                default:
                    ASSERT_FAIL("Unhandled BinaryOperationKind");
            }
        }

        CastExpr *CastExpr::createImplicit(ExprStmt *valueExpr, type::Type *toType) {
            return new CastExpr(valueExpr->sourceSpan(), toType, valueExpr, CastKind::Implicit);
        }

        CastExpr *createImplicit(ExprStmt* valueExpr, ast::TypeRef *toTypeRef) {
            return new CastExpr(valueExpr->sourceSpan(), toTypeRef, valueExpr, CastKind::Implicit);
        }

    } //namespace ast
} //namespace lwnn
