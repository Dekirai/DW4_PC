#include "dw4/lua4.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <type_traits>

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
    f = {}; f.code = {u(Createtable), u(Createtable), u(Createtable), u(Createtable), End};
    limits = {}; limits.max_objects = 3; stopped(run(f, limits), "object count"); // Four live tables.
    f = {}; f.code = {u(Createtable), Pop | (1u << 6), s(Jmp, -3), End};
    limits.max_instructions = 3000; stopped(run(f, limits), "instruction budget"); // Garbage is collected.
    f = {}; f.strings = {"a", "b"}; f.code = {s(Pushint, 1), u(Setglobal), s(Pushint, 2), u(Setglobal, 1), End};
    limits = {}; limits.max_globals = 1; stopped(run(f, limits), "global count");
    Lua4Vm partial(limits); stopped(partial.execute(program(f)), "global count");
    check(partial.get_global("a").as_number() == 1 && !partial.get_global("b").truthy(), "Failure rolled back or invented prior global state");
    f = {}; f.code = {u(Createtable), s(Pushint, 1), s(Pushint, 2), ab(Setlist, 0, 2), End};
    limits = {}; limits.max_table_entries = 1; stopped(run(f, limits), "table entry");
    limits = {}; limits.max_heap_bytes = 1; stopped(run(Function{}, limits), "not enough memory");
    f = {}; f.strings = {std::string(4096, 'x')}; f.code = {u(Pushstring), u(Pushstring), s(Jmpeq, -3), End};
    limits = {}; limits.max_string_work = 32768; stopped(run(f, limits), "string work budget");
    limits = {}; limits.max_host_calls = 1; Lua4Vm host_limited(limits);
    host_limited.set_global("f", Lua4Value::host_function("f"));
    host_limited.set_host_handler([](const Lua4HostCall&) { return std::vector<Lua4Value>{}; });
    f.strings = {"f"}; f.code = {u(Getglobal), ab(Call, 0, 0), s(Jmp, -3), End};
    stopped(host_limited.execute(program(f)), "host call budget");
    limits.max_call_depth = 0; rejects([&] { Lua4Vm bad(limits); }, "limit");
}

void resumed_frames_and_results() {
    static_assert(!std::is_copy_constructible_v<Lua4Execution> && std::is_nothrow_move_constructible_v<Lua4Execution>);
    Function child;child.parameters=2;child.vararg=true;child.strings={"pause"};
    child.code={u(Pushupvalue),u(Getglobal),u(Getlocal),u(Getlocal,1),u(Getlocal,2),ab(Call,4,3),u(Return,3),End};
    Function root;root.strings={"counter","prefix"};root.children={child};
    root.code={u(Getglobal),s(Addi,1),u(Setglobal),u(Getglobal,1),ab(Call,0,0),s(Pushint,7),ab(Closure,0,1),s(Pushint,11),ab(Call,0,255),
        u(Getglobal),s(Addi,1),u(Setglobal),Return,End};
    Lua4Vm vm;vm.set_global("counter",Lua4Value::number(0));
    vm.set_global("prefix",Lua4Value::host_function("prefix"));vm.set_global("pause",Lua4Value::host_function("pause"));
    unsigned prefix_effects=0,attempts=0;bool ready=false;const Lua4Value* arguments=nullptr;
    std::uint64_t execution_id=0,call_id=0;
    vm.set_resumable_host_handler([&](const Lua4HostCall& call) {
        if(call.name=="prefix") {++prefix_effects;return Lua4HostResult{true,{},{}};}
        ++attempts;
        check(call.name=="pause" && call.arguments.size()==3 && call.arguments[0].as_number()==11 && !call.arguments[1].truthy() &&
            vm.table_get(call.arguments[2],Lua4Value::string("n")).as_number()==0,"Retained Lua parameters/vararg table changed");
        check(call.site.source=="synthetic" && call.site.prototype=="main/0" && call.site.pc==5,"Retained host call lost its source site");
        if(!arguments) {arguments=call.arguments.data();execution_id=call.execution_id;call_id=call.call_id;}
        check(call.arguments.data()==arguments && call.execution_id==execution_id && call.call_id==call_id && call_id!=0,
            "Host retries did not retain stable argument/call ownership");
        if(!ready)return Lua4HostResult{false,{},"Awaiting actual result"};
        return Lua4HostResult{true,{Lua4Value::number(4),Lua4Value::number(5)}, {}};
    });
    auto owned=program(root);auto execution=vm.begin_execution(owned);owned={};
    check(execution.id()!=0 && !execution.completed(),"New invocation has no stable identity or ran during begin");
    const auto stopped=vm.resume(execution);
    check(stopped.stop==Lua4RunStop::host_required && !stopped.completed && stopped.host_calls==2 && prefix_effects==1 &&
        vm.get_global("counter").as_number()==1,"Explicit incomplete host result replayed or rolled back preceding effects");
    for(int i=0;i<3;++i) {
        const auto again=vm.resume(execution);
        check(again.stop==Lua4RunStop::host_required && again.instructions==stopped.instructions && again.host_calls==stopped.host_calls &&
            prefix_effects==1 && vm.get_global("counter").as_number()==1,"Retry recharged a logical call or replayed instructions");
    }
    rejects([&]{vm.set_global("counter",Lua4Value::number(99));},"suspended VM");
    rejects([&]{vm.set_host_handler({});},"suspended VM");
    rejects([&]{vm.set_resumable_host_handler({});},"suspended VM");
    rejects([&]{vm.begin_execution(program(Function{}));},"another root");
    check(vm.execute(program(Function{})).stop==Lua4RunStop::faulted,"Synchronous execute bypassed a pending invocation");
    ready=true;const auto completed=vm.resume(execution);
    check(completed.completed && completed.stop==Lua4RunStop::completed && execution.completed() && completed.returned.size()==4 &&
        completed.returned[0].as_number()==7 && completed.returned[1].as_number()==4 && completed.returned[2].as_number()==5 &&
        !completed.returned[3].truthy() && prefix_effects==1 && attempts==5 && vm.get_global("counter").as_number()==2,
        "Resuming Lua closure/capture/CALL did not apply fixed return padding exactly once");
    const auto cached=vm.resume(execution);
    check(cached.instructions==completed.instructions && cached.host_calls==2 && attempts==5,"Completed execution replayed its host call");
    check(vm.execute(program(Function{})).completed,"Completed retained root prevented a later synchronous chunk");
}
void resumed_tailcalls() {
    for(const unsigned boundary:{0u,1u})for(const unsigned result_count:{0u,1u,3u}) {
        Function child;child.strings={"pause"};child.code={s(Pushint,111),u(Getglobal),s(Pushint,6),ab(Tailcall,1,boundary),End};
        Function root;root.children={child};root.code={ab(Closure,0,0),ab(Call,0,255),Return,End};
        Lua4Vm vm;vm.set_global("pause",Lua4Value::host_function("pause"));bool ready=false;unsigned calls=0;
        vm.set_resumable_host_handler([&](const Lua4HostCall& call) {
            ++calls;check(call.arguments.size()==1 && call.arguments[0].as_number()==6,"TAILCALL lost its arguments while paused");
            if(!ready)return Lua4HostResult{};
            std::vector<Lua4Value> returned;for(unsigned i=0;i<result_count;++i)returned.push_back(Lua4Value::number(20+i));
            return Lua4HostResult{true,std::move(returned),{}};
        });
        auto execution=vm.begin_execution(program(root));check(vm.resume(execution).stop==Lua4RunStop::host_required,"TAILCALL did not suspend");
        ready=true;const auto result=vm.resume(execution);std::vector<double> expected;
        if(boundary==0)expected.push_back(111);for(unsigned i=0;i<result_count;++i)expected.push_back(20+i);
        numeric_result(result,expected);check(result.host_calls==1 && calls==2,"TAILCALL retried a logical call or lost its result boundary");
    }
}
Function increment_and_call(std::int32_t before,std::int32_t after,const char* function) {
    Function result;result.strings={"counter",function};
    result.code={u(Getglobal),s(Addi,before),u(Setglobal),u(Getglobal,1),ab(Call,0,1),u(Getglobal),s(Addi,after),u(Setglobal),Return,End};
    return result;
}
void nested_owned_invocations() {
    Lua4Vm vm;vm.set_global("counter",Lua4Value::number(0));
    vm.set_global("outer",Lua4Value::host_function("outer"));vm.set_global("inner",Lua4Value::host_function("inner"));
    auto child_program=program(increment_and_call(10,100,"inner"));
    Function sibling;sibling.strings={"counter"};sibling.code={u(Getglobal),s(Addi,1000),u(Setglobal),u(Getglobal),Return,End};
    auto sibling_program=program(sibling);std::optional<Lua4Execution> child,next;
    std::uint64_t outer_id=0,inner_id=0,outer_call=0,inner_call=0;unsigned outer_attempts=0,inner_attempts=0,sibling_runs=0;
    bool ready=false;
    vm.set_resumable_host_handler([&](const Lua4HostCall& call)->Lua4HostResult {
        if(call.name=="inner") {
            ++inner_attempts;
            if(!inner_id){inner_id=call.execution_id;inner_call=call.call_id;}
            check(call.execution_id==inner_id && call.call_id==inner_call && inner_id!=outer_id && inner_call!=outer_call,
                "Nested host invocation lost its distinct stable call identity");
            return ready ? Lua4HostResult{true,{Lua4Value::number(8)},{}} : Lua4HostResult{false,{},"Nested service unavailable"};
        }
        ++outer_attempts;
        if(!outer_id){outer_id=call.execution_id;outer_call=call.call_id;}
        check(call.execution_id==outer_id && call.call_id==outer_call,"Parent callback identity changed during child retry");
        if(!child)child.emplace(vm.begin_nested(child_program));
        const auto result=vm.resume(*child);
        if(!result.completed) {
            rejects([&]{vm.begin_nested(sibling_program);},"existing nested invocation");
            return {false,{},result.diagnostic};
        }
        if(!next){next.emplace(vm.begin_nested(sibling_program));++sibling_runs;}
        const auto later=vm.resume(*next);check(later.completed,"Sequential child did not complete in the same shared VM");
        return {true,later.returned,{}};
    });
    auto parent=vm.begin_execution(program(increment_and_call(1,1,"outer")));
    const auto first=vm.resume(parent);
    check(first.stop==Lua4RunStop::host_required && vm.get_global("counter").as_number()==11 && first.host_calls==2,
        "Nested invocation did not share the parent VM globals and call budget");
    rejects([&]{vm.resume(*child);},"original owning host callback");
    rejects([&]{vm.begin_nested(sibling_program);},"currently invoked");
    const auto second=vm.resume(parent);
    check(second.instructions==first.instructions && second.host_calls==first.host_calls && vm.get_global("counter").as_number()==11,
        "Nested pause reconstructed the child or reset its root invocation budget");
    ready=true;const auto done=vm.resume(parent);numeric_result(done,{1111});
    check(vm.get_global("counter").as_number()==1112 && outer_attempts==3 && inner_attempts==3 && sibling_runs==1 && done.host_calls==2 &&
        child->completed() && next->completed(),"Nested or sibling completion replayed a script prefix or failed to resume its parent");
}
void continuation_ownership_and_faults() {
    Function call;call.strings={"host"};call.code={u(Getglobal),ab(Call,0,1),Return,End};const auto chunk=program(call);
    Lua4Vm vm;vm.set_global("host",Lua4Value::host_function("host"));
    auto execution=vm.begin_execution(chunk);const auto missing=vm.resume(execution);
    check(missing.stop==Lua4RunStop::host_required && missing.instructions==2 && missing.host_calls==1,"Missing opt-in handler was terminal or skipped its call");
    vm.set_resumable_host_handler([](const Lua4HostCall&){return Lua4HostResult{true,{Lua4Value::number(9)},{}};});
    auto moved=std::move(vm);auto handle=std::move(execution);
    check(execution.id()==0 && !execution.completed(),"Moved-from continuation retained another handle's identity");
    rejects([&]{moved.resume(execution);},"another or expired VM");Lua4Vm other;
    rejects([&]{other.resume(handle);},"another or expired VM");
    numeric_result(moved.resume(handle),{9});
    rejects([&]{vm.resume(handle);},"another or expired VM");

    Lua4Vm strict;strict.set_global("host",Lua4Value::host_function("host"));
    strict.set_resumable_host_handler([](const Lua4HostCall&){return Lua4HostResult{};});
    const auto strict_result=strict.execute(chunk);stopped(strict_result,"unbound native host function");
    check(strict_result.stop==Lua4RunStop::faulted,"Synchronous execution silently opted into host suspension");

    for(const bool throws:{false,true}) {
        Lua4Vm fault;fault.set_global("host",Lua4Value::host_function("host"));unsigned calls=0;
        fault.set_resumable_host_handler([&](const Lua4HostCall&)->Lua4HostResult {
            ++calls;if(throws)throw std::runtime_error("Uncertain host effect");
            return {false,{Lua4Value::number(1)},"Invalid incomplete result"};
        });
        auto run=fault.begin_execution(chunk);const auto first=fault.resume(run);
        check(first.stop==Lua4RunStop::faulted && !first.completed && calls==1,"Host exception or malformed incomplete result did not fault the VM");
        check(fault.resume(run).stop==Lua4RunStop::faulted && calls==1,"Faulted invocation replayed an uncertain host effect");
        rejects([&]{fault.begin_execution(chunk);},"stopped");
    }
    Lua4Vm abandoned;abandoned.set_global("host",Lua4Value::host_function("host"));
    {auto run=abandoned.begin_execution(chunk);check(abandoned.resume(run).stop==Lua4RunStop::host_required,"Abandonment fixture did not pause");}
    rejects([&]{abandoned.begin_execution(chunk);},"stopped");
    std::optional<Lua4Execution> expired;
    {Lua4Vm temporary;expired.emplace(temporary.begin_execution(program(Function{})));}
    rejects([&]{other.resume(*expired);},"another or expired VM");expired.reset();
    Lua4Vm dropped;dropped.set_global("host",Lua4Value::host_function("host"));std::optional<Lua4Execution> running;
    Function after;after.strings={"host","after"};after.code={u(Getglobal),ab(Call,0,0),s(Pushint,99),u(Setglobal,1),End};
    dropped.set_resumable_host_handler([&](const Lua4HostCall&){running.reset();return Lua4HostResult{true,{},{}};});
    running.emplace(dropped.begin_execution(program(after)));const auto interrupted=dropped.resume(*running);
    check(interrupted.stop==Lua4RunStop::faulted && !dropped.get_global("after").truthy(),
        "Dropping the running handle let instructions commit after ownership was abandoned");
    rejects([&]{dropped.begin_execution(chunk);},"stopped");
}
void nested_faults_and_limits() {
    Function call;call.strings={"host"};call.code={u(Getglobal),ab(Call,0,0),End};const auto chunk=program(call);
    for(int mode=0;mode<4;++mode) {
        Lua4Limits limits;if(mode==2)limits.max_call_depth=1;if(mode==3)limits.max_stack_values=16;
        Lua4Vm vm(limits);vm.set_global("host",Lua4Value::host_function("host"));std::optional<Lua4Execution> child;
        vm.set_resumable_host_handler([&](const Lua4HostCall& call)->Lua4HostResult {
            if(child && call.execution_id==child->id())return {};
            child.emplace(vm.begin_nested(mode==0 || mode==1 ? chunk : program(Function{})));
            const auto result=vm.resume(*child);
            if(mode==1)child.reset(); // Dropping a paused child must fault the whole shared VM.
            if(mode>=2)check(result.stop==Lua4RunStop::faulted,"Nested entry evaded aggregate call/stack bounds");
            return {true,{},{}}; // Deliberately invalid completion or swallowed child fault.
        });
        auto root=vm.begin_execution(chunk);const auto result=vm.resume(root);
        if(mode>=2) { // A child's script error ends only the child, like a nested lua_dobuffer.
            check(result.completed && !vm.stopped(),"A nested script error faulted its parent or stopped the VM");
            continue;
        }
        check(result.stop==Lua4RunStop::faulted && !result.completed && vm.stopped(),"Pending/abandoned child was hidden by parent host completion");
        check(vm.resume(root).instructions==result.instructions,"Nested fault replayed the parent invocation");
    }
    Lua4Limits limits;limits.max_instructions=5;limits.max_host_calls=1;
    Lua4Vm vm(limits);vm.set_global("host",Lua4Value::host_function("host"));bool ready=false;unsigned attempts=0;
    vm.set_resumable_host_handler([&](const Lua4HostCall&){++attempts;return Lua4HostResult{ready,{},{}};});
    Function repeat;repeat.strings={"host"};repeat.code={u(Getglobal),ab(Call,0,0),s(Jmp,-3),End};
    auto run=vm.begin_execution(program(repeat));const auto first=vm.resume(run);
    for(int i=0;i<4;++i)check(vm.resume(run).instructions==first.instructions,"Retry consumed an instruction budget without execution");
    ready=true;const auto limited=vm.resume(run);
    check(limited.stop==Lua4RunStop::faulted && limited.instructions==5 && limited.host_calls==1 && attempts==6 &&
        limited.diagnostic.find("host call budget")!=std::string::npos,"A resumed invocation obtained another logical host-call budget");
    Lua4Vm recursive;recursive.set_global("host",Lua4Value::host_function("host"));std::optional<Lua4Execution> outer;
    recursive.set_resumable_host_handler([&](const Lua4HostCall&) {
        rejects([&]{recursive.resume(*outer);},"recursively resume");return Lua4HostResult{true,{},{}};
    });
    outer.emplace(recursive.begin_execution(chunk));check(recursive.resume(*outer).completed,"Rejected recursive misuse damaged an otherwise complete invocation");
}

// Mark & sweep (Lua 4.0.1 luaC_collectgarbage): a hit script runs on every
// hit for a whole stage on one VM. With the old cumulative allowance story
// run c9 lost hit_p_girumon.lua runs after ~140000 frames in stag0304.
void garbage_collection() {
    Function captured; captured.stack = 1; captured.code = {u(Pushupvalue), Return, End};
    Function hit; hit.strings = {"be", "MDLgetBulletKind", "I<", ">", "tmp", "MDLsetSD", "fn"}; hit.children = {captured};
    hit.code = {u(Getglobal), u(Pushself, 1), ab(Call, 0, 1), u(Pushstring, 2), u(Getlocal), u(Pushstring, 3), u(Concat, 3),
        u(Getglobal), u(Pushself, 5), u(Getlocal, 1), ab(Call, 2, 0), u(Createtable), u(Setglobal, 4),
        u(Getlocal, 1), ab(Closure, 0, 1), u(Setglobal, 6), End};
    Function keep; keep.strings = {"keep", "value"};
    keep.code = {u(Createtable), u(Pushstring, 1), s(Pushint, 42), u(Setmap, 1), u(Setglobal), End};
    Function churn; churn.strings = {"keep", "n"}; // n = n + 1; keep[n] = 1; keep[n] = nil (a new dead key per run)
    churn.code = {u(Getglobal, 1), s(Addi, 1), u(Setglobal, 1), u(Getglobal), u(Getglobal, 1), s(Pushint, 1), ab(Settable, 3, 3),
        u(Getglobal), u(Getglobal, 1), u(Pushnil, 1), ab(Settable, 3, 3), End};
    Function cycle; cycle.strings = {"self"}; // t.self = t, then dropped
    cycle.code = {u(Createtable), u(Getlocal), u(Pushstring), u(Getlocal), ab(Settable, 3, 3), Pop | (1u << 6), End};
    Function invoke; invoke.strings = {"fn"}; invoke.code = {u(Getglobal), ab(Call, 0, 1), Return, End};
    const auto hit_program = program(hit), churn_program = program(churn), cycle_program = program(cycle);
    auto session = [&](Lua4Vm& vm, unsigned runs) {
        vm.set_global("be", Lua4Value::host_object("be")); vm.set_global("n", Lua4Value::number(0));
        unsigned messages = 0;
        vm.set_host_handler([&](const Lua4HostCall& call) {
            if (call.name == "be.MDLgetBulletKind") return std::vector<Lua4Value>{Lua4Value::number(70126)};
            if (call.name != "be.MDLsetSD" || call.arguments.size() != 2 || call.arguments[1].as_string() != "I<70126>")
                throw std::runtime_error("Hit script fixture call changed");
            ++messages; return std::vector<Lua4Value>{};
        });
        check(vm.execute(program(keep)).completed, "Persistent table setup failed");
        for (unsigned i = 0; i < runs; ++i) {
            for (const auto* chunk : {&hit_program, &churn_program, &cycle_program}) {
                const auto result = vm.execute(*chunk);
                if (!result.completed) throw std::runtime_error("Repeated script run stopped: " + result.diagnostic);
            }
        }
        check(messages == runs, "Hit script host calls were lost");
    };
    // Far below the old 32 MiB allowance: only live data counts.
    Lua4Limits small; small.max_heap_bytes = 96u * 1024u; small.max_objects = 64; small.max_table_entries = 64;
    Lua4Vm vm(small); session(vm, 20000);
    const auto stats = vm.heap_stats();
    check(stats.collections > 100 && stats.live_bytes <= small.max_heap_bytes && stats.objects <= small.max_objects &&
        stats.table_entries <= small.max_table_entries, "Collector did not keep the live heap bounded");
    const auto kept = vm.get_global("keep");
    check(vm.table_get(kept, Lua4Value::string("value")).as_number() == 42 &&
        !vm.table_get(kept, Lua4Value::number(20000)).truthy(), "A table kept in a global lost its contents");
    const auto latest = vm.execute(program(invoke));
    check(latest.completed && latest.returned.size() == 1 && latest.returned[0].as_string() == "I<70126>",
        "The closure kept in a global lost its upvalue");
    vm.collect_garbage();
    const auto after = vm.heap_stats();
    check(after.collections > stats.collections && after.table_entries == 1 && after.objects == 3 &&
        after.threshold == 2 * after.live_bytes, "Explicit collection kept garbage or dead keys"); // keep, tmp, fn
    // Two VMs running the same scripts collect identically (no address order).
    Lua4Vm twin(small); session(twin, 20000); twin.execute(program(invoke)); twin.collect_garbage();
    const auto same = twin.heap_stats();
    check(same.live_bytes == after.live_bytes && same.threshold == after.threshold && same.collections == after.collections &&
        same.objects == after.objects && same.table_entries == after.table_entries, "Collection is not deterministic");
    // A value only the host still holds is collected; its handle never aliases a newer object.
    Function make; make.code = {u(Createtable), Return, End};
    const auto loose = vm.execute(program(make)); check(loose.completed, "Loose table chunk failed");
    vm.collect_garbage(); rejects([&] { vm.table_get(loose.returned[0], Lua4Value::number(1)); }, "collected");
    check(vm.execute(program(make)).completed, "Reused arena slot failed");
    rejects([&] { vm.table_get(loose.returned[0], Lua4Value::number(1)); }, "collected");
    // Live data beyond the heap: out of memory, as the original allocator would fail.
    Function grow; grow.strings = {"list", "n"}; // list[n] = {} forever
    grow.code = {u(Getglobal, 1), s(Addi, 1), u(Setglobal, 1), u(Getglobal), u(Getglobal, 1), u(Createtable), ab(Settable, 3, 3),
        s(Jmp, -8), End};
    Function setup; setup.strings = {"list", "n"}; setup.code = {u(Createtable), u(Setglobal), s(Pushint, 0), u(Setglobal, 1), End};
    Lua4Limits tight; tight.max_heap_bytes = 64u * 1024u; Lua4Vm full(tight);
    check(full.execute(program(setup)).completed, "Growth setup failed");
    const auto oom = full.execute(program(grow)); stopped(oom, "not enough memory");
    const auto grown = full.get_global("n").as_number();
    check(grown > 200 && grown < 1000 && full.heap_stats().live_bytes > tight.max_heap_bytes, "Out-of-memory point changed");
    // A collection inside a nested chunk keeps the parent's frame values.
    Function parent; parent.strings = {"v", "host"};
    parent.code = {u(Createtable), u(Pushstring), s(Pushint, 7), u(Setmap, 1), u(Getglobal, 1), ab(Call, 1, 0),
        u(Getlocal), u(Getdotted), u(Return, 1), End};
    Function garbage; garbage.code = {s(Pushint, 1), s(Pushint, 500), s(Pushint, 1), s(Forprep, 3), u(Createtable), Pop | (1u << 6),
        s(Forloop, -3), End};
    Lua4Limits few; few.max_objects = 16; Lua4Vm nested(few); nested.set_global("host", Lua4Value::host_function("host"));
    const auto garbage_program = program(garbage);
    nested.set_resumable_host_handler([&](const Lua4HostCall&) {
        auto child = nested.begin_nested(garbage_program); const auto result = nested.resume(child);
        check(result.completed, "Nested garbage chunk stopped: " + result.diagnostic);
        return Lua4HostResult{true, {}, {}};
    });
    auto root = nested.begin_execution(program(parent)); numeric_result(nested.resume(root), {7});
    check(nested.heap_stats().collections > 20, "Nested chunk did not collect");
}

// igLuaState::runBuffer (80272768) only logs a failed lua_dobuffer: the
// error first calls _ERRORMESSAGE (lua_error -> message 802788A4), then
// luaD_runprotected drops the chunk's frames and the lua_State goes on.
void script_errors_keep_the_vm() {
    Function handler; handler.parameters = 1; handler.strings = {"alert", "stage"}; // _ERRORMESSAGE(m): alert(m, stage)
    handler.code = {u(Getglobal), u(Getlocal), u(Getglobal, 1), ab(Call, 1, 0), End};
    Function define; define.strings = {"_ERRORMESSAGE"}; define.children = {handler};
    define.code = {ab(Closure, 0, 0), u(Setglobal), End};
    Function failing; failing.strings = {"stage", "missing"}; // stage = 7; missing(); stage = 9
    failing.code = {s(Pushint, 7), u(Setglobal), u(Getglobal, 1), ab(Call, 0, 0), s(Pushint, 9), u(Setglobal), End};
    Function reads; reads.strings = {"stage"}; reads.code = {u(Getglobal), Return, End};
    std::vector<std::string> alerts;
    auto alert = [&](const Lua4HostCall& call) {
        check(call.name == "alert" && call.arguments.size() == 2, "_ERRORMESSAGE fixture call changed");
        alerts.push_back(std::string(call.arguments[0].as_string()) + "|" + call.arguments[1].describe());
    };
    Lua4Vm vm; vm.set_global("alert", Lua4Value::host_function("alert"));
    vm.set_host_handler([&](const Lua4HostCall& call) { alert(call); return std::vector<Lua4Value>{}; });
    check(vm.execute(program(define)).completed, "_ERRORMESSAGE definition failed");
    const auto failed = vm.execute(program(failing));
    stopped(failed, "cannot call nil");
    check(failed.stop == Lua4RunStop::faulted && failed.site.pc == 3 && !vm.stopped(), "A script error stopped the VM");
    check(alerts.size() == 1 && alerts[0] == "cannot call nil|7", "_ERRORMESSAGE did not get the error text on top of the failing chunk");
    numeric_result(vm.execute(program(reads)), {7}); // Globals stay as written, no rollback, no later writes.
    check(alerts.size() == 1, "A completed chunk called _ERRORMESSAGE");

    // A nested chunk's error ends only that chunk (its own protected lua_call);
    // the host callback and the parent go on with their frame values.
    Function parent; parent.strings = {"v", "host"};
    parent.code = {u(Createtable), u(Pushstring), s(Pushint, 7), u(Setmap, 1), u(Getglobal, 1), ab(Call, 1, 0),
        u(Getlocal), u(Getdotted), u(Return, 1), End};
    Function bad; bad.strings = {"stage"}; bad.code = {u(Getglobal), s(Pushint, 1), Add, u(Setglobal), Pushnil | (1u << 6), s(Pushint, 1), Add, End};
    const auto bad_program = program(bad);
    Lua4Vm nested; nested.set_global("host", Lua4Value::host_function("host"));
    nested.set_global("alert", Lua4Value::host_function("alert")); nested.set_global("stage", Lua4Value::number(1));
    unsigned children = 0;
    nested.set_resumable_host_handler([&](const Lua4HostCall& call) {
        if (call.name == "alert") { alert(call); return Lua4HostResult{true, {}, {}}; }
        auto child = nested.begin_nested(bad_program); const auto result = nested.resume(child); ++children;
        check(result.stop == Lua4RunStop::faulted && result.diagnostic.find("arithmetic") != std::string::npos && !nested.stopped(),
            "The nested script error was not contained in its chunk: " + result.diagnostic);
        return Lua4HostResult{true, {}, {}};
    });
    auto defined = nested.begin_execution(program(define)); check(nested.resume(defined).completed, "Nested _ERRORMESSAGE definition failed");
    auto root = nested.begin_execution(program(parent)); numeric_result(nested.resume(root), {7});
    check(children == 1 && !nested.stopped() && nested.get_global("stage").as_number() == 2 && alerts.size() == 2 &&
        alerts[1].starts_with("arithmetic/tag method unsupported for nil|2"), "The parent did not go on after its child's error");

    // An error inside _ERRORMESSAGE calls it again further up; the chain ends,
    // the VM stays usable and the invocation reports the first error.
    Function looping; looping.parameters = 1; looping.strings = {"count"}; // count = count + 1; m()
    looping.code = {u(Getglobal), s(Addi, 1), u(Setglobal), u(Getlocal), ab(Call, 0, 0), End};
    Function define_loop; define_loop.strings = {"_ERRORMESSAGE", "count"}; define_loop.children = {looping};
    define_loop.code = {ab(Closure, 0, 0), u(Setglobal), s(Pushint, 0), u(Setglobal, 1), End};
    Lua4Vm chain; check(chain.execute(program(define_loop)).completed, "Looping handler definition failed");
    const auto chained = chain.execute(program(failing));
    stopped(chained, "cannot call nil");
    const auto levels = chain.get_global("count").as_number();
    check(!chain.stopped() && levels > 1 && levels < 2000, "A failing _ERRORMESSAGE was not bounded");
    check(chain.execute(program(reads)).completed, "The VM did not survive a failing _ERRORMESSAGE");

    // Out of memory breaks without a message (luaD_breakrun LUA_ERRMEM).
    Function grow; grow.strings = {"list", "n"}; // list[n] = {} forever
    grow.code = {u(Getglobal, 1), s(Addi, 1), u(Setglobal, 1), u(Getglobal), u(Getglobal, 1), u(Createtable), ab(Settable, 3, 3),
        s(Jmp, -8), End};
    Function setup; setup.strings = {"list", "n"}; setup.code = {u(Createtable), u(Setglobal), s(Pushint, 0), u(Setglobal, 1), End};
    Lua4Limits tight; tight.max_heap_bytes = 64u * 1024u; Lua4Vm full(tight); full.set_global("alert", Lua4Value::host_function("alert"));
    full.set_host_handler([&](const Lua4HostCall& call) { alert(call); return std::vector<Lua4Value>{}; });
    check(full.execute(program(define)).completed && full.execute(program(setup)).completed, "Out-of-memory setup failed");
    stopped(full.execute(program(grow)), "not enough memory");
    check(alerts.size() == 2 && !full.stopped(), "Out of memory called _ERRORMESSAGE or stopped the VM");
    check(full.execute(program(setup)).completed, "The VM did not go on after running out of memory");
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
        resumed_frames_and_results();resumed_tailcalls();nested_owned_invocations();continuation_ownership_and_faults();nested_faults_and_limits();
        garbage_collection(); script_errors_keep_the_vm();
        if (argc == 3) original(argv[1], argv[2]);
        else if (argc != 1) throw std::runtime_error("Usage: dw4_lua4_tests [func-chunk boot-chunk]");
        std::cout << "Lua 4 tests passed: " << checks << " checks\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "Lua 4 test failure: " << error.what() << '\n'; return 1; }
}
