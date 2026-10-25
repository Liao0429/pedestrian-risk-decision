#ifndef MINIJSON_H
#define MINIJSON_H

// =============================================================================
// minijson.h — 最小 JSON 解析器（C++11，仅标准库）
// -----------------------------------------------------------------------------
// 用途：
//   Luckfox 板端工程需要运行时读取 config/default_config.json 的阈值配置，
//   但目标环境只有标准库（无 RapidJSON / nlohmann / Boost）。本文件提供自包含、
//   header-only 的 JSON 解析器，只覆盖项目实际需要的子集，但语法遵循 RFC 8259：
//     对象 {} / 数组 [] / 字符串（含 \uXXXX 转义）/ 数字 / 布尔 / null。
//
// 使用示例：
//   #include "minijson.h"
//   try {
//       minijson::Value root = minijson::parse(text);   // text 为完整 JSON 字符串
//       double edge = root["edge_dist_m"].asDouble();
//       int dangerN = root["danger"]["n"].asInt();
//       const std::string& name = root["danger"]["rho"].asString();  // 类型错误抛异常
//   } catch (const minijson::JsonError& e) {
//       fprintf(stderr, "JSON 错误: %s\n", e.what());
//   }
//
// 错误处理：
//   - 所有解析错误抛出 minijson::JsonError（含行/列/错误描述）。
//   - 访问不存在的 key 或类型不匹配时，Value 的访问器抛出 JsonError。
//   - find(key) 返回 const Value*，不存在时返回 nullptr（不抛异常）。
//   - 深度限制 128，防止恶意嵌套导致栈溢出。
//
// 线程模型：Value 为值语义，可安全拷贝；解析器无全局状态，线程安全。
// =============================================================================

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace minijson {

// 解析错误（带行列信息）
class JsonError : public std::runtime_error {
public:
    JsonError(const std::string& msg, size_t line, size_t col)
        : std::runtime_error(msg + " (line " + std::to_string(line) +
                              ", column " + std::to_string(col) + ")"),
          line_(line), col_(col) {}
    size_t line() const { return line_; }
    size_t col() const { return col_; }
private:
    size_t line_;
    size_t col_;
};

class Value;

// 对象类型：键 -> 值（有序使用 std::map，保证遍历稳定）
using Object = std::map<std::string, Value>;
// 数组类型
using Array = std::vector<Value>;

// JSON 值（递归数据结构，值语义，共享底层对象）
class Value {
public:
    enum class Type {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Value() : type_(Type::Null), bool_(false), num_(0.0) {}
    explicit Value(bool b) : type_(Type::Bool), bool_(b), num_(0.0) {}
    explicit Value(double d) : type_(Type::Number), bool_(false), num_(d) {}
    explicit Value(int i) : type_(Type::Number), bool_(false), num_(static_cast<double>(i)) {}
    explicit Value(const std::string& s) : type_(Type::String), bool_(false), num_(0.0), str_(s) {}
    explicit Value(const char* s) : type_(Type::String), bool_(false), num_(0.0), str_(s ? s : "") {}
    Value(const Array& a) : type_(Type::Array), bool_(false), num_(0.0), arr_(a) {}
    Value(const Object& o) : type_(Type::Object), bool_(false), num_(0.0), obj_(o) {}

    Type type() const { return type_; }

    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool() const {
        require(Type::Bool, "asBool");
        return bool_;
    }
    double asDouble() const {
        require(Type::Number, "asDouble");
        return num_;
    }
    int asInt() const {
        require(Type::Number, "asInt");
        return static_cast<int>(num_);
    }
    const std::string& asString() const {
        require(Type::String, "asString");
        return str_;
    }
    const Array& asArray() const {
        require(Type::Array, "asArray");
        return arr_;
    }
    const Object& asObject() const {
        require(Type::Object, "asObject");
        return obj_;
    }

    // 查找对象成员；不存在或本值不是对象时返回 nullptr（不抛异常）。
    const Value* find(const std::string& key) const {
        if (type_ != Type::Object) return nullptr;
        Object::const_iterator it = obj_.find(key);
        return it == obj_.end() ? nullptr : &it->second;
    }

    // 对象成员访问；不是对象或 key 不存在时抛 JsonError。
    const Value& operator[](const std::string& key) const {
        require(Type::Object, "operator[]");
        Object::const_iterator it = obj_.find(key);
        if (it == obj_.end()) {
            throw JsonError("缺少对象成员: \"" + key + "\"", 0, 0);
        }
        return it->second;
    }

    // 数组下标访问；越界或不是数组时抛 JsonError。
    const Value& operator[](size_t idx) const {
        require(Type::Array, "operator[]");
        if (idx >= arr_.size()) {
            throw JsonError("数组下标越界: " + std::to_string(idx), 0, 0);
        }
        return arr_[idx];
    }

    size_t size() const {
        if (type_ == Type::Array) return arr_.size();
        if (type_ == Type::Object) return obj_.size();
        throw JsonError("size() 仅适用于数组/对象", 0, 0);
    }

private:
    void require(Type t, const char* op) const {
        if (type_ != t) {
            throw JsonError(std::string("在 ") + op + " 上类型不匹配：期望 " +
                            typeName(t) + "，实际 " + typeName(type_), 0, 0);
        }
    }

    static const char* typeName(Type t) {
        switch (t) {
            case Type::Null:   return "null";
            case Type::Bool:   return "bool";
            case Type::Number: return "number";
            case Type::String: return "string";
            case Type::Array:  return "array";
            case Type::Object: return "object";
        }
        return "unknown";
    }

    Type type_;
    bool bool_;
    double num_;
    std::string str_;
    Array arr_;
    Object obj_;
};

// 解析完整 JSON 文本，返回根 Value。文本必须是单一 JSON 值（首尾空白允许）。
Value parse(const std::string& text);

// -----------------------------------------------------------------------------
// 内部实现（仅头文件，便于单文件集成到板端工程）
// -----------------------------------------------------------------------------
namespace detail {

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text), pos_(0), line_(1), col_(1) {}

    Value parseValue() {
        skipWs();
        if (pos_ >= text_.size()) fail("意外到达结尾");
        char c = peek();
        switch (c) {
            case '{': return parseObject();
            case '[': return parseArray();
            case '"': return Value(parseString());
            case 't': return Value(parseLiteral("true", true));
            case 'f': return Value(parseLiteral("false", false));
            case 'n': parseLiteral("null", true); return Value();
            default:
                if (c == '-' || (c >= '0' && c <= '9')) return Value(parseNumber());
                fail("非法字符");
                return Value();
        }
    }

    // 已消费位置（供顶层 parse 校验尾随内容）
    size_t position() const { return pos_; }

private:
    static const size_t kMaxDepth = 128;

    void fail(const std::string& msg) const {
        throw JsonError(msg, line_, col_);
    }

    void advance() {
        if (pos_ < text_.size()) {
            if (text_[pos_] == '\n') { ++line_; col_ = 1; }
            else { ++col_; }
            ++pos_;
        }
    }

    char peek() const {
        if (pos_ >= text_.size()) return '\0';
        return text_[pos_];
    }

    void skipWs() {
        while (pos_ < text_.size()) {
            char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') advance();
            else break;
        }
    }

    bool expectChar(char c) {
        if (pos_ < text_.size() && text_[pos_] == c) { advance(); return true; }
        return false;
    }

    Value parseObject() {
        if (depth_ >= kMaxDepth) fail("嵌套过深");
        ++depth_;
        expectChar('{');
        skipWs();
        Object obj;
        if (expectChar('}')) { --depth_; return Value(obj); }
        while (true) {
            skipWs();
            if (peek() != '"') fail("对象键必须是字符串");
            std::string key = parseString();
            skipWs();
            if (!expectChar(':')) fail("对象键后缺少冒号");
            skipWs();
            obj[key] = parseValue();
            skipWs();
            if (expectChar('}')) break;
            if (!expectChar(',')) fail("对象成员之间缺少逗号");
        }
        --depth_;
        return Value(obj);
    }

    Value parseArray() {
        if (depth_ >= kMaxDepth) fail("嵌套过深");
        ++depth_;
        expectChar('[');
        skipWs();
        Array arr;
        if (expectChar(']')) { --depth_; return Value(arr); }
        while (true) {
            skipWs();
            arr.push_back(parseValue());
            skipWs();
            if (expectChar(']')) break;
            if (!expectChar(',')) fail("数组元素之间缺少逗号");
        }
        --depth_;
        return Value(arr);
    }

    std::string parseString() {
        expectChar('"');
        std::string out;
        while (true) {
            if (pos_ >= text_.size()) fail("字符串未闭合");
            char c = text_[pos_];
            if (c == '"') { advance(); break; }
            if (c == '\\') {
                advance();
                if (pos_ >= text_.size()) fail("转义序列不完整");
                char e = text_[pos_];
                advance();
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': out.append(parseUnicodeEscape()); break;
                    default: fail("非法转义字符");
                }
            } else if (static_cast<unsigned char>(c) < 0x20) {
                fail("字符串包含未转义控制字符");
            } else {
                out.push_back(c);
                advance();
            }
        }
        return out;
    }

    std::string parseUnicodeEscape() {
        // 解析 \uXXXX，处理代理对（\uD800-\uDBFF + \uDC00-\uDFFF）。
        unsigned cp = readHex4();
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            // 高代理：必须紧跟 \uXXXX 低代理
            if (pos_ + 1 < text_.size() && text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                advance(); advance();
                unsigned low = readHex4();
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                } else {
                    fail("非法低代理项");
                }
            } else {
                fail("孤立的高代理项");
            }
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            fail("孤立的低代理项");
        }
        return utf8Encode(cp);
    }

    unsigned readHex4() {
        if (pos_ + 4 > text_.size()) fail("\\u 转义长度不足");
        unsigned v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = text_[pos_];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
            else fail("\\u 转义包含非十六进制字符");
            advance();
        }
        return v;
    }

    static std::string utf8Encode(unsigned cp) {
        std::string out;
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        return out;
    }

    bool parseLiteral(const char* lit, bool /*value*/) {
        for (const char* p = lit; *p; ++p) {
            if (pos_ >= text_.size() || text_[pos_] != *p) fail("非法字面量");
            advance();
        }
        return true;
    }

    double parseNumber() {
        size_t start = pos_;
        size_t startLine = line_, startCol = col_;
        if (expectChar('-')) {}
        // 整数部分
        if (peek() == '0') {
            advance();
        } else if (peek() >= '1' && peek() <= '9') {
            while (peek() >= '0' && peek() <= '9') advance();
        } else {
            fail("数字格式非法");
        }
        // 小数部分
        if (peek() == '.') {
            advance();
            if (!(peek() >= '0' && peek() <= '9')) fail("小数点后缺少数字");
            while (peek() >= '0' && peek() <= '9') advance();
        }
        // 指数部分
        if (peek() == 'e' || peek() == 'E') {
            advance();
            if (peek() == '+' || peek() == '-') advance();
            if (!(peek() >= '0' && peek() <= '9')) fail("指数部分缺少数字");
            while (peek() >= '0' && peek() <= '9') advance();
        }
        const std::string numStr = text_.substr(start, pos_ - start);
        // 手工解析，避免依赖 strtod 的本地化/精度差异（板端与主机一致）
        double val = 0.0;
        size_t i = 0;
        bool neg = false;
        if (i < numStr.size() && numStr[i] == '-') { neg = true; ++i; }
        while (i < numStr.size() && numStr[i] >= '0' && numStr[i] <= '9') {
            val = val * 10.0 + static_cast<double>(numStr[i] - '0');
            ++i;
        }
        if (i < numStr.size() && numStr[i] == '.') {
            ++i;
            double frac = 0.1;
            while (i < numStr.size() && numStr[i] >= '0' && numStr[i] <= '9') {
                val += static_cast<double>(numStr[i] - '0') * frac;
                frac *= 0.1;
                ++i;
            }
        }
        if (i < numStr.size() && (numStr[i] == 'e' || numStr[i] == 'E')) {
            ++i;
            bool eneg = false;
            if (i < numStr.size() && (numStr[i] == '+' || numStr[i] == '-')) {
                eneg = (numStr[i] == '-');
                ++i;
            }
            int exp = 0;
            while (i < numStr.size() && numStr[i] >= '0' && numStr[i] <= '9') {
                exp = exp * 10 + static_cast<int>(numStr[i] - '0');
                ++i;
            }
            double factor = 1.0;
            for (int k = 0; k < exp; ++k) factor *= 10.0;
            if (eneg) val /= factor;
            else val *= factor;
        }
        (void)startLine; (void)startCol;
        return neg ? -val : val;
    }

    const std::string& text_;
    size_t pos_;
    size_t line_;
    size_t col_;
    size_t depth_ = 0;
};

} // namespace detail

inline Value parse(const std::string& text) {
    detail::Parser parser(text);
    Value v = parser.parseValue();
    // 校验值之后只允许空白（严格 RFC 8259 顶层单一值）
    size_t pos = parser.position();
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' ||
                                 text[pos] == '\n' || text[pos] == '\r')) {
        ++pos;
    }
    if (pos != text.size()) {
        // 定位剩余部分的起始行列（近似：从开头数到 pos）
        size_t line = 1, col = 1;
        for (size_t i = 0; i < pos; ++i) {
            if (text[i] == '\n') { ++line; col = 1; }
            else { ++col; }
        }
        throw JsonError("JSON 顶层值后存在多余内容", line, col);
    }
    return v;
}

} // namespace minijson

#endif // MINIJSON_H