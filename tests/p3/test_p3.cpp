// tests/p3/test_p3.cpp — YOUR P3 TEST SUITE.
//
// The five tests below are worked examples. Keep them, and add your own
// until you have AT LEAST 15 (spec §6 lists what must be covered).
// Name every test <area>_<what>: reg_ for the registry, ws_ for the
// workspace, calc_ for the calculator.
//
// Build & run:   cmake --build build && ./build/test_p3
// One area:      ./build/test_p3 reg_        (names starting with "reg_")
// One test:      ./build/test_p3 =reg_add_and_find

#include "../mini_test.h"
#include "../test_helpers.h"     // TempDir, write_text, args1, ...

#include "tools/calculator.h"
#include "tools/tool_registry.h"
#include "tools/workspace.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// A tool whose name you choose — use it to fill the registry in tests.
class NamedTool : public Tool {
public:
    explicit NamedTool(std::string n) : n_(std::move(n)), schema_(JsonValue::object()) {}
    std::string name() const override { return n_; }
    std::string description() const override { return "test tool"; }
    const JsonValue& schema() const override { return schema_; }
    ToolResult invoke(const JsonValue&, ToolContext&) override { return ToolResult::success(n_); }
private:
    std::string n_;
    JsonValue   schema_;   // an empty JSON object: {}
};

// Tokenize with the provided tokenizer, then run YOUR calc_evaluate.
bool eval(const std::string& expr, double* value, std::string* error) {
    std::vector<CalcToken> tokens;
    if (!calc_tokenize(expr, &tokens, error)) return false;
    return calc_evaluate(tokens, value, error);
}

}  // namespace

// ---------------------------------------------------------------- examples

TEST(reg_add_and_find) {
    ToolRegistry r;
    auto t = std::make_unique<NamedTool>("alpha");
    Tool* raw = t.get();                        // remember the address before moving
    REQUIRE(r.add(std::move(t)));
    CHECK(r.size() == 1);
    CHECK(r.find("alpha") == raw);              // same object, not a copy
    CHECK(r.find("beta") == nullptr);
}

TEST(reg_duplicate_keeps_original) {
    ToolRegistry r;
    auto first = std::make_unique<NamedTool>("alpha");
    Tool* original = first.get();
    REQUIRE(r.add(std::move(first)));
    CHECK(!r.add(std::make_unique<NamedTool>("alpha")));
    CHECK(r.find("alpha") == original);
    CHECK(r.size() == 1);
}

TEST(ws_rejects_parent_escape) {
    TempDir d;
    Workspace ws(d.path());
    fs::path out;
    std::string reason;
    REQUIRE(ws.resolve("notes/todo.txt", &out, &reason));   // sanity: accepts *something*
    CHECK(out == ws.root() / "notes" / "todo.txt");
    CHECK(!ws.resolve("../etc/passwd", &out, &reason));
    CHECK(!reason.empty());
}

TEST(calc_precedence) {
    double v = 0;
    std::string err;
    REQUIRE(eval("2 + 3 * 4", &v, &err));
    CHECK(v == 14);
}

TEST(calc_division_by_zero) {
    double v = 0;
    std::string err;
    CHECK(!eval("1 / 0", &v, &err));
    CHECK(err == "division by zero");
}

// ------------------------------------------------------------- your tests

namespace {

// A tool that counts how many times it has been destroyed (ownership tests).
class CountingTool : public Tool {
public:
    CountingTool(std::string n, int* deaths)
        : n_(std::move(n)), deaths_(deaths), schema_(JsonValue::object()) {}
    ~CountingTool() override { ++*deaths_; }
    std::string name() const override { return n_; }
    std::string description() const override { return "counting tool"; }
    const JsonValue& schema() const override { return schema_; }
    ToolResult invoke(const JsonValue&, ToolContext&) override { return ToolResult::success(n_); }
private:
    std::string n_;
    int*        deaths_;
    JsonValue   schema_;
};

// Checks one accepted row of the spec §4.4 table.
void expect_accept(const Workspace& ws, const std::string& in, const fs::path& want) {
    fs::path out;
    std::string reason;
    CHECK(ws.resolve(in, &out, &reason));
    CHECK(out == want);
}

// Checks one rejected row of the spec §4.4 table.
void expect_reject(const Workspace& ws, const std::string& in) {
    fs::path out;
    std::string reason;
    CHECK(!ws.resolve(in, &out, &reason));
    CHECK(!reason.empty());
}

// Checks that `expr` evaluates to `want`.
void expect_value(const std::string& expr, double want) {
    double v = 0;
    std::string err;
    CHECK(eval(expr, &v, &err));
    CHECK_EQ(v, want);
}

}  // namespace

// ---- Registry basics -------------------------------------------------------

TEST(reg_empty_table) {
    ToolRegistry r;
    CHECK_EQ(r.size(), std::size_t{0});
    CHECK(r.bucket_count() >= 1 && r.bucket_count() <= 64);
    CHECK_EQ(r.load_factor(), 0.0);
    CHECK_EQ(r.max_probe_length(), std::size_t{0});
    CHECK(r.find("anything") == nullptr);
}

TEST(reg_null_rejected) {
    ToolRegistry r;
    CHECK(!r.add(nullptr));
    CHECK_EQ(r.size(), std::size_t{0});
    REQUIRE(r.add(std::make_unique<NamedTool>("alpha")));
    CHECK(!r.add(std::unique_ptr<Tool>{}));
    CHECK_EQ(r.size(), std::size_t{1});
}

TEST(reg_find_absent_name) {
    ToolRegistry r;
    REQUIRE(r.add(std::make_unique<NamedTool>("read_file")));
    CHECK(r.find("read_fil") == nullptr);     // prefix
    CHECK(r.find("read_file ") == nullptr);   // extra char
    CHECK(r.find("") == nullptr);
    CHECK_EQ(r.size(), std::size_t{1});
}

TEST(reg_ownership_each_tool_destroyed_once) {
    int deaths = 0;
    {
        ToolRegistry r;
        REQUIRE(r.add(std::make_unique<CountingTool>("a", &deaths)));
        REQUIRE(r.add(std::make_unique<CountingTool>("b", &deaths)));
        CHECK(!r.add(std::make_unique<CountingTool>("a", &deaths)));
        CHECK_EQ(deaths, 1);                  // the rejected duplicate, immediately
        for (int i = 0; i < 100; ++i)         // force several resizes
            REQUIRE(r.add(std::make_unique<CountingTool>("t" + std::to_string(i), &deaths)));
        CHECK_EQ(deaths, 1);                  // resizing destroys nothing
    }
    CHECK_EQ(deaths, 1 + 2 + 100);            // every tool destroyed exactly once
}

TEST(reg_for_each_visits_each_once) {
    ToolRegistry r;
    const std::vector<std::string> names = {"calculator", "list_dir", "read_file",
                                            "write_file", "alpha", "beta", "gamma",
                                            "delta", "epsilon", "zeta"};
    for (const auto& n : names) REQUIRE(r.add(std::make_unique<NamedTool>(n)));

    std::vector<int> seen(names.size(), 0);
    int calls = 0;
    r.for_each([&](const Tool& tool) {
        ++calls;
        for (std::size_t i = 0; i < names.size(); ++i)
            if (names[i] == tool.name()) ++seen[i];
    });
    CHECK_EQ(calls, static_cast<int>(names.size()));
    for (int s : seen) CHECK_EQ(s, 1);
}

TEST(reg_hash_is_real_string_hash) {
    CHECK(ToolRegistry::hash("ab") != ToolRegistry::hash("ba"));
    CHECK(ToolRegistry::hash("tool_1234") != ToolRegistry::hash("tool_1243"));
    CHECK_EQ(ToolRegistry::hash("calculator"), ToolRegistry::hash("calculator"));
    std::vector<std::size_t> hs;
    for (int i = 0; i < 10000; ++i) hs.push_back(ToolRegistry::hash("tool_" + std::to_string(i)));
    std::sort(hs.begin(), hs.end());
    const auto distinct = std::unique(hs.begin(), hs.end()) - hs.begin();
    CHECK(distinct >= 9990);
}

// ---- Registry at scale (required) ------------------------------------------

TEST(reg_10000_tools) {
    ToolRegistry r;
    auto first = std::make_unique<NamedTool>("tool_0");
    Tool* first_ptr = first.get();
    REQUIRE(r.add(std::move(first)));
    CHECK(r.load_factor() <= 1.0);

    for (int i = 1; i < 10000; ++i) {
        REQUIRE(r.add(std::make_unique<NamedTool>("tool_" + std::to_string(i))));
        CHECK(r.load_factor() <= 1.0);
    }
    CHECK_EQ(r.size(), std::size_t{10000});
    CHECK(r.find("tool_0") == first_ptr);     // pointer stable across every resize

    int missing = 0;
    for (int i = 0; i < 10000; ++i) {
        const std::string n = "tool_" + std::to_string(i);
        Tool* t = r.find(n);
        if (t == nullptr || t->name() != n) ++missing;
    }
    CHECK_EQ(missing, 0);
    CHECK(r.find("tool_10000") == nullptr);
    CHECK(r.max_probe_length() <= 64);

    std::printf("    [FNV-1a, chaining] bucket_count=%zu load_factor=%.4f max_probe_length=%zu\n",
                r.bucket_count(), r.load_factor(), r.max_probe_length());
}

// ---- Alternative hash (required) -------------------------------------------

TEST(reg_alternative_hash) {
    ToolRegistry r;
    for (int i = 0; i < 10000; ++i)
        REQUIRE(r.add(std::make_unique<NamedTool>("tool_" + std::to_string(i))));
    const std::size_t buckets = r.bucket_count();

    // Weak hash: s[0] + s.size(), reduced the same way our table reduces.
    std::vector<int> weak(buckets, 0);
    // djb2 for comparison.
    std::vector<int> djb2(buckets, 0);
    for (int i = 0; i < 10000; ++i) {
        const std::string s = "tool_" + std::to_string(i);
        const std::size_t w = static_cast<unsigned char>(s[0]) + s.size();
        ++weak[w & (buckets - 1)];
        std::size_t h = 5381;
        for (unsigned char c : s) h = h * 33 + c;
        ++djb2[h & (buckets - 1)];
    }
    const int weak_max = *std::max_element(weak.begin(), weak.end());
    const int djb2_max = *std::max_element(djb2.begin(), djb2.end());
    const auto weak_used = std::count_if(weak.begin(), weak.end(), [](int c) { return c > 0; });

    std::printf("    [FNV-1a   ] buckets=%zu longest chain=%zu\n", buckets, r.max_probe_length());
    std::printf("    [djb2     ] buckets=%zu longest chain=%d\n", buckets, djb2_max);
    std::printf("    [s[0]+len ] buckets=%zu longest chain=%d (only %ld buckets used)\n",
                buckets, weak_max, static_cast<long>(weak_used));

    CHECK(static_cast<std::size_t>(weak_max) > 64);            // the weak hash fails the bound
    CHECK(r.max_probe_length() < static_cast<std::size_t>(weak_max));
}

// ---- Workspace: every row of spec §4.4 --------------------------------------

TEST(ws_accepts_simple_and_normalized) {
    TempDir d;
    Workspace ws(d.path());
    expect_accept(ws, "notes/todo.txt", ws.root() / "notes" / "todo.txt");
    expect_accept(ws, "./notes//todo.txt", ws.root() / "notes" / "todo.txt");
    expect_accept(ws, "my..notes.txt", ws.root() / "my..notes.txt");
}

TEST(ws_accepts_dot_and_folded_dotdot) {
    TempDir d;
    Workspace ws(d.path());
    expect_accept(ws, ".", ws.root());
    expect_accept(ws, "a/../b", ws.root() / "b");
    expect_accept(ws, "a/..", ws.root());
}

TEST(ws_accepts_nonexistent_without_creating) {
    TempDir d;
    Workspace ws(d.path());
    expect_accept(ws, "new/dir/file.txt", ws.root() / "new" / "dir" / "file.txt");
    CHECK(!fs::exists(ws.root() / "new"));     // resolve must not touch the filesystem
}

TEST(ws_rejects_empty_and_nul) {
    TempDir d;
    Workspace ws(d.path());
    expect_reject(ws, "");
    expect_reject(ws, std::string("notes") + '\0' + "/x");
}

TEST(ws_rejects_absolute) {
    TempDir d;
    Workspace ws(d.path());
    expect_reject(ws, "/etc/passwd");
    expect_reject(ws, (ws.root() / "notes").string());   // absolute even if inside root
}

TEST(ws_rejects_dotdot) {
    TempDir d;
    Workspace ws(d.path());
    expect_reject(ws, "..");
    expect_reject(ws, "../etc/passwd");
    expect_reject(ws, "a/../../b");
}

TEST(ws_rejects_escape_and_return) {
    TempDir d;
    write_text(d.path() / "ws" / "notes" / "todo.txt", "x");
    Workspace ws(d.path() / "ws");
    expect_reject(ws, "notes/../../ws/notes/todo.txt");
}

// ---- Calculator: every row of spec §4.6 + extras ---------------------------

TEST(calc_left_to_right) {
    expect_value("10 - 4 - 3", 3);
    expect_value("100 / 10 / 5", 2);
}

TEST(calc_parentheses) {
    expect_value("(2 + 3) * 4", 20);
    expect_value("2 * (3 + 4) - 5", 9);
}

TEST(calc_negatives_and_decimals) {
    expect_value("-3 * -3", 9);
    expect_value("7 / 2", 3.5);
    expect_value("(-1) - 2.5", -3.5);
}

TEST(calc_division_by_zero_in_parens) {
    double v = 0;
    std::string err;
    CHECK(!eval("1 / (2 - 2)", &v, &err));
    CHECK(err == "division by zero");
}

TEST(calc_own_expressions) {
    expect_value("((1 + 2) * (3 + 4)) / 7", 3);
    expect_value("8 - 2 * 3 + 10 / 5", 4);
    expect_value("0.5 * 0.5", 0.25);
    expect_value("42", 42);
}

TEST(calc_long_expressions) {
    std::string sum = "1";
    for (int i = 1; i < 20000; ++i) sum += "+1";
    expect_value(sum, 20000);

    std::string nested;
    for (int i = 0; i < 5000; ++i) nested += "(";
    nested += "7";
    for (int i = 0; i < 5000; ++i) nested += ")";
    expect_value(nested, 7);
}

TEST(calc_malformed_tokens_do_not_crash) {
    using K = CalcToken::Kind;
    double v = 0;
    std::string err;
    CHECK(!calc_evaluate({}, &v, &err));
    CHECK(!err.empty());
    err.clear();
    CHECK(!calc_evaluate({CalcToken{K::Plus, 0}}, &v, &err));
    CHECK(!err.empty());
    err.clear();
    CHECK(!calc_evaluate({CalcToken{K::RParen, 0}}, &v, &err));
    CHECK(!err.empty());
    err.clear();
    CHECK(!calc_evaluate({CalcToken{K::LParen, 0}, CalcToken{K::Number, 1}}, &v, &err));
    CHECK(!err.empty());
}

int main(int argc, char** argv) { return mt::run_all(argc, argv); }
