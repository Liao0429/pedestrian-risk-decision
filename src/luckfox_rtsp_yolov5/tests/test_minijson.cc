// =============================================================================
// test_minijson.cc — minijson 最小 JSON 解析器主机单元测试
// -----------------------------------------------------------------------------
// 验证点：
//   1. 对象/数组/字符串（含转义）/数字/布尔/null 正确解析；
//   2. \uXXXX 转义（含代理对）；
//   3. 错误输入抛出 minijson::JsonError（含行列）；
//   4. find() 缺失键返回 nullptr；operator[] 缺失键抛异常；
//   5. 顶层值后多余内容被拒绝。
//
// 编译：
//   g++ -std=c++11 -O2 -I../../algorithms test_minijson.cc -o minijson_test
// =============================================================================

#include <cmath>
#include <cstdio>
#include <string>

#include "minijson.h"

namespace {

int g_passed = 0;
int g_failed = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
            ++g_failed;                                                      \
        } else {                                                             \
            ++g_passed;                                                      \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b)                                                       \
    do {                                                                     \
        double _a = (double)(a);                                              \
        double _b = (double)(b);                                              \
        if (!(std::fabs(_a - _b) < 1e-9)) {                                  \
            std::printf("FAIL %s:%d  %s == %s  (%g vs %g)\n", __FILE__,       \
                         __LINE__, #a, #b, _a, _b);                            \
            ++g_failed;                                                      \
        } else {                                                             \
            ++g_passed;                                                      \
        }                                                                    \
    } while (0)

template <typename Fn>
bool expectError(Fn&& fn) {
    try {
        fn();
    } catch (const minijson::JsonError&) {
        return true;
    }
    return false;
}

void test_basic_object() {
    const std::string text = R"({
        "edge_dist_m": 2.0,
        "danger": {"n": 10, "rho": 0.55, "gcc": 0.8},
        "warning": {"n": 5, "rho": 0.28, "gcc": 0.55},
        "enabled": true,
        "name": "行人风险",
        "arr": [1, 2, 3],
        "empty": null
    })";
    minijson::Value root = minijson::parse(text);
    CHECK(root.isObject());
    CHECK_EQ(root.find("edge_dist_m")->asDouble(), 2.0);
    CHECK_EQ(root["danger"]["n"].asInt(), 10);
    CHECK_EQ(root["danger"]["rho"].asDouble(), 0.55);
    CHECK_EQ(root["danger"]["gcc"].asDouble(), 0.8);
    CHECK_EQ(root["warning"]["n"].asInt(), 5);
    CHECK(root["enabled"].asBool());
    CHECK(root["name"].asString() == std::string("行人风险"));
    CHECK(root["arr"].isArray());
    CHECK_EQ(root["arr"].size(), 3u);
    CHECK_EQ(root["arr"][2].asInt(), 3);
    CHECK(root["empty"].isNull());
    // find 缺失键返回 nullptr
    CHECK(root.find("not_exists") == nullptr);
    CHECK(root.find("danger")->find("n") != nullptr);
}

void test_escapes_and_unicode() {
    const std::string text = R"({"s":"a\"b\\c\n\u4e2d\uD83D\uDE00"})";
    minijson::Value root = minijson::parse(text);
    const std::string& s = root["s"].asString();
    // a"b\c<换行>中😀
    const std::string expected = std::string("a\"b\\c\n中\xF0\x9F\x98\x80");
    CHECK(s == expected);
}

void test_numbers() {
    minijson::Value root = minijson::parse("{\"a\":-3.5,\"b\":1e2,\"c\":0.25,\"d\":-0.5e-1}");
    CHECK_EQ(root["a"].asDouble(), -3.5);
    CHECK_EQ(root["b"].asDouble(), 100.0);
    CHECK_EQ(root["c"].asDouble(), 0.25);
    CHECK_EQ(root["d"].asDouble(), -0.05);
}

void test_errors() {
    // 非法 JSON
    CHECK(expectError([]() { minijson::parse("{"); }));
    CHECK(expectError([]() { minijson::parse("{a:1}"); }));       // 键不是字符串
    CHECK(expectError([]() { minijson::parse("{\"a\" 1}"); }));    // 缺冒号
    CHECK(expectError([]() { minijson::parse("{\"a\":}"); }));   // 缺值
    CHECK(expectError([]() { minijson::parse("[1,]"); }));        // 尾随逗号
    CHECK(expectError([]() { minijson::parse("\"abc"); }));       // 字符串未闭合
    CHECK(expectError([]() { minijson::parse("{\"a\":1} extra"); }));  // 顶层后多余内容
    // 类型访问错误
    CHECK(expectError([]() {
        minijson::Value root = minijson::parse("{\"a\":1}");
        (void)root["a"].asString();
    }));
    // 缺失键
    CHECK(expectError([]() {
        minijson::Value root = minijson::parse("{}");
        (void)root["missing"];
    }));
}

}  // namespace

int main() {
    test_basic_object();
    test_escapes_and_unicode();
    test_numbers();
    test_errors();

    std::printf("\n%d/%d tests passed\n", g_passed, g_passed + g_failed);
    if (g_failed > 0) {
        std::printf("FAILED: %d assertion(s)\n", g_failed);
        return 1;
    }
    return 0;
}