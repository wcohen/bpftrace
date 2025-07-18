#include "ast/passes/semantic_analyser.h"
#include "ast/ast.h"
#include "ast/attachpoint_parser.h"
#include "ast/passes/c_macro_expansion.h"
#include "ast/passes/clang_parser.h"
#include "ast/passes/field_analyser.h"
#include "ast/passes/fold_literals.h"
#include "ast/passes/macro_expansion.h"
#include "ast/passes/map_sugar.h"
#include "ast/passes/printer.h"
#include "bpftrace.h"
#include "btf_common.h"
#include "driver.h"
#include "mocks.h"
#include "gmock/gmock-matchers.h"
#include "gtest/gtest.h"

namespace bpftrace::test::semantic_analyser {

using ::testing::_;
using ::testing::HasSubstr;

ast::ASTContext test_for_warning(BPFtrace &bpftrace,
                                 const std::string &input,
                                 const std::string &warning,
                                 bool invert = false,
                                 bool safe_mode = true)
{
  ast::ASTContext ast("stdin", input);
  bpftrace.safe_mode_ = safe_mode;

  // N.B. No tracepoint expansion, but we will do the double parse to enable
  // macro expansion.
  auto ok = ast::PassManager()
                .put(ast)
                .put(bpftrace)
                .add(CreateParsePass())
                .add(ast::CreateParseAttachpointsPass())
                .add(ast::CreateFieldAnalyserPass())
                .add(ast::CreateClangParsePass())
                .add(ast::CreateCMacroExpansionPass())
                .add(ast::CreateFoldLiteralsPass())
                .add(ast::CreateMapSugarPass())
                .add(ast::CreateSemanticPass())
                .run();
  EXPECT_TRUE(bool(ok));

  std::stringstream out;
  ast.diagnostics().emit(out);
  if (invert)
    EXPECT_THAT(out.str(), Not(HasSubstr(warning)));
  else
    EXPECT_THAT(out.str(), HasSubstr(warning));

  return ast;
}

ast::ASTContext test_for_warning(const std::string &input,
                                 const std::string &warning,
                                 bool invert = false,
                                 bool safe_mode = true)
{
  auto bpftrace = get_mock_bpftrace();
  return test_for_warning(*bpftrace, input, warning, invert, safe_mode);
}

ast::ASTContext test(BPFtrace &bpftrace,
                     bool mock_has_features,
                     const std::string &input,
                     int expected_result,
                     std::string_view expected_error = "",
                     bool safe_mode = true,
                     bool has_child = false)
{
  std::string local_input(input);
  if (!local_input.empty() && local_input[0] == '\n')
    local_input = local_input.substr(1); // Remove initial '\n'
  ast::ASTContext ast("stdin", local_input);

  std::stringstream msg;
  msg << "\nInput:\n" << input << "\n\nOutput:\n";

  bpftrace.safe_mode_ = safe_mode;
  bpftrace.feature_ = std::make_unique<MockBPFfeature>(mock_has_features);
  if (has_child) {
    bpftrace.cmd_ = "not-empty"; // Used by SemanticAnalyser.
  }

  // N.B. See above.
  auto ok = ast::PassManager()
                .put(ast)
                .put(bpftrace)
                .add(CreateParsePass())
                .add(ast::CreateMacroExpansionPass())
                .add(ast::CreateParseAttachpointsPass())
                .add(ast::CreateFieldAnalyserPass())
                .add(ast::CreateClangParsePass())
                .add(ast::CreateCMacroExpansionPass())
                .add(ast::CreateFoldLiteralsPass())
                .add(ast::CreateMapSugarPass())
                .add(ast::CreateSemanticPass())
                .run();

  // Reproduce the full string.
  std::stringstream out;
  ast.diagnostics().emit(out);

  if (expected_result == 0) {
    // Accept no errors.
    EXPECT_TRUE(ok && ast.diagnostics().ok()) << msg.str() << out.str();
  } else {
    // Accept any failure result.
    EXPECT_FALSE(ok && ast.diagnostics().ok()) << msg.str();
  }
  if (expected_error.data() && !expected_error.empty()) {
    if (!expected_error.empty() && expected_error[0] == '\n')
      expected_error.remove_prefix(1); // Remove initial '\n'
    EXPECT_EQ(out.str(), expected_error);
  }

  return ast;
}

ast::ASTContext test(BPFtrace &bpftrace,
                     const std::string &input,
                     bool safe_mode = true)
{
  return test(bpftrace, true, input, 0, {}, safe_mode, false);
}

ast::ASTContext test(BPFtrace &bpftrace,
                     const std::string &input,
                     int expected_result,
                     bool safe_mode = true)
{
  // This function will eventually be deprecated in favour of test_error()
  assert(expected_result != 0 &&
         "Use test(BPFtrace&, const std::string&) for expected successes");
  return test(bpftrace, true, input, expected_result, {}, safe_mode, false);
}

ast::ASTContext test(MockBPFfeature &feature, const std::string &input)
{
  auto bpftrace = get_mock_bpftrace();
  bool mock_has_features = feature.has_features_;
  return test(*bpftrace, mock_has_features, input, 0, {}, true, false);
}

ast::ASTContext test(MockBPFfeature &feature,
                     const std::string &input,
                     int expected_result,
                     bool safe_mode = true)
{
  // This function will eventually be deprecated in favour of test_error()
  assert(
      expected_result != 0 &&
      "Use test(MockBPFfeature&, const std::string&) for expected successes");
  auto bpftrace = get_mock_bpftrace();
  bool mock_has_features = feature.has_features_;
  return test(*bpftrace,
              mock_has_features,
              input,
              expected_result,
              {},
              safe_mode,
              false);
}

ast::ASTContext test(const std::string &input,
                     int expected_result,
                     bool safe_mode,
                     bool has_child = false)
{
  auto bpftrace = get_mock_bpftrace();
  return test(
      *bpftrace, true, input, expected_result, {}, safe_mode, has_child);
}

ast::ASTContext test(const std::string &input, int expected_result)
{
  // This function will eventually be deprecated in favour of test_error()
  assert(expected_result != 0 &&
         "Use test(const std::string&) for expected successes");
  auto bpftrace = get_mock_bpftrace();
  return test(*bpftrace, true, input, expected_result, {}, true, false);
}

ast::ASTContext test(const std::string &input)
{
  auto bpftrace = get_mock_bpftrace();
  return test(*bpftrace, true, input, 0, {}, true, false);
}

ast::ASTContext test(BPFtrace &bpftrace,
                     const std::string &input,
                     std::string_view expected_ast)
{
  auto ast = test(bpftrace, true, input, 0, {}, true, false);

  if (expected_ast[0] == '\n')
    expected_ast.remove_prefix(1); // Remove initial '\n'

  std::ostringstream out;
  ast::Printer printer(out);
  printer.visit(ast.root);

  if (expected_ast[0] == '*' && expected_ast[expected_ast.size() - 1] == '*') {
    // Remove globs from beginning and end
    expected_ast.remove_prefix(1);
    expected_ast.remove_suffix(1);
    EXPECT_THAT(out.str(), HasSubstr(expected_ast));
    return ast;
  }

  EXPECT_EQ(expected_ast, out.str());
  return ast;
}

ast::ASTContext test(const std::string &input, std::string_view expected_ast)
{
  auto bpftrace = get_mock_bpftrace();
  return test(*bpftrace, input, expected_ast);
}

ast::ASTContext test_error(BPFtrace &bpftrace,
                           const std::string &input,
                           std::string_view expected_error,
                           bool has_features = true)
{
  return test(bpftrace, has_features, input, -1, expected_error, true, false);
}

ast::ASTContext test_error(const std::string &input,
                           std::string_view expected_error,
                           bool has_features = true)
{
  auto bpftrace = get_mock_bpftrace();
  return test_error(*bpftrace, input, expected_error, has_features);
}

TEST(semantic_analyser, builtin_variables)
{
  // Just check that each builtin variable exists.
  test("kprobe:f { pid }");
  test("kprobe:f { tid }");
  test("kprobe:f { cgroup }");
  test("kprobe:f { uid }");
  test("kprobe:f { username }");
  test("kprobe:f { gid }");
  test("kprobe:f { nsecs }");
  test("kprobe:f { elapsed }");
  test("kprobe:f { numaid }");
  test("kprobe:f { cpu }");
  test("kprobe:f { ncpus }");
  test("kprobe:f { curtask }");
  test("kprobe:f { rand }");
  test("kprobe:f { ctx }");
  test("kprobe:f { comm }");
  test("kprobe:f { kstack }");
  test("kprobe:f { ustack }");
  test("kprobe:f { arg0 }");
  test("kprobe:f { sarg0 }");
  test("kretprobe:f { retval }");
  test("kprobe:f { func }");
  test("uprobe:/bin/sh:f { func }");
  test("kprobe:f { probe }");
  test("tracepoint:a:b { args }");
  test("kprobe:f { jiffies }");

  test_error("kprobe:f { fake }", R"(
stdin:1:12-16: ERROR: Unknown identifier: 'fake'
kprobe:f { fake }
           ~~~~
)");

  MockBPFfeature feature(false);
  test(feature, "k:f { jiffies }", 1);
}

TEST(semantic_analyser, builtin_cpid)
{
  test(R"(i:ms:100 { printf("%d\n", cpid); })", 1, false, false);
  test("i:ms:100 { @=cpid }", 1, false, false);
  test("i:ms:100 { $a=cpid }", 1, false, false);

  test(R"(i:ms:100 { printf("%d\n", cpid); })", 0, false, true);
  test("i:ms:100 { @=cpid }", 0, false, true);
  test("i:ms:100 { $a=cpid }", 0, false, true);
}

TEST(semantic_analyser, builtin_functions)
{
  // Just check that each function exists.
  // Each function should also get its own test case for more thorough testing
  test("kprobe:f { @x = hist(123) }");
  test("kprobe:f { @x = lhist(123, 0, 123, 1) }");
  test("kprobe:f { @x = tseries(3, 1s, 1) }");
  test("kprobe:f { @x = count() }");
  test("kprobe:f { @x = sum(pid) }");
  test("kprobe:f { @x = min(pid) }");
  test("kprobe:f { @x = max(pid) }");
  test("kprobe:f { @x = avg(pid) }");
  test("kprobe:f { @x = stats(pid) }");
  test("kprobe:f { @x = 1; delete(@x) }");
  test("kprobe:f { @x = 1; print(@x) }");
  test("kprobe:f { @x = 1; clear(@x) }");
  test("kprobe:f { @x = 1; zero(@x) }");
  test("kprobe:f { @x[1] = 1; if (has_key(@x, 1)) {} }");
  test("kprobe:f { @x[1] = 1; @s = len(@x) }");
  test("kprobe:f { time() }");
  test("kprobe:f { exit() }");
  test("kprobe:f { str(0xffff) }");
  test("kprobe:f { buf(0xffff, 1) }");
  test(R"(kprobe:f { printf("hello\n") })");
  test(R"(kprobe:f { system("ls\n") })", 0, false /* safe_node */);
  test("kprobe:f { join(0) }");
  test("kprobe:f { ksym(0xffff) }");
  test("kprobe:f { usym(0xffff) }");
  test("kprobe:f { kaddr(\"sym\") }");
  test("kprobe:f { ntop(0xffff) }");
  test("kprobe:f { ntop(2, 0xffff) }");
  test("kprobe:f { pton(\"127.0.0.1\") }");
  test("kprobe:f { pton(\"::1\") }");
  test("kprobe:f { pton(\"0000:0000:0000:0000:0000:0000:0000:0001\") }");
#ifdef __x86_64__
  test("kprobe:f { reg(\"ip\") }");
#endif
  test("kprobe:f { kstack(1) }");
  test("kprobe:f { ustack(1) }");
  test("kprobe:f { cat(\"/proc/uptime\") }");
  test("uprobe:/bin/sh:main { uaddr(\"glob_asciirange\") }");
  test("kprobe:f { cgroupid(\"/sys/fs/cgroup/unified/mycg\"); }");
  test("kprobe:f { macaddr(0xffff) }");
  test("kprobe:f { nsecs() }");
  test("kprobe:f { pid() }");
  test("kprobe:f { tid() }");
}

TEST(semantic_analyser, undefined_map)
{
  test("kprobe:f / @mymap == 123 / { @mymap = 0 }");
  test_error("kprobe:f / @mymap == 123 / { 456; }", R"(
stdin:1:12-18: ERROR: Undefined map: @mymap
kprobe:f / @mymap == 123 / { 456; }
           ~~~~~~
)");
  test_error("kprobe:f / @mymap1 == 1234 / { 1234; @mymap1 = @mymap2; }", R"(
stdin:1:48-55: ERROR: Undefined map: @mymap2
kprobe:f / @mymap1 == 1234 / { 1234; @mymap1 = @mymap2; }
                                               ~~~~~~~
)");
  test_error("kprobe:f { print(@x); }", R"(
stdin:1:12-20: ERROR: Undefined map: @x
kprobe:f { print(@x); }
           ~~~~~~~~
)");
  test_error("kprobe:f { zero(@x); }", R"(
stdin:1:12-19: ERROR: Undefined map: @x
kprobe:f { zero(@x); }
           ~~~~~~~
)");
}

TEST(semantic_analyser, consistent_map_values)
{
  test("kprobe:f { @x = 0; @x = 1; }");
  test(
      R"(BEGIN { $a = (3, "hello"); @m[1] = $a; $a = (1,"aaaaaaaaaa"); @m[2] = $a; })");
  test_error("kprobe:f { @x = 0; @x = \"a\"; }", R"(
stdin:1:20-28: ERROR: Type mismatch for @x: trying to assign value of type 'string' when map already contains a value of type 'int64'
kprobe:f { @x = 0; @x = "a"; }
                   ~~~~~~~~
)");
  test_error("kprobe:f { @x = 0; @x = *curtask; }", R"(
stdin:1:20-33: ERROR: Type mismatch for @x: trying to assign value of type 'struct task_struct' when map already contains a value of type 'int64'
kprobe:f { @x = 0; @x = *curtask; }
                   ~~~~~~~~~~~~~
)");
}

TEST(semantic_analyser, consistent_map_keys)
{
  test("BEGIN { @x = 0; @x; }");
  test("BEGIN { @x[1] = 0; @x[2]; }");
  test("BEGIN { @x[@y] = 5; @y = 1;}");
  test("BEGIN { @x[@y[@z]] = 5; @y[2] = 1; @z = @x[0]; }");

  test_error("BEGIN { @x = 0; @x[1]; }", R"(
stdin:1:17-19: ERROR: @x used as a map with an explicit key (non-scalar map), previously used without an explicit key (scalar map)
BEGIN { @x = 0; @x[1]; }
                ~~
)");
  test_error("BEGIN { @x[1] = 0; @x; }", R"(
stdin:1:20-22: ERROR: @x used as a map without an explicit key (scalar map), previously used with an explicit key (non-scalar map)
BEGIN { @x[1] = 0; @x; }
                   ~~
)");

  test("BEGIN { @x[1,2] = 0; @x[3,4]; }");
  test("BEGIN { @x[1, 1] = 0; @x[(3, 4)]; }");
  test("BEGIN { @x[1, ((int8)2, ((int16)3, 4))] = 0; @x[5, (6, (7, 8))]; }");

  test_error("BEGIN { @x[1,2] = 0; @x[3]; }", R"(
stdin:1:22-26: ERROR: Argument mismatch for @x: trying to access with arguments: 'int64' when map expects arguments: '(int64,int64)'
BEGIN { @x[1,2] = 0; @x[3]; }
                     ~~~~
)");
  test_error("BEGIN { @x[1] = 0; @x[2,3]; }", R"(
stdin:1:20-27: ERROR: Argument mismatch for @x: trying to access with arguments: '(int64,int64)' when map expects arguments: 'int64'
BEGIN { @x[1] = 0; @x[2,3]; }
                   ~~~~~~~
)");

  test(R"(BEGIN { @x[1,"a",kstack] = 0; @x[2,"b", kstack]; })");

  test_error(R"(
    BEGIN {
      @x[1,"a",kstack] = 0;
      @x["b", 2, kstack];
    })",
             R"(
stdin:3:7-25: ERROR: Argument mismatch for @x: trying to access with arguments: '(string,int64,kstack)' when map expects arguments: '(int64,string,kstack)'
      @x["b", 2, kstack];
      ~~~~~~~~~~~~~~~~~~
)");

  test("BEGIN { @map[1, 2] = 1; for ($kv : @map) { @map[$kv.0] = 2; } }");

  test_error(
      R"(BEGIN { @map[1, 2] = 1; for ($kv : @map) { @map[$kv.0.0] = 2; } })",
      R"(
stdin:1:45-57: ERROR: Argument mismatch for @map: trying to access with arguments: 'int64' when map expects arguments: '(int64,int64)'
BEGIN { @map[1, 2] = 1; for ($kv : @map) { @map[$kv.0.0] = 2; } }
                                            ~~~~~~~~~~~~
)");

  test(R"(BEGIN { $a = (3, "hi"); @map[1, "by"] = 1; @map[$a] = 2; })");
  test(R"(BEGIN { @map[1, "hellohello"] = 1; @map[(3, "hi")] = 2; })");
  test(R"(BEGIN { $a = (3, "hi"); @map[1, "hellohello"] = 1; @map[$a] = 2; })");
  test(
      R"(BEGIN { $a = (3, "hello"); @m[$a] = 1; $a = (1,"aaaaaaaaaa"); @m[$a] = 2; })");
  test(
      R"(BEGIN { $a = (3, "hi", 50); $b = "goodbye"; $c = (4, $b, 60); @map[$a] = 1; @map[$c] = 2; })");
  test(
      R"(BEGIN { @["hi", ("hellolongstr", 2)] = 1; @["hellolongstr", ("hi", 5)] = 2; })");
  test(
      R"(BEGIN { $a = (3, (uint64)1234); $b = (4, (uint8)5); @map[$a] = 1; @map[$b] = 2; })");
  test(
      R"(BEGIN { $a = (3, (uint8)5); $b = (4, (uint64)1234); @map[$a] = 1; @map[$b] = 2; })");
}

TEST(semantic_analyser, if_statements)
{
  test("kprobe:f { if(true) { 123 } }");
  test("kprobe:f { if(false) { 123 } }");
  test("kprobe:f { if(1) { 123 } }");
  test("kprobe:f { if(1) { 123 } else { 456 } }");
  test("kprobe:f { if(0) { 123 } else if(1) { 456 } else { 789 } }");
  test("kprobe:f { if((int32)pid) { 123 } }");
  test("kprobe:f { if(curtask) { 123 } }");
  test("kprobe:f { if(curtask && (int32)pid) { 123 } }");
}

TEST(semantic_analyser, predicate_expressions)
{
  test("kprobe:f / 999 / { 123 }");
  test_error("kprobe:f / \"str\" / { 123 }", R"(
stdin:1:10-19: ERROR: Invalid type for predicate: string
kprobe:f / "str" / { 123 }
         ~~~~~~~~~
)");
  test_error("kprobe:f / kstack / { 123 }", R"(
stdin:1:10-20: ERROR: Invalid type for predicate: kstack
kprobe:f / kstack / { 123 }
         ~~~~~~~~~~
)");
  test_error("kprobe:f / @mymap / { @mymap = \"str\" }", R"(
stdin:1:10-20: ERROR: Invalid type for predicate: string
kprobe:f / @mymap / { @mymap = "str" }
         ~~~~~~~~~~
)");
}

TEST(semantic_analyser, ternary_expressions)
{
  // There are some supported types left out of this list
  // as they don't make sense or cause other errors e.g.
  // map aggregate functions and builtins
  std::unordered_map<std::string, std::string> supported_types = {
    { "1", "2" },
    { "true", "false" },
    { "\"lo\"", "\"high\"" },
    { "(\"hi\", 1)", "(\"bye\", 2)" },
    { "printf(\"lo\")", "exit()" },
    { "buf(\"mystr\", 5)", "buf(\"mystr\", 4)" },
    { "macaddr(arg0)", "macaddr(arg1)" },
    { "kstack(3)", "kstack(3)" },
    { "ustack(3)", "ustack(3)" },
    { "ntop(arg0)", "ntop(arg1)" },
    { "nsecs(boot)", "nsecs(monotonic)" },
    { "ksym(arg0)", "ksym(arg1)" },
    { "usym(arg0)", "usym(arg1)" },
    { "cgroup_path(1)", "cgroup_path(2)" },
    { "strerror(1)", "strerror(2)" },
    { "pid(curr_ns)", "pid(init)" },
    { "tid(curr_ns)", "tid(init)" },
  };

  for (const auto &[left, right] : supported_types) {
    test("kprobe:f { curtask ? " + left + " : " + right + " }");
  }

  test("kprobe:f { pid < 10000 ? printf(\"lo\") : exit() }");
  test(R"(kprobe:f { @x = pid < 10000 ? printf("lo") : cat("/proc/uptime") })",
       3);
  test("struct Foo { int x; } kprobe:f { curtask ? (struct Foo)*arg0 : (struct "
       "Foo)*arg1 }",
       1);
  test("struct Foo { int x; } kprobe:f { curtask ? (struct Foo*)arg0 : (struct "
       "Foo*)arg1 }");
  test(
      R"(kprobe:f { pid < 10000 ? ("a", "hellolongstr") : ("hellolongstr", "b") })");

  test(
      R"(kprobe:f { pid < 10000 ? ("a", "hellolongstr") : ("hellolongstr", "b") })",
      R"(
Program
 kprobe:f
  ?: :: [(string[13],string[13])]
   < :: [uint64]
    builtin: pid :: [uint32]
    int: 10000
   tuple: :: [(string[2],string[13])]
    string: a
    string: hellolongstr
   tuple: :: [(string[13],string[2])]
    string: hellolongstr
    string: b
)");

  // Error location is incorrect: #3063
  test_error("kprobe:f { pid < 10000 ? 3 : cat(\"/proc/uptime\") }", R"(
stdin:1:12-50: ERROR: Ternary operator must return the same type: have 'int64' and 'none'
kprobe:f { pid < 10000 ? 3 : cat("/proc/uptime") }
           ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
)");
  // Error location is incorrect: #3063
  test_error("kprobe:f { @x = pid < 10000 ? 1 : \"high\" }", R"(
stdin:1:17-42: ERROR: Ternary operator must return the same type: have 'int64' and 'string'
kprobe:f { @x = pid < 10000 ? 1 : "high" }
                ~~~~~~~~~~~~~~~~~~~~~~~~~
)");
  // Error location is incorrect: #3063
  test_error("kprobe:f { @x = pid < 10000 ? \"lo\" : 2 }", R"(
stdin:1:17-40: ERROR: Ternary operator must return the same type: have 'string' and 'int64'
kprobe:f { @x = pid < 10000 ? "lo" : 2 }
                ~~~~~~~~~~~~~~~~~~~~~~~
)");
  // Error location is incorrect: #3063
  test_error("kprobe:f { @x = pid < 10000 ? (1, 2) : (\"a\", 4) }", R"(
stdin:1:17-49: ERROR: Ternary operator must return the same type: have '(int64,int64)' and '(string,int64)'
kprobe:f { @x = pid < 10000 ? (1, 2) : ("a", 4) }
                ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
)");
  // Error location is incorrect: #3063
  test_error("kprobe:f { @x = pid < 10000 ? ustack(1) : ustack(2) }", R"(
stdin:1:17-53: ERROR: Ternary operator must have the same stack type on the right and left sides.
kprobe:f { @x = pid < 10000 ? ustack(1) : ustack(2) }
                ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
)");
  // Error location is incorrect: #3063
  test_error("kprobe:f { @x = pid < 10000 ? kstack(raw) : kstack(perf) }", R"(
stdin:1:17-58: ERROR: Ternary operator must have the same stack type on the right and left sides.
kprobe:f { @x = pid < 10000 ? kstack(raw) : kstack(perf) }
                ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
)");
}

TEST(semantic_analyser, mismatched_call_types)
{
  test_error("kprobe:f { @x = 1; @x = count(); }", R"(
stdin:1:25-32: ERROR: Type mismatch for @x: trying to assign value of type 'count_t' when map already contains a value of type 'int64'
kprobe:f { @x = 1; @x = count(); }
                        ~~~~~~~
)");
  test_error("kprobe:f { @x = count(); @x = sum(pid); }", R"(
stdin:1:31-39: ERROR: Type mismatch for @x: trying to assign value of type 'usum_t' when map already contains a value of type 'count_t'
kprobe:f { @x = count(); @x = sum(pid); }
                              ~~~~~~~~
)");
  test_error("kprobe:f { @x = 1; @x = hist(0); }", R"(
stdin:1:25-32: ERROR: Type mismatch for @x: trying to assign value of type 'hist_t' when map already contains a value of type 'int64'
kprobe:f { @x = 1; @x = hist(0); }
                        ~~~~~~~
)");
}

TEST(semantic_analyser, compound_left)
{
  test_error("kprobe:f { $a <<= 0 }", R"(
stdin:1:12-14: ERROR: Undefined or undeclared variable: $a
kprobe:f { $a <<= 0 }
           ~~
)");
  test("kprobe:f { $a = 0; $a <<= 1 }");
  test("kprobe:f { @a <<= 1 }");
}

TEST(semantic_analyser, compound_right)
{
  test_error("kprobe:f { $a >>= 0 }", R"(
stdin:1:12-14: ERROR: Undefined or undeclared variable: $a
kprobe:f { $a >>= 0 }
           ~~
)");
  test("kprobe:f { $a = 0; $a >>= 1 }");
  test("kprobe:f { @a >>= 1 }");
}

TEST(semantic_analyser, compound_plus)
{
  test_error("kprobe:f { $a += 0 }", R"(
stdin:1:12-14: ERROR: Undefined or undeclared variable: $a
kprobe:f { $a += 0 }
           ~~
)");
  test("kprobe:f { $a = 0; $a += 1 }");
  test("kprobe:f { @a += 1 }");
}

TEST(semantic_analyser, compound_minus)
{
  test_error("kprobe:f { $a -= 0 }", R"(
stdin:1:12-14: ERROR: Undefined or undeclared variable: $a
kprobe:f { $a -= 0 }
           ~~
)");
  test("kprobe:f { $a = 0; $a -= 1 }");
  test("kprobe:f { @a -= 1 }");
}

TEST(semantic_analyser, compound_mul)
{
  test_error("kprobe:f { $a *= 0 }", R"(
stdin:1:12-14: ERROR: Undefined or undeclared variable: $a
kprobe:f { $a *= 0 }
           ~~
)");
  test("kprobe:f { $a = 0; $a *= 1 }");
  test("kprobe:f { @a *= 1 }");
}

TEST(semantic_analyser, compound_div)
{
  test_error("kprobe:f { $a /= 0 }", R"(
stdin:1:12-14: ERROR: Undefined or undeclared variable: $a
kprobe:f { $a /= 0 }
           ~~
)");
  test("kprobe:f { $a = 0; $a /= 1 }");
  test("kprobe:f { @a /= 1 }");
}

TEST(semantic_analyser, compound_mod)
{
  test_error("kprobe:f { $a %= 0 }", R"(
stdin:1:12-14: ERROR: Undefined or undeclared variable: $a
kprobe:f { $a %= 0 }
           ~~
)");
  test("kprobe:f { $a = 0; $a %= 1 }");
  test("kprobe:f { @a %= 1 }");
}

TEST(semantic_analyser, compound_band)
{
  test_error("kprobe:f { $a &= 0 }", R"(
stdin:1:12-14: ERROR: Undefined or undeclared variable: $a
kprobe:f { $a &= 0 }
           ~~
)");
  test("kprobe:f { $a = 0; $a &= 1 }");
  test("kprobe:f { @a &= 1 }");
}

TEST(semantic_analyser, compound_bor)
{
  test_error("kprobe:f { $a |= 0 }", R"(
stdin:1:12-14: ERROR: Undefined or undeclared variable: $a
kprobe:f { $a |= 0 }
           ~~
)");
  test("kprobe:f { $a = 0; $a |= 1 }");
  test("kprobe:f { @a |= 1 }");
}

TEST(semantic_analyser, compound_bxor)
{
  test_error("kprobe:f { $a ^= 0 }", R"(
stdin:1:12-14: ERROR: Undefined or undeclared variable: $a
kprobe:f { $a ^= 0 }
           ~~
)");
  test("kprobe:f { $a = 0; $a ^= 1 }");
  test("kprobe:f { @a ^= 1 }");
}

TEST(semantic_analyser, call_hist)
{
  test("kprobe:f { @x = hist(1); }");
  test("kprobe:f { @x = hist(1, 0); }");
  test("kprobe:f { @x = hist(1, 5); }");
  test_error("kprobe:f { @x = hist(1, 10); }", R"(
stdin:1:17-28: ERROR: hist: bits 10 must be 0..5
kprobe:f { @x = hist(1, 10); }
                ~~~~~~~~~~~
)");
  test_error("kprobe:f { $n = 3; @x = hist(1, $n); }", R"(
stdin:1:25-36: ERROR: hist() expects a int literal (int provided)
kprobe:f { $n = 3; @x = hist(1, $n); }
                        ~~~~~~~~~~~
)");
  test_error("kprobe:f { @x = hist(); }", R"(
stdin:1:17-23: ERROR: hist() requires at least one argument (0 provided)
kprobe:f { @x = hist(); }
                ~~~~~~
)");
  test_error("kprobe:f { hist(1); }", R"(
stdin:1:12-19: ERROR: hist() must be assigned directly to a map
kprobe:f { hist(1); }
           ~~~~~~~
)");
  test_error("kprobe:f { $x = hist(1); }", R"(
stdin:1:17-24: ERROR: hist() must be assigned directly to a map
kprobe:f { $x = hist(1); }
                ~~~~~~~
)");
  test_error("kprobe:f { @x[hist(1)] = 1; }", R"(
stdin:1:12-22: ERROR: hist() must be assigned directly to a map
kprobe:f { @x[hist(1)] = 1; }
           ~~~~~~~~~~
)");
  test_error("kprobe:f { if(hist()) { 123 } }", R"(
stdin:1:12-21: ERROR: hist() must be assigned directly to a map
kprobe:f { if(hist()) { 123 } }
           ~~~~~~~~~
)");
  test_error("kprobe:f { hist() ? 0 : 1; }", R"(
stdin:1:12-18: ERROR: hist() must be assigned directly to a map
kprobe:f { hist() ? 0 : 1; }
           ~~~~~~
)");
}

TEST(semantic_analyser, call_lhist)
{
  test("kprobe:f { @ = lhist(5, 0, 10, 1); }");
  test_error("kprobe:f { @ = lhist(5, 0, 10); }", R"(
stdin:1:16-31: ERROR: lhist() requires 4 arguments (3 provided)
kprobe:f { @ = lhist(5, 0, 10); }
               ~~~~~~~~~~~~~~~
)");
  test_error("kprobe:f { @ = lhist(5, 0); }", R"(
stdin:1:16-27: ERROR: lhist() requires 4 arguments (2 provided)
kprobe:f { @ = lhist(5, 0); }
               ~~~~~~~~~~~
)");
  test_error("kprobe:f { @ = lhist(5); }", R"(
stdin:1:16-24: ERROR: lhist() requires 4 arguments (1 provided)
kprobe:f { @ = lhist(5); }
               ~~~~~~~~
)");
  test_error("kprobe:f { @ = lhist(); }", R"(
stdin:1:16-23: ERROR: lhist() requires 4 arguments (0 provided)
kprobe:f { @ = lhist(); }
               ~~~~~~~
)");
  test_error("kprobe:f { @ = lhist(5, 0, 10, 1, 2); }", R"(
stdin:1:16-37: ERROR: lhist() requires 4 arguments (5 provided)
kprobe:f { @ = lhist(5, 0, 10, 1, 2); }
               ~~~~~~~~~~~~~~~~~~~~~
)");
  test_error("kprobe:f { lhist(-10, -10, 10, 1); }", R"(
stdin:1:12-34: ERROR: lhist() must be assigned directly to a map
kprobe:f { lhist(-10, -10, 10, 1); }
           ~~~~~~~~~~~~~~~~~~~~~~
)");
  test_error("kprobe:f { @ = lhist(-10, -10, 10, 1); }", R"(
stdin:1:16-38: ERROR: lhist: invalid min value (must be non-negative literal)
kprobe:f { @ = lhist(-10, -10, 10, 1); }
               ~~~~~~~~~~~~~~~~~~~~~~
)");
  test_error("kprobe:f { $x = lhist(); }", R"(
stdin:1:17-24: ERROR: lhist() must be assigned directly to a map
kprobe:f { $x = lhist(); }
                ~~~~~~~
)");
  test_error("kprobe:f { @[lhist()] = 1; }", R"(
stdin:1:12-21: ERROR: lhist() must be assigned directly to a map
kprobe:f { @[lhist()] = 1; }
           ~~~~~~~~~
)");
  test_error("kprobe:f { if(lhist()) { 123 } }", R"(
stdin:1:12-22: ERROR: lhist() must be assigned directly to a map
kprobe:f { if(lhist()) { 123 } }
           ~~~~~~~~~~
)");
  test_error("kprobe:f { lhist() ? 0 : 1; }", R"(
stdin:1:12-19: ERROR: lhist() must be assigned directly to a map
kprobe:f { lhist() ? 0 : 1; }
           ~~~~~~~
)");
}

TEST(semantic_analyser, call_lhist_posparam)
{
  BPFtrace bpftrace;
  bpftrace.add_param("0");
  bpftrace.add_param("10");
  bpftrace.add_param("1");
  bpftrace.add_param("hello");
  test(bpftrace, "kprobe:f { @ = lhist(5, $1, $2, $3); }");
  test(bpftrace, "kprobe:f { @ = lhist(5, $1, $2, $4); }", 3);
}

TEST(semantic_analyser, call_tseries)
{
  test("kprobe:f { @ = tseries(5, 10s, 1); }");
  test("kprobe:f { @ = tseries(-5, 10s, 1); }");
  test_error("kprobe:f { @ = tseries(5, 10s); }", R"(
stdin:1:16-31: ERROR: tseries() requires at least 3 arguments (2 provided)
kprobe:f { @ = tseries(5, 10s); }
               ~~~~~~~~~~~~~~~
)");
  test_error("kprobe:f { @ = tseries(5); }", R"(
stdin:1:16-26: ERROR: tseries() requires at least 3 arguments (1 provided)
kprobe:f { @ = tseries(5); }
               ~~~~~~~~~~
)");
  test_error("kprobe:f { @ = tseries(); }", R"(
stdin:1:16-25: ERROR: tseries() requires at least 3 arguments (0 provided)
kprobe:f { @ = tseries(); }
               ~~~~~~~~~
)");
  test_error("kprobe:f { @ = tseries(5, 10s, 1, 10, 10); }", R"(
stdin:1:16-42: ERROR: tseries() takes up to 4 arguments (5 provided)
kprobe:f { @ = tseries(5, 10s, 1, 10, 10); }
               ~~~~~~~~~~~~~~~~~~~~~~~~~~
)");
  test_error("kprobe:f { tseries(5, 10s, 1); }", R"(
stdin:1:12-30: ERROR: tseries() must be assigned directly to a map
kprobe:f { tseries(5, 10s, 1); }
           ~~~~~~~~~~~~~~~~~~
)");
  test_error("kprobe:f { $x = tseries(); }", R"(
stdin:1:17-26: ERROR: tseries() must be assigned directly to a map
kprobe:f { $x = tseries(); }
                ~~~~~~~~~
)");
  test_error("kprobe:f { @[tseries()] = 1; }", R"(
stdin:1:12-23: ERROR: tseries() must be assigned directly to a map
kprobe:f { @[tseries()] = 1; }
           ~~~~~~~~~~~
)");
  test_error("kprobe:f { if(tseries()) { 123 } }", R"(
stdin:1:12-24: ERROR: tseries() must be assigned directly to a map
kprobe:f { if(tseries()) { 123 } }
           ~~~~~~~~~~~~
)");
  test_error("kprobe:f { tseries() ? 0 : 1; }", R"(
stdin:1:12-21: ERROR: tseries() must be assigned directly to a map
kprobe:f { tseries() ? 0 : 1; }
           ~~~~~~~~~
)");
  test("kprobe:f { @ = tseries(-1, 10s, 5); }");
  test_error("kprobe:f { @ = tseries(1, 10s, 0); }", R"(
stdin:1:16-34: ERROR: tseries() num_intervals must be >= 1 (0 provided)
kprobe:f { @ = tseries(1, 10s, 0); }
               ~~~~~~~~~~~~~~~~~~
)");
  test_error("kprobe:f { @ = tseries(1, 10s, -1); }", R"(
stdin:1:16-35: ERROR: tseries: invalid num_intervals value (must be non-negative literal)
kprobe:f { @ = tseries(1, 10s, -1); }
               ~~~~~~~~~~~~~~~~~~~
)");
  test_error("kprobe:f { @ = tseries(1, 10s, 1000001); }", R"(
stdin:1:16-40: ERROR: tseries() num_intervals must be < 1000000 (1000001 provided)
kprobe:f { @ = tseries(1, 10s, 1000001); }
               ~~~~~~~~~~~~~~~~~~~~~~~~
)");
  test_error("kprobe:f { @ = tseries(1, 0, 10); }", R"(
stdin:1:16-33: ERROR: tseries() interval_ns must be >= 1 (0 provided)
kprobe:f { @ = tseries(1, 0, 10); }
               ~~~~~~~~~~~~~~~~~
)");
  test_error("kprobe:f { @ = tseries(1, -1, 10); }", R"(
stdin:1:16-34: ERROR: tseries: invalid interval_ns value (must be non-negative literal)
kprobe:f { @ = tseries(1, -1, 10); }
               ~~~~~~~~~~~~~~~~~~
)");
  // Good duration strings.
  test("kprobe:f { @ = tseries(1, 10ns, 5); }");
  test("kprobe:f { @ = tseries(1, 10us, 5); }");
  test("kprobe:f { @ = tseries(1, 10ms, 5); }");
  test("kprobe:f { @ = tseries(1, 10s, 5); }");
  // All aggregator functions.
  test(R"(kprobe:f { @ = tseries(1, 10s, 5, "avg"); })");
  test(R"(kprobe:f { @ = tseries(1, 10s, 5, "max"); })");
  test(R"(kprobe:f { @ = tseries(1, 10s, 5, "min"); })");
  test(R"(kprobe:f { @ = tseries(1, 10s, 5, "sum"); })");
  // Invalid aggregator function.
  test_error(R"(kprobe:f { @ = tseries(1, 10s, 5, "stats"); })", R"(
stdin:1:16-43: ERROR: tseries() expects one of the following aggregation functions: avg, max, min, sum ("stats" provided)
kprobe:f { @ = tseries(1, 10s, 5, "stats"); }
               ~~~~~~~~~~~~~~~~~~~~~~~~~~~
)");
}

TEST(semantic_analyser, call_tseries_posparam)
{
  BPFtrace bpftrace;
  bpftrace.add_param("10s");
  bpftrace.add_param("5");
  bpftrace.add_param("20");
  test(bpftrace, "kprobe:f { @ = tseries(5, $1, $2); }");
}

TEST(semantic_analyser, call_count)
{
  test("kprobe:f { @x = count(); }");
  test("kprobe:f { @x = count(1); }", 1);
  test("kprobe:f { count(); }", 1);
  test("kprobe:f { $x = count(); }", 1);
  test("kprobe:f { @[count()] = 1; }", 1);
  test("kprobe:f { if(count()) { 123 } }", 1);
  test("kprobe:f { count() ? 0 : 1; }", 1);
}

TEST(semantic_analyser, call_sum)
{
  test("kprobe:f { @x = sum(123); }");
  test("kprobe:f { @x = sum(); }", 1);
  test("kprobe:f { @x = sum(123, 456); }", 1);
  test("kprobe:f { sum(123); }", 1);
  test("kprobe:f { $x = sum(123); }", 1);
  test("kprobe:f { @[sum(123)] = 1; }", 1);
  test("kprobe:f { if(sum(1)) { 123 } }", 1);
  test("kprobe:f { sum(1) ? 0 : 1; }", 1);
}

TEST(semantic_analyser, call_min)
{
  test("kprobe:f { @x = min(123); }");
  test("kprobe:f { @x = min(); }", 1);
  test("kprobe:f { min(123); }", 1);
  test("kprobe:f { $x = min(123); }", 1);
  test("kprobe:f { @[min(123)] = 1; }", 1);
  test("kprobe:f { if(min(1)) { 123 } }", 1);
  test("kprobe:f { min(1) ? 0 : 1; }", 1);
}

TEST(semantic_analyser, call_max)
{
  test("kprobe:f { @x = max(123); }");
  test("kprobe:f { @x = max(); }", 1);
  test("kprobe:f { max(123); }", 1);
  test("kprobe:f { $x = max(123); }", 1);
  test("kprobe:f { @[max(123)] = 1; }", 1);
  test("kprobe:f { if(max(1)) { 123 } }", 1);
  test("kprobe:f { max(1) ? 0 : 1; }", 1);
}

TEST(semantic_analyser, call_avg)
{
  test("kprobe:f { @x = avg(123); }");
  test("kprobe:f { @x = avg(); }", 1);
  test("kprobe:f { avg(123); }", 1);
  test("kprobe:f { $x = avg(123); }", 1);
  test("kprobe:f { @[avg(123)] = 1; }", 1);
  test("kprobe:f { if(avg(1)) { 123 } }", 1);
  test("kprobe:f { avg(1) ? 0 : 1; }", 1);
}

TEST(semantic_analyser, call_stats)
{
  test("kprobe:f { @x = stats(123); }");
  test("kprobe:f { @x = stats(); }", 1);
  test("kprobe:f { stats(123); }", 1);
  test("kprobe:f { $x = stats(123); }", 1);
  test("kprobe:f { @[stats(123)] = 1; }", 1);
  test("kprobe:f { if(stats(1)) { 123 } }", 1);
  test("kprobe:f { stats(1) ? 0 : 1; }", 1);
}

TEST(semantic_analyser, call_delete)
{
  test("kprobe:f { @x = 1; delete(@x); }");
  test("kprobe:f { @y[5] = 5; delete(@y, 5); }");
  test("kprobe:f { @a[1] = 1; delete(@a, @a[1]); }");
  test("kprobe:f { @a = 1; @b[2] = 2; delete(@b, @a); }");
  test("kprobe:f { @a[1] = 1; $x = 1; delete(@a, $x); }");
  test(R"(kprobe:f { @y["hi"] = 5; delete(@y, "longerstr"); })");
  test(R"(kprobe:f { @y["hi", 5] = 5; delete(@y, ("hi", 5)); })");
  test(R"(kprobe:f { @y["longerstr", 5] = 5; delete(@y, ("hi", 5)); })");
  test(R"(kprobe:f { @y["hi", 5] = 5; delete(@y, ("longerstr", 5)); })");
  test("kprobe:f { @y[(3, 4, 5)] = 5; delete(@y, (1, 2, 3)); }");
  test("kprobe:f { @y[((int8)3, 4, 5)] = 5; delete(@y, (1, 2, 3)); }");
  test("kprobe:f { @y[(3, 4, 5)] = 5; delete(@y, ((int8)1, 2, 3)); }");
  test("kprobe:f { @x = 1; @y = delete(@x); }");
  test("kprobe:f { @x = 1; $y = delete(@x); }");
  test("kprobe:f { @x = 1; @[delete(@x)] = 1; }");
  test("kprobe:f { @x = 1; if(delete(@x)) { 123 } }");
  test("kprobe:f { @x = 1; delete(@x) ? 0 : 1; }");
  // The second arg gets treated like a map key, in terms of int type adjustment
  test("kprobe:f { @y[5] = 5; delete(@y, (int8)5); }");
  test("kprobe:f { @y[5, 4] = 5; delete(@y, ((int8)5, (int64)4)); }");

  test_error("kprobe:f { delete(1); }", R"(
stdin:1:12-20: ERROR: delete() expects a map argument
kprobe:f { delete(1); }
           ~~~~~~~~
)");

  test_error("kprobe:f { delete(1, 1); }", R"(
stdin:1:12-20: ERROR: delete() expects a map argument
kprobe:f { delete(1, 1); }
           ~~~~~~~~
)");

  test_error("kprobe:f { @y[(3, 4, 5)] = 5; delete(@y, (1, 2)); }", R"(
stdin:1:42-48: ERROR: Argument mismatch for @y: trying to access with arguments: '(int64,int64)' when map expects arguments: '(int64,int64,int64)'
kprobe:f { @y[(3, 4, 5)] = 5; delete(@y, (1, 2)); }
                                         ~~~~~~
)");

  test_error("kprobe:f { @y[1] = 2; delete(@y); }", R"(
stdin:1:23-32: ERROR: call to delete() expects a map without explicit keys (scalar map)
kprobe:f { @y[1] = 2; delete(@y); }
                      ~~~~~~~~~
)");

  test_error("kprobe:f { @a[1] = 1; delete(@a, @a); }", R"(
stdin:1:34-36: ERROR: @a used as a map without an explicit key (scalar map), previously used with an explicit key (non-scalar map)
kprobe:f { @a[1] = 1; delete(@a, @a); }
                                 ~~
)");

  // Deprecated API
  test("kprobe:f { @x = 1; delete(@x); }");
  test("kprobe:f { @y[5] = 5; delete(@y[5]); }");
  test(R"(kprobe:f { @y[1, "hi"] = 5; delete(@y[1, "longerstr"]); })");
  test(R"(kprobe:f { @y[1, "longerstr"] = 5; delete(@y[1, "hi"]); })");

  test_error("kprobe:f { @x = 1; @y = 5; delete(@x, @y); }", R"(
stdin:1:28-37: ERROR: call to delete() expects a map with explicit keys (non-scalar map)
kprobe:f { @x = 1; @y = 5; delete(@x, @y); }
                           ~~~~~~~~~
)");

  test_error(R"(kprobe:f { @x[1, "hi"] = 1; delete(@x["hi", 1]); })", R"(
stdin:1:29-47: ERROR: Argument mismatch for @x: trying to access with arguments: '(string,int64)' when map expects arguments: '(int64,string)'
kprobe:f { @x[1, "hi"] = 1; delete(@x["hi", 1]); }
                            ~~~~~~~~~~~~~~~~~~
)");

  test_error("kprobe:f { @x[0] = 1; @y[5] = 5; delete(@x, @y[5], @y[6]); }", R"(
stdin:1:34-58: ERROR: delete() requires 1 or 2 arguments (3 provided)
kprobe:f { @x[0] = 1; @y[5] = 5; delete(@x, @y[5], @y[6]); }
                                 ~~~~~~~~~~~~~~~~~~~~~~~~
)");

  test_error("kprobe:f { @x = 1; @y[5] = 5; delete(@x, @y[5], @y[6]); }", R"(
stdin:1:31-55: ERROR: delete() requires 1 or 2 arguments (3 provided)
kprobe:f { @x = 1; @y[5] = 5; delete(@x, @y[5], @y[6]); }
                              ~~~~~~~~~~~~~~~~~~~~~~~~
)");

  test_error("kprobe:f { @x = 1; delete(@x[1]); }", R"(
stdin:1:20-29: ERROR: call to delete() expects a map with explicit keys (non-scalar map)
kprobe:f { @x = 1; delete(@x[1]); }
                   ~~~~~~~~~
)");
}

TEST(semantic_analyser, call_exit)
{
  test("kprobe:f { exit(); }");
  test("kprobe:f { exit(1); }");
  test("kprobe:f { $a = 1; exit($a); }");
  test("kprobe:f { @a = exit(); }", 1);
  test("kprobe:f { @a = exit(1); }", 1);
  test("kprobe:f { $a = exit(1); }", 1);
  test("kprobe:f { @[exit(1)] = 1; }", 1);
  test("kprobe:f { if(exit()) { 123 } }", 2);
  test("kprobe:f { exit() ? 0 : 1; }", 2);

  test_error("kprobe:f { exit(1, 2); }", R"(
stdin:1:12-22: ERROR: exit() takes up to one argument (2 provided)
kprobe:f { exit(1, 2); }
           ~~~~~~~~~~
)");
  test_error("kprobe:f { $a = \"1\"; exit($a); }", R"(
stdin:1:22-30: ERROR: exit() only supports int arguments (string provided)
kprobe:f { $a = "1"; exit($a); }
                     ~~~~~~~~
)");
}

TEST(semantic_analyser, call_print)
{
  test("kprobe:f { @x = count(); print(@x); }");
  test("kprobe:f { @x = count(); print(@x, 5); }");
  test("kprobe:f { @x = count(); print(@x, 5, 10); }");
  test("kprobe:f { @x = count(); print(@x, 5, 10, 1); }", 1);
  test("kprobe:f { @x = count(); @x = print(); }", 1);

  test("kprobe:f { print(@x); @x[1,2] = count(); }");
  test("kprobe:f { @x[1,2] = count(); print(@x); }");

  test("kprobe:f { @x = count(); @ = print(@x); }", 1);
  test("kprobe:f { @x = count(); $y = print(@x); }", 1);
  test("kprobe:f { @x = count(); @[print(@x)] = 1; }", 1);
  test("kprobe:f { @x = count(); if(print(@x)) { 123 } }", 3);
  test("kprobe:f { @x = count(); print(@x) ? 0 : 1; }", 3);

  test_for_warning("kprobe:f { @x = stats(10); print(@x, 2); }",
                   "top and div arguments are ignored");
  test_for_warning("kprobe:f { @x = stats(10); print(@x, 2, 3); }",
                   "top and div arguments are ignored");
}

TEST(semantic_analyser, call_print_map_item)
{
  test(R"_(BEGIN { @x[1] = 1; print(@x[1]); })_");
  test(R"_(BEGIN { @x[1] = 1; @x[2] = 2; print(@x[2]); })_");
  test(R"_(BEGIN { @x[1] = 1; print(@x[2]); })_");
  test(R"_(BEGIN { @x[3, 5] = 1; print(@x[3, 5]); })_");
  test(R"_(BEGIN { @x[1,2] = "asdf"; print((1, 2, @x[1,2])); })_");

  test_error("BEGIN { @x[1] = 1; print(@x[\"asdf\"]); }", R"(
stdin:1:20-35: ERROR: Argument mismatch for @x: trying to access with arguments: 'string' when map expects arguments: 'int64'
BEGIN { @x[1] = 1; print(@x["asdf"]); }
                   ~~~~~~~~~~~~~~~
)");
  test_error("BEGIN { print(@x[2]); }", R"(
stdin:1:9-20: ERROR: Undefined map: @x
BEGIN { print(@x[2]); }
        ~~~~~~~~~~~
)");
  test_error("BEGIN { @x[1] = 1; print(@x[1], 3, 5); }", R"(
stdin:1:20-38: ERROR: Non-map print() only takes 1 argument, 3 found
BEGIN { @x[1] = 1; print(@x[1], 3, 5); }
                   ~~~~~~~~~~~~~~~~~~
)");
  test_error("BEGIN { @x[1] = hist(10); print(@x[1]); }", R"(
stdin:1:27-39: ERROR: Map type hist_t cannot print the value of individual keys. You must print the whole map.
BEGIN { @x[1] = hist(10); print(@x[1]); }
                          ~~~~~~~~~~~~
)");
}

TEST(semantic_analyser, call_print_non_map)
{
  test(R"_(BEGIN { print(1) })_");
  test(R"_(BEGIN { print(comm) })_");
  test(R"_(BEGIN { print(nsecs) })_");
  test(R"_(BEGIN { print("string") })_");
  test(R"_(BEGIN { print((1, 2, "tuple")) })_");
  test(R"_(BEGIN { $x = 1; print($x) })_");
  test(R"_(BEGIN { $x = 1; $y = $x + 3; print($y) })_");
  test(R"_(BEGIN { print((int8 *)0) })_");

  test(R"_(BEGIN { print(3, 5) })_", 1);
  test(R"_(BEGIN { print(3, 5, 2) })_", 1);

  test(R"_(BEGIN { print(exit()) })_", 2);
  test(R"_(BEGIN { print(count()) })_", 1);
  test(R"_(BEGIN { print(ctx) })_", 1);
}

TEST(semantic_analyser, call_clear)
{
  test("kprobe:f { @x = count(); clear(@x); }");
  test("kprobe:f { @x = count(); clear(@x, 1); }", 1);
  test("kprobe:f { @x = count(); @x = clear(); }", 1);

  test("kprobe:f { clear(@x); @x[1,2] = count(); }");
  test("kprobe:f { @x[1,2] = count(); clear(@x); }");
  test("kprobe:f { @x[1,2] = count(); clear(@x[3,4]); }", 1);

  test("kprobe:f { @x = count(); @ = clear(@x); }", 1);
  test("kprobe:f { @x = count(); $y = clear(@x); }", 1);
  test("kprobe:f { @x = count(); @[clear(@x)] = 1; }", 1);
  test("kprobe:f { @x = count(); if(clear(@x)) { 123 } }", 3);
  test("kprobe:f { @x = count(); clear(@x) ? 0 : 1; }", 3);
}

TEST(semantic_analyser, call_zero)
{
  test("kprobe:f { @x = count(); zero(@x); }");
  test("kprobe:f { @x = count(); zero(@x, 1); }", 1);
  test("kprobe:f { @x = count(); @x = zero(); }", 1);

  test("kprobe:f { zero(@x); @x[1,2] = count(); }");
  test("kprobe:f { @x[1,2] = count(); zero(@x); }");
  test("kprobe:f { @x[1,2] = count(); zero(@x[3,4]); }", 1);

  test("kprobe:f { @x = count(); @ = zero(@x); }", 1);
  test("kprobe:f { @x = count(); $y = zero(@x); }", 1);
  test("kprobe:f { @x = count(); @[zero(@x)] = 1; }", 1);
  test("kprobe:f { @x = count(); if(zero(@x)) { 123 } }", 3);
  test("kprobe:f { @x = count(); zero(@x) ? 0 : 1; }", 3);
}

TEST(semantic_analyser, call_len)
{
  test("kprobe:f { @x[0] = 0; len(@x); }");
  test("kprobe:f { @x[0] = 0; len(); }", 1);
  test("kprobe:f { @x[0] = 0; len(@x, 1); }", 1);
  test("kprobe:f { @x[0] = 0; len(@x[2]); }", 1);
  test("kprobe:f { $x = 0; len($x); }", 1);
  test("kprobe:f { len(ustack) }");
  test("kprobe:f { len(kstack) }");

  test_error("kprobe:f { len(0) }", R"(
stdin:1:12-18: ERROR: len() expects a map or stack to be provided
kprobe:f { len(0) }
           ~~~~~~
)");

  test_error("kprobe:f { @x = 1; @s = len(@x) }", R"(
stdin:1:25-31: ERROR: call to len() expects a map with explicit keys (non-scalar map)
kprobe:f { @x = 1; @s = len(@x) }
                        ~~~~~~
)");
}

TEST(semantic_analyser, call_has_key)
{
  test("kprobe:f { @x[1] = 0; if (has_key(@x, 1)) {} }");
  test("kprobe:f { @x[1, 2] = 0; if (has_key(@x, (3, 4))) {} }");
  test("kprobe:f { @x[1, (int8)2] = 0; if (has_key(@x, (3, 4))) {} }");
  test(R"(kprobe:f { @x[1, "hi"] = 0; if (has_key(@x, (2, "bye"))) {} })");
  test(
      R"(kprobe:f { @x[1, "hi"] = 0; if (has_key(@x, (2, "longerstr"))) {} })");
  test(
      R"(kprobe:f { @x[1, "longerstr"] = 0; if (has_key(@x, (2, "hi"))) {} })");
  test("kprobe:f { @x[1, 2] = 0; $a = (3, 4); if (has_key(@x, $a)) {} }");
  test("kprobe:f { @x[1, 2] = 0; @a = (3, 4); if (has_key(@x, @a)) {} }");
  test("kprobe:f { @x[1, 2] = 0; @a[1] = (3, 4); if (has_key(@x, @a[1])) {} }");
  test("kprobe:f { @x[1] = 0; @a = has_key(@x, 1); }");
  test("kprobe:f { @x[1] = 0; $a = has_key(@x, 1); }");
  test("kprobe:f { @x[1] = 0; @a[has_key(@x, 1)] = 1; }");

  test_error("kprobe:f { @x[1] = 1;  if (has_key(@x)) {} }",
             R"(
stdin:1:27-39: ERROR: has_key() requires 2 arguments (1 provided)
kprobe:f { @x[1] = 1;  if (has_key(@x)) {} }
                          ~~~~~~~~~~~~
)");

  test_error("kprobe:f { @x[1] = 1;  if (has_key(@x[1], 1)) {} }",
             R"(
stdin:1:27-41: ERROR: has_key() expects a map argument
kprobe:f { @x[1] = 1;  if (has_key(@x[1], 1)) {} }
                          ~~~~~~~~~~~~~~
)");

  test_error("kprobe:f { @x = 1;  if (has_key(@x, 1)) {} }",
             R"(
stdin:1:24-35: ERROR: call to has_key() expects a map with explicit keys (non-scalar map)
kprobe:f { @x = 1;  if (has_key(@x, 1)) {} }
                       ~~~~~~~~~~~
)");

  test_error("kprobe:f { @x[1, 2] = 1;  if (has_key(@x, 1)) {} }",
             R"(
stdin:1:43-44: ERROR: Argument mismatch for @x: trying to access with arguments: 'int64' when map expects arguments: '(int64,int64)'
kprobe:f { @x[1, 2] = 1;  if (has_key(@x, 1)) {} }
                                          ~
)");

  test_error(R"(kprobe:f { @x[1, "hi"] = 0; if (has_key(@x, (2, 1))) {} })",
             R"(
stdin:1:45-51: ERROR: Argument mismatch for @x: trying to access with arguments: '(int64,int64)' when map expects arguments: '(int64,string)'
kprobe:f { @x[1, "hi"] = 0; if (has_key(@x, (2, 1))) {} }
                                            ~~~~~~
)");

  test_error("kprobe:f { @x[1] = 1; $a = 1; if (has_key($a, 1)) {} }",
             R"(
stdin:1:34-45: ERROR: has_key() expects a map argument
kprobe:f { @x[1] = 1; $a = 1; if (has_key($a, 1)) {} }
                                 ~~~~~~~~~~~
)");

  test_error("kprobe:f { @a[1] = 1; has_key(@a, @a); }", R"(
stdin:1:35-37: ERROR: @a used as a map without an explicit key (scalar map), previously used with an explicit key (non-scalar map)
kprobe:f { @a[1] = 1; has_key(@a, @a); }
                                  ~~
)");
}

TEST(semantic_analyser, call_time)
{
  test("kprobe:f { time(); }");
  test("kprobe:f { time(\"%M:%S\"); }");
  test("kprobe:f { time(\"%M:%S\", 1); }", 1);
  test("kprobe:f { @x = time(); }", 1);
  test("kprobe:f { $x = time(); }", 1);
  test("kprobe:f { @[time()] = 1; }", 1);
  test("kprobe:f { time(1); }", 2);
  test("kprobe:f { $x = \"str\"; time($x); }", 2);
  test("kprobe:f { if(time()) { 123 } }", 2);
  test("kprobe:f { time() ? 0 : 1; }", 2);
}

TEST(semantic_analyser, call_strftime)
{
  test("kprobe:f { strftime(\"%M:%S\", 1); }");
  test("kprobe:f { strftime(\"%M:%S\", nsecs); }");
  test(R"(kprobe:f { strftime("%M:%S", ""); })", 2);
  test("kprobe:f { strftime(1, nsecs); }", 2);
  test("kprobe:f { $var = \"str\"; strftime($var, nsecs); }", 2);
  test("kprobe:f { strftime(); }", 1);
  test("kprobe:f { strftime(\"%M:%S\"); }", 1);
  test("kprobe:f { strftime(\"%M:%S\", 1, 1); }", 1);
  test("kprobe:f { strftime(1, 1, 1); }", 1);
  test(R"(kprobe:f { strftime("%M:%S", "", 1); })", 1);
  test("kprobe:f { $ts = strftime(\"%M:%S\", 1); }");
  test("kprobe:f { @ts = strftime(\"%M:%S\", nsecs); }");
  test("kprobe:f { @[strftime(\"%M:%S\", nsecs)] = 1; }");
  test(R"(kprobe:f { printf("%s", strftime("%M:%S", nsecs)); })");
  test(R"(kprobe:f { strncmp("str", strftime("%M:%S", nsecs), 10); })", 2);

  test("kprobe:f { strftime(\"%M:%S\", nsecs(monotonic)); }", 2);
  test("kprobe:f { strftime(\"%M:%S\", nsecs(boot)); }");
  test("kprobe:f { strftime(\"%M:%S\", nsecs(tai)); }");
}

TEST(semantic_analyser, call_str)
{
  test("kprobe:f { str(arg0); }");
  test("kprobe:f { @x = str(arg0); }");
  test("kprobe:f { str(); }", 1);
  test("kprobe:f { str(\"hello\"); }");
}

TEST(semantic_analyser, call_str_2_lit)
{
  test("kprobe:f { str(arg0, 3); }");
  test("kprobe:f { str(arg0, -3); }", 2);
  test("kprobe:f { @x = str(arg0, 3); }");
  test("kprobe:f { str(arg0, \"hello\"); }", 2);

  // Check the string size
  BPFtrace bpftrace;
  auto ast = test("kprobe:f { $x = str(arg0, 3); }");

  auto *x =
      ast.root->probes.at(0)->block->stmts.at(0).as<ast::AssignVarStatement>();
  EXPECT_EQ(CreateString(3), x->var()->var_type);
}

TEST(semantic_analyser, call_str_2_expr)
{
  test("kprobe:f { str(arg0, arg1); }");
  test("kprobe:f { @x = str(arg0, arg1); }");
}

TEST(semantic_analyser, call_str_state_leak_regression_test)
{
  // Previously, the semantic analyser would leak state in the first str()
  // call. This would make the semantic analyser think it's still processing
  // a positional parameter in the second str() call causing confusing error
  // messages.
  test(R"PROG(kprobe:f { $x = str($1) == "asdf"; $y = str(arg0, 1) })PROG");
}

TEST(semantic_analyser, call_buf)
{
  test("kprobe:f { buf(arg0, 1); }");
  test("kprobe:f { buf(arg0, -1); }", 1);
  test("kprobe:f { @x = buf(arg0, 1); }");
  test("kprobe:f { $x = buf(arg0, 1); }");
  test("kprobe:f { buf(); }", 1);
  test("kprobe:f { buf(\"hello\"); }", 2);
  test("struct x { int c[4] }; kprobe:f { $foo = (struct x*)0; @x = "
       "buf($foo->c); }");
}

TEST(semantic_analyser, call_buf_lit)
{
  test("kprobe:f { @x = buf(arg0, 3); }");
  test("kprobe:f { buf(arg0, \"hello\"); }", 2);
}

TEST(semantic_analyser, call_buf_expr)
{
  test("kprobe:f { buf(arg0, arg1); }");
  test("kprobe:f { @x = buf(arg0, arg1); }");
}

TEST(semantic_analyser, call_buf_posparam)
{
  BPFtrace bpftrace;
  bpftrace.add_param("1");
  bpftrace.add_param("hello");
  test(bpftrace, "kprobe:f { buf(arg0, $1); }");
  test(bpftrace, "kprobe:f { buf(arg0, $2); }", 1);
}

TEST(semantic_analyser, call_ksym)
{
  test("kprobe:f { ksym(arg0); }");
  test("kprobe:f { @x = ksym(arg0); }");
  test("kprobe:f { ksym(); }", 1);
  test("kprobe:f { ksym(\"hello\"); }", 1);
}

TEST(semantic_analyser, call_usym)
{
  test("kprobe:f { usym(arg0); }");
  test("kprobe:f { @x = usym(arg0); }");
  test("kprobe:f { usym(); }", 1);
  test("kprobe:f { usym(\"hello\"); }", 1);
}

TEST(semantic_analyser, call_ntop)
{
  std::string structs = "struct inet { unsigned char ipv4[4]; unsigned char "
                        "ipv6[16]; unsigned char invalid[10]; } ";

  test("kprobe:f { ntop(2, arg0); }");
  test("kprobe:f { ntop(arg0); }");
  test(structs + "kprobe:f { ntop(10, ((struct inet*)0)->ipv4); }");
  test(structs + "kprobe:f { ntop(10, ((struct inet*)0)->ipv6); }");
  test(structs + "kprobe:f { ntop(((struct inet*)0)->ipv4); }");
  test(structs + "kprobe:f { ntop(((struct inet*)0)->ipv6); }");

  test("kprobe:f { @x = ntop(2, arg0); }");
  test("kprobe:f { @x = ntop(arg0); }");
  test("kprobe:f { @x = ntop(2, 0xFFFF); }");
  test("kprobe:f { @x = ntop(0xFFFF); }");
  test(structs + "kprobe:f { @x = ntop(((struct inet*)0)->ipv4); }");
  test(structs + "kprobe:f { @x = ntop(((struct inet*)0)->ipv6); }");

  // Regression test that ntop can use arguments from the prog context
  test("tracepoint:tcp:some_tcp_tp { ntop(args.saddr_v6); }");

  test("kprobe:f { ntop(); }", 1);
  test("kprobe:f { ntop(2, \"hello\"); }", 1);
  test("kprobe:f { ntop(\"hello\"); }", 1);
  test(structs + "kprobe:f { ntop(((struct inet*)0)->invalid); }", 1);
}

TEST(semantic_analyser, call_pton)
{
  test("kprobe:f { $addr_v4 = pton(\"127.0.0.1\"); }");
  test("kprobe:f { $addr_v4 = pton(\"127.0.0.1\"); $b1 = $addr_v4[0]; }");
  test("kprobe:f { $addr_v6 = pton(\"::1\"); }");
  test("kprobe:f { $addr_v6 = pton(\"::1\"); $b1 = $addr_v6[0]; }");

  std::string def = "#define AF_INET 2\n #define AF_INET6 10\n";
  test("kprobe:f { $addr_v4_text = ntop(pton(\"127.0.0.1\")); }");
  test(def +
       "kprobe:f { $addr_v4_text = ntop(AF_INET, pton(\"127.0.0.1\")); }");
  test(def + "kprobe:f { $addr_v6_text = ntop(AF_INET6, pton(\"::1\")); }");

  test("kprobe:f { $addr_v4 = pton(); }", 1);
  test("kprobe:f { $addr_v4 = pton(\"\"); }", 1);
  test("kprobe:f { $addr_v4 = pton(\"127.0.1\"); }", 1);
  test("kprobe:f { $addr_v4 = pton(\"127.0.0.0.1\"); }", 1);
  test("kprobe:f { $addr_v6 = pton(\":\"); }", 1);
  test("kprobe:f { $addr_v6 = pton(\"1:1:1:1:1:1:1:1:1\"); }", 1);

  std::string structs = "struct inet { unsigned char non_literal_string[4]; } ";
  test("kprobe:f { $addr_v4 = pton(1); }", 1);
  test(structs + "kprobe:f { $addr_v4 = pton(((struct "
                 "inet*)0)->non_literal_string); }",
       1);
}

TEST(semantic_analyser, call_kaddr)
{
  test("kprobe:f { kaddr(\"avenrun\"); }");
  test("kprobe:f { @x = kaddr(\"avenrun\"); }");
  test("kprobe:f { kaddr(); }", 1);
  test("kprobe:f { kaddr(123); }", 1);
}

TEST(semantic_analyser, call_uaddr)
{
  test("u:/bin/sh:main { uaddr(\"github.com/golang/glog.severityName\"); }");
  test("uprobe:/bin/sh:main { uaddr(\"glob_asciirange\"); }");
  test("u:/bin/sh:main,u:/bin/sh:readline { uaddr(\"glob_asciirange\"); }");
  test("uprobe:/bin/sh:main { @x = uaddr(\"glob_asciirange\"); }");
  test("uprobe:/bin/sh:main { uaddr(); }", 1);
  test("uprobe:/bin/sh:main { uaddr(123); }", 1);
  test("uprobe:/bin/sh:main { uaddr(\"?\"); }", 1);
  test("uprobe:/bin/sh:main { $str = \"glob_asciirange\"; uaddr($str); }", 1);
  test("uprobe:/bin/sh:main { @str = \"glob_asciirange\"; uaddr(@str); }", 1);

  test("k:f { uaddr(\"A\"); }", 1);
  test("i:s:1 { uaddr(\"A\"); }", 1);

  // The C struct parser should set the is_signed flag on signed types
  BPFtrace bpftrace;
  std::string prog = "uprobe:/bin/sh:main {"
                     "$a = uaddr(\"12345_1\");"
                     "$b = uaddr(\"12345_2\");"
                     "$c = uaddr(\"12345_4\");"
                     "$d = uaddr(\"12345_8\");"
                     "$e = uaddr(\"12345_5\");"
                     "$f = uaddr(\"12345_33\");"
                     "}";

  auto ast = test(prog);

  std::vector<int> sizes = { 8, 16, 32, 64, 64, 64 };

  for (size_t i = 0; i < sizes.size(); i++) {
    auto *v = ast.root->probes.at(0)
                  ->block->stmts.at(i)
                  .as<ast::AssignVarStatement>();
    EXPECT_TRUE(v->var()->var_type.IsPtrTy());
    EXPECT_TRUE(v->var()->var_type.GetPointeeTy()->IsIntTy());
    EXPECT_EQ((unsigned long int)sizes.at(i),
              v->var()->var_type.GetPointeeTy()->GetIntBitWidth());
  }
}

TEST(semantic_analyser, call_cgroupid)
{
  // Handle args above default max-string length (64)
  test("kprobe:f { cgroupid("
       //          1         2         3         4         5         6
       "\"123456789/123456789/123456789/123456789/123456789/123456789/12345\""
       "); }");
}

TEST(semantic_analyser, call_reg)
{
#ifdef __x86_64__
  test("kprobe:f { reg(\"ip\"); }");
  test("kprobe:f { @x = reg(\"ip\"); }");
#endif
  test("kprobe:f { reg(\"blah\"); }", 1);
  test("kprobe:f { reg(); }", 1);
  test("kprobe:f { reg(123); }", 1);
}

TEST(semantic_analyser, call_func)
{
  test("kprobe:f { @[func] = count(); }");
  test("kprobe:f { printf(\"%s\", func);  }");
  test("uprobe:/bin/sh:f { @[func] = count(); }");
  test("uprobe:/bin/sh:f { printf(\"%s\", func);  }");

  test("fentry:f { func }");
  test("fexit:f { func }");
  test("kretprobe:f { func }");
  test("uretprobe:/bin/sh:f { func }");

  // We only care about the BPF_FUNC_get_func_ip feature and error message here,
  // but don't have enough control over the mock features to only disable that.
  test_error("fentry:f { func }",
             R"(
stdin:1:1-9: ERROR: fentry/fexit not available for your kernel version.
fentry:f { func }
~~~~~~~~
stdin:1:12-16: ERROR: BPF_FUNC_get_func_ip not available for your kernel version
fentry:f { func }
           ~~~~
)",
             false);

  test_error("fexit:f { func }",
             R"(
stdin:1:1-8: ERROR: fentry/fexit not available for your kernel version.
fexit:f { func }
~~~~~~~
stdin:1:11-15: ERROR: BPF_FUNC_get_func_ip not available for your kernel version
fexit:f { func }
          ~~~~
)",
             false);

  test_error("kretprobe:f { func }",
             R"(
stdin:1:15-19: ERROR: The 'func' builtin is not available for kretprobes on kernels without the get_func_ip BPF feature. Consider using the 'probe' builtin instead.
kretprobe:f { func }
              ~~~~
)",
             false);

  test_error("uretprobe:/bin/sh:f { func }",
             R"(
stdin:1:23-27: ERROR: The 'func' builtin is not available for uretprobes on kernels without the get_func_ip BPF feature. Consider using the 'probe' builtin instead.
uretprobe:/bin/sh:f { func }
                      ~~~~
)",
             false);
}

TEST(semantic_analyser, call_probe)
{
  test("kprobe:f { @[probe] = count(); }");
  test("kprobe:f { printf(\"%s\", probe);  }");
}

TEST(semantic_analyser, call_cat)
{
  test("kprobe:f { cat(\"/proc/loadavg\"); }");
  test("kprobe:f { cat(\"/proc/%d/cmdline\", 1); }");
  test("kprobe:f { cat(); }", 1);
  test("kprobe:f { cat(123); }", 1);
  test("kprobe:f { @x = cat(\"/proc/loadavg\"); }", 1);
  test("kprobe:f { $x = cat(\"/proc/loadavg\"); }", 1);
  test("kprobe:f { @[cat(\"/proc/loadavg\")] = 1; }", 1);
  test("kprobe:f { if(cat(\"/proc/loadavg\")) { 123 } }", 2);
  test("kprobe:f { cat(\"/proc/loadavg\") ? 0 : 1; }", 2);
}

TEST(semantic_analyser, call_stack)
{
  test("kprobe:f { kstack() }");
  test("kprobe:f { ustack() }");
  test("kprobe:f { kstack(bpftrace) }");
  test("kprobe:f { ustack(bpftrace) }");
  test("kprobe:f { kstack(perf) }");
  test("kprobe:f { ustack(perf) }");
  test("kprobe:f { kstack(3) }");
  test("kprobe:f { ustack(3) }");
  test("kprobe:f { kstack(perf, 3) }");
  test("kprobe:f { ustack(perf, 3) }");
  test("kprobe:f { kstack(raw, 3) }");
  test("kprobe:f { ustack(raw, 3) }");

  // Wrong arguments
  test("kprobe:f { kstack(3, perf) }", 1);
  test("kprobe:f { ustack(3, perf) }", 1);
  test("kprobe:f { kstack(perf, 3, 4) }", 1);
  test("kprobe:f { ustack(perf, 3, 4) }", 1);
  test("kprobe:f { kstack(bob) }", 1);
  test("kprobe:f { ustack(bob) }", 1);
  test("kprobe:f { kstack(\"str\") }", 1);
  test("kprobe:f { ustack(\"str\") }", 1);
  test("kprobe:f { kstack(perf, \"str\") }", 1);
  test("kprobe:f { ustack(perf, \"str\") }", 1);
  test("kprobe:f { kstack(\"str\", 3) }", 1);
  test("kprobe:f { ustack(\"str\", 3) }", 1);

  // Non-literals
  test("kprobe:f { @x = perf; kstack(@x) }", 1);
  test("kprobe:f { @x = perf; ustack(@x) }", 1);
  test("kprobe:f { @x = perf; kstack(@x, 3) }", 1);
  test("kprobe:f { @x = perf; ustack(@x, 3) }", 1);
  test("kprobe:f { @x = 3; kstack(@x) }", 1);
  test("kprobe:f { @x = 3; ustack(@x) }", 1);
  test("kprobe:f { @x = 3; kstack(perf, @x) }", 1);
  test("kprobe:f { @x = 3; ustack(perf, @x) }", 1);

  // Positional params
  BPFtrace bpftrace;
  bpftrace.add_param("3");
  bpftrace.add_param("hello");
  test(bpftrace, "kprobe:f { kstack($1) }");
  test(bpftrace, "kprobe:f { ustack($1) }");
  test(bpftrace, "kprobe:f { kstack(perf, $1) }");
  test(bpftrace, "kprobe:f { ustack(perf, $1) }");
  test(bpftrace, "kprobe:f { kstack($2) }", 1);
  test(bpftrace, "kprobe:f { ustack($2) }", 1);
  test(bpftrace, "kprobe:f { kstack(perf, $2) }", 1);
  test(bpftrace, "kprobe:f { ustack(perf, $2) }", 1);
}

TEST(semantic_analyser, call_macaddr)
{
  std::string structs =
      "struct mac { char addr[6]; }; struct invalid { char addr[7]; }; ";

  test("kprobe:f { macaddr(arg0); }");

  test(structs + "kprobe:f { macaddr((struct mac*)arg0); }");

  test(structs + "kprobe:f { @x[macaddr((struct mac*)arg0)] = 1; }");
  test(structs + "kprobe:f { @x = macaddr((struct mac*)arg0); }");

  test(structs + "kprobe:f { printf(\"%s\", macaddr((struct mac*)arg0)); }");

  test(structs + "kprobe:f { macaddr(((struct invalid*)arg0)->addr); }", 1);
  test(structs + "kprobe:f { macaddr(*(struct mac*)arg0); }", 1);

  test("kprobe:f { macaddr(); }", 1);
  test("kprobe:f { macaddr(\"hello\"); }", 1);
}

TEST(semantic_analyser, call_bswap)
{
  test("kprobe:f { bswap(arg0); }");

  test("kprobe:f { bswap(0x12); }");
  test("kprobe:f { bswap(0x12 + 0x34); }");

  test("kprobe:f { bswap((int8)0x12); }");
  test("kprobe:f { bswap((int16)0x12); }");
  test("kprobe:f { bswap((int32)0x12); }");
  test("kprobe:f { bswap((int64)0x12); }");

  test("kprobe:f { bswap(); }", 1);
  test("kprobe:f { bswap(0x12, 0x34); }", 1);

  test("kprobe:f { bswap(\"hello\"); }", 1);
}

TEST(semantic_analyser, call_cgroup_path)
{
  test("kprobe:f { cgroup_path(1) }");
  test("kprobe:f { cgroup_path(1, \"hello\") }");

  test("kprobe:f { cgroup_path(1, 2) }", 2);
  test("kprobe:f { cgroup_path(\"1\") }", 2);

  test("kprobe:f { printf(\"%s\", cgroup_path(1)) }");
  test("kprobe:f { printf(\"%s %s\", cgroup_path(1), cgroup_path(2)) }");
  test("kprobe:f { $var = cgroup_path(0); printf(\"%s %s\", $var, $var) }");

  test("kprobe:f { printf(\"%d\", cgroup_path(1)) }", 2);
}

TEST(semantic_analyser, call_strerror)
{
  test("kprobe:f { strerror(1) }");

  test("kprobe:f { strerror(1, 2) }", 1);
  test("kprobe:f { strerror(\"1\") }", 2);

  test("kprobe:f { printf(\"%s\", strerror(1)) }");
  test("kprobe:f { printf(\"%s %s\", strerror(1), strerror(2)) }");
  test("kprobe:f { $var = strerror(0); printf(\"%s %s\", $var, $var) }");

  test("kprobe:f { printf(\"%d\", strerror(1)) }", 2);
}

TEST(semantic_analyser, map_reassignment)
{
  test("kprobe:f { @x = 1; @x = 2; }");
  test("kprobe:f { @x = 1; @x = \"foo\"; }", 1);
}

TEST(semantic_analyser, variable_reassignment)
{
  test("kprobe:f { $x = 1; $x = 2; }");
  test("kprobe:f { $x = 1; $x = \"foo\"; }", 1);
  test(R"(kprobe:f { $b = "hi"; $b = @b; } kprobe:g { @b = "bye"; })");

  test_error(R"(kprobe:f { $b = "hi"; $b = @b; } kprobe:g { @b = 1; })", R"(
stdin:1:23-30: ERROR: Type mismatch for $b: trying to assign value of type 'int64' when variable already contains a value of type 'string'
kprobe:f { $b = "hi"; $b = @b; } kprobe:g { @b = 1; }
                      ~~~~~~~
)");
}

TEST(semantic_analyser, map_use_before_assign)
{
  test("kprobe:f { @x = @y; @y = 2; }");
}

TEST(semantic_analyser, variable_use_before_assign)
{
  test("kprobe:f { @x = $y; $y = 2; }", 1);
}

TEST(semantic_analyser, maps_are_global)
{
  test("kprobe:f { @x = 1 } kprobe:g { @y = @x }");
  test("kprobe:f { @x = 1 } kprobe:g { @x = \"abc\" }", 1);
}

TEST(semantic_analyser, variables_are_local)
{
  test("kprobe:f { $x = 1 } kprobe:g { $x = \"abc\"; }");
  test("kprobe:f { $x = 1 } kprobe:g { @y = $x }", 1);
}

TEST(semantic_analyser, array_access)
{
  test("kprobe:f { $s = arg0; @x = $s->y[0];}", 3);
  test("kprobe:f { $s = 0; @x = $s->y[0];}", 3);
  test("struct MyStruct { int y[4]; } kprobe:f { $s = (struct MyStruct *) "
       "arg0; @x = $s->y[5];}",
       3);
  test("struct MyStruct { int y[4]; } kprobe:f { $s = (struct MyStruct *) "
       "arg0; @x = $s->y[-1];}",
       3);
  test("struct MyStruct { int y[4]; } kprobe:f { $s = (struct MyStruct *) "
       "arg0; @x = $s->y[\"0\"];}",
       3);
  test("struct MyStruct { int y[4]; } kprobe:f { $s = (struct MyStruct *) "
       "arg0; $idx = 0; @x = $s->y[$idx];}",
       3);
  test("kprobe:f { $s = arg0; @x = $s[0]; }", 3);
  test("struct MyStruct { void *y; } kprobe:f { $s = (struct MyStruct *) "
       "arg0; @x = $s->y[5];}",
       3);
  BPFtrace bpftrace;
  auto ast = test(
      "struct MyStruct { int y[4]; } kprobe:f { $s = (struct MyStruct *) "
      "arg0; @x = $s->y[0];}");
  auto *assignment =
      ast.root->probes.at(0)->block->stmts.at(1).as<ast::AssignMapStatement>();
  EXPECT_EQ(CreateInt64(), assignment->map->value_type);

  ast = test(
      "struct MyStruct { int y[4]; } kprobe:f { $s = ((struct MyStruct *) "
      "arg0)->y; @x = $s[0];}");
  auto *array_var_assignment =
      ast.root->probes.at(0)->block->stmts.at(0).as<ast::AssignVarStatement>();
  EXPECT_EQ(CreateArray(4, CreateInt32()),
            array_var_assignment->var()->var_type);

  ast = test(
      "struct MyStruct { int y[4]; } kprobe:f { @a[0] = ((struct MyStruct *) "
      "arg0)->y; @x = @a[0][0];}");
  auto *array_map_assignment =
      ast.root->probes.at(0)->block->stmts.at(0).as<ast::AssignMapStatement>();
  EXPECT_EQ(CreateArray(4, CreateInt32()),
            array_map_assignment->map->value_type);

  ast = test("kprobe:f { $s = (int32 *) arg0; $x = $s[0]; }");
  auto *var_assignment =
      ast.root->probes.at(0)->block->stmts.at(1).as<ast::AssignVarStatement>();
  EXPECT_EQ(CreateInt32(), var_assignment->var()->var_type);

  // Positional parameter as index
  bpftrace.add_param("0");
  bpftrace.add_param("hello");
  test(bpftrace,
       "struct MyStruct { int y[4]; } "
       "kprobe:f { $s = ((struct MyStruct *)arg0)->y[$1]; }");
  test(bpftrace,
       "struct MyStruct { int y[4]; } "
       "kprobe:f { $s = ((struct MyStruct *)arg0)->y[$2]; }",
       2);

  test(bpftrace,
       "struct MyStruct { int x; int y[]; } "
       "kprobe:f { $s = (struct MyStruct *) "
       "arg0; @y = $s->y[0];}");
}

TEST(semantic_analyser, array_in_map)
{
  test("struct MyStruct { int x[2]; int y[4]; } "
       "kprobe:f { @ = ((struct MyStruct *)arg0)->x; }");
  test("struct MyStruct { int x[2]; int y[4]; } "
       "kprobe:f { @a[0] = ((struct MyStruct *)arg0)->x; }");
  // Mismatched map value types
  test("struct MyStruct { int x[2]; int y[4]; } "
       "kprobe:f { "
       "    @a[0] = ((struct MyStruct *)arg0)->x; "
       "    @a[1] = ((struct MyStruct *)arg0)->y; "
       "}",
       1);
  test("#include <stdint.h>\n"
       "struct MyStruct { uint8_t x[8]; uint32_t y[2]; }"
       "kprobe:f { "
       "    @a[0] = ((struct MyStruct *)arg0)->x; "
       "    @a[1] = ((struct MyStruct *)arg0)->y; "
       "}",
       1);
}

TEST(semantic_analyser, array_as_map_key)
{
  test("struct MyStruct { int x[2]; int y[4]; }"
       "kprobe:f { @x[((struct MyStruct *)arg0)->x] = 0; }");

  test("struct MyStruct { int x[2]; int y[4]; }"
       "kprobe:f { @x[((struct MyStruct *)arg0)->x, "
       "              ((struct MyStruct *)arg0)->y] = 0; }");

  // Mismatched key types
  test_error(R"(
    struct MyStruct { int x[2]; int y[4]; }
    BEGIN {
      @x[((struct MyStruct *)0)->x] = 0;
      @x[((struct MyStruct *)0)->y] = 1;
    })",
             R"(
stdin:4:7-36: ERROR: Argument mismatch for @x: trying to access with arguments: 'int32[4]' when map expects arguments: 'int32[2]'
      @x[((struct MyStruct *)0)->y] = 1;
      ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
)");
}

TEST(semantic_analyser, array_compare)
{
  test("#include <stdint.h>\n"
       "struct MyStruct { uint8_t x[4]; }"
       "kprobe:f { $s = (struct MyStruct *) arg0; @ = ($s->x == $s->x); }");
  test("#include <stdint.h>\n"
       "struct MyStruct { uint64_t x[4]; } "
       "kprobe:f { $s = (struct MyStruct *) arg0; @ = ($s->x == $s->x); }");
  test("struct MyStruct { int x[4]; } "
       "kprobe:f { $s = (struct MyStruct *) arg0; @ = ($s->x != $s->x); }");

  // unsupported operators
  test("struct MyStruct { int x[4]; } "
       "kprobe:f { $s = (struct MyStruct *) arg0; @ = ($s->x > $s->x); }",
       3);

  // different length
  test("struct MyStruct { int x[4]; int y[8]; }"
       "kprobe:f { $s = (struct MyStruct *) arg0; @ = ($s->x == $s->y); }",
       3);

  // different element type
  test("#include <stdint.h>\n"
       "struct MyStruct { uint8_t x[4]; uint16_t y[4]; } "
       "kprobe:f { $s = (struct MyStruct *) arg0; @ = ($s->x == $s->y); }",
       3);

  // compare with other type
  test("struct MyStruct { int x[4]; int y; } "
       "kprobe:f { $s = (struct MyStruct *) arg0; @ = ($s->x == $s->y); }",
       3);
}

TEST(semantic_analyser, variable_type)
{
  BPFtrace bpftrace;
  auto ast = test("kprobe:f { $x = 1 }");
  auto st = CreateInt64();
  auto *assignment =
      ast.root->probes.at(0)->block->stmts.at(0).as<ast::AssignVarStatement>();
  EXPECT_EQ(st, assignment->var()->var_type);
}

TEST(semantic_analyser, unroll)
{
  test("kprobe:f { $i = 0; unroll(5) { printf(\"i: %d\\n\", $i); $i = $i + 1; "
       "} }");
  test("kprobe:f { $i = 0; unroll(101) { printf(\"i: %d\\n\", $i); $i = $i + "
       "1; } }",
       1);
  test("kprobe:f { $i = 0; unroll(0) { printf(\"i: %d\\n\", $i); $i = $i + 1; "
       "} }",
       1);

  BPFtrace bpftrace;
  bpftrace.add_param("10");
  bpftrace.add_param("hello");
  bpftrace.add_param("101");
  test(bpftrace, R"(kprobe:f { unroll($#) { printf("hi\n"); } })");
  test(bpftrace, R"(kprobe:f { unroll($1) { printf("hi\n"); } })");
  test(bpftrace, R"(kprobe:f { unroll($2) { printf("hi\n"); } })", 1);
  test(bpftrace, R"(kprobe:f { unroll($3) { printf("hi\n"); } })", 1);
}

TEST(semantic_analyser, map_integer_sizes)
{
  BPFtrace bpftrace;
  auto ast = test("kprobe:f { $x = (int32) -1; @x = $x; }");

  auto *var_assignment =
      ast.root->probes.at(0)->block->stmts.at(0).as<ast::AssignVarStatement>();
  auto *map_assignment =
      ast.root->probes.at(0)->block->stmts.at(1).as<ast::AssignMapStatement>();
  EXPECT_EQ(CreateInt32(), var_assignment->var()->var_type);
  EXPECT_EQ(CreateInt64(), map_assignment->map->value_type);
}

TEST(semantic_analyser, binop_integer_promotion)
{
  BPFtrace bpftrace;
  auto ast = test("kprobe:f { $x = (int32)5 + (int16)6 }");

  auto *var_assignment =
      ast.root->probes.at(0)->block->stmts.at(0).as<ast::AssignVarStatement>();
  EXPECT_EQ(CreateInt32(), var_assignment->var()->var_type);
}

TEST(semantic_analyser, binop_integer_no_promotion)
{
  BPFtrace bpftrace;
  auto ast = test("kprobe:f { $x = (int8)5 + (int8)6 }");

  auto *var_assignment =
      ast.root->probes.at(0)->block->stmts.at(0).as<ast::AssignVarStatement>();
  EXPECT_EQ(CreateInt8(), var_assignment->var()->var_type);
}

TEST(semantic_analyser, unop_dereference)
{
  test("kprobe:f { *0; }");
  test("struct X { int n; } kprobe:f { $x = (struct X*)0; *$x; }");
  test("struct X { int n; } kprobe:f { $x = *(struct X*)0; *$x; }", 1);
  test("kprobe:f { *\"0\"; }", 2);
}

TEST(semantic_analyser, unop_not)
{
  std::string structs = "struct X { int x; };";
  test("kprobe:f { ~0; }");
  test(structs + "kprobe:f { $x = *(struct X*)0; ~$x; }", 2);
  test(structs + "kprobe:f { $x = (struct X*)0; ~$x; }", 2);
  test("kprobe:f { ~\"0\"; }", 2);
}

TEST(semantic_analyser, unop_lnot)
{
  test("kprobe:f { !0; }");
  test("kprobe:f { !false; }");
  test("kprobe:f { !(int32)0; }");
  test("struct X { int n; } kprobe:f { $x = (struct X*)0; !$x; }", 2);
  test("struct X { int n; } kprobe:f { $x = *(struct X*)0; !$x; }", 2);
  test("kprobe:f { !\"0\"; }", 2);
}

TEST(semantic_analyser, unop_increment_decrement)
{
  test("kprobe:f { $x = 0; $x++; }");
  test("kprobe:f { $x = 0; $x--; }");
  test("kprobe:f { $x = 0; ++$x; }");
  test("kprobe:f { $x = 0; --$x; }");

  test("kprobe:f { @x++; }");
  test("kprobe:f { @x--; }");
  test("kprobe:f { ++@x; }");
  test("kprobe:f { --@x; }");

  test("kprobe:f { $x++; }", 1);
  test("kprobe:f { @x = \"a\"; @x++; }", 3); // should be 2
  test("kprobe:f { $x = \"a\"; $x++; }", 2);
}

TEST(semantic_analyser, printf)
{
  test("kprobe:f { printf(\"hi\") }");
  test("kprobe:f { printf(1234) }", 1);
  test("kprobe:f { printf() }", 1);
  test("kprobe:f { $fmt = \"mystring\"; printf($fmt) }", 1);
  test("kprobe:f { printf(\"%s\", comm) }");
  test("kprobe:f { printf(\"%-16s\", comm) }");
  test("kprobe:f { printf(\"%-10.10s\", comm) }");
  test("kprobe:f { printf(\"%A\", comm) }", 2);
  test("kprobe:f { @x = printf(\"hi\") }", 1);
  test("kprobe:f { $x = printf(\"hi\") }", 1);
  test("kprobe:f { printf(\"%d %d %d %d %d %d %d %d %d\", 1, 2, 3, 4, 5, 6, 7, "
       "8, 9); }");
  test("kprobe:f { printf(\"%dns\", nsecs) }");

  {
    // Long format string should be ok
    std::stringstream prog;

    prog << "i:ms:100 { printf(\"" << std::string(200, 'a')
         << " %d\\n\", 1); }";
    test(prog.str());
  }
}

TEST(semantic_analyser, debugf)
{
  test_for_warning(
      "kprobe:f { debugf(\"warning\") }",
      "The debugf() builtin is not recommended for production use.");
  test("kprobe:f { debugf(\"hi\") }");
  test("kprobe:f { debugf(1234) }", 1);
  test("kprobe:f { debugf() }", 1);
  test("kprobe:f { $fmt = \"mystring\"; debugf($fmt) }", 1);
  test("kprobe:f { debugf(\"%s\", comm) }");
  test("kprobe:f { debugf(\"%-16s\", comm) }");
  test("kprobe:f { debugf(\"%-10.10s\", comm) }");
  test("kprobe:f { debugf(\"%lluns\", nsecs) }");
  test("kprobe:f { debugf(\"%A\", comm) }", 2);
  test("kprobe:f { @x = debugf(\"hi\") }", 1);
  test("kprobe:f { $x = debugf(\"hi\") }", 1);
  test("kprobe:f { debugf(\"%d\", 1) }");
  test("kprobe:f { debugf(\"%d %d\", 1, 1) }");
  test("kprobe:f { debugf(\"%d %d %d\", 1, 1, 1) }");
  test("kprobe:f { debugf(\"%d %d %d %d\", 1, 1, 1, 1) }", 2);

  {
    // Long format string should be ok
    std::stringstream prog;
    prog << "i:ms:100 { debugf(\"" << std::string(59, 'a')
         << R"(%s\n", "a"); })";
    test(prog.str());
  }
}

TEST(semantic_analyser, system)
{
  test("kprobe:f { system(\"ls\") }", 0, false /* safe_mode */);
  test("kprobe:f { system(1234) }", 1, false /* safe_mode */);
  test("kprobe:f { system() }", 1, false /* safe_mode */);
  test("kprobe:f { $fmt = \"mystring\"; system($fmt) }",
       1,
       false /* safe_mode */);
}

TEST(semantic_analyser, printf_format_int)
{
  test("kprobe:f { printf(\"int: %d\", 1234) }");
  test("kprobe:f { printf(\"int: %d\", pid) }");
  test("kprobe:f { @x = 123; printf(\"int: %d\", @x) }");
  test("kprobe:f { $x = 123; printf(\"int: %d\", $x) }");

  test("kprobe:f { printf(\"int: %u\", 1234) }");
  test("kprobe:f { printf(\"int: %o\", 1234) }");
  test("kprobe:f { printf(\"int: %x\", 1234) }");
  test("kprobe:f { printf(\"int: %X\", 1234) }");
}

TEST(semantic_analyser, printf_format_int_with_length)
{
  test("kprobe:f { printf(\"int: %d\", 1234) }");
  test("kprobe:f { printf(\"int: %u\", 1234) }");
  test("kprobe:f { printf(\"int: %o\", 1234) }");
  test("kprobe:f { printf(\"int: %x\", 1234) }");
  test("kprobe:f { printf(\"int: %X\", 1234) }");
  test("kprobe:f { printf(\"int: %p\", 1234) }");

  test("kprobe:f { printf(\"int: %hhd\", 1234) }");
  test("kprobe:f { printf(\"int: %hhu\", 1234) }");
  test("kprobe:f { printf(\"int: %hho\", 1234) }");
  test("kprobe:f { printf(\"int: %hhx\", 1234) }");
  test("kprobe:f { printf(\"int: %hhX\", 1234) }");
  test("kprobe:f { printf(\"int: %hhp\", 1234) }");

  test("kprobe:f { printf(\"int: %hd\", 1234) }");
  test("kprobe:f { printf(\"int: %hu\", 1234) }");
  test("kprobe:f { printf(\"int: %ho\", 1234) }");
  test("kprobe:f { printf(\"int: %hx\", 1234) }");
  test("kprobe:f { printf(\"int: %hX\", 1234) }");
  test("kprobe:f { printf(\"int: %hp\", 1234) }");

  test("kprobe:f { printf(\"int: %ld\", 1234) }");
  test("kprobe:f { printf(\"int: %lu\", 1234) }");
  test("kprobe:f { printf(\"int: %lo\", 1234) }");
  test("kprobe:f { printf(\"int: %lx\", 1234) }");
  test("kprobe:f { printf(\"int: %lX\", 1234) }");
  test("kprobe:f { printf(\"int: %lp\", 1234) }");

  test("kprobe:f { printf(\"int: %lld\", 1234) }");
  test("kprobe:f { printf(\"int: %llu\", 1234) }");
  test("kprobe:f { printf(\"int: %llo\", 1234) }");
  test("kprobe:f { printf(\"int: %llx\", 1234) }");
  test("kprobe:f { printf(\"int: %llX\", 1234) }");
  test("kprobe:f { printf(\"int: %llp\", 1234) }");

  test("kprobe:f { printf(\"int: %jd\", 1234) }");
  test("kprobe:f { printf(\"int: %ju\", 1234) }");
  test("kprobe:f { printf(\"int: %jo\", 1234) }");
  test("kprobe:f { printf(\"int: %jx\", 1234) }");
  test("kprobe:f { printf(\"int: %jX\", 1234) }");
  test("kprobe:f { printf(\"int: %jp\", 1234) }");

  test("kprobe:f { printf(\"int: %zd\", 1234) }");
  test("kprobe:f { printf(\"int: %zu\", 1234) }");
  test("kprobe:f { printf(\"int: %zo\", 1234) }");
  test("kprobe:f { printf(\"int: %zx\", 1234) }");
  test("kprobe:f { printf(\"int: %zX\", 1234) }");
  test("kprobe:f { printf(\"int: %zp\", 1234) }");

  test("kprobe:f { printf(\"int: %td\", 1234) }");
  test("kprobe:f { printf(\"int: %tu\", 1234) }");
  test("kprobe:f { printf(\"int: %to\", 1234) }");
  test("kprobe:f { printf(\"int: %tx\", 1234) }");
  test("kprobe:f { printf(\"int: %tX\", 1234) }");
  test("kprobe:f { printf(\"int: %tp\", 1234) }");
}

TEST(semantic_analyser, printf_format_string)
{
  test(R"(kprobe:f { printf("str: %s", "mystr") })");
  test("kprobe:f { printf(\"str: %s\", comm) }");
  test("kprobe:f { printf(\"str: %s\", str(arg0)) }");
  test(R"(kprobe:f { @x = "hi"; printf("str: %s", @x) })");
  test(R"(kprobe:f { $x = "hi"; printf("str: %s", $x) })");
}

TEST(semantic_analyser, printf_bad_format_string)
{
  test(R"(kprobe:f { printf("%d", "mystr") })", 2);
  test("kprobe:f { printf(\"%d\", str(arg0)) }", 2);

  test("kprobe:f { printf(\"%s\", 1234) }", 2);
  test("kprobe:f { printf(\"%s\", arg0) }", 2);
}

TEST(semantic_analyser, printf_format_buf)
{
  test(R"(kprobe:f { printf("%r", buf("mystr", 5)) })");
}

TEST(semantic_analyser, printf_bad_format_buf)
{
  test(R"(kprobe:f { printf("%r", "mystr") })", 2);
  test("kprobe:f { printf(\"%r\", arg0) }", 2);
}

TEST(semantic_analyser, printf_format_buf_no_ascii)
{
  test(R"(kprobe:f { printf("%rx", buf("mystr", 5)) })");
}

TEST(semantic_analyser, printf_bad_format_buf_no_ascii)
{
  test(R"(kprobe:f { printf("%rx", "mystr") })", 2);
  test("kprobe:f { printf(\"%rx\", arg0) }", 2);
}

TEST(semantic_analyser, printf_format_buf_nonescaped_hex)
{
  test(R"(kprobe:f { printf("%rh", buf("mystr", 5)) })");
}

TEST(semantic_analyser, printf_bad_format_buf_nonescaped_hex)
{
  test(R"(kprobe:f { printf("%rh", "mystr") })", 2);
  test("kprobe:f { printf(\"%rh\", arg0) }", 2);
}

TEST(semantic_analyser, printf_format_multi)
{
  test(R"(kprobe:f { printf("%d %d %s", 1, 2, "mystr") })");
  test(R"(kprobe:f { printf("%d %s %d", 1, 2, "mystr") })", 2);
}

TEST(semantic_analyser, join)
{
  test("kprobe:f { join(arg0) }");
  test("kprobe:f { printf(\"%s\", join(arg0)) }", 2);
  test("kprobe:f { join() }", 1);
  test("kprobe:f { $fmt = \"mystring\"; join($fmt) }", 2);
  test("kprobe:f { @x = join(arg0) }", 1);
  test("kprobe:f { $x = join(arg0) }", 1);
}

TEST(semantic_analyser, join_delimiter)
{
  test("kprobe:f { join(arg0, \",\") }");
  test(R"(kprobe:f { printf("%s", join(arg0, ",")) })", 2);
  test(R"(kprobe:f { $fmt = "mystring"; join($fmt, ",") })", 2);
  test("kprobe:f { @x = join(arg0, \",\") }", 1);
  test("kprobe:f { $x = join(arg0, \",\") }", 1);
  test("kprobe:f { join(arg0, 3) }", 2);
}

TEST(semantic_analyser, kprobe)
{
  test("kprobe:f { 1 }");
  test("kretprobe:f { 1 }");
}

TEST(semantic_analyser, uprobe)
{
  test("uprobe:/bin/sh:f { 1 }");
  test("u:/bin/sh:f { 1 }");
  test("uprobe:/bin/sh:0x10 { 1 }");
  test("u:/bin/sh:0x10 { 1 }");
  test("uprobe:/bin/sh:f+0x10 { 1 }");
  test("u:/bin/sh:f+0x10 { 1 }");
  test("uprobe:sh:f { 1 }");
  test("uprobe:/bin/sh:cpp:f { 1 }");
  test("uprobe:/notexistfile:f { 1 }", 1);
  test("uprobe:notexistfile:f { 1 }", 1);
  test("uprobe:/bin/sh:nolang:f { 1 }", 1);

  test("uretprobe:/bin/sh:f { 1 }");
  test("ur:/bin/sh:f { 1 }");
  test("uretprobe:sh:f { 1 }");
  test("ur:sh:f { 1 }");
  test("uretprobe:/bin/sh:0x10 { 1 }");
  test("ur:/bin/sh:0x10 { 1 }");
  test("uretprobe:/bin/sh:cpp:f { 1 }");
  test("uretprobe:/notexistfile:f { 1 }", 1);
  test("uretprobe:notexistfile:f { 1 }", 1);
  test("uretprobe:/bin/sh:nolang:f { 1 }", 1);
}

TEST(semantic_analyser, usdt)
{
  test("usdt:/bin/sh:probe { 1 }");
  test("usdt:sh:probe { 1 }");
  test("usdt:/bin/sh:namespace:probe { 1 }");
  test("usdt:/notexistfile:probe { 1 }", 1);
  test("usdt:notexistfile:probe { 1 }", 1);
}

TEST(semantic_analyser, begin_end_probes)
{
  test("BEGIN { 1 }");
  test("BEGIN { 1 } BEGIN { 2 }", 2);

  test("END { 1 }");
  test("END { 1 } END { 2 }", 2);
}

TEST(semantic_analyser, self_probe)
{
  test("self:signal:SIGUSR1 { 1 }");

  test_error("self:signal:sighup { 1 }", R"(
stdin:1:1-19: ERROR: sighup is not a supported signal
self:signal:sighup { 1 }
~~~~~~~~~~~~~~~~~~
)");
  test_error("self:keypress:space { 1 }", R"(
stdin:1:1-20: ERROR: keypress is not a supported trigger
self:keypress:space { 1 }
~~~~~~~~~~~~~~~~~~~
)");
}

TEST(semantic_analyser, tracepoint)
{
  test("tracepoint:category:event { 1 }");
}

TEST(semantic_analyser, rawtracepoint)
{
  test("rawtracepoint:event { 1 }");
  test("rawtracepoint:event { arg0 }");
  test("rawtracepoint:mod:event { arg0 }");
}

#if defined(__x86_64__) || defined(__aarch64__)
TEST(semantic_analyser, watchpoint_invalid_modes)
{
  auto bpftrace = get_mock_bpftrace();
  bpftrace->procmon_ = std::make_unique<MockProcMon>(123);

#if defined(__x86_64__)
  test(*bpftrace, "watchpoint:0x1234:8:r { 1 }", 1);
#elif defined(__aarch64__)
  test(*bpftrace, "watchpoint:0x1234:8:r { 1 }");
#endif
  test(*bpftrace, "watchpoint:0x1234:8:rx { 1 }", 1);
  test(*bpftrace, "watchpoint:0x1234:8:wx { 1 }", 1);
  test(*bpftrace, "watchpoint:0x1234:8:xw { 1 }", 1);
  test(*bpftrace, "watchpoint:0x1234:8:rwx { 1 }", 1);
  test(*bpftrace, "watchpoint:0x1234:8:xx { 1 }", 1);
  test(*bpftrace, "watchpoint:0x1234:8:b { 1 }", 1);
}

TEST(semantic_analyser, watchpoint_absolute)
{
  auto bpftrace = get_mock_bpftrace();
  bpftrace->procmon_ = std::make_unique<MockProcMon>(123);

  test(*bpftrace, "watchpoint:0x1234:8:rw { 1 }");
  test(*bpftrace, "watchpoint:0x1234:9:rw { 1 }", 1);
  test(*bpftrace, "watchpoint:0x0:8:rw { 1 }", 1);
}

TEST(semantic_analyser, watchpoint_function)
{
  auto bpftrace = get_mock_bpftrace();
  bpftrace->procmon_ = std::make_unique<MockProcMon>(123);

  test(*bpftrace, "watchpoint:func1+arg2:8:rw { 1 }");
  test(*bpftrace, "w:func1+arg2:8:rw { 1 }");
  test(*bpftrace, "w:func1.one_two+arg2:8:rw { 1 }");
  test(*bpftrace, "watchpoint:func1+arg99999:8:rw { 1 }", 1);

  bpftrace->procmon_ = nullptr;
  test(*bpftrace, "watchpoint:func1+arg2:8:rw { 1 }", 1);
}

TEST(semantic_analyser, asyncwatchpoint)
{
  auto bpftrace = get_mock_bpftrace();
  bpftrace->procmon_ = std::make_unique<MockProcMon>(123);

  test(*bpftrace, "asyncwatchpoint:func1+arg2:8:rw { 1 }");
  test(*bpftrace, "aw:func1+arg2:8:rw { 1 }");
  test(*bpftrace, "aw:func1.one_two+arg2:8:rw { 1 }");
  test(*bpftrace, "asyncwatchpoint:func1+arg99999:8:rw { 1 }", 1);

  // asyncwatchpoint's may not use absolute addresses
  test(*bpftrace, "asyncwatchpoint:0x1234:8:rw { 1 }", 1);

  bpftrace->procmon_ = nullptr;
  test(*bpftrace, "watchpoint:func1+arg2:8:rw { 1 }", 1);
}
#endif // if defined(__x86_64__) || defined(__aarch64__)

TEST(semantic_analyser, args_builtin_wrong_use)
{
  test("BEGIN { args.foo }", 1);
  test("END { args.foo }", 1);
  test("kprobe:f { args.foo }", 1);
  test("kretprobe:f { args.foo }", 1);
  test("uretprobe:/bin/sh/:f { args.foo }", 1);
  test("profile:ms:1 { args.foo }", 1);
  test("usdt:sh:probe { args.foo }", 1);
  test("profile:ms:100 { args.foo }", 1);
  test("hardware:cache-references:1000000 { args.foo }", 1);
  test("software:faults:1000 { args.foo }", 1);
  test("interval:s:1 { args.foo }", 1);
}

TEST(semantic_analyser, profile)
{
  test("profile:hz:997 { 1 }");
  test("profile:s:10 { 1 }");
  test("profile:ms:100 { 1 }");
  test("profile:us:100 { 1 }");
  test("profile:unit:100 { 1 }", 1);
}

TEST(semantic_analyser, interval)
{
  test("interval:hz:997 { 1 }");
  test("interval:s:10 { 1 }");
  test("interval:ms:100 { 1 }");
  test("interval:us:100 { 1 }");
  test("interval:unit:100 { 1 }", 1);
}

TEST(semantic_analyser, variable_cast_types)
{
  std::string structs =
      "struct type1 { int field; } struct type2 { int field; }";
  test(structs +
       "kprobe:f { $x = (struct type1*)cpu; $x = (struct type1*)cpu; }");
  test(structs +
           "kprobe:f { $x = (struct type1*)cpu; $x = (struct type2*)cpu; }",
       1);
}

TEST(semantic_analyser, map_cast_types)
{
  std::string structs =
      "struct type1 { int field; } struct type2 { int field; }";
  test(structs +
       "kprobe:f { @x = *(struct type1*)cpu; @x = *(struct type1*)cpu; }");
  test(structs +
           "kprobe:f { @x = *(struct type1*)cpu; @x = *(struct type2*)cpu; }",
       1);
}

TEST(semantic_analyser, map_aggregations_implicit_cast)
{
  // When assigning an aggregation to a map containing integers,
  // the aggregation is implicitly cast to an integer.
  test("kprobe:f { @x = 1; @y = count(); @x = @y; }", R"(*
  =
   map: @x :: [int64]int64
    int: 0
   (int64)
    [] :: [count_t]
     map: @y :: [int64]count_t
     int: 0
*)");
  test("kprobe:f { @x = 1; @y = sum(5); @x = @y; }", R"(*
  =
   map: @x :: [int64]int64
    int: 0
   (int64)
    [] :: [sum_t]
     map: @y :: [int64]sum_t
     int: 0
*)");
  test("kprobe:f { @x = 1; @y = min(5); @x = @y; }", R"(*
  =
   map: @x :: [int64]int64
    int: 0
   (int64)
    [] :: [min_t]
     map: @y :: [int64]min_t
     int: 0
*)");
  test("kprobe:f { @x = 1; @y = max(5); @x = @y; }", R"(*
  =
   map: @x :: [int64]int64
    int: 0
   (int64)
    [] :: [max_t]
     map: @y :: [int64]max_t
     int: 0
*)");
  test("kprobe:f { @x = 1; @y = avg(5); @x = @y; }", R"(*
  =
   map: @x :: [int64]int64
    int: 0
   (int64)
    [] :: [avg_t]
     map: @y :: [int64]avg_t
     int: 0
*)");

  // Assigning to a newly declared map requires an explicit cast
  // to get the value of the aggregation.
  test("kprobe:f { @x = count(); @y = (uint64)@x; }");
  test("kprobe:f { @x = sum(5); @y = (uint64)@x; }");
  test("kprobe:f { @x = min(5); @y = (uint64)@x; }");
  test("kprobe:f { @x = max(5); @y = (uint64)@x; }");
  test("kprobe:f { @x = avg(5); @y = (uint64)@x; }");

  // However, if there is no explicit cast,
  // the assignment is rejected and casting is suggested.
  test_error("kprobe:f { @y = count(); @x = @y; }", R"(
stdin:1:26-33: ERROR: Map value 'count_t' cannot be assigned from one map to another. The function that returns this type must be called directly e.g. `@x = count();`.
kprobe:f { @y = count(); @x = @y; }
                         ~~~~~~~
HINT: Add a cast to integer if you want the value of the aggregate, e.g. `@x = (int64)@y;`.
)");
  test_error("kprobe:f { @y = sum(5); @x = @y; }", R"(
stdin:1:25-32: ERROR: Map value 'sum_t' cannot be assigned from one map to another. The function that returns this type must be called directly e.g. `@x = sum(retval);`.
kprobe:f { @y = sum(5); @x = @y; }
                        ~~~~~~~
HINT: Add a cast to integer if you want the value of the aggregate, e.g. `@x = (int64)@y;`.
)");
  test_error("kprobe:f { @y = min(5); @x = @y; }", R"(
stdin:1:25-32: ERROR: Map value 'min_t' cannot be assigned from one map to another. The function that returns this type must be called directly e.g. `@x = min(retval);`.
kprobe:f { @y = min(5); @x = @y; }
                        ~~~~~~~
HINT: Add a cast to integer if you want the value of the aggregate, e.g. `@x = (int64)@y;`.
)");
  test_error("kprobe:f { @y = max(5); @x = @y; }", R"(
stdin:1:25-32: ERROR: Map value 'max_t' cannot be assigned from one map to another. The function that returns this type must be called directly e.g. `@x = max(retval);`.
kprobe:f { @y = max(5); @x = @y; }
                        ~~~~~~~
HINT: Add a cast to integer if you want the value of the aggregate, e.g. `@x = (int64)@y;`.
)");
  test_error("kprobe:f { @y = avg(5); @x = @y; }", R"(
stdin:1:25-32: ERROR: Map value 'avg_t' cannot be assigned from one map to another. The function that returns this type must be called directly e.g. `@x = avg(retval);`.
kprobe:f { @y = avg(5); @x = @y; }
                        ~~~~~~~
HINT: Add a cast to integer if you want the value of the aggregate, e.g. `@x = (int64)@y;`.
)");
  test_error("kprobe:f { @y = stats(5); @x = @y; }", R"(
stdin:1:27-34: ERROR: Map value 'stats_t' cannot be assigned from one map to another. The function that returns this type must be called directly e.g. `@x = stats(arg2);`.
kprobe:f { @y = stats(5); @x = @y; }
                          ~~~~~~~
)");
  test_error("kprobe:f { @x = 1; @y = stats(5); @x = @y; }", R"(
stdin:1:35-42: ERROR: Map value 'stats_t' cannot be assigned from one map to another. The function that returns this type must be called directly e.g. `@x = stats(arg2);`.
kprobe:f { @x = 1; @y = stats(5); @x = @y; }
                                  ~~~~~~~
stdin:1:35-42: ERROR: Type mismatch for @x: trying to assign value of type 'stats_t' when map already contains a value of type 'int64'
kprobe:f { @x = 1; @y = stats(5); @x = @y; }
                                  ~~~~~~~
)");

  test("kprobe:f { @ = count(); if (@ > 0) { print((1)); } }");
  test("kprobe:f { @ = sum(5); if (@ > 0) { print((1)); } }");
  test("kprobe:f { @ = min(5); if (@ > 0) { print((1)); } }");
  test("kprobe:f { @ = max(5); if (@ > 0) { print((1)); } }");
  test("kprobe:f { @ = avg(5); if (@ > 0) { print((1)); } }");

  test_error("kprobe:f { @ = hist(5); if (@ > 0) { print((1)); } }", R"(
stdin:1:31-32: ERROR: Type mismatch for '>': comparing hist_t with int64
kprobe:f { @ = hist(5); if (@ > 0) { print((1)); } }
                              ~
stdin:1:28-30: ERROR: left (hist_t)
kprobe:f { @ = hist(5); if (@ > 0) { print((1)); } }
                           ~~
stdin:1:33-34: ERROR: right (int64)
kprobe:f { @ = hist(5); if (@ > 0) { print((1)); } }
                                ~
)");
  test_error("kprobe:f { @ = count(); @ += 5 }", R"(
stdin:1:25-31: ERROR: Type mismatch for @: trying to assign value of type 'uint64' when map already contains a value of type 'count_t'
kprobe:f { @ = count(); @ += 5 }
                        ~~~~~~
)");
}

TEST(semantic_analyser, map_aggregations_explicit_cast)
{
  test("kprobe:f { @ = count(); print((1, (uint16)@)); }");
  test("kprobe:f { @ = sum(5); print((1, (uint16)@)); }");
  test("kprobe:f { @ = min(5); print((1, (uint16)@)); }");
  test("kprobe:f { @ = max(5); print((1, (uint16)@)); }");
  test("kprobe:f { @ = avg(5); print((1, (uint16)@)); }");

  test_error("kprobe:f { @ = hist(5); print((1, (uint16)@)); }", R"(
stdin:1:35-43: ERROR: Cannot cast from "hist_t" to "uint16"
kprobe:f { @ = hist(5); print((1, (uint16)@)); }
                                  ~~~~~~~~
)");
}

TEST(semantic_analyser, variable_casts_are_local)
{
  std::string structs =
      "struct type1 { int field; } struct type2 { int field; }";
  test(structs + "kprobe:f { $x = *(struct type1 *)cpu } "
                 "kprobe:g { $x = *(struct type2 *)cpu; }");
}

TEST(semantic_analyser, map_casts_are_global)
{
  std::string structs =
      "struct type1 { int field; } struct type2 { int field; }";
  test(structs + "kprobe:f { @x = *(struct type1 *)cpu }"
                 "kprobe:g { @x = *(struct type2 *)cpu }",
       1);
}

TEST(semantic_analyser, cast_unknown_type)
{
  test_error("BEGIN { (struct faketype *)cpu }", R"(
stdin:1:9-29: ERROR: Cannot resolve unknown type "struct faketype"
BEGIN { (struct faketype *)cpu }
        ~~~~~~~~~~~~~~~~~~~~
)");
  test_error("BEGIN { (faketype)cpu }", R"(
stdin:1:9-19: ERROR: Cannot resolve unknown type "faketype"
BEGIN { (faketype)cpu }
        ~~~~~~~~~~
stdin:1:9-19: ERROR: Cannot cast to "faketype"
BEGIN { (faketype)cpu }
        ~~~~~~~~~~
)");
}

TEST(semantic_analyser, cast_c_integers)
{
  // Casting to a C integer type gives a hint with the correct name
  test_error("BEGIN { (char)cpu }", R"(
stdin:1:9-15: ERROR: Cannot resolve unknown type "char"
BEGIN { (char)cpu }
        ~~~~~~
stdin:1:9-15: ERROR: Cannot cast to "char"
BEGIN { (char)cpu }
        ~~~~~~
HINT: Did you mean "int8"?
)");
  test_error("BEGIN { (short)cpu }", R"(
stdin:1:9-16: ERROR: Cannot resolve unknown type "short"
BEGIN { (short)cpu }
        ~~~~~~~
stdin:1:9-16: ERROR: Cannot cast to "short"
BEGIN { (short)cpu }
        ~~~~~~~
HINT: Did you mean "int16"?
)");
  test_error("BEGIN { (int)cpu }", R"(
stdin:1:9-14: ERROR: Cannot resolve unknown type "int"
BEGIN { (int)cpu }
        ~~~~~
stdin:1:9-14: ERROR: Cannot cast to "int"
BEGIN { (int)cpu }
        ~~~~~
HINT: Did you mean "int32"?
)");
  test_error("BEGIN { (long)cpu }", R"(
stdin:1:9-15: ERROR: Cannot resolve unknown type "long"
BEGIN { (long)cpu }
        ~~~~~~
stdin:1:9-15: ERROR: Cannot cast to "long"
BEGIN { (long)cpu }
        ~~~~~~
HINT: Did you mean "int64"?
)");
}

TEST(semantic_analyser, cast_struct)
{
  // Casting struct by value is forbidden
  test_error("struct mytype { int field; }\n"
             "BEGIN { $s = (struct mytype *)cpu; (uint32)*$s; }",
             R"(
stdin:2:37-45: ERROR: Cannot cast from struct type "struct mytype"
BEGIN { $s = (struct mytype *)cpu; (uint32)*$s; }
                                    ~~~~~~~~
stdin:2:37-45: ERROR: Cannot cast from "struct mytype" to "uint32"
BEGIN { $s = (struct mytype *)cpu; (uint32)*$s; }
                                    ~~~~~~~~
)");
  test_error("struct mytype { int field; } BEGIN { (struct mytype)cpu }", R"(
stdin:1:38-54: ERROR: Cannot cast to "struct mytype"
struct mytype { int field; } BEGIN { (struct mytype)cpu }
                                     ~~~~~~~~~~~~~~~~
)");
}

TEST(semantic_analyser, cast_bool)
{
  test("kprobe:f { $a = (bool)1; }");
  test("kprobe:f { $a = (bool)\"str\"; }");
  test("kprobe:f { $a = (bool)comm; }");
  test("kprobe:f { $a = (int64 *)0; $b = (bool)$a; }");
  test("kprobe:f { $a = (int64)true; $b = (int64)false; }");

  test_error("kprobe:f { $a = (bool)kstack; }", R"(
stdin:1:17-23: ERROR: Cannot cast from "kstack" to "bool"
kprobe:f { $a = (bool)kstack; }
                ~~~~~~
)");

  test_error("kprobe:f { $a = (bool)pton(\"127.0.0.1\"); }", R"(
stdin:1:17-23: ERROR: Cannot cast from "uint8[4]" to "bool"
kprobe:f { $a = (bool)pton("127.0.0.1"); }
                ~~~~~~
)");
}

TEST(semantic_analyser, field_access)
{
  std::string structs = "struct type1 { int field; }";
  test(structs + "kprobe:f { $x = *(struct type1*)cpu; $x.field }");
  test(structs + "kprobe:f { @x = *(struct type1*)cpu; @x.field }");
  test("struct task_struct {int x;} kprobe:f { curtask->x }");
}

TEST(semantic_analyser, field_access_wrong_field)
{
  std::string structs = "struct type1 { int field; }";
  test(structs + "kprobe:f { ((struct type1 *)cpu)->blah }", 1);
  test(structs + "kprobe:f { $x = (struct type1 *)cpu; $x->blah }", 1);
  test(structs + "kprobe:f { @x = (struct type1 *)cpu; @x->blah }", 1);
}

TEST(semantic_analyser, field_access_wrong_expr)
{
  std::string structs = "struct type1 { int field; }";
  test(structs + "kprobe:f { 1234->field }", 2);
}

TEST(semantic_analyser, field_access_types)
{
  std::string structs = "struct type1 { int field; char mystr[8]; }"
                        "struct type2 { int field; }";

  test(structs + "kprobe:f { (*((struct type1*)0)).field == 123 }");
  test(structs + "kprobe:f { (*((struct type1*)0)).field == \"abc\" }", 2);

  test(structs + "kprobe:f { (*((struct type1*)0)).mystr == \"abc\" }");
  test(structs + "kprobe:f { (*((struct type1*)0)).mystr == 123 }", 2);

  test(structs + "kprobe:f { (*((struct type1*)0)).field == (*((struct "
                 "type2*)0)).field }");
  test(structs + "kprobe:f { (*((struct type1*)0)).mystr == (*((struct "
                 "type2*)0)).field }",
       2);
}

TEST(semantic_analyser, field_access_pointer)
{
  std::string structs = "struct type1 { int field; }";
  test(structs + "kprobe:f { ((struct type1*)0)->field }");
  test(structs + "kprobe:f { ((struct type1*)0).field }", 1);
  test(structs + "kprobe:f { *((struct type1*)0) }");
}

TEST(semantic_analyser, field_access_sub_struct)
{
  std::string structs =
      "struct type2 { int field; } "
      "struct type1 { struct type2 *type2ptr; struct type2 type2; }";

  test(structs + "kprobe:f { (*(struct type1*)0).type2ptr->field }");
  test(structs + "kprobe:f { (*(struct type1*)0).type2.field }");
  test(structs +
       "kprobe:f { $x = *(struct type2*)0; $x = (*(struct type1*)0).type2 }");
  test(structs + "kprobe:f { $x = (struct type2*)0; $x = (*(struct "
                 "type1*)0).type2ptr }");
  test(
      structs +
          "kprobe:f { $x = *(struct type1*)0; $x = (*(struct type1*)0).type2 }",
      1);
  test(structs + "kprobe:f { $x = (struct type1*)0; $x = (*(struct "
                 "type1*)0).type2ptr }",
       1);
}

TEST(semantic_analyser, field_access_is_internal)
{
  BPFtrace bpftrace;
  std::string structs = "struct type1 { int x; }";

  {
    auto ast = test(structs + "kprobe:f { $x = (*(struct type1*)0).x }");
    auto &stmts = ast.root->probes.at(0)->block->stmts;
    auto *var_assignment1 = stmts.at(0).as<ast::AssignVarStatement>();
    EXPECT_FALSE(var_assignment1->var()->var_type.is_internal);
  }

  {
    auto ast = test(structs +
                    "kprobe:f { @type1 = *(struct type1*)0; $x = @type1.x }");
    auto &stmts = ast.root->probes.at(0)->block->stmts;
    auto *map_assignment = stmts.at(0).as<ast::AssignMapStatement>();
    auto *var_assignment2 = stmts.at(1).as<ast::AssignVarStatement>();
    EXPECT_TRUE(map_assignment->map->value_type.is_internal);
    EXPECT_TRUE(var_assignment2->var()->var_type.is_internal);
  }
}

TEST(semantic_analyser, struct_as_map_key)
{
  test("struct A { int x; } struct B { char x; } "
       "kprobe:f { @x[*((struct A *)arg0)] = 0; }");

  test("struct A { int x; } struct B { char x; } "
       "kprobe:f { @x[*((struct A *)arg0), *((struct B *)arg1)] = 0; }");

  // Mismatched key types
  test_error(R"(
    struct A { int x; } struct B { char x; }
    BEGIN {
        @x[*((struct A *)0)] = 0;
        @x[*((struct B *)0)] = 1;
    })",
             R"(
stdin:4:9-13: ERROR: Argument mismatch for @x: trying to access with arguments: 'struct B' when map expects arguments: 'struct A'
        @x[*((struct B *)0)] = 1;
        ~~~~
)");
}

TEST(semantic_analyser, per_cpu_map_as_map_key)
{
  test("BEGIN { @x = count(); @y[@x] = 1; }");
  test("BEGIN { @x = sum(10); @y[@x] = 1; }");
  test("BEGIN { @x = min(1); @y[@x] = 1; }");
  test("BEGIN { @x = max(1); @y[@x] = 1; }");
  test("BEGIN { @x = avg(1); @y[@x] = 1; }");

  test_error("BEGIN { @x = hist(10); @y[@x] = 1; }", R"(
stdin:1:24-29: ERROR: hist_t cannot be used as a map key
BEGIN { @x = hist(10); @y[@x] = 1; }
                       ~~~~~
)");

  test_error("BEGIN { @x = lhist(10, 0, 10, 1); @y[@x] = 1; }", R"(
stdin:1:35-40: ERROR: lhist_t cannot be used as a map key
BEGIN { @x = lhist(10, 0, 10, 1); @y[@x] = 1; }
                                  ~~~~~
)");

  test_error("BEGIN { @x = tseries(10, 1s, 10); @y[@x] = 1; }", R"(
stdin:1:35-40: ERROR: tseries_t cannot be used as a map key
BEGIN { @x = tseries(10, 1s, 10); @y[@x] = 1; }
                                  ~~~~~
)");

  test_error("BEGIN { @x = stats(10); @y[@x] = 1; }", R"(
stdin:1:25-30: ERROR: stats_t cannot be used as a map key
BEGIN { @x = stats(10); @y[@x] = 1; }
                        ~~~~~
)");
}

TEST(semantic_analyser, probe_short_name)
{
  test("t:a:b { args }");
  test("k:f { pid }");
  test("kr:f { pid }");
  test("u:sh:f { 1 }");
  test("ur:sh:f { 1 }");
  test("p:hz:997 { 1 }");
  test("h:cache-references:1000000 { 1 }");
  test("s:faults:1000 { 1 }");
  test("i:s:1 { 1 }");
}

TEST(semantic_analyser, positional_parameters)
{
  BPFtrace bpftrace;
  bpftrace.add_param("123");
  bpftrace.add_param("hello");
  bpftrace.add_param("0x123");

  test(bpftrace, "kprobe:f { printf(\"%d\", $1); }");
  test(bpftrace, "kprobe:f { printf(\"%s\", str($1)); }");

  test(bpftrace, "kprobe:f { printf(\"%s\", str($2)); }");
  test(bpftrace, "kprobe:f { printf(\"%s\", str($2 + 1)); }");
  test(bpftrace, "kprobe:f { printf(\"%d\", $2); }", 2);

  test(bpftrace, "kprobe:f { printf(\"%d\", $3); }");

  // Pointer arithmetic in str() for parameters
  test(bpftrace, "kprobe:f { printf(\"%s\", str($1 + 1)); }");
  test(bpftrace, "kprobe:f { printf(\"%s\", str(1 + $1)); }");
  test(bpftrace, "kprobe:f { printf(\"%s\", str($1 + 4)); }");
  test(bpftrace, "kprobe:f { printf(\"%s\", str($1 * 2)); }");
  test(bpftrace, "kprobe:f { printf(\"%s\", str($1 + 1 + 1)); }");

  // Parameters are not required to exist to be used:
  test(bpftrace, "kprobe:f { printf(\"%s\", str($4)); }");
  test(bpftrace, "kprobe:f { printf(\"%d\", $4); }");

  test(bpftrace, "kprobe:f { printf(\"%d\", $#); }");
  test(bpftrace, "kprobe:f { printf(\"%s\", str($#)); }");
  test(bpftrace, "kprobe:f { printf(\"%s\", str($#+1)); }");

  // Parameters can be used as string literals
  test(bpftrace, "kprobe:f { printf(\"%d\", cgroupid(str($2))); }");

  auto ast = test("k:f { $1 }");
  auto *stmt =
      ast.root->probes.at(0)->block->stmts.at(0).as<ast::ExprStatement>();
  auto *pp = stmt->expr.as<ast::PositionalParameter>();
  EXPECT_EQ(CreateNone(), pp->type());

  bpftrace.add_param("0999");
  test(bpftrace, "kprobe:f { printf(\"%d\", $4); }", 2);
}

TEST(semantic_analyser, c_macros)
{
  test("#define A 1\nkprobe:f { printf(\"%d\", A); }");
  test("#define A A\nkprobe:f { printf(\"%d\", A); }", 1);
  test("enum { A = 1 }\n#define A A\nkprobe:f { printf(\"%d\", A); }");
}

TEST(semantic_analyser, enums)
{
  // Anonymous enums have empty string names in libclang <= 15,
  // so this is an important test
  test("enum { a = 1, b } kprobe:f { printf(\"%d\", a); }");
  test("enum { a = 1, b } kprobe:f { printf(\"%s\", a); }");
  test("enum { a = 1, b } kprobe:f { $e = a; printf(\"%s\", $e); }");
  test("enum { a = 1, b } kprobe:f { printf(\"%15s %-15s\", a, a); }");

  test("enum named { a = 1, b } kprobe:f { printf(\"%d\", a); }");
  test("enum named { a = 1, b } kprobe:f { printf(\"%s\", a); }");
  test("enum named { a = 1, b } kprobe:f { $e = a; printf(\"%s\", $e); }");
  test("enum named { a = 1, b } kprobe:f { printf(\"%15s %-15s\", a, a); }");

  // Cannot symbolize a non-enum
  test_error("kprobe:f { $x = (uint8)1; printf(\"%s\", $x) }", R"(
stdin:1:27-43: ERROR: printf: %s specifier expects a value of type string (int supplied)
kprobe:f { $x = (uint8)1; printf("%s", $x) }
                          ~~~~~~~~~~~~~~~~
)");
}

TEST(semantic_analyser, enum_casts)
{
  test("enum named { a = 1, b } kprobe:f { print((enum named)1); }");
  // We can't detect this issue because the cast expr is not a literal
  test("enum named { a = 1, b } kprobe:f { $x = 3; print((enum named)$x); }");

  test_error("enum named { a = 1, b } kprobe:f { print((enum named)3); }", R"(
stdin:1:36-55: ERROR: Enum: named doesn't contain a variant value of 3
enum named { a = 1, b } kprobe:f { print((enum named)3); }
                                   ~~~~~~~~~~~~~~~~~~~
)");

  test_error("enum Foo { a = 1, b } kprobe:f { print((enum Bar)1); }", R"(
stdin:1:34-51: ERROR: Unknown enum: Bar
enum Foo { a = 1, b } kprobe:f { print((enum Bar)1); }
                                 ~~~~~~~~~~~~~~~~~
)");

  test_error("enum named { a = 1, b } kprobe:f { $a = \"str\"; print((enum "
             "named)$a); }",
             R"(
stdin:1:48-67: ERROR: Cannot cast from "string" to "enum named"
enum named { a = 1, b } kprobe:f { $a = "str"; print((enum named)$a); }
                                               ~~~~~~~~~~~~~~~~~~~
)");
}

TEST(semantic_analyser, signed_int_comparison_warnings)
{
  bool invert = true;
  std::string cmp_sign = "comparison of integers of different signs";
  test_for_warning("kretprobe:f /-1 < retval/ {}", cmp_sign);
  test_for_warning("kretprobe:f /-1 > retval/ {}", cmp_sign);
  test_for_warning("kretprobe:f /-1 >= retval/ {}", cmp_sign);
  test_for_warning("kretprobe:f /-1 <= retval/ {}", cmp_sign);
  test_for_warning("kretprobe:f /-1 != retval/ {}", cmp_sign);
  test_for_warning("kretprobe:f /-1 == retval/ {}", cmp_sign);
  test_for_warning("kretprobe:f /retval > -1/ {}", cmp_sign);
  test_for_warning("kretprobe:f /retval < -1/ {}", cmp_sign);

  // These should not trigger a warning
  test_for_warning("kretprobe:f /1 < retval/ {}", cmp_sign, invert);
  test_for_warning("kretprobe:f /1 > retval/ {}", cmp_sign, invert);
  test_for_warning("kretprobe:f /1 >= retval/ {}", cmp_sign, invert);
  test_for_warning("kretprobe:f /1 <= retval/ {}", cmp_sign, invert);
  test_for_warning("kretprobe:f /1 != retval/ {}", cmp_sign, invert);
  test_for_warning("kretprobe:f /1 == retval/ {}", cmp_sign, invert);
  test_for_warning("kretprobe:f /retval > 1/ {}", cmp_sign, invert);
  test_for_warning("kretprobe:f /retval < 1/ {}", cmp_sign, invert);
}

TEST(semantic_analyser, string_comparison)
{
  test("struct MyStruct {char y[4]; } kprobe:f { $s = (struct MyStruct*)arg0; "
       "$s->y == \"abc\"}");
  test("struct MyStruct {char y[4]; } kprobe:f { $s = (struct MyStruct*)arg0; "
       "\"abc\" != $s->y}");
  test("struct MyStruct {char y[4]; } kprobe:f { $s = (struct MyStruct*)arg0; "
       "\"abc\" == \"abc\"}");

  bool invert = true;
  std::string msg = "the condition is always false";
  test_for_warning("struct MyStruct {char y[4]; } kprobe:f { $s = (struct "
                   "MyStruct*)arg0; $s->y == \"long string\"}",
                   msg,
                   invert);
  test_for_warning("struct MyStruct {char y[4]; } kprobe:f { $s = (struct "
                   "MyStruct*)arg0; \"long string\" != $s->y}",
                   msg,
                   invert);
}

TEST(semantic_analyser, signed_int_arithmetic_warnings)
{
  // Test type warnings for arithmetic
  bool invert = true;
  std::string msg = "arithmetic on integers of different signs";

  test_for_warning("kprobe:f { @ = -1 - arg0 }", msg);
  test_for_warning("kprobe:f { @ = -1 + arg0 }", msg);
  test_for_warning("kprobe:f { @ = -1 * arg0 }", msg);
  test_for_warning("kprobe:f { @ = -1 / arg0 }", msg);

  test_for_warning("kprobe:f { @ = arg0 + 1 }", msg, invert);
  test_for_warning("kprobe:f { @ = arg0 - 1 }", msg, invert);
  test_for_warning("kprobe:f { @ = arg0 * 1 }", msg, invert);
  test_for_warning("kprobe:f { @ = arg0 / 1 }", msg, invert);
}

TEST(semantic_analyser, signed_int_division_warnings)
{
  bool invert = true;
  std::string msg = "signed operands";
  test_for_warning("kprobe:f { @x = -1; @y = @x / 1 }", msg);
  test_for_warning("kprobe:f { @x = (uint64)1; @y = @x / -1 }", msg);

  // These should not trigger a warning. Note that we need to assign to a map
  // in order to ensure that they are typed. Literals are not yet typed.
  test_for_warning("kprobe:f { @x = (uint64)1; @y = @x / 1 }", msg, invert);
  test_for_warning("kprobe:f { @x = (uint64)1; @y = -(@x / 1) }", msg, invert);
}

TEST(semantic_analyser, signed_int_modulo_warnings)
{
  bool invert = true;
  std::string msg = "signed operands";
  test_for_warning("kprobe:f { @x = -1; @y = @x % 1 }", msg);
  test_for_warning("kprobe:f { @x = (uint64)1; @y = @x % -1 }", msg);

  // These should not trigger a warning. See above re: types.
  test_for_warning("kprobe:f { @x = (uint64)1; @y = @x % 1 }", msg, invert);
  test_for_warning("kprobe:f { @x = (uint64)1; @y = -(@x % 1) }", msg, invert);
}

TEST(semantic_analyser, map_as_lookup_table)
{
  // Initializing a map should not lead to usage issues
  test("BEGIN { @[0] = \"abc\"; @[1] = \"def\" } kretprobe:f { "
       "printf(\"%s\\n\", @[(int64)retval])}");
}

TEST(semantic_analyser, cast_sign)
{
  // The C struct parser should set the is_signed flag on signed types
  BPFtrace bpftrace;
  std::string prog =
      "struct t { int s; unsigned int us; long l; unsigned long ul }; "
      "kprobe:f { "
      "  $t = ((struct t *)0xFF);"
      "  $s = $t->s; $us = $t->us; $l = $t->l; $lu = $t->ul; }";
  auto ast = test(prog);

  auto *s =
      ast.root->probes.at(0)->block->stmts.at(1).as<ast::AssignVarStatement>();
  auto *us =
      ast.root->probes.at(0)->block->stmts.at(2).as<ast::AssignVarStatement>();
  auto *l =
      ast.root->probes.at(0)->block->stmts.at(3).as<ast::AssignVarStatement>();
  auto *ul =
      ast.root->probes.at(0)->block->stmts.at(4).as<ast::AssignVarStatement>();
  EXPECT_EQ(CreateInt32(), s->var()->var_type);
  EXPECT_EQ(CreateUInt32(), us->var()->var_type);
  EXPECT_EQ(CreateInt64(), l->var()->var_type);
  EXPECT_EQ(CreateUInt64(), ul->var()->var_type);
}

TEST(semantic_analyser, binop_sign)
{
  // Make sure types are correct
  std::string prog_pre = "struct t { long l; unsigned long ul }; "
                         "kprobe:f { "
                         "  $t = ((struct t *)0xFF); ";

  std::string operators[] = { "==", "!=", "<", "<=", ">",
                              ">=", "+",  "-", "/",  "*" };
  for (std::string op : operators) {
    BPFtrace bpftrace;
    std::string prog = prog_pre + "$varA = $t->l " + op +
                       " $t->l; "
                       "$varB = $t->ul " +
                       op +
                       " $t->l; "
                       "$varC = $t->ul " +
                       op +
                       " $t->ul;"
                       "}";

    auto ast = test(prog);
    auto *varA = ast.root->probes.at(0)
                     ->block->stmts.at(1)
                     .as<ast::AssignVarStatement>();
    EXPECT_EQ(CreateInt64(), varA->var()->var_type);
    auto *varB = ast.root->probes.at(0)
                     ->block->stmts.at(2)
                     .as<ast::AssignVarStatement>();
    EXPECT_EQ(CreateUInt64(), varB->var()->var_type);
    auto *varC = ast.root->probes.at(0)
                     ->block->stmts.at(3)
                     .as<ast::AssignVarStatement>();
    EXPECT_EQ(CreateUInt64(), varC->var()->var_type);
  }
}

TEST(semantic_analyser, int_cast_types)
{
  test("kretprobe:f { @ = (int8)retval }");
  test("kretprobe:f { @ = (int16)retval }");
  test("kretprobe:f { @ = (int32)retval }");
  test("kretprobe:f { @ = (int64)retval }");
  test("kretprobe:f { @ = (uint8)retval }");
  test("kretprobe:f { @ = (uint16)retval }");
  test("kretprobe:f { @ = (uint32)retval }");
  test("kretprobe:f { @ = (uint64)retval }");
}

TEST(semantic_analyser, int_cast_usage)
{
  test("kretprobe:f /(int32) retval < 0 / {}");
  test("kprobe:f /(int32) arg0 < 0 / {}");
  test("kprobe:f { @=sum((int32)arg0) }");
  test("kprobe:f { @=avg((int32)arg0) }");
  test("kprobe:f { @=avg((int32)arg0) }");

  test("kprobe:f { @=avg((int32)\"abc\") }", 1);
}

TEST(semantic_analyser, intptr_cast_types)
{
  test("kretprobe:f { @ = *(int8*)retval }");
  test("kretprobe:f { @ = *(int16*)retval }");
  test("kretprobe:f { @ = *(int32*)retval }");
  test("kretprobe:f { @ = *(int64*)retval }");
  test("kretprobe:f { @ = *(uint8*)retval }");
  test("kretprobe:f { @ = *(uint16*)retval }");
  test("kretprobe:f { @ = *(uint32*)retval }");
  test("kretprobe:f { @ = *(uint64*)retval }");
}

TEST(semantic_analyser, intptr_cast_usage)
{
  test("kretprobe:f /(*(int32*) retval) < 0 / {}");
  test("kprobe:f /(*(int32*) arg0) < 0 / {}");
  test("kprobe:f { @=sum(*(int32*)arg0) }");
  test("kprobe:f { @=avg(*(int32*)arg0) }");
  test("kprobe:f { @=avg(*(int32*)arg0) }");

  // This is OK (@ = 0x636261)
  test("kprobe:f { @=avg(*(int32*)\"abc\") }");
  test("kprobe:f { @=avg(*(int32*)123) }");
}

TEST(semantic_analyser, intarray_cast_types)
{
  test("kprobe:f { @ = (int8[8])1 }");
  test("kprobe:f { @ = (int16[4])1 }");
  test("kprobe:f { @ = (int32[2])1 }");
  test("kprobe:f { @ = (int64[1])1 }");
  test("kprobe:f { @ = (int8[4])(int32)1 }");
  test("kprobe:f { @ = (int8[2])(int16)1 }");
  test("kprobe:f { @ = (int8[1])(int8)1 }");
  test("kprobe:f { @ = (int8[])1 }");
  test("kprobe:f { @ = (uint8[8])1 }");
  test("kretprobe:f { @ = (int8[8])retval }");

  test("kprobe:f { @ = (int8[4])1 }", 1);
  test("kprobe:f { @ = (int32[])(int16)1 }", 1);
  test("kprobe:f { @ = (int8[6])\"hello\" }", 1);

  test("struct Foo { int x; } kprobe:f { @ = (struct Foo [2])1 }", 1);
}

TEST(semantic_analyser, bool_array_cast_types)
{
  test("kprobe:f { @ = (bool[8])1 }");
  test("kprobe:f { @ = (bool[4])(uint32)1 }");
  test("kprobe:f { @ = (bool[2])(uint16)1 }");

  test("kprobe:f { @ = (bool[4])1 }", 1);
  test("kprobe:f { @ = (bool[64])1 }", 1);
}

TEST(semantic_analyser, intarray_cast_usage)
{
  test("kprobe:f { $a=(int8[8])1; }");
  test("kprobe:f { @=(int8[8])1; }");
  test("kprobe:f { @[(int8[8])1] = 0; }");
  test("kprobe:f { if (((int8[8])1)[0] == 1) {} }");
}

TEST(semantic_analyser, intarray_to_int_cast)
{
  test("#include <stdint.h>\n"
       "struct Foo { uint8_t x[8]; } "
       "kprobe:f { @ = (int64)((struct Foo *)arg0)->x; }");
  test("#include <stdint.h>\n"
       "struct Foo { uint32_t x[2]; } "
       "kprobe:f { @ = (int64)((struct Foo *)arg0)->x; }");
  test("#include <stdint.h>\n"
       "struct Foo { uint8_t x[4]; } "
       "kprobe:f { @ = (int32)((struct Foo *)arg0)->x; }");

  test("#include <stdint.h>\n"
       "struct Foo { uint8_t x[8]; } "
       "kprobe:f { @ = (int32)((struct Foo *)arg0)->x; }",
       1);
  test("#include <stdint.h>\n"
       "struct Foo { uint8_t x[8]; } "
       "kprobe:f { @ = (int32 *)((struct Foo *)arg0)->x; }",
       1);
}

TEST(semantic_analyser, mixed_int_var_assignments)
{
  test("kprobe:f { $x = (uint64)0; $x = (uint16)1; }");
  test("kprobe:f { $x = (int8)1; $x = 5; }");
  test("kprobe:f { $x = 1; $x = -1; }");
  test("kprobe:f { $x = (uint8)1; $x = 200; }");
  test("kprobe:f { $x = (int8)1; $x = -2; }");
  test("kprobe:f { $x = (int16)1; $x = 20000; }");
  // We'd like the below to work, but blocked on #3518.
  // TLDR: It looks like a literal and thus amenable to static "fits into"
  // checks. But it's not, the parser has actually desugared it to:
  //    AssignVarStatement(Variable, Binop(Variable, Integer(1)))
  // test("kprobe:f { $x = (uint32)5; $x += 1; }");

  test_error("kprobe:f { $x = (uint8)1; $x = -1; }", R"(
stdin:1:27-34: ERROR: Type mismatch for $x: trying to assign value of type 'int64' when variable already contains a value of type 'uint8'
kprobe:f { $x = (uint8)1; $x = -1; }
                          ~~~~~~~
)");
  test_error("kprobe:f { $x = (int16)1; $x = 100000; }", R"(
stdin:1:27-38: ERROR: Type mismatch for $x: trying to assign value '100000' which does not fit into the variable of type 'int16'
kprobe:f { $x = (int16)1; $x = 100000; }
                          ~~~~~~~~~~~
)");
  test_error("kprobe:f { $a = (uint16)5; $x = (uint8)0; $x = $a; }", R"(
stdin:1:43-50: ERROR: Integer size mismatch. Assignment type 'uint16' is larger than the variable type 'uint8'.
kprobe:f { $a = (uint16)5; $x = (uint8)0; $x = $a; }
                                          ~~~~~~~
)");
  test_error("kprobe:f { $a = (int8)-1; $x = (uint8)0; $x = $a; }", R"(
stdin:1:42-49: ERROR: Type mismatch for $x: trying to assign value of type 'int8' when variable already contains a value of type 'uint8'
kprobe:f { $a = (int8)-1; $x = (uint8)0; $x = $a; }
                                         ~~~~~~~
)");
  test_error("kprobe:f { $x = -1; $x = 10223372036854775807; }", R"(
stdin:1:21-46: ERROR: Type mismatch for $x: trying to assign value '10223372036854775807' which does not fit into the variable of type 'int64'
kprobe:f { $x = -1; $x = 10223372036854775807; }
                    ~~~~~~~~~~~~~~~~~~~~~~~~~
)");
  test_error("kprobe:f { $x = (0, (uint32)123); $x = (0, (int32)-123); }", R"(
stdin:1:35-56: ERROR: Type mismatch for $x: trying to assign value of type '(int64,int32)' when variable already contains a value of type '(int64,uint32)'
kprobe:f { $x = (0, (uint32)123); $x = (0, (int32)-123); }
                                  ~~~~~~~~~~~~~~~~~~~~~
)");
  test("BEGIN { $x = (uint8)1; $x = 5; }", R"(
Program
 BEGIN
  =
   variable: $x :: [uint8]
   (uint8)
    int: 1
  =
   variable: $x :: [uint8]
   (uint8)
    int: 5
)");
  test("BEGIN { $x = (int8)1; $x = 5; }", R"(
Program
 BEGIN
  =
   variable: $x :: [int8]
   (int8)
    int: 1
  =
   variable: $x :: [int8]
   (int8)
    int: 5
)");
}

TEST(semantic_analyser, mixed_int_like_map_assignments)
{
  // Map values are automatically promoted to 64bit ints
  test("kprobe:f { @x = (uint64)0; @x = (uint16)1; }");
  test("kprobe:f { @x = (int8)1; @x = 5; }");
  test("kprobe:f { @x = 1; @x = -1; }");
  test("kprobe:f { @x = (int8)1; @x = -2; }");
  test("kprobe:f { @x = (int16)1; @x = 20000; }");
  test("kprobe:f { @x = (uint16)1; @x = 200; }");
  test("kprobe:f { @x = (uint16)1; @x = 10223372036854775807; }");
  test("kprobe:f { @x = 1; @x = 9223372036854775807; }");
  test("kprobe:f { @x = 1; @x = -9223372036854775808; }");

  test_error("kprobe:f { @x = (uint8)1; @x = -1; }", R"(
stdin:1:27-34: ERROR: Type mismatch for @x: trying to assign value of type 'int64' when map already contains a value of type 'uint64'
kprobe:f { @x = (uint8)1; @x = -1; }
                          ~~~~~~~
)");

  test_error("kprobe:f { @x = 1; @x = 10223372036854775807; }", R"(
stdin:1:20-45: ERROR: Type mismatch for @x: trying to assign value '10223372036854775807' which does not fit into the map of type 'int64'
kprobe:f { @x = 1; @x = 10223372036854775807; }
                   ~~~~~~~~~~~~~~~~~~~~~~~~~
)");
  test_error("kprobe:f { @x = sum((uint64)1); @x = sum(-1); }", R"(
stdin:1:38-45: ERROR: Type mismatch for @x: trying to assign value of type 'sum_t' when map already contains a value of type 'usum_t'
kprobe:f { @x = sum((uint64)1); @x = sum(-1); }
                                     ~~~~~~~
)");
  test_error("kprobe:f { @x = min((uint64)1); @x = min(-1); }", R"(
stdin:1:38-45: ERROR: Type mismatch for @x: trying to assign value of type 'min_t' when map already contains a value of type 'umin_t'
kprobe:f { @x = min((uint64)1); @x = min(-1); }
                                     ~~~~~~~
)");
  test_error("kprobe:f { @x = max((uint64)1); @x = max(-1); }", R"(
stdin:1:38-45: ERROR: Type mismatch for @x: trying to assign value of type 'max_t' when map already contains a value of type 'umax_t'
kprobe:f { @x = max((uint64)1); @x = max(-1); }
                                     ~~~~~~~
)");
  test_error("kprobe:f { @x = avg((uint64)1); @x = avg(-1); }", R"(
stdin:1:38-45: ERROR: Type mismatch for @x: trying to assign value of type 'avg_t' when map already contains a value of type 'uavg_t'
kprobe:f { @x = avg((uint64)1); @x = avg(-1); }
                                     ~~~~~~~
)");
  test_error("kprobe:f { @x = stats((uint64)1); @x = stats(-1); }", R"(
stdin:1:40-49: ERROR: Type mismatch for @x: trying to assign value of type 'stats_t' when map already contains a value of type 'ustats_t'
kprobe:f { @x = stats((uint64)1); @x = stats(-1); }
                                       ~~~~~~~~~
)");
}

TEST(semantic_analyser, mixed_int_map_access)
{
  // Map keys are automatically promoted to 64bit ints
  test("kprobe:f { @x[1] = 1; @x[(int16)2] }");
  test("kprobe:f { @x[(int16)1] = 1; @x[2] }");
  test("kprobe:f { @x[(int16)1] = 1; @x[(int64)2] }");
  test("kprobe:f { @x[(uint16)1] = 1; @x[(uint64)2] }");
  test("kprobe:f { @x[(uint64)1] = 1; @x[(uint16)2] }");
  test("kprobe:f { @x[(uint16)1] = 1; @x[2] }");
  test("kprobe:f { @x[(uint16)1] = 1; @x[10223372036854775807] }");
  test("kprobe:f { @x[1] = 1; @x[9223372036854775807] }");
  test("kprobe:f { @x[1] = 1; @x[-9223372036854775808] }");

  test_error("kprobe:f { @x[1] = 1; @x[10223372036854775807] }", R"(
stdin:1:23-46: ERROR: Argument mismatch for @x: trying to access with argument '10223372036854775807' which does not fit into the map of key type 'int64'
kprobe:f { @x[1] = 1; @x[10223372036854775807] }
                      ~~~~~~~~~~~~~~~~~~~~~~~
)");
  test_error("kprobe:f { @x[(uint64)1] = 1; @x[-1] }", R"(
stdin:1:31-35: ERROR: Argument mismatch for @x: trying to access with arguments: 'int64' when map expects arguments: 'uint64'
kprobe:f { @x[(uint64)1] = 1; @x[-1] }
                              ~~~~
)");
  test_error("kretprobe:f { @x[1] = 1; @x[retval] }", R"(
stdin:1:26-35: ERROR: Argument mismatch for @x: trying to access with arguments: 'uint64' when map expects arguments: 'int64'
kretprobe:f { @x[1] = 1; @x[retval] }
                         ~~~~~~~~~
)");
}

TEST(semantic_analyser, signal)
{
  // int literals
  test("k:f { signal(1); }", 0, false);
  test("kr:f { signal(1); }", 0, false);
  test("u:/bin/sh:f { signal(11); }", 0, false);
  test("ur:/bin/sh:f { signal(11); }", 0, false);
  test("p:hz:1 { signal(1); }", 0, false);

  // vars
  test("k:f { @=1; signal(@); }", 0, false);
  test("k:f { @=1; signal((int32)arg0); }", 0, false);

  // String
  test("k:f { signal(\"KILL\"); }", 0, false);
  test("k:f { signal(\"SIGKILL\"); }", 0, false);

  // Not allowed for:
  test("hardware:pcm:1000 { signal(1); }", 1, false);
  test("software:pcm:1000 { signal(1); }", 1, false);
  test("BEGIN { signal(1); }", 1, false);
  test("END { signal(1); }", 1, false);
  test("i:s:1 { signal(1); }", 1, false);

  // invalid signals
  test("k:f { signal(0); }", 1, false);
  test("k:f { signal(-100); }", 1, false);
  test("k:f { signal(100); }", 1, false);
  test("k:f { signal(\"SIGABC\"); }", 1, false);
  test("k:f { signal(\"ABC\"); }", 1, false);

  // Missing kernel support
  MockBPFfeature feature(false);
  test(feature, "k:f { signal(1) }", 1, false);
  test(feature, "k:f { signal(\"KILL\"); }", 1, false);

  // Positional parameter
  BPFtrace bpftrace;
  bpftrace.add_param("1");
  bpftrace.add_param("hello");
  test(bpftrace, "k:f { signal($1) }", false);
  test(bpftrace, "k:f { signal($2) }", 1, false);
}

TEST(semantic_analyser, strncmp)
{
  // Test strncmp builtin
  test(R"(i:s:1 { $a = "bar"; strncmp("foo", $a, 1) })");
  test(R"(i:s:1 { strncmp("foo", "bar", 1) })");
  test("i:s:1 { strncmp(1) }", 1);
  test("i:s:1 { strncmp(1,1,1) }", 2);
  test("i:s:1 { strncmp(\"a\",1,1) }", 2);
  test(R"(i:s:1 { strncmp("a","a",-1) })", 1);
  test(R"(i:s:1 { strncmp("a","a","foo") })", 1);
}

TEST(semantic_analyser, strncmp_posparam)
{
  BPFtrace bpftrace;
  bpftrace.add_param("1");
  bpftrace.add_param("hello");
  test(bpftrace, R"(i:s:1 { strncmp("foo", "bar", $1) })");
  test(bpftrace, R"(i:s:1 { strncmp("foo", "bar", $2) })", 1);
}

TEST(semantic_analyser, strcontains)
{
  // Test strcontains builtin
  test(R"(i:s:1 { $a = "bar"; strcontains("foo", $a) })");
  test(R"(i:s:1 { strcontains("foo", "bar") })");
  test("i:s:1 { strcontains(1) }", 1);
  test("i:s:1 { strcontains(1,1) }", 2);
  test("i:s:1 { strcontains(\"a\",1) }", 2);
}

TEST(semantic_analyser, strcontains_large_warnings)
{
  test_for_warning(
      "k:f { $s1 = str(arg0); $s2 = str(arg1); strcontains($s1, $s2) }",
      "both string sizes is larger");

  test_for_warning(
      "k:f { $s1 = str(arg0, 64); $s2 = str(arg1, 16); strcontains($s1, $s2) }",
      "both string sizes is larger",
      /* invert= */ true);

  auto bpftrace = get_mock_bpftrace();
  bpftrace->config_->max_strlen = 16;

  test_for_warning(
      *bpftrace,
      "k:f { $s1 = str(arg0); $s2 = str(arg1); strcontains($s1, $s2) }",
      "both string sizes is larger",
      /* invert= */ true);
}

TEST(semantic_analyser, strcontains_posparam)
{
  BPFtrace bpftrace;
  bpftrace.add_param("hello");
  test(bpftrace, "i:s:1 { strcontains(\"foo\", str($1)) }");
}

TEST(semantic_analyser, override)
{
  // literals
  test("k:f { override(-1); }", 0, false);

  // variables
  test("k:f { override(arg0); }", 0, false);

  // Probe types
  test("kr:f { override(-1); }", 1, false);
  test("u:/bin/sh:f { override(-1); }", 1, false);
  test("t:syscalls:sys_enter_openat { override(-1); }", 1, false);
  test("i:s:1 { override(-1); }", 1, false);
  test("p:hz:1 { override(-1); }", 1, false);
}

TEST(semantic_analyser, unwatch)
{
  test("i:s:1 { unwatch(12345) }");
  test("i:s:1 { unwatch(0x1234) }");
  test("i:s:1 { $x = 1; unwatch($x); }");
  test("i:s:1 { @x = 1; @x++; unwatch(@x); }");
  test("k:f { unwatch(arg0); }");
  test("k:f { unwatch((int64)arg0); }");
  test("k:f { unwatch(*(int64*)arg0); }");

  test("i:s:1 { unwatch(\"asdf\") }", 2);
  test(R"(i:s:1 { @x["hi"] = "world"; unwatch(@x["hi"]) })", 3);
  test("i:s:1 { printf(\"%d\", unwatch(2)) }", 2);
}

TEST(semantic_analyser, struct_member_keywords)
{
  std::string keywords[] = {
    "arg0",
    "args",
    "curtask",
    "func",
    "gid"
    "rand",
    "uid",
    "avg",
    "cat",
    "exit",
    "kaddr",
    "min",
    "printf",
    "usym",
    "kstack",
    "ustack",
    "bpftrace",
    "perf",
    "raw",
    "uprobe",
    "kprobe",
    "config",
    "fn",
  };
  for (auto kw : keywords) {
    test("struct S{ int " + kw + ";}; k:f { ((struct S*)arg0)->" + kw + "}");
    test("struct S{ int " + kw + ";}; k:f { (*(struct S*)arg0)." + kw + "}");
  }
}

TEST(semantic_analyser, jumps)
{
  test("i:s:1 { return; }");
  // must be used in loops
  test("i:s:1 { break; }", 1);
  test("i:s:1 { continue; }", 1);
}

TEST(semantic_analyser, while_loop)
{
  test("i:s:1 { $a = 1; while ($a < 10) { $a++ }}");
  test("i:s:1 { $a = 1; while (1) { if($a > 50) { break } $a++ }}");
  test("i:s:1 { $a = 1; while ($a < 10) { $a++ }}");
  test("i:s:1 { $a = 1; while (1) { if($a > 50) { break } $a++ }}");
  test("i:s:1 { $a = 1; while (1) { if($a > 50) { return } $a++ }}");
  test(R"PROG(
i:s:1 {
  $a = 1;
  while ($a < 10) {
    $a++; $j=0;
    while ($j < 10) {
      $j++;
    }
  }
})PROG");

  test_for_warning("i:s:1 { $a = 1; while ($a < 10) { break; $a++ }}",
                   "code after a 'break'");
  test_for_warning("i:s:1 { $a = 1; while ($a < 10) { continue; $a++ }}",
                   "code after a 'continue'");
  test_for_warning("i:s:1 { $a = 1; while ($a < 10) { return; $a++ }}",
                   "code after a 'return'");

  test_for_warning("i:s:1 { $a = 1; while ($a < 10) { @=$a++; print(@); }}",
                   "'print()' in a loop");
}

TEST(semantic_analyser, builtin_args)
{
  auto bpftrace = get_mock_bpftrace();
  test(*bpftrace, "t:sched:sched_one { args.common_field }");
  test(*bpftrace, "t:sched:sched_two { args.common_field }");
  test(*bpftrace,
       "t:sched:sched_one,"
       "t:sched:sched_two { args.common_field }");
  test(*bpftrace, "t:sched:sched_* { args.common_field }");
  test(*bpftrace, "t:sched:sched_one { args.not_a_field }", 1);
  // Backwards compatibility
  test(*bpftrace, "t:sched:sched_one { args->common_field }");
}

TEST(semantic_analyser, type_ctx)
{
  BPFtrace bpftrace;
  std::string structs = "struct c {char c} struct x { long a; short b[4]; "
                        "struct c c; struct c *d;}";
  auto ast = test(structs +
                  "kprobe:f { $x = (struct x*)ctx; $a = $x->a; $b = $x->b[0]; "
                  "$c = $x->c.c; $d = $x->d->c;}");
  auto &stmts = ast.root->probes.at(0)->block->stmts;

  // $x = (struct x*)ctx;
  auto *assignment = stmts.at(0).as<ast::AssignVarStatement>();
  EXPECT_TRUE(assignment->var()->var_type.IsPtrTy());

  // $a = $x->a;
  assignment = stmts.at(1).as<ast::AssignVarStatement>();
  EXPECT_EQ(CreateInt64(), assignment->var()->var_type);
  auto *fieldaccess = assignment->expr.as<ast::FieldAccess>();
  EXPECT_EQ(CreateInt64(), fieldaccess->field_type);
  auto *unop = fieldaccess->expr.as<ast::Unop>();
  EXPECT_TRUE(unop->result_type.IsCtxAccess());
  auto *var = unop->expr.as<ast::Variable>();
  EXPECT_TRUE(var->var_type.IsPtrTy());

  // $b = $x->b[0];
  assignment = stmts.at(2).as<ast::AssignVarStatement>();
  EXPECT_EQ(CreateInt16(), assignment->var()->var_type);
  auto *arrayaccess = assignment->expr.as<ast::ArrayAccess>();
  EXPECT_EQ(CreateInt16(), arrayaccess->element_type);
  fieldaccess = arrayaccess->expr.as<ast::FieldAccess>();
  EXPECT_TRUE(fieldaccess->field_type.IsCtxAccess());
  unop = fieldaccess->expr.as<ast::Unop>();
  EXPECT_TRUE(unop->result_type.IsCtxAccess());
  var = unop->expr.as<ast::Variable>();
  EXPECT_TRUE(var->var_type.IsPtrTy());

#ifdef __x86_64__
  auto chartype = CreateInt8();
#else
  auto chartype = CreateUInt8();
#endif

  // $c = $x->c.c;
  assignment = stmts.at(3).as<ast::AssignVarStatement>();
  EXPECT_EQ(chartype, assignment->var()->var_type);
  fieldaccess = assignment->expr.as<ast::FieldAccess>();
  EXPECT_EQ(chartype, fieldaccess->field_type);
  fieldaccess = fieldaccess->expr.as<ast::FieldAccess>();
  EXPECT_TRUE(fieldaccess->field_type.IsCtxAccess());
  unop = fieldaccess->expr.as<ast::Unop>();
  EXPECT_TRUE(unop->result_type.IsCtxAccess());
  var = unop->expr.as<ast::Variable>();
  EXPECT_TRUE(var->var_type.IsPtrTy());

  // $d = $x->d->c;
  assignment = stmts.at(4).as<ast::AssignVarStatement>();
  EXPECT_EQ(chartype, assignment->var()->var_type);
  fieldaccess = assignment->expr.as<ast::FieldAccess>();
  EXPECT_EQ(chartype, fieldaccess->field_type);
  unop = fieldaccess->expr.as<ast::Unop>();
  EXPECT_TRUE(unop->result_type.IsRecordTy());
  fieldaccess = unop->expr.as<ast::FieldAccess>();
  EXPECT_TRUE(fieldaccess->field_type.IsPtrTy());
  unop = fieldaccess->expr.as<ast::Unop>();
  EXPECT_TRUE(unop->result_type.IsCtxAccess());
  var = unop->expr.as<ast::Variable>();
  EXPECT_TRUE(var->var_type.IsPtrTy());

  test("k:f, kr:f { @ = (uint64)ctx; }");
  test("k:f, i:s:1 { @ = (uint64)ctx; }", 1);
  test("t:sched:sched_one { @ = (uint64)ctx; }", 1);
}

TEST(semantic_analyser, double_pointer_basic)
{
  test(R"_(BEGIN { $pp = (int8 **)0; $p = *$pp; $val = *$p; })_");
  test(R"_(BEGIN { $pp = (int8 **)0; $val = **$pp; })_");

  const std::string structs = "struct Foo { int x; }";
  test(structs + R"_(BEGIN { $pp = (struct Foo **)0; $val = (*$pp)->x; })_");
}

TEST(semantic_analyser, double_pointer_int)
{
  auto ast = test("kprobe:f { $pp = (int8 **)1; $p = *$pp; $val = *$p; }");
  auto &stmts = ast.root->probes.at(0)->block->stmts;

  // $pp = (int8 **)1;
  auto *assignment = stmts.at(0).as<ast::AssignVarStatement>();
  ASSERT_TRUE(assignment->var()->var_type.IsPtrTy());
  ASSERT_TRUE(assignment->var()->var_type.GetPointeeTy()->IsPtrTy());
  ASSERT_TRUE(
      assignment->var()->var_type.GetPointeeTy()->GetPointeeTy()->IsIntTy());
  EXPECT_EQ(assignment->var()
                ->var_type.GetPointeeTy()
                ->GetPointeeTy()
                ->GetIntBitWidth(),
            8ULL);

  // $p = *$pp;
  assignment = stmts.at(1).as<ast::AssignVarStatement>();
  ASSERT_TRUE(assignment->var()->var_type.IsPtrTy());
  ASSERT_TRUE(assignment->var()->var_type.GetPointeeTy()->IsIntTy());
  EXPECT_EQ(assignment->var()->var_type.GetPointeeTy()->GetIntBitWidth(), 8ULL);

  // $val = *$p;
  assignment = stmts.at(2).as<ast::AssignVarStatement>();
  ASSERT_TRUE(assignment->var()->var_type.IsIntTy());
  EXPECT_EQ(assignment->var()->var_type.GetIntBitWidth(), 8ULL);
}

TEST(semantic_analyser, double_pointer_struct)
{
  auto ast = test(
      "struct Foo { char x; long y; }"
      "kprobe:f { $pp = (struct Foo **)1; $p = *$pp; $val = $p->x; }");
  auto &stmts = ast.root->probes.at(0)->block->stmts;

  // $pp = (struct Foo **)1;
  auto *assignment = stmts.at(0).as<ast::AssignVarStatement>();
  ASSERT_TRUE(assignment->var()->var_type.IsPtrTy());
  ASSERT_TRUE(assignment->var()->var_type.GetPointeeTy()->IsPtrTy());
  ASSERT_TRUE(
      assignment->var()->var_type.GetPointeeTy()->GetPointeeTy()->IsRecordTy());
  EXPECT_EQ(
      assignment->var()->var_type.GetPointeeTy()->GetPointeeTy()->GetName(),
      "struct Foo");

  // $p = *$pp;
  assignment = stmts.at(1).as<ast::AssignVarStatement>();
  ASSERT_TRUE(assignment->var()->var_type.IsPtrTy());
  ASSERT_TRUE(assignment->var()->var_type.GetPointeeTy()->IsRecordTy());
  EXPECT_EQ(assignment->var()->var_type.GetPointeeTy()->GetName(),
            "struct Foo");

  // $val = $p->x;
  assignment = stmts.at(2).as<ast::AssignVarStatement>();
  ASSERT_TRUE(assignment->var()->var_type.IsIntTy());
  EXPECT_EQ(assignment->var()->var_type.GetIntBitWidth(), 8ULL);
}

TEST(semantic_analyser, pointer_arith)
{
  test(R"_(BEGIN { $t = (int32*) 32; $t = $t + 1 })_");
  test(R"_(BEGIN { $t = (int32*) 32; $t +=1 })_");
  test(R"_(BEGIN { $t = (int32*) 32; $t++ })_");
  test(R"_(BEGIN { $t = (int32*) 32; ++$t })_");
  test(R"_(BEGIN { $t = (int32*) 32; $t = $t - 1 })_");
  test(R"_(BEGIN { $t = (int32*) 32; $t -=1 })_");
  test(R"_(BEGIN { $t = (int32*) 32; $t-- })_");
  test(R"_(BEGIN { $t = (int32*) 32; --$t })_");

  // pointer compare
  test(R"_(BEGIN { $t = (int32*) 32; @ = ($t > $t); })_");
  test(R"_(BEGIN { $t = (int32*) 32; @ = ($t < $t); })_");
  test(R"_(BEGIN { $t = (int32*) 32; @ = ($t >= $t); })_");
  test(R"_(BEGIN { $t = (int32*) 32; @ = ($t <= $t); })_");
  test(R"_(BEGIN { $t = (int32*) 32; @ = ($t == $t); })_");

  // map
  test(R"_(BEGIN { @ = (int32*) 32; @ = @ + 1 })_");
  test(R"_(BEGIN { @ = (int32*) 32; @ +=1 })_");
  test(R"_(BEGIN { @ = (int32*) 32; @++ })_");
  test(R"_(BEGIN { @ = (int32*) 32; ++@ })_");
  test(R"_(BEGIN { @ = (int32*) 32; @ = @ - 1 })_");
  test(R"_(BEGIN { @ = (int32*) 32; @ -=1 })_");
  test(R"_(BEGIN { @ = (int32*) 32; @-- })_");
  test(R"_(BEGIN { @ = (int32*) 32; --@ })_");

  // associativity
  test(R"_(BEGIN { $t = (int32*) 32; $t = $t + 1 })_");
  test(R"_(BEGIN { $t = (int32*) 32; $t = 1 + $t })_");
  test(R"_(BEGIN { $t = (int32*) 32; $t = $t - 1 })_");
  test(R"_(BEGIN { $t = (int32*) 32; $t = 1 - $t })_", 1);

  // invalid ops
  test(R"_(BEGIN { $t = (int32*) 32; $t *= 5 })_", 1);
  test(R"_(BEGIN { $t = (int32*) 32; $t /= 5 })_", 1);
  test(R"_(BEGIN { $t = (int32*) 32; $t %= 5 })_", 1);
  test(R"_(BEGIN { $t = (int32*) 32; $t <<= 5 })_", 1);
  test(R"_(BEGIN { $t = (int32*) 32; $t >>= 5 })_", 1);

  test(R"_(BEGIN { $t = (int32*) 32; $t -= $t })_", 1);
  test(R"_(BEGIN { $t = (int32*) 32; $t += $t })_", 1);

  // invalid types
  test(R"_(BEGIN { $t = (int32*) 32; $t += "abc" })_", 1);
  test(R"_(BEGIN { $t = (int32*) 32; $t += comm })_", 1);
  test(
      R"_(struct A {}; BEGIN { $t = (int32*) 32; $s = *(struct A*) 0; $t += $s })_",
      1);
}

TEST(semantic_analyser, pointer_compare)
{
  test(R"_(BEGIN { $t = (int32*) 32; $c = $t < 1 })_");
  test(R"_(BEGIN { $t = (int32*) 32; $c = $t > 1 })_");
  test(R"_(BEGIN { $t = (int32*) 32; $c = $t <= 1 })_");
  test(R"_(BEGIN { $t = (int32*) 32; $c = $t >= 1 })_");
  test(R"_(BEGIN { $t = (int32*) 32; $c = $t != 1 })_");

  test(R"_(BEGIN { $t = (int32*) 32; $c = $t < $t })_");
  test(R"_(BEGIN { $t = (int32*) 32; $c = $t > $t })_");
  test(R"_(BEGIN { $t = (int32*) 32; $c = $t <= $t })_");
  test(R"_(BEGIN { $t = (int32*) 32; $c = $t >= $t })_");
  test(R"_(BEGIN { $t = (int32*) 32; $c = $t != $t })_");

  // pointer compare diff types
  test(R"_(BEGIN { $t = (int32*) 32; $y = (int64*) 1024; @ = ($t > $y); })_");
  test(R"_(BEGIN { $t = (int32*) 32; $y = (int64*) 1024; @ = ($t < $y); })_");
  test(R"_(BEGIN { $t = (int32*) 32; $y = (int64*) 1024; @ = ($t >= $y); })_");
  test(R"_(BEGIN { $t = (int32*) 32; $y = (int64*) 1024; @ = ($t <= $y); })_");
  test(R"_(BEGIN { $t = (int32*) 32; $y = (int64*) 1024; @ = ($t == $y); })_");

  test_for_warning("k:f { $a = (int8*) 1; $b = (int16*) 2; $c = ($a == $b) }",
                   "comparison of distinct pointer types: int8, int16");
}

// Basic functionality test
TEST(semantic_analyser, tuple)
{
  test(R"_(BEGIN { $t = (1)})_");
  test(R"_(BEGIN { $t = (1, 2); $v = $t;})_");
  test(R"_(BEGIN { $t = (1, 2, "string")})_");
  test(R"_(BEGIN { $t = (1, 2, "string"); $t = (3, 4, "other"); })_");
  test(R"_(BEGIN { $t = (1, kstack()) })_");
  test(R"_(BEGIN { $t = (1, (2,3)) })_");

  test(R"_(BEGIN { @t = (1)})_");
  test(R"_(BEGIN { @t = (1, 2); @v = @t;})_");
  test(R"_(BEGIN { @t = (1, 2, "string")})_");
  test(R"_(BEGIN { @t = (1, 2, "string"); @t = (3, 4, "other"); })_");
  test(R"_(BEGIN { @t = (1, kstack()) })_");
  test(R"_(BEGIN { @t = (1, (2,3)) })_");
  test(R"_(BEGIN { $t = (1, (int64)2); $t = (2, (int32)3); })_");

  test_error(R"_(BEGIN { $t = (1, (int32)2); $t = (2, (int64)3); })_", R"(
stdin:1:29-47: ERROR: Type mismatch for $t: trying to assign value of type '(int64,int64)' when variable already contains a value of type '(int64,int32)'
BEGIN { $t = (1, (int32)2); $t = (2, (int64)3); }
                            ~~~~~~~~~~~~~~~~~~
)");

  test(R"_(struct task_struct { int x; } BEGIN { $t = (1, curtask); })_");
  test(R"_(struct task_struct { int x[4]; } BEGIN { $t = (1, curtask->x); })_");

  test(R"_(BEGIN { $t = (1, 2); $t = (4, "other"); })_", 1);
  test(R"_(BEGIN { $t = (1, 2); $t = 5; })_", 1);
  test(R"_(BEGIN { $t = (1, count()) })_", 1);

  test(R"_(BEGIN { @t = (1, 2); @t = (4, "other"); })_", 3);
  test(R"_(BEGIN { @t = (1, 2); @t = 5; })_", 1);
  test(R"_(BEGIN { @t = (1, count()) })_", 1);

  test(R"_(BEGIN { $t = (1, (2, 3)); $t = (4, ((int8)5, 6)); })_");

  test_error(R"_(BEGIN { $t = (1, ((int8)2, 3)); $t = (4, (5, 6)); })_",
             R"(
stdin:1:33-49: ERROR: Type mismatch for $t: trying to assign value of type '(int64,(int64,int64))' when variable already contains a value of type '(int64,(int8,int64))'
BEGIN { $t = (1, ((int8)2, 3)); $t = (4, (5, 6)); }
                                ~~~~~~~~~~~~~~~~
)");

  test_error(R"_(BEGIN { $t = ((uint8)1, (2, 3)); $t = (4, ((int8)5, 6)); })_",
             R"(
stdin:1:34-56: ERROR: Type mismatch for $t: trying to assign value of type '(int64,(int8,int64))' when variable already contains a value of type '(uint8,(int64,int64))'
BEGIN { $t = ((uint8)1, (2, 3)); $t = (4, ((int8)5, 6)); }
                                 ~~~~~~~~~~~~~~~~~~~~~~
)");

  test(R"_(BEGIN { @t = (1, 2, "hi"); @t = (3, 4, "hellolongstr"); })_");
  test(R"_(BEGIN { $t = (1, ("hi", 2)); $t = (3, ("hellolongstr", 4)); })_");

  test_error("BEGIN { @x[1] = hist(10); $y = (1, @x[1]); }", R"(
stdin:1:36-41: ERROR: Map type hist_t cannot exist inside a tuple.
BEGIN { @x[1] = hist(10); $y = (1, @x[1]); }
                                   ~~~~~
)");
}

TEST(semantic_analyser, tuple_indexing)
{
  test(R"_(BEGIN { (1,2).0 })_");
  test(R"_(BEGIN { (1,2).1 })_");
  test(R"_(BEGIN { (1,2,3).2 })_");
  test(R"_(BEGIN { $t = (1,2,3).0 })_");
  test(R"_(BEGIN { $t = (1,2,3); $v = $t.0; })_");

  test(R"_(BEGIN { (1,2,3).3 })_", 2);
  test(R"_(BEGIN { (1,2,3).9999999999999 })_", 2);
}

// More in depth inspection of AST
TEST(semantic_analyser, tuple_assign_var)
{
  BPFtrace bpftrace;
  SizedType ty = CreateTuple(
      Struct::CreateTuple({ CreateInt64(), CreateString(6) }));
  auto ast = test(
      bpftrace, true, R"_(BEGIN { $t = (1, "str"); $t = (4, "other"); })_", 0);

  auto &stmts = ast.root->probes.at(0)->block->stmts;

  // $t = (1, "str");
  auto *assignment = stmts.at(0).as<ast::AssignVarStatement>();
  EXPECT_EQ(ty, assignment->var()->var_type);

  // $t = (4, "other");
  assignment = stmts.at(1).as<ast::AssignVarStatement>();
  EXPECT_EQ(ty, assignment->var()->var_type);
}

// More in depth inspection of AST
TEST(semantic_analyser, tuple_assign_map)
{
  BPFtrace bpftrace;
  SizedType ty;
  auto ast = test(
      bpftrace, true, R"_(BEGIN { @ = (1, 3, 3, 7); @ = (0, 0, 0, 0); })_", 0);

  auto &stmts = ast.root->probes.at(0)->block->stmts;

  // $t = (1, 3, 3, 7);
  auto *assignment = stmts.at(0).as<ast::AssignMapStatement>();
  ty = CreateTuple(Struct::CreateTuple(
      { CreateInt64(), CreateInt64(), CreateInt64(), CreateInt64() }));
  EXPECT_EQ(ty, assignment->map->value_type);

  // $t = (0, 0, 0, 0);
  assignment = stmts.at(1).as<ast::AssignMapStatement>();
  ty = CreateTuple(Struct::CreateTuple(
      { CreateInt64(), CreateInt64(), CreateInt64(), CreateInt64() }));
  EXPECT_EQ(ty, assignment->map->value_type);
}

// More in depth inspection of AST
TEST(semantic_analyser, tuple_nested)
{
  BPFtrace bpftrace;
  SizedType ty_inner = CreateTuple(
      Struct::CreateTuple({ CreateInt64(), CreateInt64() }));
  SizedType ty = CreateTuple(Struct::CreateTuple({ CreateInt64(), ty_inner }));
  auto ast = test(bpftrace, true, R"_(BEGIN { $t = (1,(1,2)); })_", 0);

  auto &stmts = ast.root->probes.at(0)->block->stmts;

  // $t = (1, "str");
  auto *assignment = stmts.at(0).as<ast::AssignVarStatement>();
  EXPECT_EQ(ty, assignment->var()->var_type);
}

TEST(semantic_analyser, multi_pass_type_inference_zero_size_int)
{
  auto bpftrace = get_mock_bpftrace();
  // The first pass on processing the Unop does not have enough information
  // to figure out size of `@i` yet. The analyzer figures out the size
  // after seeing the `@i++`. On the second pass the correct size is
  // determined.
  test(*bpftrace, "BEGIN { if (!@i) { @i++; } }");
}

TEST(semantic_analyser, call_kptr_uptr)
{
  test("k:f { @  = kptr((int8*) arg0); }");
  test("k:f { $a = kptr((int8*) arg0); }");

  test("k:f { @ = kptr(arg0); }");
  test("k:f { $a = kptr(arg0); }");

  test("k:f { @  = uptr((int8*) arg0); }");
  test("k:f { $a = uptr((int8*) arg0); }");

  test("k:f { @ = uptr(arg0); }");
  test("k:f { $a = uptr(arg0); }");
}

TEST(semantic_analyser, call_path)
{
  test("kprobe:f { $k = path( arg0 ) }", 1);
  test("kretprobe:f { $k = path( arg0 ) }", 1);
  test("tracepoint:category:event { $k = path( NULL ) }", 1);
  test("kprobe:f { $k = path( arg0 ) }", 1);
  test("kretprobe:f{ $k = path( \"abc\" ) }", 1);
  test("tracepoint:category:event { $k = path( -100 ) }", 1);
  test("uprobe:/bin/sh:f { $k = path( arg0 ) }", 1);
  test("BEGIN { $k = path( 1 ) }", 1);
  test("END { $k = path( 1 ) }", 1);
}

TEST(semantic_analyser, call_offsetof)
{
  test("struct Foo { int x; long l; char c; } \
        BEGIN { @x = offsetof(struct Foo, x); }");
  test("struct Foo { int comm; } \
        BEGIN { @x = offsetof(struct Foo, comm); }");
  test("struct Foo { int ctx; } \
        BEGIN { @x = offsetof(struct Foo, ctx); }");
  test("struct Foo { int args; } \
        BEGIN { @x = offsetof(struct Foo, args); }");
  test("struct Foo { int x; long l; char c; } \
        struct Bar { struct Foo foo; int x; } \
        BEGIN { @x = offsetof(struct Bar, x); }");
  test("struct Foo { int x; long l; char c; } \
        union Bar { struct Foo foo; int x; } \
        BEGIN { @x = offsetof(union Bar, x); }");
  test("struct Foo { int x; long l; char c; } \
        struct Fun { struct Foo foo; int (*call)(void); } \
        BEGIN { @x = offsetof(struct Fun, call); }");
  test("struct Foo { int x; long l; char c; } \
        BEGIN { $foo = (struct Foo *)0; \
        @x = offsetof(*$foo, x); }");
  test("struct Foo { int x; long l; char c; } \
        struct Ano { \
          struct { \
            struct Foo foo; \
            int a; \
          }; \
          long l; \
        } \
        BEGIN { @x = offsetof(struct Ano, a); }");
  test("struct Foo { struct Bar { int a; } bar; } \
        BEGIN { @x = offsetof(struct Foo, bar.a); }");
  test("struct Foo { struct Bar { int *a; } bar; } \
        BEGIN { @x = offsetof(struct Foo, bar.a); }");
  test("struct Foo { struct Bar { struct { int a; } anon; } bar; } \
        BEGIN { @x = offsetof(struct Foo, bar.anon.a); }");
  test("struct Foo { struct Bar { struct { int a; }; } bar; } \
        BEGIN { @x = offsetof(struct Foo, bar.a); }");

  // Error tests

  // Bad type
  test_error("struct Foo { struct Bar { int a; } *bar; } \
              BEGIN { @x = offsetof(struct Foo, bar.a); }",
             R"(
stdin:1:71-99: ERROR: 'struct Bar *' is not a record type.
struct Foo { struct Bar { int a; } *bar; }               BEGIN { @x = offsetof(struct Foo, bar.a); }
                                                                      ~~~~~~~~~~~~~~~~~~~~~~~~~~~~
)");
  // Not exist (sub)field
  test_error("struct Foo { int x; long l; char c; } \
              BEGIN { @x = offsetof(struct Foo, __notexistfield__); }",
             R"(
stdin:1:66-106: ERROR: 'struct Foo' has no field named '__notexistfield__'
struct Foo { int x; long l; char c; }               BEGIN { @x = offsetof(struct Foo, __notexistfield__); }
                                                                 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
)");
  test_error("struct Foo { struct Bar { int a; } bar; } \
              BEGIN { @x = offsetof(struct Foo, bar.__notexist_subfield__); }",
             R"(
stdin:1:70-118: ERROR: 'struct Bar' has no field named '__notexist_subfield__'
struct Foo { struct Bar { int a; } bar; }               BEGIN { @x = offsetof(struct Foo, bar.__notexist_subfield__); }
                                                                     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
)");
  // Not exist record
  test("BEGIN { @x = offsetof(__passident__, x); }", 1);
  test("BEGIN { @x = offsetof(__passident__, x.y.z); }", 1);
  test("BEGIN { @x = offsetof(struct __notexiststruct__, x); }", 1);
  test("BEGIN { @x = offsetof(struct __notexiststruct__, x.y.z); }", 1);
}

TEST(semantic_analyser, int_ident)
{
  test("BEGIN { sizeof(int32) }");
}

TEST(semantic_analyser, tracepoint_common_field)
{
  test("tracepoint:file:filename { args.filename }");
  test("tracepoint:file:filename { args.common_field }", 1);
}

TEST(semantic_analyser, string_size)
{
  // Size of the variable should be the size of the larger string (incl. null)
  BPFtrace bpftrace;
  auto ast = test(bpftrace, true, R"_(BEGIN { $x = "hi"; $x = "hello"; })_", 0);
  auto stmt = ast.root->probes.at(0)->block->stmts.at(0);
  auto *var_assign = stmt.as<ast::AssignVarStatement>();
  ASSERT_TRUE(var_assign->var()->var_type.IsStringTy());
  ASSERT_EQ(var_assign->var()->var_type.GetSize(), 6UL);

  ast = test(bpftrace, true, R"_(k:f1 {@ = "hi";} k:f2 {@ = "hello";})_", 0);
  stmt = ast.root->probes.at(0)->block->stmts.at(0);
  auto *map_assign = stmt.as<ast::AssignMapStatement>();
  ASSERT_TRUE(map_assign->map->value_type.IsStringTy());
  ASSERT_EQ(map_assign->map->value_type.GetSize(), 6UL);

  ast = test(
      bpftrace, true, R"_(k:f1 {@["hi"] = 0;} k:f2 {@["hello"] = 1;})_", 0);
  stmt = ast.root->probes.at(0)->block->stmts.at(0);
  map_assign = stmt.as<ast::AssignMapStatement>();
  ASSERT_TRUE(map_assign->key.type().IsStringTy());
  ASSERT_EQ(map_assign->key.type().GetSize(), 3UL);
  ASSERT_EQ(map_assign->map->key_type.GetSize(), 6UL);

  ast = test(bpftrace,
             true,
             R"_(k:f1 {@["hi", 0] = 0;} k:f2 {@["hello", 1] = 1;})_",
             0);
  stmt = ast.root->probes.at(0)->block->stmts.at(0);
  map_assign = stmt.as<ast::AssignMapStatement>();
  ASSERT_TRUE(map_assign->key.type().IsTupleTy());
  ASSERT_TRUE(map_assign->key.type().GetField(0).type.IsStringTy());
  ASSERT_EQ(map_assign->key.type().GetField(0).type.GetSize(), 3UL);
  ASSERT_EQ(map_assign->map->key_type.GetField(0).type.GetSize(), 6UL);
  ASSERT_EQ(map_assign->key.type().GetSize(), 16UL);
  ASSERT_EQ(map_assign->map->key_type.GetSize(), 16UL);

  ast = test(bpftrace,
             true,
             R"_(k:f1 {$x = ("hello", 0);} k:f2 {$x = ("hi", 0); })_",
             0);
  stmt = ast.root->probes.at(0)->block->stmts.at(0);
  var_assign = stmt.as<ast::AssignVarStatement>();
  ASSERT_TRUE(var_assign->var()->var_type.IsTupleTy());
  ASSERT_TRUE(var_assign->var()->var_type.GetField(0).type.IsStringTy());
  ASSERT_EQ(var_assign->var()->var_type.GetSize(), 16UL); // tuples are not
                                                          // packed
  ASSERT_EQ(var_assign->var()->var_type.GetField(0).type.GetSize(), 6UL);
}

TEST(semantic_analyser, call_nsecs)
{
  test("BEGIN { $ns = nsecs(); }");
  test("BEGIN { $ns = nsecs(monotonic); }");
  test("BEGIN { $ns = nsecs(boot); }");
  MockBPFfeature hasfeature(true);
  test(hasfeature, "BEGIN { $ns = nsecs(tai); }");
  test("BEGIN { $ns = nsecs(sw_tai); }");
  test_error("BEGIN { $ns = nsecs(xxx); }", R"(
stdin:1:15-24: ERROR: Invalid timestamp mode: xxx
BEGIN { $ns = nsecs(xxx); }
              ~~~~~~~~~
)");
}

TEST(semantic_analyser, call_pid_tid)
{
  test("BEGIN { $i = tid(); }");
  test("BEGIN { $i = pid(); }");
  test("BEGIN { $i = tid(curr_ns); }");
  test("BEGIN { $i = pid(curr_ns); }");
  test("BEGIN { $i = tid(init); }");
  test("BEGIN { $i = pid(init); }");
  test_error("BEGIN { $i = tid(xxx); }", R"(
stdin:1:14-21: ERROR: Invalid PID namespace mode: xxx (expects: curr_ns or init)
BEGIN { $i = tid(xxx); }
             ~~~~~~~
)");
  test_error("BEGIN { $i = tid(1); }", R"(
stdin:1:14-20: ERROR: tid() only supports curr_ns and init as the argument (int provided)
BEGIN { $i = tid(1); }
             ~~~~~~
)");
}

TEST(semantic_analyser, config)
{
  test("config = { BPFTRACE_MAX_AST_NODES=1 } BEGIN { $ns = nsecs(); }");
  test("config = { BPFTRACE_MAX_AST_NODES=1; stack_mode=raw } BEGIN { $ns = "
       "nsecs(); }");
}

TEST(semantic_analyser, subprog_return)
{
  test("fn f(): void { return; }");
  test("fn f(): int64 { return 1; }");

  // Error location is incorrect: #3063
  test_error("fn f(): void { return 1; }", R"(
stdin:1:17-25: ERROR: Function f is of type void, cannot return int64
fn f(): void { return 1; }
                ~~~~~~~~
)");
  // Error location is incorrect: #3063
  test_error("fn f(): int64 { return; }", R"(
stdin:1:18-24: ERROR: Function f is of type int64, cannot return void
fn f(): int64 { return; }
                 ~~~~~~
)");
}

TEST(semantic_analyser, subprog_arguments)
{
  test("fn f($a : int64): int64 { return $a; }");
  // Error location is incorrect: #3063
  test_error("fn f($a : int64): string { return $a; }", R"(
stdin:1:30-39: ERROR: Function f is of type string, cannot return int64
fn f($a : int64): string { return $a; }
                             ~~~~~~~~~
)");
}

TEST(semantic_analyser, subprog_map)
{
  test("fn f(): void { @a = 0; }");
  test("fn f(): int64 { @a = 0; return @a + 1; }");
  test("fn f(): void { @a[0] = 0; }");
  test("fn f(): int64 { @a[0] = 0; return @a[0] + 1; }");
}

TEST(semantic_analyser, subprog_builtin)
{
  test("fn f(): void { print(\"Hello world\"); }");
  test("fn f(): uint64 { return sizeof(int64); }");
  test("fn f(): uint64 { return nsecs; }");
}

TEST(semantic_analyser, subprog_buildin_disallowed)
{
  // Error location is incorrect: #3063
  test_error("fn f(): int64 { return func; }", R"(
stdin:1:25-29: ERROR: Builtin func not supported outside probe
fn f(): int64 { return func; }
                        ~~~~
stdin:1:18-29: ERROR: Function f is of type int64, cannot return none
fn f(): int64 { return func; }
                 ~~~~~~~~~~~
)");
}

class semantic_analyser_btf : public test_btf {};

TEST_F(semantic_analyser_btf, fentry)
{
  test("fentry:func_1 { 1 }");
  test("fexit:func_1 { 1 }");
  test("fentry:func_1 { $x = args.a; $y = args.foo1; $z = args.foo2->f.a; }");
  test("fexit:func_1 { $x = retval; }");
  test("fentry:vmlinux:func_1 { 1 }");
  test("fentry:*:func_1 { 1 }");

  test_error("fexit:func_1 { $x = args.foo; }", R"(
stdin:1:21-26: ERROR: Can't find function parameter foo
fexit:func_1 { $x = args.foo; }
                    ~~~~~
)");
  test("fexit:func_1 { $x = args; }");
  test("fentry:func_1 { @ = args; }");
  test("fentry:func_1 { @[args] = 1; }");
  // reg() is not available in fentry
#ifdef __x86_64__
  test_error("fentry:func_1 { reg(\"ip\") }", R"(
stdin:1:17-26: ERROR: reg can not be used with "fentry" probes
fentry:func_1 { reg("ip") }
                ~~~~~~~~~
)");
  test_error("fexit:func_1 { reg(\"ip\") }", R"(
stdin:1:16-25: ERROR: reg can not be used with "fexit" probes
fexit:func_1 { reg("ip") }
               ~~~~~~~~~
)");
#endif
  // Backwards compatibility
  test("fentry:func_1 { $x = args->a; }");
}

TEST_F(semantic_analyser_btf, short_name)
{
  test("f:func_1 { 1 }");
  test("fr:func_1 { 1 }");
}

TEST_F(semantic_analyser_btf, call_path)
{
  test("fentry:func_1 { @k = path( args.foo1 ) }");
  test("fexit:func_1 { @k = path( retval->foo1 ) }");
  test("fentry:func_1 { path( args.foo1, 16);}");
  test("fentry:func_1 { path( args.foo1, \"Na\");}", 1);
  test("fentry:func_1 { path( args.foo1, -1);}", 1);
}

TEST_F(semantic_analyser_btf, call_skb_output)
{
  test("fentry:func_1 { $ret = skboutput(\"one.pcap\", args.foo1, 1500, 0); }");
  test("fexit:func_1 { $ret = skboutput(\"one.pcap\", args.foo1, 1500, 0); "
       "}");

  test_error("fentry:func_1 { $ret = skboutput(); }", R"(
stdin:1:24-35: ERROR: skboutput() requires 4 arguments (0 provided)
fentry:func_1 { $ret = skboutput(); }
                       ~~~~~~~~~~~
)");
  test_error("fentry:func_1 { $ret = skboutput(\"one.pcap\"); }", R"(
stdin:1:24-45: ERROR: skboutput() requires 4 arguments (1 provided)
fentry:func_1 { $ret = skboutput("one.pcap"); }
                       ~~~~~~~~~~~~~~~~~~~~~
)");
  test_error("fentry:func_1 { $ret = skboutput(\"one.pcap\", args.foo1); }", R"(
stdin:1:24-56: ERROR: skboutput() requires 4 arguments (2 provided)
fentry:func_1 { $ret = skboutput("one.pcap", args.foo1); }
                       ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
)");
  test_error(
      "fentry:func_1 { $ret = skboutput(\"one.pcap\", args.foo1, 1500); }", R"(
stdin:1:24-62: ERROR: skboutput() requires 4 arguments (3 provided)
fentry:func_1 { $ret = skboutput("one.pcap", args.foo1, 1500); }
                       ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
)");
  test_error("kprobe:func_1 { $ret = skboutput(\"one.pcap\", arg1, 1500, 0); }",
             R"(
stdin:1:24-60: ERROR: skboutput can not be used with "kprobe" probes
kprobe:func_1 { $ret = skboutput("one.pcap", arg1, 1500, 0); }
                       ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
)");
}

TEST_F(semantic_analyser_btf, call_percpu_kaddr)
{
  test("kprobe:f { percpu_kaddr(\"process_counts\"); }");
  test("kprobe:f { percpu_kaddr(\"process_counts\", 0); }");
  test("kprobe:f { @x = percpu_kaddr(\"process_counts\"); }");
  test("kprobe:f { @x = percpu_kaddr(\"process_counts\", 0); }");
  test("kprobe:f { percpu_kaddr(); }", 1);
  test("kprobe:f { percpu_kaddr(0); }", 1);

  test_error("kprobe:f { percpu_kaddr(\"nonsense\"); }",
             R"(
stdin:1:12-36: ERROR: Could not resolve variable "nonsense" from BTF
kprobe:f { percpu_kaddr("nonsense"); }
           ~~~~~~~~~~~~~~~~~~~~~~~~
)",
             false);
}

TEST_F(semantic_analyser_btf, call_socket_cookie)
{
  test("fentry:tcp_shutdown { $ret = socket_cookie(args.sk); }");
  test("fexit:tcp_shutdown { $ret = socket_cookie(args.sk); }");

  test_error("fentry:tcp_shutdown { $ret = socket_cookie(); }", R"(
stdin:1:30-45: ERROR: socket_cookie() requires one argument (0 provided)
fentry:tcp_shutdown { $ret = socket_cookie(); }
                             ~~~~~~~~~~~~~~~
)");
  test_error("fentry:tcp_shutdown { $ret = socket_cookie(args.how); }", R"(
stdin:1:30-53: ERROR: socket_cookie() only supports 'struct sock *' as the argument (int provided)
fentry:tcp_shutdown { $ret = socket_cookie(args.how); }
                             ~~~~~~~~~~~~~~~~~~~~~~~
)");
  test_error("fentry:func_1 { $ret = socket_cookie(args.foo1); }", R"(
stdin:1:24-48: ERROR: socket_cookie() only supports 'struct sock *' as the argument ('struct Foo1 *' provided)
fentry:func_1 { $ret = socket_cookie(args.foo1); }
                       ~~~~~~~~~~~~~~~~~~~~~~~~
)");
  test_error(
      "kprobe:tcp_shutdown { $ret = socket_cookie((struct sock *)arg0); }", R"(
stdin:1:30-65: ERROR: socket_cookie can not be used with "kprobe" probes
kprobe:tcp_shutdown { $ret = socket_cookie((struct sock *)arg0); }
                             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
)");
}

TEST_F(semantic_analyser_btf, iter)
{
  test("iter:task { 1 }");
  test("iter:task { $x = ctx->task->pid }");
  test("iter:task_file { $x = ctx->file->ino }");
  test("iter:task_vma { $x = ctx->vma->vm_start }");
  test("iter:task { printf(\"%d\", ctx->task->pid); }");
  test_error("iter:task { $x = args.foo; }", R"(
stdin:1:18-22: ERROR: The args builtin can only be used with tracepoint/fentry/uprobe probes (iter used here)
iter:task { $x = args.foo; }
                 ~~~~
)");
  test_error("iter:task,iter:task_file { 1 }", R"(
stdin:1:1-10: ERROR: Only single iter attach point is allowed.
iter:task,iter:task_file { 1 }
~~~~~~~~~
)");
  test_error("iter:task,f:func_1 { 1 }", R"(
stdin:1:1-10: ERROR: Only single iter attach point is allowed.
iter:task,f:func_1 { 1 }
~~~~~~~~~
)");
}

TEST_F(semantic_analyser_btf, rawtracepoint)
{
  test("rawtracepoint:event_rt { args.first_real_arg }");

  test_error("rawtracepoint:event_rt { args.bad_arg }", R"(
stdin:1:26-31: ERROR: Can't find function parameter bad_arg
rawtracepoint:event_rt { args.bad_arg }
                         ~~~~~
)");
}

// Sanity check for kfunc/kretfunc aliases
TEST_F(semantic_analyser_btf, kfunc)
{
  test("kfunc:func_1 { 1 }");
  test("kretfunc:func_1 { 1 }");
  test("kfunc:func_1 { $x = args.a; $y = args.foo1; $z = args.foo2->f.a; }");
  test("kretfunc:func_1 { $x = retval; }");
  test("kfunc:vmlinux:func_1 { 1 }");
  test("kfunc:*:func_1 { 1 }");
  test("kfunc:func_1 { @[func] = 1; }");

  test_error("kretfunc:func_1 { $x = args.foo; }", R"(
stdin:1:24-29: ERROR: Can't find function parameter foo
kretfunc:func_1 { $x = args.foo; }
                       ~~~~~
)");
  test("kretfunc:func_1 { $x = args; }");
  test("kfunc:func_1 { @ = args; }");
  test("kfunc:func_1 { @[args] = 1; }");
  // reg() is not available in kfunc
#ifdef __x86_64__
  test_error("kfunc:func_1 { reg(\"ip\") }", R"(
stdin:1:16-25: ERROR: reg can not be used with "fentry" probes
kfunc:func_1 { reg("ip") }
               ~~~~~~~~~
)");
  test_error("kretfunc:func_1 { reg(\"ip\") }", R"(
stdin:1:19-28: ERROR: reg can not be used with "fexit" probes
kretfunc:func_1 { reg("ip") }
                  ~~~~~~~~~
)");
#endif
  // Backwards compatibility
  test("kfunc:func_1 { $x = args->a; }");
}

TEST(semantic_analyser, btf_type_tags)
{
  test("t:btf:tag { args.parent }");
  test_error("t:btf:tag { args.real_parent }", R"(
stdin:1:13-18: ERROR: Attempting to access pointer field 'real_parent' with unsupported tag attribute: percpu
t:btf:tag { args.real_parent }
            ~~~~~
)");
}

TEST(semantic_analyser, for_loop_map_one_key)
{
  test("BEGIN { @map[0] = 1; for ($kv : @map) { print($kv); } }", R"(
Program
 BEGIN
  =
   map: @map :: [int64]int64
    int: 0
   int: 1
  for
   decl
    variable: $kv :: [(int64,int64)]
    map: @map :: [int64]int64
   stmts
    call: print
     variable: $kv :: [(int64,int64)]
)");
}

TEST(semantic_analyser, for_loop_map_two_keys)
{
  test("BEGIN { @map[0,0] = 1; for ($kv : @map) { print($kv); } }", R"(
Program
 BEGIN
  =
   map: @map :: [(int64,int64)]int64
    tuple: :: [(int64,int64)]
     int: 0
     int: 0
   int: 1
  for
   decl
    variable: $kv :: [((int64,int64),int64)]
    map: @map :: [(int64,int64)]int64
   stmts
    call: print
     variable: $kv :: [((int64,int64),int64)]
)");
}

TEST(semantic_analyser, for_loop_map)
{
  test("BEGIN { @map[0] = 1; for ($kv : @map) { print($kv); } }");
  test("BEGIN { @map[0] = 1; for ($kv : @map) { print($kv.0); } }");
  test("BEGIN { @map[0] = 1; for ($kv : @map) { print($kv.1); } }");
  test("BEGIN { @map1[@map2] = 1; @map2 = 1; for ($kv : @map1) { print($kv); } "
       "}");
}

TEST(semantic_analyser, for_loop_map_declared_after)
{
  // Regression test: What happens with @map[$kv.0] when @map hasn't been
  // defined yet?
  test("BEGIN { for ($kv : @map) { @map[$kv.0] } @map[0] = 1; }");
}

TEST(semantic_analyser, for_loop_map_no_key)
{
  // Error location is incorrect: #3063
  test_error("BEGIN { @map = 1; for ($kv : @map) { } }", R"(
stdin:1:30-35: ERROR: @map has no explicit keys (scalar map), and cannot be used for iteration
BEGIN { @map = 1; for ($kv : @map) { } }
                             ~~~~~
)");
}

TEST(semantic_analyser, for_loop_map_undefined)
{
  // Error location is incorrect: #3063
  test_error("BEGIN { for ($kv : @map) { } }", R"(
stdin:1:20-25: ERROR: Undefined map: @map
BEGIN { for ($kv : @map) { } }
                   ~~~~~
)");
}

TEST(semantic_analyser, for_loop_map_undefined2)
{
  // Error location is incorrect: #3063
  test_error("BEGIN { @map[0] = 1; for ($kv : @undef) { @map[$kv.0]; } }", R"(
stdin:1:33-40: ERROR: Undefined map: @undef
BEGIN { @map[0] = 1; for ($kv : @undef) { @map[$kv.0]; } }
                                ~~~~~~~
)");
}

TEST(semantic_analyser, for_loop_map_restricted_types)
{
  test_error("BEGIN { @map[0] = hist(10); for ($kv : @map) { } }", R"(
stdin:1:40-45: ERROR: Loop expression does not support type: hist_t
BEGIN { @map[0] = hist(10); for ($kv : @map) { } }
                                       ~~~~~
)");
  test_error("BEGIN { @map[0] = lhist(10, 0, 10, 1); for ($kv : @map) { } }",
             R"(
stdin:1:51-56: ERROR: Loop expression does not support type: lhist_t
BEGIN { @map[0] = lhist(10, 0, 10, 1); for ($kv : @map) { } }
                                                  ~~~~~
)");
  test_error("BEGIN { @map[0] = tseries(10, 10s, 10); for ($kv : @map) { } }",
             R"(
stdin:1:52-57: ERROR: Loop expression does not support type: tseries_t
BEGIN { @map[0] = tseries(10, 10s, 10); for ($kv : @map) { } }
                                                   ~~~~~
)");
  test_error("BEGIN { @map[0] = stats(10); for ($kv : @map) { } }", R"(
stdin:1:41-46: ERROR: Loop expression does not support type: stats_t
BEGIN { @map[0] = stats(10); for ($kv : @map) { } }
                                        ~~~~~
)");
}

TEST(semantic_analyser, for_loop_shadowed_decl)
{
  test_error(R"(
    BEGIN {
      $kv = 1;
      @map[0] = 1;
      for ($kv : @map) { }
    })",
             R"(
stdin:4:11-15: ERROR: Loop declaration shadows existing variable: $kv
      for ($kv : @map) { }
          ~~~~
)");
}

TEST(semantic_analyser, for_loop_variables_read_only)
{
  test(R"(
    BEGIN {
      $var = 0;
      @map[0] = 1;
      for ($kv : @map) {
        print($var);
      }
      print($var);
    })",
       R"(*
  for
   ctx
    $var :: [int64 *, AS(kernel)]
   decl
*)");
}

TEST(semantic_analyser, for_loop_variables_modified_during_loop)
{
  test(R"(
    BEGIN {
      $var = 0;
      @map[0] = 1;
      for ($kv : @map) {
        $var++;
      }
      print($var);
    })",
       R"(*
  for
   ctx
    $var :: [int64 *, AS(kernel)]
   decl
*)");
}

TEST(semantic_analyser, for_loop_variables_created_in_loop)
{
  // $var should not appear in ctx
  test(R"(
    BEGIN {
      @map[0] = 1;
      for ($kv : @map) {
        $var = 2;
        print($var);
      }
    })",
       R"(*
  for
   decl
*)");
}

TEST(semantic_analyser, for_loop_variables_multiple)
{
  test(R"(
    BEGIN {
      @map[0] = 1;
      $var1 = 123;
      $var2 = "abc";
      $var3 = "def";
      for ($kv : @map) {
        $var1 = 456;
        print($var3);
      }
    })",
       R"(*
  for
   ctx
    $var1 :: [int64 *, AS(kernel)]
    $var3 :: [string[4] *, AS(kernel)]
   decl
*)");
}

TEST(semantic_analyser, for_loop_variables_created_in_loop_used_after)
{
  test_error(R"(
    BEGIN {
      @map[0] = 1;
      for ($kv : @map) {
        $var = 2;
      }
      print($var);
    })",
             R"(
stdin:6:7-17: ERROR: Undefined or undeclared variable: $var
      print($var);
      ~~~~~~~~~~
)");

  test_error(R"(
    BEGIN {
      @map[0] = 1;
      for ($kv : @map) {
        print($kv);
      }
      print($kv);
    })",
             R"(
stdin:6:7-16: ERROR: Undefined or undeclared variable: $kv
      print($kv);
      ~~~~~~~~~
)");
}

TEST(semantic_analyser, for_loop_invalid_expr)
{
  // Error location is incorrect: #3063
  test_error("BEGIN { for ($x : $var) { } }", R"(
stdin:1:19-25: ERROR: syntax error, unexpected ), expecting [ or . or ->
BEGIN { for ($x : $var) { } }
                  ~~~~~~
)");
  test_error("BEGIN { for ($x : 1+2) { } }", R"(
stdin:1:19-22: ERROR: syntax error, unexpected +, expecting [ or . or ->
BEGIN { for ($x : 1+2) { } }
                  ~~~
)");
  test_error("BEGIN { for ($x : \"abc\") { } }", R"(
stdin:1:19-26: ERROR: syntax error, unexpected ), expecting [ or . or ->
BEGIN { for ($x : "abc") { } }
                  ~~~~~~~
)");
}

TEST(semantic_analyser, for_loop_multiple_errors)
{
  // Error location is incorrect: #3063
  test_error(R"(
    BEGIN {
      $kv = 1;
      @map[0] = 1;
      for ($kv : @map) { }
    })",
             R"(
stdin:4:11-15: ERROR: Loop declaration shadows existing variable: $kv
      for ($kv : @map) { }
          ~~~~
)");
}

TEST(semantic_analyser, for_loop_control_flow)
{
  test("BEGIN { @map[0] = 1; for ($kv : @map) { break; } }");
  test("BEGIN { @map[0] = 1; for ($kv : @map) { continue; } }");

  // Error location is incorrect: #3063
  test_error("BEGIN { @map[0] = 1; for ($kv : @map) { return; } }", R"(
stdin:1:42-48: ERROR: 'return' statement is not allowed in a for-loop
BEGIN { @map[0] = 1; for ($kv : @map) { return; } }
                                         ~~~~~~
)");
}

TEST(semantic_analyser, for_loop_missing_feature)
{
  test_error("BEGIN { @map[0] = 1; for ($kv : @map) { print($kv); } }",
             R"(
stdin:1:22-25: ERROR: Missing required kernel feature: for_each_map_elem
BEGIN { @map[0] = 1; for ($kv : @map) { print($kv); } }
                     ~~~
)",
             false);
}

TEST(semantic_analyser, for_loop_castable_map_missing_feature)
{
  test_error("BEGIN { @map[0] = count(); for ($kv : @map) { print($kv); } }",
             R"(
stdin:1:28-31: ERROR: Missing required kernel feature: for_each_map_elem
BEGIN { @map[0] = count(); for ($kv : @map) { print($kv); } }
                           ~~~
)",
             false);
}

TEST(semantic_analyser, for_range_loop)
{
  // These are all technically valid, although they may result in zero
  // iterations (for example 5..0 will result in no iterations).
  test(R"(BEGIN { for ($i : 0..5) { printf("%d\n", $i); } })");
  test(R"(BEGIN { for ($i : 5..0) { printf("%d\n", $i); } })");
  test(R"(BEGIN { for ($i : (-10)..10) { printf("%d\n", $i); } })");
  test(R"(BEGIN { $start = 0; for ($i : $start..5) { printf("%d\n", $i); } })");
  test(R"(BEGIN { $end = 5; for ($i : 0..$end) { printf("%d\n", $i); } })");
  test(
      R"(BEGIN { $start = 0; $end = 5; for ($i : $start..$end) { printf("%d\n", $i); } })");
  test(
      R"(BEGIN { for ($i : nsecs()..(nsecs()+100)) { printf("%d\n", $i); } })");
  test(
      R"(BEGIN { for ($i : sizeof(int8)..sizeof(int64)) { printf("%d\n", $i); } })");
  test(R"(BEGIN { for ($i : ((int8)0)..((int8)5)) { printf("%d\n", $i); } })");
}

TEST(semantic_analyser, for_range_nested)
{
  test("BEGIN { for ($i : 0..5) { for ($j : 0..$i) { printf(\"%d %d\\n\", "
       "$i, $j); } } }");
}

TEST(semantic_analyser, for_range_variable_use)
{
  test("BEGIN { for ($i : 0..5) { @[$i] = $i * 2; } }");
}

TEST(semantic_analyser, for_range_shadowing)
{
  test_error(R"(BEGIN { $i = 10; for ($i : 0..5) { printf("%d", $i); } })",
             R"(
stdin:1:22-25: ERROR: Loop declaration shadows existing variable: $i
BEGIN { $i = 10; for ($i : 0..5) { printf("%d", $i); } }
                     ~~~
)");
}

TEST(semantic_analyser, for_range_invalid_types)
{
  test_error(R"(BEGIN { for ($i : "str"..5) { printf("%d", $i); } })",
             R"(
stdin:1:19-28: ERROR: Loop range requires an integer for the start value
BEGIN { for ($i : "str"..5) { printf("%d", $i); } }
                  ~~~~~~~~~
)");

  test_error(R"(BEGIN { for ($i : 0.."str") { printf("%d", $i); } })",
             R"(
stdin:1:19-28: ERROR: Loop range requires an integer for the end value
BEGIN { for ($i : 0.."str") { printf("%d", $i); } }
                  ~~~~~~~~~
)");

  test_error(R"(BEGIN { for ($i : 0.0..5) { printf("%d", $i); } })", R"(
stdin:1:19-23: ERROR: Can not access index '0' on expression of type 'int64'
BEGIN { for ($i : 0.0..5) { printf("%d", $i); } }
                  ~~~~
stdin:1:19-26: ERROR: Loop range requires an integer for the start value
BEGIN { for ($i : 0.0..5) { printf("%d", $i); } }
                  ~~~~~~~
)");
}

TEST(semantic_analyser, for_range_control_flow)
{
  test("BEGIN { for ($i : 0..5) { break; } }");
  test("BEGIN { for ($i : 0..5) { continue; } }");

  test_error("BEGIN { for ($i : 0..5) { return; } }", R"(
stdin:1:28-34: ERROR: 'return' statement is not allowed in a for-loop
BEGIN { for ($i : 0..5) { return; } }
                           ~~~~~~
)");
}

TEST(semantic_analyser, for_range_out_of_scope)
{
  test_error(
      R"(BEGIN { for ($i : 0..5) { printf("%d", $i); } printf("%d", $i); })",
      R"(
stdin:1:61-63: ERROR: Undefined or undeclared variable: $i
BEGIN { for ($i : 0..5) { printf("%d", $i); } printf("%d", $i); }
                                                            ~~
)");
}

TEST(semantic_analyser, for_range_context_access)
{
  test_error("kprobe:f { for ($i : 0..5) { arg0 } }", R"(
stdin:1:31-35: ERROR: 'arg0' builtin is not allowed in a for-loop
kprobe:f { for ($i : 0..5) { arg0 } }
                              ~~~~
)");
}

TEST(semantic_analyser, for_range_nested_range)
{
  test("BEGIN { for ($i : 0..5) { for ($j : 0..$i) { printf(\"%d %d\\n\", $i, "
       "$j); } } }");
}

TEST(semantic_analyser, castable_map_missing_feature)
{
  MockBPFfeature feature(false);
  test(feature, "k:f {  @a = count(); }");
  test(feature, "k:f {  @a = count(); print(@a) }");
  test(feature, "k:f {  @a = count(); clear(@a) }");
  test(feature, "k:f {  @a = count(); zero(@a) }");
  test(feature, "k:f {  @a[1] = count(); delete(@a, 1) }");
  test(feature, "k:f {  @a[1] = count(); has_key(@a, 1) }");

  test_error("k:f {  @a = count(); len(@a) }",
             R"(
stdin:1:22-28: ERROR: call to len() expects a map with explicit keys (non-scalar map)
k:f {  @a = count(); len(@a) }
                     ~~~~~~
)",
             false);

  test_error("BEGIN { @a = count(); print((uint64)@a) }",
             R"(
stdin:1:23-39: ERROR: Missing required kernel feature: map_lookup_percpu_elem
BEGIN { @a = count(); print((uint64)@a) }
                      ~~~~~~~~~~~~~~~~
)",
             false);

  test_error("BEGIN { @a = count(); print((@a, 1)) }",
             R"(
stdin:1:23-32: ERROR: Missing required kernel feature: map_lookup_percpu_elem
BEGIN { @a = count(); print((@a, 1)) }
                      ~~~~~~~~~
)",
             false);

  test_error("BEGIN { @a[1] = count(); print(@a[1]) }",
             R"(
stdin:1:26-37: ERROR: Missing required kernel feature: map_lookup_percpu_elem
BEGIN { @a[1] = count(); print(@a[1]) }
                         ~~~~~~~~~~~
)",
             false);

  test_error("BEGIN { @a = count(); $b = @a; }",
             R"(
stdin:1:28-30: ERROR: Missing required kernel feature: map_lookup_percpu_elem
BEGIN { @a = count(); $b = @a; }
                           ~~
)",
             false);

  test_error("BEGIN { @a = count(); @b = 1; @b = @a; }",
             R"(
stdin:1:36-38: ERROR: Missing required kernel feature: map_lookup_percpu_elem
BEGIN { @a = count(); @b = 1; @b = @a; }
                                   ~~
)",
             false);
}

TEST(semantic_analyser, for_loop_no_ctx_access)
{
  test_error("kprobe:f { @map[0] = 1; for ($kv : @map) { arg0 } }",
             R"(
stdin:1:45-49: ERROR: 'arg0' builtin is not allowed in a for-loop
kprobe:f { @map[0] = 1; for ($kv : @map) { arg0 } }
                                            ~~~~
)");
}

TEST_F(semantic_analyser_btf, args_builtin_mixed_probes)
{
  test_error("fentry:func_1,tracepoint:sched:sched_one { args }", R"(
stdin:1:44-48: ERROR: The args builtin can only be used within the context of a single probe type, e.g. "probe1 {args}" is valid while "probe1,probe2 {args}" is not.
fentry:func_1,tracepoint:sched:sched_one { args }
                                           ~~~~
)");
}

TEST_F(semantic_analyser_btf, binop_late_ptr_resolution)
{
  test(R"(fentry:func_1 { if (@a[1] == args.foo1) { } @a[1] = args.foo1; })");
}

TEST(semantic_analyser, buf_strlen_too_large)
{
  auto bpftrace = get_mock_bpftrace();
  bpftrace->config_->max_strlen = 9999999999;

  test_error(*bpftrace, "uprobe:/bin/sh:f { buf(arg0, 4) }", R"(
stdin:1:20-32: ERROR: BPFTRACE_MAX_STRLEN too large to use on buffer (9999999999 > 4294967295)
uprobe:/bin/sh:f { buf(arg0, 4) }
                   ~~~~~~~~~~~~
)");

  test_error(*bpftrace, "uprobe:/bin/sh:f { buf(arg0) }", R"(
stdin:1:20-29: ERROR: BPFTRACE_MAX_STRLEN too large to use on buffer (9999999999 > 4294967295)
uprobe:/bin/sh:f { buf(arg0) }
                   ~~~~~~~~~
)");
}

TEST(semantic_analyser, variable_declarations)
{
  test("BEGIN { let $a; $a = 1; }");
  test("BEGIN { let $a: int16; $a = 1; }");
  test("BEGIN { let $a = 1; }");
  test("BEGIN { let $a: uint16 = 1; }");
  test("BEGIN { let $a: int16 = 1; }");
  test("BEGIN { let $a: uint8 = 1; $a = 100; }");
  test("BEGIN { let $a: int8 = 1; $a = -100; }");
  test(R"(BEGIN { let $a: string; $a = "hiya"; })");
  test("BEGIN { let $a: int16; print($a); }");
  test("BEGIN { let $a; print($a); $a = 1; }");
  test(R"(BEGIN { let $a = "hiya"; $a = "longerstr"; })");
  test("BEGIN { let $a: int16 = 1; $a = (int8)2; }");
  // Test more types
  test("BEGIN { let $a: struct x; }");
  test("BEGIN { let $a: struct x *; }");
  test("BEGIN { let $a: struct task_struct *; $a = curtask; }");
  test("BEGIN { let $a: struct Foo[10]; }");
  test("BEGIN { if (1) { let $x; } $x = 2; }");
  test("BEGIN { if (1) { let $x; } else { let $x; } let $x; }");

  // https://github.com/bpftrace/bpftrace/pull/3668#issuecomment-2596432923
  test_for_warning("BEGIN { let $a; print($a); $a = 1; }",
                   "Variable used before it was assigned:");

  test_error("BEGIN { let $a; let $a; }", R"(
stdin:1:17-23: ERROR: Variable $a was already declared. Variable shadowing is not allowed.
BEGIN { let $a; let $a; }
                ~~~~~~
stdin:1:9-15: WARNING: This is the initial declaration.
BEGIN { let $a; let $a; }
        ~~~~~~
)");

  test_error("BEGIN { let $a: uint16; $a = -1; }", R"(
stdin:1:26-33: ERROR: Type mismatch for $a: trying to assign value of type 'int64' when variable already has a type 'uint16'
BEGIN { let $a: uint16; $a = -1; }
                         ~~~~~~~
)");

  test_error("BEGIN { let $a: uint8 = 1; $a = 10000; }", R"(
stdin:1:29-39: ERROR: Type mismatch for $a: trying to assign value '10000' which does not fit into the variable of type 'uint8'
BEGIN { let $a: uint8 = 1; $a = 10000; }
                            ~~~~~~~~~~
)");

  test_error("BEGIN { let $a: int8 = 1; $a = -10000; }", R"(
stdin:1:28-39: ERROR: Type mismatch for $a: trying to assign value '-10000' which does not fit into the variable of type 'int8'
BEGIN { let $a: int8 = 1; $a = -10000; }
                           ~~~~~~~~~~~
)");

  test_error("BEGIN { let $a; $a = (uint8)1; $a = -1; }", R"(
stdin:1:32-39: ERROR: Type mismatch for $a: trying to assign value of type 'int64' when variable already contains a value of type 'uint8'
BEGIN { let $a; $a = (uint8)1; $a = -1; }
                               ~~~~~~~
)");

  test_error("BEGIN { let $a: int8; $a = 10000; }", R"(
stdin:1:24-34: ERROR: Type mismatch for $a: trying to assign value '10000' which does not fit into the variable of type 'int8'
BEGIN { let $a: int8; $a = 10000; }
                       ~~~~~~~~~~
)");

  test_error("BEGIN { $a = -1; let $a; }", R"(
stdin:1:18-24: ERROR: Variable declarations need to occur before variable usage or assignment. Variable: $a
BEGIN { $a = -1; let $a; }
                 ~~~~~~
)");

  test_error("BEGIN { let $a: uint16 = -1; }", R"(
stdin:1:9-29: ERROR: Type mismatch for $a: trying to assign value of type 'int64' when variable already has a type 'uint16'
BEGIN { let $a: uint16 = -1; }
        ~~~~~~~~~~~~~~~~~~~~
)");

  test_error(R"(BEGIN { let $a: sum_t; })", R"(
stdin:1:9-23: ERROR: Invalid variable declaration type: sum_t
BEGIN { let $a: sum_t; }
        ~~~~~~~~~~~~~~
)");

  test_error(R"(BEGIN { let $a: struct bad_task; $a = *curtask; })", R"(
stdin:1:34-47: ERROR: Type mismatch for $a: trying to assign value of type 'struct task_struct' when variable already has a type 'struct bad_task'
BEGIN { let $a: struct bad_task; $a = *curtask; }
                                 ~~~~~~~~~~~~~
)");

  test_error(R"(BEGIN { $x = 2; if (1) { let $x; } })", R"(
stdin:1:26-32: ERROR: Variable declarations need to occur before variable usage or assignment. Variable: $x
BEGIN { $x = 2; if (1) { let $x; } }
                         ~~~~~~
)");
}

TEST(semantic_analyser, block_scoping)
{
  // if/else
  test("BEGIN { $a = 1; if (1) { $b = 2; print(($a, $b)); } }");
  test(R"(
      BEGIN {
        $a = 1;
        if (1) {
          print(($a));
          $b = 2;
          if (1) {
            print(($a, $b));
          } else {
            print(($a, $b));
          }
        }
      })");

  // for loops
  test(R"(
      BEGIN {
        @x[0] = 1;
        $a = 1;
        for ($kv : @x) {
          $b = 2;
          print(($a, $b));
        }
      })");
  test(R"(
    BEGIN {
      @x[0] = 1;
      @y[0] = 2;
      $a = 1;
      for ($kv : @x) {
        $b = 2;
        for ($ap : @y) {
          print(($a, $b));
        }
      }
    })");

  // while loops
  test(R"(
    BEGIN {
      $a = 1;
      while (1) {
        $b = 2;
        print(($a, $b));
      }
    })");
  test(R"(
    BEGIN {
      $a = 1;
      while (1) {
        print(($a));
        $b = 2;
        while (1) {
          print(($a, $b));
        }
      }
    })");

  // unroll
  test("BEGIN { $a = 1; unroll(1) { $b = 2; print(($a, $b)); } }");
  test(R"(
    BEGIN {
      $a = 1;
      unroll(1) {
        $b = 2;
        unroll(2) {
          print(($a, $b));
        }
      }
    })");

  // mixed
  test(R"(
    BEGIN {
      $a = 1;
      @x[0] = 1;
      if (1) {
        $b = 2;
        for ($kv : @x) {
          $c = 3;
          while (1) {
            $d = 4;
            unroll(1) {
              $e = 5;
              print(($a, $b, $c, $d, $e));
            }
          }
        }
      }
    })");

  // if/else
  test_error("BEGIN { if (1) { $a = 1; } print(($a)); }",
             R"(
stdin:1:28-37: ERROR: Undefined or undeclared variable: $a
BEGIN { if (1) { $a = 1; } print(($a)); }
                           ~~~~~~~~~
)");
  test_error("BEGIN { if (1) { $a = 1; } else { print(($a)); } }",
             R"(
stdin:1:35-44: ERROR: Undefined or undeclared variable: $a
BEGIN { if (1) { $a = 1; } else { print(($a)); } }
                                  ~~~~~~~~~
)");
  test_error("BEGIN { if (1) { $b = 1; } else { $b = 2; } print(($b)); }",
             R"(
stdin:1:45-54: ERROR: Undefined or undeclared variable: $b
BEGIN { if (1) { $b = 1; } else { $b = 2; } print(($b)); }
                                            ~~~~~~~~~
)");

  // for loops
  test_error(
      "kprobe:f { @map[0] = 1; for ($kv : @map) { $a = 1; } print(($a)); }",
      R"(
stdin:1:55-64: ERROR: Undefined or undeclared variable: $a
kprobe:f { @map[0] = 1; for ($kv : @map) { $a = 1; } print(($a)); }
                                                      ~~~~~~~~~
)");

  // while loops
  test_error("BEGIN { while (1) { $a = 1; } print(($a)); }",
             R"(
stdin:1:31-40: ERROR: Undefined or undeclared variable: $a
BEGIN { while (1) { $a = 1; } print(($a)); }
                              ~~~~~~~~~
)");

  // unroll
  test_error("BEGIN { unroll(1) { $a = 1; } print(($a)); }",
             R"(
stdin:1:31-40: ERROR: Undefined or undeclared variable: $a
BEGIN { unroll(1) { $a = 1; } print(($a)); }
                              ~~~~~~~~~
)");
}

TEST(semantic_analyser, invalid_assignment)
{
  test_error("BEGIN { @a = hist(10); let $b = @a; }", R"(
stdin:1:24-35: ERROR: Value 'hist_t' cannot be assigned to a scratch variable.
BEGIN { @a = hist(10); let $b = @a; }
                       ~~~~~~~~~~~
stdin:1:24-30: WARNING: Variable $b never assigned to.
BEGIN { @a = hist(10); let $b = @a; }
                       ~~~~~~
)");

  test_error("BEGIN { @a = lhist(123, 0, 123, 1); let $b = @a; }", R"(
stdin:1:37-48: ERROR: Value 'lhist_t' cannot be assigned to a scratch variable.
BEGIN { @a = lhist(123, 0, 123, 1); let $b = @a; }
                                    ~~~~~~~~~~~
stdin:1:37-43: WARNING: Variable $b never assigned to.
BEGIN { @a = lhist(123, 0, 123, 1); let $b = @a; }
                                    ~~~~~~
)");

  test_error("BEGIN { @a = tseries(10, 10s, 1); let $b = @a; }", R"(
stdin:1:35-46: ERROR: Value 'tseries_t' cannot be assigned to a scratch variable.
BEGIN { @a = tseries(10, 10s, 1); let $b = @a; }
                                  ~~~~~~~~~~~
stdin:1:35-41: WARNING: Variable $b never assigned to.
BEGIN { @a = tseries(10, 10s, 1); let $b = @a; }
                                  ~~~~~~
)");

  test_error("BEGIN { @a = stats(10); let $b = @a; }", R"(
stdin:1:25-36: ERROR: Value 'stats_t' cannot be assigned to a scratch variable.
BEGIN { @a = stats(10); let $b = @a; }
                        ~~~~~~~~~~~
stdin:1:25-31: WARNING: Variable $b never assigned to.
BEGIN { @a = stats(10); let $b = @a; }
                        ~~~~~~
)");

  test_error("BEGIN { @a = hist(10); @b = @a; }", R"(
stdin:1:24-31: ERROR: Map value 'hist_t' cannot be assigned from one map to another. The function that returns this type must be called directly e.g. `@b = hist(retval);`.
BEGIN { @a = hist(10); @b = @a; }
                       ~~~~~~~
)");

  test_error("BEGIN { @a = lhist(123, 0, 123, 1); @b = @a; }", R"(
stdin:1:37-44: ERROR: Map value 'lhist_t' cannot be assigned from one map to another. The function that returns this type must be called directly e.g. `@b = lhist(rand %10, 0, 10, 1);`.
BEGIN { @a = lhist(123, 0, 123, 1); @b = @a; }
                                    ~~~~~~~
)");

  test_error("BEGIN { @a = tseries(10, 10s, 1); @b = @a; }", R"(
stdin:1:35-42: ERROR: Map value 'tseries_t' cannot be assigned from one map to another. The function that returns this type must be called directly e.g. `@b = tseries(rand %10, 10s, 1);`.
BEGIN { @a = tseries(10, 10s, 1); @b = @a; }
                                  ~~~~~~~
)");

  test_error("BEGIN { @a = stats(10); @b = @a; }", R"(
stdin:1:25-32: ERROR: Map value 'stats_t' cannot be assigned from one map to another. The function that returns this type must be called directly e.g. `@b = stats(arg2);`.
BEGIN { @a = stats(10); @b = @a; }
                        ~~~~~~~
)");
}

TEST(semantic_analyser, no_maximum_passes)
{
  test("interval:s:1 { @j = @i; @i = @h; @h = @g; @g = @f; @f = @e; @e = @d; "
       "@d = @c; "
       "@c = @b; @b = @a; } interval:s:1 { @a = 1; }");
}

TEST(semantic_analyser, block_expressions)
{
  // Illegal, check that variable is not available
  test_error("BEGIN { let $x = { let $y = $x; $y }; print($x) }", R"(
stdin:1:29-31: ERROR: Undefined or undeclared variable: $x
BEGIN { let $x = { let $y = $x; $y }; print($x) }
                            ~~
)");

  // Good, variable is not shadowed
  test("BEGIN { let $x = { let $x = 1; $x }; print($x) }", R"(
Program
 BEGIN
  decl
   variable: $x :: [int64]
   decl
    variable: $x :: [int64]
    int: 1
   variable: $x :: [int64]
  call: print
   variable: $x :: [int64]
)");
}

TEST(semantic_analyser, map_declarations)
{
  auto bpftrace = get_mock_bpftrace();

  test(*bpftrace, "let @a = hash(2); BEGIN { @a = 1; }");
  test(*bpftrace, "let @a = lruhash(2); BEGIN { @a = 1; }");
  test(*bpftrace, "let @a = percpuhash(2); BEGIN { @a[1] = count(); }");
  test(*bpftrace, "let @a = percpulruhash(2); BEGIN { @a[1] = count(); }");
  test(*bpftrace, "let @a = percpulruhash(2); BEGIN { @a[1] = count(); }");
  test(*bpftrace, "let @a = percpuarray(1); BEGIN { @a = count(); }");

  test_for_warning(*bpftrace,
                   "let @a = hash(2); BEGIN { print(1); }",
                   "WARNING: Unused map: @a");

  test_error(*bpftrace, "let @a = percpuhash(2); BEGIN { @a = 1; }", R"(
stdin:1:33-35: ERROR: Incompatible map types. Type from declaration: percpuhash. Type from value/key type: hash
let @a = percpuhash(2); BEGIN { @a = 1; }
                                ~~
)");
  test_error(*bpftrace, "let @a = percpulruhash(2); BEGIN { @a = 1; }", R"(
stdin:1:36-38: ERROR: Incompatible map types. Type from declaration: percpulruhash. Type from value/key type: hash
let @a = percpulruhash(2); BEGIN { @a = 1; }
                                   ~~
)");
  test_error(*bpftrace, "let @a = hash(2); BEGIN { @a = count(); }", R"(
stdin:1:27-29: ERROR: Incompatible map types. Type from declaration: hash. Type from value/key type: percpuarray
let @a = hash(2); BEGIN { @a = count(); }
                          ~~
)");
  test_error(*bpftrace, "let @a = lruhash(2); BEGIN { @a = count(); }", R"(
stdin:1:30-32: ERROR: Incompatible map types. Type from declaration: lruhash. Type from value/key type: percpuarray
let @a = lruhash(2); BEGIN { @a = count(); }
                             ~~
)");
  test_error(*bpftrace,
             "let @a = percpuarray(1); BEGIN { @a[1] = count(); }",
             R"(
stdin:1:34-36: ERROR: Incompatible map types. Type from declaration: percpuarray. Type from value/key type: percpuhash
let @a = percpuarray(1); BEGIN { @a[1] = count(); }
                                 ~~
)");
  test_error(*bpftrace, "let @a = potato(2); BEGIN { @a[1] = count(); }", R"(
stdin:1:1-20: ERROR: Invalid bpf map type: potato
let @a = potato(2); BEGIN { @a[1] = count(); }
~~~~~~~~~~~~~~~~~~~
HINT: Valid map types: percpulruhash, percpuarray, percpuhash, lruhash, hash
)");

  test_error(*bpftrace, "let @a = percpuarray(10); BEGIN { @a = count(); }", R"(
stdin:1:1-26: ERROR: Max entries can only be 1 for map type percpuarray
let @a = percpuarray(10); BEGIN { @a = count(); }
~~~~~~~~~~~~~~~~~~~~~~~~~
)");
}

TEST(semantic_analyser, macros)
{
  auto bpftrace = get_mock_bpftrace();
  bpftrace->config_->unstable_macro = ConfigUnstable::enable;

  test_error(*bpftrace,
             "macro set($x) { $x = 1; $x } BEGIN { $a = \"string\"; set($a); }",
             R"(
stdin:1:17-23: ERROR: Type mismatch for $a: trying to assign value of type 'int64' when variable already contains a value of type 'string'
macro set($x) { $x = 1; $x } BEGIN { $a = "string"; set($a); }
                ~~~~~~
stdin:1:53-60: ERROR: expanded from
macro set($x) { $x = 1; $x } BEGIN { $a = "string"; set($a); }
                                                    ~~~~~~~
)");

  test_error(*bpftrace,
             "macro add2($x) { $x + 1 } macro add1($x) { add2($x) } BEGIN { $a "
             "= \"string\"; add1($a); }",
             R"(
stdin:1:21-22: ERROR: Type mismatch for '+': comparing string with int64
macro add2($x) { $x + 1 } macro add1($x) { add2($x) } BEGIN { $a = "string"; add1($a); }
                    ~
stdin:1:18-20: ERROR: left (string)
macro add2($x) { $x + 1 } macro add1($x) { add2($x) } BEGIN { $a = "string"; add1($a); }
                 ~~
stdin:1:23-24: ERROR: right (int64)
macro add2($x) { $x + 1 } macro add1($x) { add2($x) } BEGIN { $a = "string"; add1($a); }
                      ~
stdin:1:78-86: ERROR: expanded from
macro add2($x) { $x + 1 } macro add1($x) { add2($x) } BEGIN { $a = "string"; add1($a); }
                                                                             ~~~~~~~~
stdin:1:44-52: ERROR: expanded from
macro add2($x) { $x + 1 } macro add1($x) { add2($x) } BEGIN { $a = "string"; add1($a); }
                                           ~~~~~~~~
)");
}

TEST(semantic_analyser, warning_for_empty_positional_parameters)
{
  BPFtrace bpftrace;
  bpftrace.add_param("1");
  test_for_warning(bpftrace,
                   "BEGIN { print(($1, $2)) }",
                   "Positional parameter $2 is empty or not provided.");
}

TEST(semantic_analyser, warning_for_discared_return_value)
{
  // Non exhaustive testing, just a few examples
  test_for_warning("k:f { bswap(arg0); }",
                   "Return value discarded for bswap. It should be used");
  test_for_warning("k:f { cgroup_path(1); }",
                   "Return value discarded for cgroup_path. It should be used");
  test_for_warning("k:f { @x[1] = 0; has_key(@x, 1); }",
                   "Return value discarded for has_key. It should be used");
  test_for_warning("k:f { @x[1] = 1; len(@x); }",
                   "Return value discarded for len. It should be used");
  test_for_warning("k:f { uptr((int8*) arg0); }",
                   "Return value discarded for uptr. It should be used");
  test_for_warning("k:f { ustack(raw); }",
                   "Return value discarded for ustack. It should be used");
}

} // namespace bpftrace::test::semantic_analyser
