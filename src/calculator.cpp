// calculator.cpp — Безопасный калькулятор математических выражений (C++ ABI 3).
// Портирован из Python-модуля calculator.py (v1.2.0).
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include <string>
#include <vector>
#include <sstream>
#include <cmath>
#include <cctype>
#include <stdexcept>
#include <iomanip>

namespace {

static void send_result(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }
}

// ── Безопасный парсер выражений (Recursive Descent) ───────────────────────────

enum TokenType {
    TOK_END,
    TOK_NUM,
    TOK_IDENT,
    TOK_PLUS,
    TOK_MINUS,
    TOK_MUL,
    TOK_DIV,
    TOK_MOD,
    TOK_POW,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_COMMA
};

struct Token {
    TokenType type;
    double num_val;
    std::string str_val;
};

class Lexer {
public:
    explicit Lexer(const std::string& input) : src_(input), pos_(0) {}

    Token next_token() {
        skip_ws();
        if (pos_ >= src_.size()) {
            return {TOK_END, 0.0, ""};
        }

        char c = src_[pos_];

        if (isdigit(static_cast<unsigned char>(c)) || (c == '.' && pos_ + 1 < src_.size() && isdigit(static_cast<unsigned char>(src_[pos_ + 1])))) {
            size_t start = pos_;
            while (pos_ < src_.size() && (isdigit(static_cast<unsigned char>(src_[pos_])) || src_[pos_] == '.')) {
                pos_++;
            }
            std::string sub = src_.substr(start, pos_ - start);
            double val = std::stod(sub);
            return {TOK_NUM, val, sub};
        }

        if (isalpha(static_cast<unsigned char>(c)) || c == '_') {
            size_t start = pos_;
            while (pos_ < src_.size() && (isalnum(static_cast<unsigned char>(src_[pos_])) || src_[pos_] == '_')) {
                pos_++;
            }
            std::string ident = src_.substr(start, pos_ - start);
            std::string lower_ident;
            for (char ch : ident) lower_ident += static_cast<char>(tolower(static_cast<unsigned char>(ch)));
            return {TOK_IDENT, 0.0, lower_ident};
        }

        pos_++;
        switch (c) {
            case '+': return {TOK_PLUS, 0.0, "+"};
            case '-': return {TOK_MINUS, 0.0, "-"};
            case '*':
                if (pos_ < src_.size() && src_[pos_] == '*') {
                    pos_++;
                    return {TOK_POW, 0.0, "**"};
                }
                return {TOK_MUL, 0.0, "*"};
            case '/': return {TOK_DIV, 0.0, "/"};
            case ':': return {TOK_DIV, 0.0, ":"};
            case '%': return {TOK_MOD, 0.0, "%"};
            case '^': return {TOK_POW, 0.0, "^"};
            case '(': return {TOK_LPAREN, 0.0, "("};
            case ')': return {TOK_RPAREN, 0.0, ")"};
            case ',': return {TOK_COMMA, 0.0, ","};
            default:
                throw std::runtime_error(std::string("Недопустимый символ: ") + c);
        }
    }

private:
    void skip_ws() {
        while (pos_ < src_.size() && isspace(static_cast<unsigned char>(src_[pos_]))) {
            pos_++;
        }
    }

    std::string src_;
    size_t pos_;
};

class Parser {
public:
    explicit Parser(const std::string& expr) : lexer_(expr) {
        advance();
    }

    double parse() {
        double res = parse_expr();
        if (current_.type != TOK_END) {
            throw std::runtime_error("Синтаксическая ошибка: лишние символы в конце выражения");
        }
        return res;
    }

private:
    void advance() {
        current_ = lexer_.next_token();
    }

    double parse_expr() {
        double left = parse_term();
        while (current_.type == TOK_PLUS || current_.type == TOK_MINUS) {
            TokenType op = current_.type;
            advance();
            double right = parse_term();
            if (op == TOK_PLUS) left += right;
            else left -= right;
        }
        return left;
    }

    double parse_term() {
        double left = parse_factor();
        while (current_.type == TOK_MUL || current_.type == TOK_DIV || current_.type == TOK_MOD) {
            TokenType op = current_.type;
            advance();
            double right = parse_factor();
            if (op == TOK_MUL) {
                left *= right;
            } else if (op == TOK_DIV) {
                if (std::abs(right) < 1e-15) throw std::runtime_error("Деление на ноль");
                left /= right;
            } else {
                if (std::abs(right) < 1e-15) throw std::runtime_error("Деление на ноль при делении по модулю");
                left = std::fmod(left, right);
            }
        }
        return left;
    }

    double parse_factor() {
        double left = parse_unary();
        if (current_.type == TOK_POW) {
            advance();
            double right = parse_factor(); // Правоассоциативный оператор
            left = std::pow(left, right);
        }
        return left;
    }

    double parse_unary() {
        if (current_.type == TOK_PLUS) {
            advance();
            return parse_unary();
        }
        if (current_.type == TOK_MINUS) {
            advance();
            return -parse_unary();
        }
        return parse_primary();
    }

    double parse_primary() {
        if (current_.type == TOK_NUM) {
            double val = current_.num_val;
            advance();
            return val;
        }

        if (current_.type == TOK_IDENT) {
            std::string name = current_.str_val;
            advance();

            if (name == "pi") return 3.14159265358979323846;
            if (name == "e")  return 2.71828182845904523536;

            if (current_.type == TOK_LPAREN) {
                advance();
                std::vector<double> args;
                if (current_.type != TOKPAREN_CLOSE()) {
                    args.push_back(parse_expr());
                    while (current_.type == TOK_COMMA) {
                        advance();
                        args.push_back(parse_expr());
                    }
                }
                if (current_.type != TOKPAREN_CLOSE()) {
                    throw std::runtime_error("Ожидалась закрывающая скобка ')' после аргументов функции " + name);
                }
                advance();

                return call_function(name, args);
            }

            throw std::runtime_error("Неизвестная переменная или функция без скобок: " + name);
        }

        if (current_.type == TOK_LPAREN) {
            advance();
            double val = parse_expr();
            if (current_.type != TOKPAREN_CLOSE()) {
                throw std::runtime_error("Ожидалась закрывающая скобка ')'");
            }
            advance();
            return val;
        }

        throw std::runtime_error("Ожидалось число, функция или скобка");
    }

    TokenType TOKPAREN_CLOSE() const { return TOK_RPAREN; }

    double call_function(const std::string& func, const std::vector<double>& args) {
        if (func == "sqrt") {
            if (args.size() != 1) throw std::runtime_error("sqrt() принимает 1 аргумент");
            if (args[0] < 0) throw std::runtime_error("sqrt() от отрицательного числа");
            return std::sqrt(args[0]);
        }
        if (func == "cbrt") {
            if (args.size() != 1) throw std::runtime_error("cbrt() принимает 1 аргумент");
            return std::cbrt(args[0]);
        }
        if (func == "abs") {
            if (args.size() != 1) throw std::runtime_error("abs() принимает 1 аргумент");
            return std::fabs(args[0]);
        }
        if (func == "sin") {
            if (args.size() != 1) throw std::runtime_error("sin() принимает 1 аргумент");
            return std::sin(args[0]);
        }
        if (func == "cos") {
            if (args.size() != 1) throw std::runtime_error("cos() принимает 1 аргумент");
            return std::cos(args[0]);
        }
        if (func == "tan") {
            if (args.size() != 1) throw std::runtime_error("tan() принимает 1 аргумент");
            return std::tan(args[0]);
        }
        if (func == "asin") {
            if (args.size() != 1) throw std::runtime_error("asin() принимает 1 аргумент");
            return std::asin(args[0]);
        }
        if (func == "acos") {
            if (args.size() != 1) throw std::runtime_error("acos() принимает 1 аргумент");
            return std::acos(args[0]);
        }
        if (func == "atan") {
            if (args.size() != 1) throw std::runtime_error("atan() принимает 1 аргумент");
            return std::atan(args[0]);
        }
        if (func == "log" || func == "ln") {
            if (args.size() != 1) throw std::runtime_error("log() принимает 1 аргумент");
            if (args[0] <= 0) throw std::runtime_error("log() от неположительного числа");
            return std::log(args[0]);
        }
        if (func == "log10") {
            if (args.size() != 1) throw std::runtime_error("log10() принимает 1 аргумент");
            if (args[0] <= 0) throw std::runtime_error("log10() от неположительного числа");
            return std::log10(args[0]);
        }
        if (func == "log2") {
            if (args.size() != 1) throw std::runtime_error("log2() принимает 1 аргумент");
            if (args[0] <= 0) throw std::runtime_error("log2() от неположительного числа");
            return std::log2(args[0]);
        }
        if (func == "exp") {
            if (args.size() != 1) throw std::runtime_error("exp() принимает 1 аргумент");
            return std::exp(args[0]);
        }
        if (func == "round") {
            if (args.empty() || args.size() > 2) throw std::runtime_error("round() принимает 1 или 2 аргумента");
            if (args.size() == 1) return std::round(args[0]);
            double mult = std::pow(10.0, args[1]);
            return std::round(args[0] * mult) / mult;
        }
        if (func == "floor") {
            if (args.size() != 1) throw std::runtime_error("floor() принимает 1 аргумент");
            return std::floor(args[0]);
        }
        if (func == "ceil") {
            if (args.size() != 1) throw std::runtime_error("ceil() принимает 1 аргумент");
            return std::ceil(args[0]);
        }
        if (func == "pow") {
            if (args.size() != 2) throw std::runtime_error("pow() принимает 2 аргумента");
            return std::pow(args[0], args[1]);
        }

        throw std::runtime_error("Неизвестная функция: " + func);
    }

    Lexer lexer_;
    Token current_;
};

// Препроцессинг строки: вставка неявных умножений (2(3) -> 2*(3), (2)5 -> (2)*5, 2sqrt -> 2*sqrt)
std::string preprocess(const std::string& input) {
    std::string s;
    for (size_t i = 0; i < input.size(); ++i) {
        char c = input[i];
        if (c == '^') {
            s += "**";
            continue;
        }
        if (c == ':') {
            s += "/";
            continue;
        }

        // 2( -> 2*( или 2x -> 2*x
        if (i > 0) {
            char prev = input[i - 1];
            if ((isdigit(static_cast<unsigned char>(prev)) && (c == '(' || isalpha(static_cast<unsigned char>(c)))) ||
                (prev == ')' && (isdigit(static_cast<unsigned char>(c)) || c == '(' || isalpha(static_cast<unsigned char>(c))))) {
                s += '*';
            }
        }
        s += c;
    }
    return s;
}

std::string format_result(double val) {
    if (std::isnan(val)) return "NaN";
    if (std::isinf(val)) return val > 0 ? "Infinity" : "-Infinity";
    if (std::floor(val) == val && std::abs(val) < 1e14) {
        return std::to_string(static_cast<long long>(val));
    }
    std::ostringstream oss;
    oss << std::setprecision(8) << val;
    return oss.str();
}

void cmd_calc(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string expr = raw ? koto_utils::trim(raw) : "";

    if (expr.empty()) {
        char pbuf[32] = ".";
        api->setting_get(ctx, "prefix", pbuf, sizeof(pbuf));
        std::string p = pbuf;

        api->c_reset(ctx);
        api->c_text(ctx, "🧮 ");
        api->c_fmt(ctx, "Ошибка: ", KOTO_ENT_BOLD);
        api->c_text(ctx, "Введите выражение.\nПример: ");
        api->c_fmt(ctx, (p + "calc 2 + 2 * sqrt(16)").c_str(), KOTO_ENT_CODE);
        send_result(ctx, api);
        return;
    }

    std::string original = expr;
    try {
        std::string pre = preprocess(expr);
        Parser parser(pre);
        double result = parser.parse();

        api->c_reset(ctx);
        api->c_text(ctx, "🧮 ");
        api->c_fmt(ctx, "Calculator\n", KOTO_ENT_BOLD);
        api->c_fmt(ctx, "Expr: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, (original + "\n").c_str(), KOTO_ENT_CODE);
        api->c_fmt(ctx, "Res: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, format_result(result).c_str(), KOTO_ENT_CODE);
        send_result(ctx, api);
    } catch (const std::exception& e) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ ");
        api->c_fmt(ctx, "Ошибка вычисления: ", KOTO_ENT_BOLD);
        api->c_text(ctx, e.what());
        api->c_text(ctx, "\nВыражение: ");
        api->c_fmt(ctx, original.c_str(), KOTO_ENT_CODE);
        send_result(ctx, api);
    }
}

const koto_command COMMANDS[] = {
    {
        "calc",
        &cmd_calc,
        KOTO_LEVEL_TRUSTED,
        "Безопасный математический калькулятор (+, -, *, /, ^, %, sqrt, sin, cos...)",
        "<выражение>"
    }
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "calculator",
    "Безопасный математический калькулятор с парсером выражений (C++ ABI 3).",
    "1.2.0",
    26,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr, 0,
    nullptr,
    nullptr, 0,
    nullptr
};

} // namespace

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
