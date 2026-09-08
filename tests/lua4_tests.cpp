#include "dw4/lua4.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {
using namespace dw4;
using Bytes = std::vector<std::uint8_t>;
int checks{};
void check(bool value, const std::string& reason) { ++checks; if (!value) throw std::runtime_error(reason); }
template<class F> void rejects(F&& operation, std::string_view fragment) {
    std::string diagnostic;
    try { operation(); } catch (const Lua4Error& error) { diagnostic = error.what(); }
    check(diagnostic.find(fragment) != std::string::npos && !diagnostic.empty(),
        "Expected Lua error containing " + std::string(fragment) + ", got " + diagnostic);
}
enum Op : std::uint32_t {
    End, Return, Call, Tailcall, Pushnil, Pop, Pushint, Pushstring, Pushnum, Pushnegnum,
    Pushupvalue, Getlocal, Getglobal, Gettable, Getdotted, Getindexed, Pushself, Createtable,
    Setlocal, Setglobal, Settable, Setlist, Setmap, Add, Addi, Sub, Mult, Div, Pow, Concat,
    Minus, Not, Jmpne, Jmpeq, Jmplt, Jmple, Jmpgt, Jmpge, Jmpt, Jmpf, Jmpont, Jmponf,
    Jmp, Pushniljmp, Forprep, Forloop, Lforprep, Lforloop, Closure
};
std::uint32_t u(Op op, std::uint32_t value = 0) { return op | (value << 6); }
std::uint32_t s(Op op, std::int32_t value) { return u(op, static_cast<std::uint32_t>(value + 33554431)); }
std::uint32_t ab(Op op, std::uint32_t a, std::uint32_t b) { return op | (a << 15) | (b << 6); }
struct Function {
    std::uint32_t parameters{}, stack = 16;
    bool vararg{};
    std::vector<std::string> strings;
    std::vector<double> numbers;
    std::vector<Function> children;
    std::vector<std::uint32_t> code{End};
};
struct Writer {
    Bytes data;
    bool little = true;
    void integer(std::uint64_t value, std::size_t size = 4) {
        for (std::size_t i = 0; i < size; ++i)
            data.push_back(static_cast<std::uint8_t>(value >> (8 * (little ? i : size - i - 1))));
    }
    void number(double value) { integer(std::bit_cast<std::uint64_t>(value), 8); }
    void string(const std::string& value) {
        integer(value.size() + 1); data.insert(data.end(), value.begin(), value.end()); data.push_back(0);
    }
    void function(const Function& value) {
        string("@synthetic.lua"); integer(0); integer(value.parameters);
        data.push_back(value.vararg ? 1 : 0); integer(value.stack);
        integer(0); integer(0); // Debug locals and lines.
        integer(value.strings.size()); for (const auto& item : value.strings) string(item);
        integer(value.numbers.size()); for (const auto item : value.numbers) number(item);
        integer(value.children.size()); for (const auto& item : value.children) function(item);
        integer(value.code.size()); for (const auto item : value.code) integer(item);
    }
};
Bytes bytes(const Function& function, bool little = true) {
    Writer writer{{0x1b, 'L', 'u', 'a', 0x40, static_cast<std::uint8_t>(little), 4, 4, 4, 32, 6, 9, 8}, little};
    writer.number(314159265.3589793); writer.function(function); return writer.data;
}
Lua4Program program(const Function& function, bool little = true) { return read_lua4_chunk(bytes(function, little), "synthetic"); }
Lua4RunResult run(const Function& function, Lua4Limits limits = {}) {
    Lua4Vm vm(limits); const auto result = vm.execute(program(function));
    return result;
}
void numeric_result(const Lua4RunResult& result, const std::vector<double>& expected) {
    check(result.completed, "Execution failed: " + result.diagnostic);
    check(result.returned.size() == expected.size(), "Unexpected result count");
    for (std::size_t i = 0; i < expected.size(); ++i)
        check(result.returned[i].type() == Lua4Type::Number && result.returned[i].as_number() == expected[i], "Incorrect numeric result");
}
void stopped(const Lua4RunResult& result, std::string_view fragment) {
    check(!result.completed && result.diagnostic.find(fragment) != std::string::npos,
        "Expected execution stop containing " + std::string(fragment) + ", got " + result.diagnostic);
}

void reader_checks() {
    Function f; f.code = {s(Pushint, -17), u(Return), End};
    for (const bool little : {false, true}) {
        Lua4Vm vm; numeric_result(vm.execute(program(f, little)), {-17});
    }
    auto data = bytes(f);
    for (std::size_t length = 0; length < data.size(); ++length)
        rejects([&] { read_lua4_chunk(std::span<const std::uint8_t>(data).first(length)); }, "Lua 4");
    for (std::size_t at = 0; at < 13; ++at) {
        auto changed = data; changed[at] = 0xff;
        rejects([&] { read_lua4_chunk(changed); }, "header");
    }
    auto changed = data; changed[13] ^= 0x40; changed[20] ^= 0x40;
    rejects([&] { read_lua4_chunk(changed); }, "number");
    changed = data; changed.push_back(0);
    rejects([&] { read_lua4_chunk(changed); }, "trailing");
    changed = data; for (std::size_t at = 21; at < 25; ++at) changed[at] = 0xff;
    rejects([&] { read_lua4_chunk(changed); }, "string");
    changed = data; changed[39] = 'x';
    rejects([&] { read_lua4_chunk(changed); }, "terminator");
    Lua4ReadLimits limits; limits.max_bytes = data.size() - 1;
    rejects([&] { read_lua4_chunk(data, "limited", limits); }, "byte limit");
    limits = {}; limits.max_instructions = 2;
    rejects([&] { read_lua4_chunk(data, "limited", limits); }, "instruction");
    limits = {}; limits.max_string_bytes = 2;
    rejects([&] { read_lua4_chunk(data, "limited", limits); }, "strings");
    limits = {}; limits.max_depth = 1; Function nested; nested.children = {f};
    rejects([&] { read_lua4_chunk(bytes(nested), "limited", limits); }, "depth");
    limits = {}; limits.max_functions = 1;
    rejects([&] { read_lua4_chunk(bytes(nested), "limited", limits); }, "prototype");
    limits = {}; limits.max_bytes = 0;
    rejects([&] { read_lua4_chunk(data, "limited", limits); }, "limit");
    f.code = {63, End}; rejects([&] { program(f); }, "opcode");
    f.code = {u(Pushstring), End}; rejects([&] { program(f); }, "string index");
    f.code = {u(Pushnum), End}; rejects([&] { program(f); }, "number index");
    f.code = {ab(Call, 0, 511), End}; rejects([&] { program(f); }, "fixed results");
    f.code = {u(Getlocal, 16), End}; rejects([&] { program(f); }, "local index");
    f.code = {s(Jmp, -2), End}; rejects([&] { program(f); }, "jump target");
    f.code = {s(Jmp, 2), End}; rejects([&] { program(f); }, "jump target");
    f.code = {Pushniljmp, End}; rejects([&] { program(f); }, "PUSHNILJMP");
    f.code = {Pushnil, End}; rejects([&] { program(f); }, "stack operand");
    f.code = {u(Concat, 1), End}; rejects([&] { program(f); }, "stack operand");
    f.code = {u(Closure), End}; rejects([&] { program(f); }, "closure");
    f.code = {u(Pushupvalue), End}; rejects([&] { program(f); }, "top-level");
    nested.children = {f}; nested.code = {ab(Closure, 0, 0), End};
    rejects([&] { program(nested); }, "capture count");
    f.code = {s(Pushint, 1)}; rejects([&] { program(f); }, "END");
    f.code = {End}; f.numbers = {std::numeric_limits<double>::infinity()};
    rejects([&] { program(f); }, "non-finite");
    f = {}; f.stack = 251; rejects([&] { program(f); }, "layout");
    f = {}; f.parameters = 17; rejects([&] { program(f); }, "layout");
    f = {}; f.parameters = 16; f.vararg = true; rejects([&] { program(f); }, "layout");
    f = {}; f.code = {s(Pushint, 42), Return, End};
    data = bytes(f); const auto owned = read_lua4_chunk(data); std::fill(data.begin(), data.end(), std::uint8_t{0});
    Lua4Vm vm; numeric_result(vm.execute(owned), {42});
    check(owned.info().functions == 1 && owned.info().instructions == 3, "Program metadata changed");
}

void values_and_arithmetic() {
    check(!Lua4Value{}.truthy() && Lua4Value::number(0).truthy() && Lua4Value::string("").truthy(), "Lua 4 truthiness changed");
    check(Lua4Value::number(1.234567890123456).describe() == "1.234567890123456", "Lua 4 number formatting lost its 16-digit precision");
    rejects([] { Lua4Value::number(std::numeric_limits<double>::quiet_NaN()); }, "non-finite");
    rejects([] { Lua4Value::string(std::string(1024u * 1024u + 1, 'x')); }, "1 MiB");
    rejects([] { Lua4Value{}.as_number(); }, "not a number");
    rejects([] { Lua4Value::number(1).as_string(); }, "not a string");
    Function f;
    f.numbers = {2.5}; f.code = {u(Pushnum), u(Pushnegnum), s(Pushint, 0), Return, End};
    numeric_result(run(f), {2.5, -2.5, 0});
    f.code = {s(Pushint, 2), s(Pushint, 3), Add, s(Addi, 4), s(Pushint, 2), Mult,
        s(Pushint, 3), Sub, s(Pushint, 5), Div, Minus, Return, End};
    numeric_result(run(f), {-3});
    f.strings = {" +2.5 ", "tail", std::string("a\0b", 3)};
    f.code = {u(Pushstring), s(Pushint, 2), Mult, Return, End}; numeric_result(run(f), {5});
    f.strings[0] = "+-1"; stopped(run(f), "numeric string sign"); f.strings[0] = " +2.5 ";
    f.code = {s(Pushint, 12), u(Pushstring, 1), u(Pushstring, 2), u(Concat, 3), Return, End};
    const auto joined = run(f);
    check(joined.completed && joined.returned[0].as_string() == std::string("12taila\0b", 9), "CONCAT lost order, number conversion or embedded NUL");
    f.code = {u(Pushnil, 1), Not, s(Pushint, 0), Not, u(Pushstring, 1), Not, Return, End};
    const auto inverted = run(f);
    check(inverted.completed && inverted.returned.size() == 3 && inverted.returned[0].as_number() == 1 &&
        !inverted.returned[1].truthy() && !inverted.returned[2].truthy(), "NOT used modern boolean semantics");
    f.code = {s(Pushint, 1), s(Pushint, 0), Div, Return, End}; stopped(run(f), "non-finite");
    f.code = {s(Pushint, 2), s(Pushint, 3), Pow, Return, End}; stopped(run(f), "tag method");
    f.code = {u(Pushnil, 1), Minus, Return, End}; stopped(run(f), "arithmetic");
    f.code = {u(Pushnil, 1), u(Pushstring, 1), u(Concat, 2), Return, End}; stopped(run(f), "concatenation");
    f.code = {Add, End}; stopped(run(f), "underflow");
    f.stack = 1; f.code = {u(Pushnil, 2), End}; stopped(run(f), "stack");
    f = {}; f.code = {u(Return, 1), End}; stopped(run(f), "return");
    f.code = {u(Getlocal), End}; stopped(run(f), "active stack");
    f.code = {s(Pushint, 1), u(Setlocal), End}; stopped(run(f), "assignment");
}

void control_flow() {
    for (const auto op : {Jmpne, Jmpeq, Jmplt, Jmple, Jmpgt, Jmpge}) {
        for (const int right : {1, 2, 3}) {
            Function f; f.code = {s(Pushint, 2), s(Pushint, right), s(op, 1), Pushniljmp, s(Pushint, 1), Return, End};
            const bool expected = op == Jmpne ? 2 != right : op == Jmpeq ? 2 == right : op == Jmplt ? 2 < right :
                op == Jmple ? 2 <= right : op == Jmpgt ? 2 > right : 2 >= right;
            const auto result = run(f);
            check(result.completed && result.returned.size() == 1 && result.returned[0].truthy() == expected, "Conditional comparison branch changed");
        }
    }
    for (const auto op : {Jmpt, Jmpf}) {
        for (const bool truthy : {false, true}) {
            Function f; f.code = {truthy ? s(Pushint, 0) : u(Pushnil, 1), s(op, 1), Pushniljmp, s(Pushint, 1), Return, End};
            const auto result = run(f);
            check(result.completed && result.returned[0].truthy() == (truthy == (op == Jmpt)), "Truth branch pop/condition changed");
        }
    }
    for (const auto op : {Jmpont, Jmponf}) {
        for (const bool truthy : {false, true}) {
            Function f; f.code = {truthy ? s(Pushint, 0) : u(Pushnil, 1), s(op, 1), s(Pushint, 7), Return, End};
            const auto result = run(f); const bool retained = truthy == (op == Jmpont);
            check(result.completed && result.returned.size() == 1, "Short-circuit branch changed stack size");
            check(retained && !truthy ? !result.returned[0].truthy() : result.returned[0].as_number() == (retained ? 0 : 7),
                "Short-circuit branch retained/popped the wrong value");
        }
    }
    for (const auto spec : {std::array<int, 4>{1, 3, 1, 6}, {3, 1, -1, 6}, {3, 1, 1, 0}, {1, 3, -1, 0}}) {
        Function f; f.code = {s(Pushint, 0), s(Pushint, spec[0]), s(Pushint, spec[1]), s(Pushint, spec[2]),
            s(Forprep, 5), u(Getlocal, 0), u(Getlocal, 1), Add, u(Setlocal, 0), s(Forloop, -5), Return, End};
        numeric_result(run(f), {static_cast<double>(spec[3])});
    }
    Function f; f.code = {s(Pushint, 0), s(Pushint, 0), s(Pushint, 0), s(Forprep, 1), s(Forloop, -1), End};
    Lua4Limits limits; limits.max_instructions = 20;
    const auto infinite = run(f, limits); stopped(infinite, "instruction budget");
    check(infinite.instructions == 20, "Zero-step loop exceeded the exact instruction budget");
    f.code = {s(Jmp, -1), End}; stopped(run(f, limits), "instruction budget");
    f.strings = {"2"}; f.code = {s(Pushint, 1), s(Pushint, 3), s(Pushint, 1), s(Forprep, 3),
        u(Pushstring), u(Setlocal, 0), s(Forloop, -3), End};
    stopped(run(f), "not a number");
    f.code = {u(Createtable), s(Lforprep, 0), End}; stopped(run(f), "hash traversal");
    f.strings = {"b", "a"}; f.code = {u(Pushstring), u(Pushstring, 1), s(Jmpgt, 1), Pushniljmp, s(Pushint, 1), Return, End};
    numeric_result(run(f), {1});
    f.code = {u(Pushstring), s(Pushint, 1), s(Jmplt, 1), Pushniljmp, s(Pushint, 1), Return, End};
    stopped(run(f), "comparison tag");
}

void calls_closures_and_tables() {
    Function captured; captured.stack = 1; captured.code = {u(Pushupvalue), Return, End};
    Function definitions; definitions.strings = {"saved"}; definitions.children = {captured};
    definitions.code = {s(Pushint, 17), ab(Closure, 0, 1), u(Setglobal), End};
    Lua4Vm vm; check(vm.execute(program(definitions)).completed, "Closure definition failed");
    check(vm.get_global("saved").type() == Lua4Type::Function, "Persistent closure global was lost");
    Function invoke; invoke.strings = {"saved"}; invoke.code = {u(Getglobal), ab(Call, 0, 1), Return, End};
    numeric_result(vm.execute(program(invoke)), {17});
    Function child; child.parameters = 2; child.code = {Return, End};
    Function main; main.children = {child}; main.code = {ab(Closure, 0, 0), s(Pushint, 9), ab(Call, 0, 255), Return, End};
    const auto missing = run(main);
    check(missing.completed && missing.returned.size() == 2 && missing.returned[0].as_number() == 9 &&
        !missing.returned[1].truthy(), "Missing function arguments were not padded with nil");
    main.code = {ab(Closure, 0, 0), s(Pushint, 9), s(Pushint, 8), s(Pushint, 7), ab(Call, 0, 255), Return, End};
    numeric_result(run(main), {9, 8});
    main.children[0].parameters = 1; main.children[0].vararg = true;
    Lua4Vm packed_vm; const auto packed = packed_vm.execute(program(main));
    check(packed.completed && packed.returned.size() == 2 && packed.returned[0].as_number() == 9, "Vararg function fixed parameter changed");
    const auto args = packed.returned[1];
    check(packed_vm.table_get(args, Lua4Value::string("n")).as_number() == 2 &&
        packed_vm.table_get(args, Lua4Value::number(1)).as_number() == 8 &&
        packed_vm.table_get(args, Lua4Value::number(2)).as_number() == 7, "Lua 4 arg table indexing/count changed");
    rejects([&] { vm.table_get(args, Lua4Value::number(1)); }, "another VM");
    rejects([&] { vm.set_global("foreign", args); }, "another VM");
    check(!packed_vm.table_get(args, Lua4Value{}).truthy(), "Nil lookup should produce nil");
    main.code = {ab(Closure, 0, 0), ab(Call, 0, 255), Return, End};
    Lua4Vm noargs_vm; const auto noargs = noargs_vm.execute(program(main));
    check(noargs.completed && !noargs.returned[0].truthy() &&
        noargs_vm.table_get(noargs.returned[1], Lua4Value::string("n")).as_number() == 0, "Empty varargs did not retain a zero n field");

    Function table; table.strings = {"t", "key"};
    table.code = {u(Createtable), u(Setglobal), u(Getglobal), u(Pushstring, 1), s(Pushint, 23), ab(Settable, 3, 3),
        u(Getglobal), u(Getdotted, 1), Return, End};
    numeric_result(run(table), {23});
    table.code = {u(Createtable), u(Pushstring, 1), s(Pushint, 1), u(Pushstring, 1), s(Pushint, 2), u(Setmap, 2),
        u(Getdotted, 1), Return, End};
    numeric_result(run(table), {1}); // Original SETMAP stores stack pairs in reverse order.
    table.code = {u(Createtable), s(Pushint, 11), s(Pushint, 12), ab(Setlist, 1, 2), s(Pushint, 63), Gettable, Return, End};
    numeric_result(run(table), {11});
    table.code = {s(Pushint, 1), u(Createtable), s(Pushint, 31), ab(Setlist, 0, 1), u(Getindexed, 0), u(Return, 1), End};
    numeric_result(run(table), {31});
    table.code = {u(Createtable), u(Pushnil, 1), s(Pushint, 1), ab(Settable, 3, 3), End}; stopped(run(table), "nil table key");
    table.code = {s(Pushint, 1), u(Pushstring, 1), Gettable, End}; stopped(run(table), "needs a table");
    table.code = {u(Createtable), u(Setglobal), u(Getglobal), u(Pushstring, 1), u(Getglobal), ab(Settable, 3, 3), u(Getglobal), Return, End};
    Lua4Vm cycle_vm; const auto cycle = cycle_vm.execute(program(table));
    check(cycle.completed && cycle_vm.table_get(cycle.returned[0], Lua4Value::string("key")).type() == Lua4Type::Table,
        "Self-referencing table lost reference identity"); // Arena owns cycles, not shared_ptr chains.
}

void host_and_limits() {
    Function f; f.strings = {"be", "method"}; f.code = {u(Getglobal), u(Pushself, 1), s(Pushint, 5), ab(Call, 0, 3), Return, End};
    Lua4Vm vm; vm.set_global("be", Lua4Value::host_object("be"));
    vm.set_host_handler([](const Lua4HostCall& call) {
        check(call.name == "be.method" && call.arguments.size() == 2 && call.arguments[0].host_name() == "be" &&
            call.arguments[1].as_number() == 5 && call.site.prototype == "main" && call.site.pc == 3, "Colon call receiver, arguments or site changed");
        return std::vector<Lua4Value>{Lua4Value::number(9), Lua4Value::number(8)};
    });
    const auto returned = vm.execute(program(f));
    check(returned.completed && returned.returned.size() == 3 && returned.returned[0].as_number() == 9 &&
        returned.returned[1].as_number() == 8 && !returned.returned[2].truthy() && returned.host_calls == 1, "Host result count was not padded");
    f.code[3] = ab(Call, 0, 1);
    numeric_result(vm.execute(program(f)), {9});
    Lua4Vm unbound; unbound.set_global("be", Lua4Value::host_object("be"));
    const auto stop = unbound.execute(program(f)); stopped(stop, "unbound native host function be.method");
    check(stop.site.pc == 3 && stop.instructions == 4 && stop.host_calls == 1, "Unbound host call stop metadata changed");
    stopped(unbound.execute(program(f)), "fresh VM");
    rejects([&] { unbound.set_host_handler({}); }, "stopped VM");
    Lua4Vm throwing; throwing.set_global("be", Lua4Value::host_object("be"));
    throwing.set_host_handler([](const Lua4HostCall&) -> std::vector<Lua4Value> { throw std::runtime_error("unported command"); });
    stopped(throwing.execute(program(f)), "unported command");
    Lua4Vm tail; tail.set_global("f", Lua4Value::host_function("f"));
    tail.set_host_handler([](const Lua4HostCall&) { return std::vector<Lua4Value>{Lua4Value::number(10), Lua4Value::number(20)}; });
    f.strings = {"f"}; f.code = {s(Pushint, 99), u(Getglobal), ab(Tailcall, 1, 1), End};
    numeric_result(tail.execute(program(f)), {10, 20});
    f.code = {s(Pushint, 99), u(Getglobal), ab(Tailcall, 1, 0), End};
    numeric_result(tail.execute(program(f)), {99, 10, 20});
    f.code = {u(Getglobal), ab(Call, 0, 255), Return, End};
    numeric_result(tail.execute(program(f)), {10, 20});

    Function recurse; recurse.strings = {"f"}; recurse.code = {u(Getglobal), ab(Call, 0, 0), End};
    f.children = {recurse}; f.code = {ab(Closure, 0, 0), u(Setglobal), u(Getglobal), ab(Call, 0, 0), End};
    Lua4Limits limits; limits.max_call_depth = 4; stopped(run(f, limits), "call depth");
    limits = {}; limits.max_stack_values = 31; stopped(run(f, limits), "aggregate stack");
    f = {}; f.code = {u(Createtable), Pop | (1u << 6), s(Jmp, -3), End};
    limits = {}; limits.max_objects = 3; stopped(run(f, limits), "object count");
    f = {}; f.strings = {"a", "b"}; f.code = {s(Pushint, 1), u(Setglobal), s(Pushint, 2), u(Setglobal, 1), End};
    limits = {}; limits.max_globals = 1; stopped(run(f, limits), "global count");
    Lua4Vm partial(limits); stopped(partial.execute(program(f)), "global count");
    check(partial.get_global("a").as_number() == 1 && !partial.get_global("b").truthy(), "Failure rolled back or invented prior global state");
    f = {}; f.code = {u(Createtable), s(Pushint, 1), s(Pushint, 2), ab(Setlist, 0, 2), End};
    limits = {}; limits.max_table_entries = 1; stopped(run(f, limits), "table entry");
    limits = {}; limits.max_heap_bytes = 1; stopped(run(Function{}, limits), "heap allocation");
    f = {}; f.strings = {std::string(4096, 'x')}; f.code = {u(Pushstring), u(Pushstring), s(Jmpeq, -3), End};
    limits = {}; limits.max_heap_bytes = 32768; stopped(run(f, limits), "heap allocation");
    limits = {}; limits.max_host_calls = 1; Lua4Vm host_limited(limits);
    host_limited.set_global("f", Lua4Value::host_function("f"));
    host_limited.set_host_handler([](const Lua4HostCall&) { return std::vector<Lua4Value>{}; });
    f.strings = {"f"}; f.code = {u(Getglobal), ab(Call, 0, 0), s(Jmp, -3), End};
    stopped(host_limited.execute(program(f)), "host call budget");
    limits.max_call_depth = 0; rejects([&] { Lua4Vm bad(limits); }, "limit");
}

Bytes file_bytes(const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot read optional original Lua chunk");
    file.seekg(0, std::ios::end); const auto size = file.tellg();
    if (size < 0 || size > 4 * 1024 * 1024) throw std::runtime_error("Original chunk size exceeds test limit");
    file.seekg(0); return {std::istreambuf_iterator<char>(file), {}};
}
void original(const char* func_path, const char* boot_path) {
    const auto func = read_lua4_chunk(file_bytes(func_path), "func.lua");
    const auto boot = read_lua4_chunk(file_bytes(boot_path), "boot.lua");
    check(func.info().functions == 18 && func.info().instructions == 2516, "Original func prototype/code census changed");
    check(boot.info().functions == 8 && boot.info().instructions == 761, "Original boot prototype/code census changed");
    Lua4Vm vm; vm.set_global("be", Lua4Value::host_object("be"));
    const auto defined = vm.execute(func);
    check(defined.completed && defined.instructions == 35 && !defined.host_calls, "Original func top-level did not define helpers without native calls");
    for (const auto* name : {"_ERRORMESSAGE", "funcPlayerSet", "funcStageIn", "funcStartLayerSet", "funcFirstLayerSet"})
        check(vm.get_global(name).type() == Lua4Type::Function, "Original helper did not persist across chunks");
    const auto stopped_boot = vm.execute(boot);
    stopped(stopped_boot, "unbound native host function be.CFLclear");
    check(stopped_boot.site.source == "boot.lua" && stopped_boot.site.prototype == "main" && stopped_boot.site.pc == 9 &&
        stopped_boot.instructions == 10 && stopped_boot.host_calls == 1, "Original first unsupported host boundary changed");
}
} // namespace
int main(int argc, char** argv) {
    try {
        reader_checks(); values_and_arithmetic(); control_flow(); calls_closures_and_tables(); host_and_limits();
        if (argc == 3) original(argv[1], argv[2]);
        else if (argc != 1) throw std::runtime_error("Usage: dw4_lua4_tests [func-chunk boot-chunk]");
        std::cout << "Lua 4 tests passed: " << checks << " checks\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "Lua 4 test failure: " << error.what() << '\n'; return 1; }
}
