// src/tools/calculator_eval.cpp — calc_evaluate (spec §4.6).
//
// The classic two-stack algorithm: `values` holds numbers, `ops` holds
// operators and '('. Iterative only (no recursion), so 20,000 terms or
// 5,000 nested parentheses cannot overflow the call stack.

#include "tools/calculator.h"

#include <stack>

namespace {

using Kind = CalcToken::Kind;

bool is_operator(Kind k) {
    return k == Kind::Plus || k == Kind::Minus || k == Kind::Times || k == Kind::Divide;
}

int precedence(Kind k) {
    return (k == Kind::Times || k == Kind::Divide) ? 2 : 1;
}

// Pops one operator and two operands, pushes `left op right`.
bool apply_top(std::stack<double>& values, std::stack<Kind>& ops, std::string* error) {
    if (ops.empty()) {
        *error = "malformed expression: missing operator";
        return false;
    }
    const Kind op = ops.top();
    ops.pop();
    if (!is_operator(op)) {
        *error = "malformed expression: unbalanced parentheses";
        return false;
    }
    if (values.size() < 2) {
        *error = "malformed expression: missing operand";
        return false;
    }
    const double right = values.top();   // first pop is the RIGHT operand
    values.pop();
    const double left = values.top();
    values.pop();

    double result = 0.0;
    switch (op) {
        case Kind::Plus:   result = left + right; break;
        case Kind::Minus:  result = left - right; break;
        case Kind::Times:  result = left * right; break;
        case Kind::Divide:
            if (right == 0.0) {
                *error = "division by zero";
                return false;
            }
            result = left / right;
            break;
        default:
            *error = "malformed expression: unknown operator";
            return false;
    }
    values.push(result);
    return true;
}

}  // namespace

bool calc_evaluate(const std::vector<CalcToken>& tokens, double* out, std::string* error) {
    std::stack<double> values;
    std::stack<Kind>   ops;

    for (const CalcToken& t : tokens) {
        switch (t.kind) {
            case Kind::Number:
                values.push(t.value);
                break;

            case Kind::LParen:
                ops.push(Kind::LParen);
                break;

            case Kind::RParen:
                // Apply everything inside the parentheses, then drop the '('.
                while (!ops.empty() && ops.top() != Kind::LParen) {
                    if (!apply_top(values, ops, error)) return false;
                }
                if (ops.empty()) {
                    *error = "malformed expression: unbalanced ')'";
                    return false;
                }
                ops.pop();   // the matching '('
                break;

            case Kind::Plus:
            case Kind::Minus:
            case Kind::Times:
            case Kind::Divide:
                // >= gives left-to-right order for equal precedence.
                while (!ops.empty() && is_operator(ops.top()) &&
                       precedence(ops.top()) >= precedence(t.kind)) {
                    if (!apply_top(values, ops, error)) return false;
                }
                ops.push(t.kind);
                break;
        }
    }

    while (!ops.empty()) {
        if (ops.top() == Kind::LParen) {
            *error = "malformed expression: unbalanced '('";
            return false;
        }
        if (!apply_top(values, ops, error)) return false;
    }

    if (values.size() != 1) {
        *error = "malformed expression";
        return false;
    }
    *out = values.top();
    return true;
}
