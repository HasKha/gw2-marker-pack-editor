// The NeoLua oracle: runs the cases in test/viewer/lua_parity/cases.lua the way Pathing runs a pack's
// script, and writes beside them what NeoLua printed for each and the names its libraries have, for
// lua_parity_test.cpp to hold Markus's engine to. Run it again after changing the cases:
//
//   dotnet run -c Release --project scripts/neolua_oracle
//
// NeoLua is the package Pathing ships, built as Pathing's ScriptEngine.BuildEnv builds it: Int32
// integers and single-precision floats, which is why 1/3 prints as 0.3333333. The environment's
// globals are PathingGlobal's "Replicated LuaGlobal" region, copied below member for member. `os` is
// Pathing's own LuaLibraryOS and is left out: script_test covers it, and its clock and dates give
// nothing fixed to compare.
//
// Pathing's print is .NET's ToString in the player's culture, so a German client prints 0,5. This runs
// in the invariant culture, which is the English one and the one Markus matches.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading;
using Neo.IronLua;

// PathingGlobal's "Replicated LuaGlobal" region, with print collecting what it prints.
public class Sandbox : LuaTable
{
    private readonly LuaGlobal _sandboxedGlobal;

    public Sandbox(Lua lua) { _sandboxedGlobal = new LuaGlobal(lua); }

    public List<string> Printed { get; } = new();

    [LuaMember("getmetatable")]
    public static LuaTable LuaGetMetaTable(object obj) => LuaGlobal.LuaGetMetaTable(obj);

    [LuaMember("rawmembers")]
    public static IEnumerable<KeyValuePair<string, object>> LuaRawMembers(LuaTable t) => LuaGlobal.LuaRawMembers(t);

    [LuaMember("rawarray")]
    public static IList<object> LuaRawArray(LuaTable t) => LuaGlobal.LuaRawArray(t);

    [LuaMember("ipairs")]
    public static LuaResult LuaIPairs(LuaTable t) => LuaGlobal.LuaIPairs(t);

    [LuaMember("mpairs")]
    public static LuaResult LuaMPairs(LuaTable t) => LuaGlobal.LuaMPairs(t);

    [LuaMember("pairs")]
    public static LuaResult LuaPairs(LuaTable t) => LuaGlobal.LuaPairs(t);

    [LuaMember("next")]
    private static LuaResult LuaNext(LuaTable t, object next) {
        if (t == null)
            return null;
        var n = t.NextKey(next);
        return new LuaResult(n, t[n]);
    }

    [LuaMember("nextKey")]
    private static object LuaNextKey(LuaTable t, object next) => t?.NextKey(next);

    [LuaMember("print")]
    private void LuaPrint(params object[] args) {
        if (args == null)
            return;
        Printed.Add(string.Join(" ", from a in args select a == null ? string.Empty : a.ToString()));
    }

    [LuaMember("rawequal")]
    public static bool LuaRawEqual(object a, object b) => LuaGlobal.LuaRawEqual(a, b);

    [LuaMember("rawget")]
    public static object LuaRawGet(LuaTable t, object index) => LuaGlobal.LuaRawGet(t, index);

    [LuaMember("rawlen")]
    public static int LuaRawLen(object v) => LuaGlobal.LuaRawLen(v);

    [LuaMember("rawset")]
    public static LuaTable LuaRawSet(LuaTable t, object index, object value) => LuaGlobal.LuaRawSet(t, index, value);

    [LuaMember("select")]
    public static LuaResult LuaSelect(string index, params object[] values) => LuaGlobal.LuaSelect(index, values);

    [LuaMember("setmetatable")]
    public static LuaTable LuaSetMetaTable(LuaTable t, LuaTable metaTable) => LuaGlobal.LuaSetMetaTable(t, metaTable);

    [LuaMember("tonumber")]
    public object LuaToNumber(object v, int? iBase = null) => _sandboxedGlobal.LuaToNumber(v, iBase);

    [LuaMember("tostring")]
    public static string LuaToString(object v) => LuaGlobal.LuaToString(v);

    [LuaMember("type")]
    public static string LuaTypeTest(object v, bool clr = false) => LuaGlobal.LuaTypeTest(v, clr);

    [LuaMember("_VERSION")]
    public virtual string Version => LuaGlobal.VersionString;

    [LuaMember("coroutine")]
    private static dynamic LuaLibraryCoroutine => LuaType.GetType(typeof(LuaThread));

    [LuaMember("bit32")]
    private static dynamic LuaLibraryBit32 => LuaType.GetType(typeof(LuaLibraryBit32));

    [LuaMember("math")]
    private static dynamic LuaLibraryMath => LuaType.GetType(typeof(LuaLibraryMath));

    [LuaMember("string")]
    private static dynamic LuaLibraryString => LuaType.GetType(typeof(LuaLibraryString));

    [LuaMember("table")]
    private static dynamic LuaLibraryTable => LuaType.GetType(typeof(LuaTable));
}

public static class Program
{
    private const string CaseMarker = "-- case: ";

    // The libraries as PathingGlobal names them, and the type each is.
    private static readonly (string Name, Type Type)[] Libraries = {
        ("bit32", typeof(LuaLibraryBit32)),
        ("coroutine", typeof(LuaThread)),
        ("math", typeof(LuaLibraryMath)),
        ("string", typeof(LuaLibraryString)),
        ("table", typeof(LuaTable)),
    };

    public static int Main(string[] args) {
        Thread.CurrentThread.CurrentCulture = CultureInfo.InvariantCulture;
        string folder = args.Length > 0 ? args[0] : Path.Combine(RepoRoot(), "test", "viewer", "lua_parity");

        using var lua = new Lua(LuaIntegerType.Int32, LuaFloatType.Float);
        var expected = new StringBuilder("# What NeoLua printed for each case of cases.lua: written by scripts/neolua_oracle, never by hand.\n");
        int count = 0;
        foreach (var (name, code) in ReadCases(Path.Combine(folder, "cases.lua"))) {
            expected.Append("== ").Append(name).Append('\n');
            var env = lua.CreateEnvironment<Sandbox>();
            string error = null;
            try {
                lua.CompileChunk(code, "cases.lua", new LuaCompileOptions()).Run(env);
            } catch (Exception e) {
                // Pathing's WrapScriptCall reports any exception as the script's error, a .NET
                // cast as much as a Lua error.
                error = e.GetType().Name + ": " + e.Message.Split('\n')[0].TrimEnd('\r');
            }
            foreach (var line in env.Printed.SelectMany(p => p.Split('\n')))
                expected.Append(line.TrimEnd('\r')).Append('\n');
            if (error != null)
                expected.Append("!error ").Append(error).Append('\n');
            ++count;
        }
        File.WriteAllText(Path.Combine(folder, "expected.txt"), expected.ToString());

        var surface = new StringBuilder("# What NeoLua's libraries and Pathing's replicated globals hold: written by scripts/neolua_oracle.\n");
        foreach (var name in GlobalNames())
            surface.Append("_G ").Append(name).Append('\n');
        foreach (var (library, type) in Libraries) {
            foreach (var member in LibraryMembers(lua, library, type))
                surface.Append(library).Append(' ').Append(member).Append('\n');
        }
        File.WriteAllText(Path.Combine(folder, "surface.txt"), surface.ToString());

        Console.WriteLine($"{count} cases written to {folder}");
        return 0;
    }

    // Each "-- case: <name>" line starts a case that runs to the next; what comes before the first
    // is the file's own comment.
    private static IEnumerable<(string, string)> ReadCases(string path) {
        string name = null;
        var code = new StringBuilder();
        foreach (var line in File.ReadAllLines(path)) {
            if (line.StartsWith(CaseMarker)) {
                if (name != null)
                    yield return (name, code.ToString());
                name = line.Substring(CaseMarker.Length).Trim();
                code.Clear();
            } else if (name != null) {
                code.Append(line).Append('\n');
            }
        }
        if (name != null)
            yield return (name, code.ToString());
    }

    private static IEnumerable<string> GlobalNames() =>
        typeof(Sandbox)
            .GetMembers(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static | BindingFlags.Instance | BindingFlags.DeclaredOnly)
            .SelectMany(m => m.GetCustomAttributes<LuaMemberAttribute>())
            .Select(a => a.LuaName)
            .Distinct()
            .OrderBy(n => n, StringComparer.Ordinal);

    // What a script can index on a library: every public static name of its type that NeoLua
    // resolves to something, accessors and operators aside. Read as `math.abs`: NeoLua takes
    // `math["abs"]` on a .NET type for a generic type's arguments, and fails.
    private static IEnumerable<string> LibraryMembers(Lua lua, string library, Type type) {
        var candidates = type.GetMembers(BindingFlags.Public | BindingFlags.Static)
                             .Where(m => m.MemberType != MemberTypes.NestedType)
                             .Select(m => m.Name)
                             .Where(n => !n.StartsWith("op_") && !n.StartsWith("get_") && !n.StartsWith("set_") &&
                                         !n.StartsWith("add_") && !n.StartsWith("remove_") && !n.StartsWith("."))
                             .Distinct()
                             .OrderBy(n => n, StringComparer.Ordinal);
        foreach (var name in candidates) {
            var env = lua.CreateEnvironment<Sandbox>();
            bool found;
            try {
                var result = lua.CompileChunk($"return {library}.{name} ~= nil", "surface.lua", new LuaCompileOptions()).Run(env);
                found = result.Count > 0 && result[0] is bool b && b;
            } catch (Exception) {
                found = false;
            }
            if (found)
                yield return name;
        }
    }

    private static string RepoRoot([CallerFilePath] string source = "") =>
        Path.GetFullPath(Path.Combine(Path.GetDirectoryName(source), "..", ".."));
}
