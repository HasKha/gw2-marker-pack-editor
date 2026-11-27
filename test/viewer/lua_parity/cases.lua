-- Cases Markus's Lua is held to NeoLua's on, as Pathing runs it: lua_parity_test.cpp runs each in a
-- fresh engine and compares what it prints with what scripts/neolua_oracle wrote for it in
-- expected.txt. Run the oracle again after changing a case.
--
-- Each "-- case: <name>" line starts a case. A case prints what it is about and nothing that changes
-- between runs: no table or function addresses, random numbers only through what their range shows.
-- One idea a case, so that a difference names itself; a case that errors stops there, so an error
-- gets a case of its own.

-- case: print-basic
print("a", 1, nil, true, false)

-- case: print-nothing
print()
print(nil)

-- case: print-table
print({}, setmetatable({}, {}))

-- case: tostring-table
print(tostring({}):sub(1, 6))

-- case: tostring-values
print(tostring(nil), tostring(true), tostring(false), tostring("s"), tostring(12))

-- case: number-integers
print(1, -7, 0, 2147483647, -2147483648)

-- case: number-integral-floats
print(1.0, 10 / 2, 2 ^ 2, -0.0, 3 / 1)

-- case: number-fractions
print(0.5, 0.25, 1.5, -2.75)

-- case: number-single-precision
print(0.1, 1 / 3, 2 / 3, 10 / 3, 0.1 + 0.2)

-- case: number-large-integral
print(1e10, 2 ^ 24, 2 ^ 31)

-- case: number-large-exponent
print(2 ^ 53, 2 ^ 60)

-- case: number-large-single-precision
print(1e15, 1e16, 1e20)

-- case: number-small
print(2 ^ (-10), 2 ^ (-20))

-- case: power-negative-exponent
print(2 ^ -1)

-- case: number-small-single-precision
print(1e-3, 1e-4, 1e-5, 1e-10)

-- case: number-infinity
print(1 / 0, -1 / 0)

-- case: number-nan
local nan = 0 / 0
print(tostring(nan ~= nan))

-- case: number-concat
print(1 .. "", 1.5 .. "", (10 / 2) .. "", (2 ^ 31) .. "")

-- case: number-concat-single-precision
print((1 / 3) .. "")

-- case: number-tostring
print(tostring(1.5), tostring(10 / 2), tostring(2 ^ 53))

-- case: number-tostring-single-precision
print(tostring(1 / 3), tostring(1e20))

-- case: integer-overflow
print(2147483647 + 1, -2147483648 - 1, 65536 * 65536)

-- case: integer-and-float-equal
print(tostring(1 == 1.0), tostring(2 ^ 2 == 4), tostring(10 / 2 == 5))

-- case: arithmetic-division
print(7 / 2, 7 // 2, 7.5 // 2)

-- case: arithmetic-floor-division-negative
print(-7 // 2, -7.5 // 2)

-- case: arithmetic-modulo
print(7 % 3, -7 % 3, 7 % -3, -7 % -3, 7.5 % 2)

-- case: arithmetic-power
print(2 ^ 10, 2 ^ 0.5, tostring((-8) ^ (1 / 3) ~= (-8) ^ (1 / 3)))

-- case: arithmetic-float-division-by-zero
print(1.0 / 0, 1 / 0.0)

-- case: arithmetic-float-floor-division-by-zero
print(1.0 // 0)

-- case: arithmetic-modulo-huge
print(-1 % math.huge)

-- case: arithmetic-integer-division-by-zero
print(1 // 0)

-- case: arithmetic-integer-modulo-by-zero
print(1 % 0)

-- case: bitwise-operators
print(5 & 3, 5 | 3, 5 ~ 3, ~5, 1 << 4, 256 >> 4)

-- case: coercion-arithmetic
print("10" + 1, "3" * "4", "0x10" + 0, "1.5" + 1)

-- case: coercion-concat
print(10 .. 20, 1 .. "", "a" .. 2)

-- case: coercion-equality
print(tostring("10" == 10), tostring("1" == 1.0))

-- case: coercion-comparison
print("10" < 5)

-- case: string-comparison
print(tostring("a" < "b"), tostring("B" < "a"), tostring("a" < "B"), tostring("abc" < "abd"), tostring("" < "a"), tostring("Z" < "a"))

-- case: truthiness
print(tostring(not nil), tostring(not false), tostring(not 0), tostring(not ""), tostring(nil == false), tostring(0 == false))

-- case: logic-operators
print(nil or "x", tostring(false and 1), 1 and 2, nil and 1, false or nil)

-- case: for-numeric
local out = {}
for i = 1, 3 do out[#out + 1] = i end
for i = 3, 1, -1 do out[#out + 1] = i end
print(table.concat(out, ","))

-- case: for-float-step
local out = {}
for i = 1, 2, 0.5 do out[#out + 1] = tostring(i) end
print(table.concat(out, ","))

-- case: varargs
local function count(...) return select("#", ...) end
local function last(...) return select(-1, ...) end
print(count(), count(nil), count(1, nil, 3), last(1, 2, 3))

-- case: select-positive
print(select(2, "a", "b", "c"))

-- case: select-float-index
print(select(1.5, "a", "b", "c"))

-- case: closures
local function counter()
  local n = 0
  return function() n = n + 1; return n end
end
local c = counter()
c()
print(c(), counter()())

-- case: string-escapes
print("\65\066", "\x41", "a\tb", #"\0ab")

-- case: string-escape-unicode
print("\u{48}")

-- case: string-long
print([[
long]], [==[a]]b]==])

-- case: string-length
print(#"abc", ("abc"):len(), string.len(""))

-- case: string-length-utf8
print(#"é", string.len("日本"))

-- case: string-sub
local s = "hello"
print(s:sub(2), s:sub(-3), s:sub(2, 3), s:sub(0), s:sub(10), s:sub(3, 2), s:sub(-100, 2))

-- case: string-sub-float
print(("abcdef"):sub(1, 2.5), ("abcdef"):sub(1, 3.5), ("abcdef"):sub(1.5))

-- case: string-sub-text-index
print(("abcdef"):sub("2", "3"))

-- case: string-sub-float-rounding
print(("abcdef"):sub(1, 2.7), ("abcdef"):sub(-2.5), ("abcdef"):sub(-1.7), ("abcdef"):sub(1, -1.5))

-- case: string-byte-float
print(string.byte("abc", 1.5), string.byte("abc", 2.5))

-- case: string-char-float
print(string.char(72.4, 104.5))

-- case: string-upper-lower
print(("Hello"):upper(), ("Hello"):lower(), string.upper("ß"), string.lower("ÀB"))

-- case: string-rep
print(("ab"):rep(3), ("ab"):rep(0), ("ab"):rep(3, ","), ("a"):rep(2.5))

-- case: string-reverse
print(("abc"):reverse(), (""):reverse())

-- case: string-byte-char
print(string.byte("A"), string.byte("abc", 2), string.byte("abc", 1, 3))
print(string.char(72, 105), string.char())

-- case: string-find-plain
print(("a.b"):find(".", 1, true), ("a.b"):find("b", 1, true), ("abc"):find("x", 1, true))

-- case: string-find-pattern
print(("hello world"):find("o w"), ("hello"):find("l+"), ("key=val"):find("(%w+)=(%w+)"))

-- case: string-find-init
print(("abcabc"):find("b", 3))
print(("abcabc"):find("b", -2))

-- case: string-find-empty
print(("abc"):find(""))
print(("abc"):find("", 4))
print(("abc"):find("", 10))

-- case: string-match-captures
print(("key = value"):match("^(%w+)%s*=%s*(%w+)$"), ("  trim  "):match("^%s*(.-)%s*$") .. "|")

-- case: string-match-number
local n = ("x=42"):match("%d+")
print(n, type(n))

-- case: string-match-position-capture
print(("hello"):match("()ll()"))

-- case: string-match-classes
print(("a1 B2_c3"):match("%a+"), ("a1 B2"):match("%u%d"), ("x  y"):match("%s+") .. "|", ("a,b"):match("%p"))

-- case: string-match-anchor
print(("abc"):match("^b"), ("abc"):match("c$"), ("a^b"):match("a^b"))

-- case: string-match-lazy
print(("<a><b>"):match("<(.-)>"), ("<a><b>"):match("<(.*)>"))

-- case: string-match-optional
print(("color colour"):match("colou?r"), ("ac"):match("ab?c"))

-- case: string-match-set
print(("x-y_z"):match("[%w_]+"), ("abc123"):match("[^%a]+"), ("a]b"):match("[]]"))

-- case: string-match-balanced
print(("f(a(b)c)d"):match("%b()"))

-- case: string-match-frontier
print(("THE (quick) fox"):find("%f[%a]%a+"))

-- case: string-match-backreference
print(('say "hi" now'):match("([\"'])(.-)%1"))

-- case: string-gmatch
local out = {}
for k, v in ("a=1, b=2, c=3"):gmatch("(%w+)=(%w+)") do out[#out + 1] = k .. ":" .. v end
print(table.concat(out, " "))

-- case: string-gmatch-words
local out = {}
for w in ("one two  three"):gmatch("%S+") do out[#out + 1] = w end
print(table.concat(out, "|"))

-- case: string-gsub
print(("hello world"):gsub("o", "0"))
print(("a.b.c"):gsub("%.", "/"))
print(("abc"):gsub("", "-"))

-- case: string-gsub-limit
print(("aaaa"):gsub("a", "b", 2))

-- case: string-gsub-captures
print(("hello world"):gsub("(%w+)", "<%1>"))
print(("ab"):gsub("(a)(b)", "%2%1%%"))

-- case: string-gsub-function
print(("a b c"):gsub("%a", function(c) return c:upper() end))

-- case: string-gsub-function-nil
print(("a b"):gsub("%a", function(c) if c == "a" then return nil end return "X" end))

-- case: string-gsub-function-number
print(("a b"):gsub("%a", function(c) return 1 end))

-- case: string-gsub-table
print(("$x and $y"):gsub("%$(%w+)", { x = "1", y = 2 }))

-- case: string-format-integer
print(string.format("%d|%5d|%-5d|%05d|%i", 42, 42, 42, 42, 7))

-- case: string-format-float
print(string.format("%f|%.2f|%5.1f|%.0f|%e", 3.14159, 3.14159, 2.25, 2.5, 1234.5))

-- case: string-format-general
print(string.format("%g|%g|%g|%g", 1.5, 100000, 1e20, 0.0001))

-- case: string-format-string
print(string.format("%s|%5s|%-5s|%.2s", "abc", "abc", "abc", "abc"))

-- case: string-format-values
print(string.format("%s %s %s", 1, 1.5, true))

-- case: string-format-nil
print(string.format("%s", nil))

-- case: string-format-hex
print(string.format("%x|%X|%o|%c|%%", 255, 255, 8, 65))

-- case: string-format-quoted
print(string.format("%q", 'a "b"'))

-- case: string-format-float-as-integer
print(string.format("%d", 2.0))

-- case: string-format-fraction-as-integer
print(string.format("%d", 2.5))

-- case: string-format-method
print(("%d-%s"):format(3, "x"))

-- case: string-format-number-as-string
print(string.format("%s|%s|%s", 10 / 2, 2 ^ 53, 1 / 0))

-- case: string-dotnet-members
print(("a b"):Replace(" ", "_"), ("abc").Length, ("abc"):ToUpper(), tostring(("abc"):Contains("b")))

-- case: string-library-absent
print(type(string.pack), type(string.unpack), type(string.packsize), type(utf8))

-- case: tonumber
print(tonumber("10"), tonumber("0x10"), tonumber(" 5 "), tonumber("5."), tonumber(""), tonumber("abc"))
print(tonumber("1e2"), tonumber("10", 2), tonumber("ff", 16), tonumber("zz", 36), tonumber(5))

-- case: tonumber-type
print(math.type(tonumber("10")), math.type(tonumber("10.0")), math.type(tonumber("1e2")))

-- case: number-text-round-trip
local text = tostring(10 / 2)
print(text, tonumber(text), math.type(tonumber(text)))

-- case: number-as-key-text
local t = {}
t["n" .. (10 / 2)] = "found"
print(t.n5, t["n5.0"])

-- case: type
print(type(1), type(1.5), type("a"), type(nil), type(true), type({}), type(print), type(function() end))

-- case: type-libraries
print(type(math), type(string), type(table), type(coroutine), type(math.floor))

-- case: math-constants
print(math.pi)

-- case: math-huge
print(math.huge, tostring(math.huge == 1 / 0))

-- case: math-integer-limits
print(math.maxinteger, math.mininteger)

-- case: math-type
print(math.type(1), math.type(1.0), math.type("1"), math.type(2 ^ 2), math.type(7 // 2))

-- case: math-floor-ceil
print(math.floor(2.5), math.floor(-0.5), math.ceil(2.1), math.ceil(-2.1), math.floor(3))

-- case: math-floor-type
print(math.type(math.floor(2.5)), math.type(math.ceil(2.5)))

-- case: math-floor-large
print(math.floor(2 ^ 40))

-- case: math-abs-min-max
print(math.abs(-3), math.abs(-2.5), math.max(1, 2.5, 2), math.min(3, 1, 2), math.max(1, 2))

-- case: math-sqrt-exp-log
print(math.sqrt(16), math.sqrt(2), math.exp(0), math.log(1), math.log(8, 2), math.log(100, 10))

-- case: math-trig
print(math.sin(0), math.cos(0), tostring(math.atan(1, 1) == math.pi / 4), math.deg(math.pi), math.rad(180))

-- case: math-neolua-additions
print(tostring(math.atan2(1, 1) == math.atan(1, 1)), math.pow(2, 10), math.cosh(0), math.ldexp(1, 3), math.frexp(8))

-- case: math-fmod-modf
print(math.fmod(7, 3), math.fmod(-7, 3), math.modf(3.5), math.modf(-3.5))

-- case: math-tointeger
print(math.tointeger(3.0), math.tointeger(3.5), math.tointeger("8"), math.tointeger(3))

-- case: math-ult
print(tostring(math.ult(1, 2)), tostring(math.ult(-1, 2)))

-- case: math-random-unit
local low, high = 1, 0
for i = 1, 2000 do
  local r = math.random()
  if r < low then low = r end
  if r > high then high = r end
end
print(tostring(low >= 0), tostring(high < 1), tostring(low < 0.01), tostring(high > 0.99))

-- case: math-random-upper
local seen = {}
for i = 1, 2000 do seen[math.random(3)] = "seen" end
print(seen[0], seen[1], seen[2], seen[3], seen[4])

-- case: math-random-range
local seen = {}
for i = 1, 2000 do seen[math.random(1, 3)] = "seen" end
print(seen[0], seen[1], seen[2], seen[3], seen[4])

-- case: math-random-float-range
local seen = {}
for i = 1, 2000 do seen[math.random(1, 2.5)] = "seen" end
print(seen[1], seen[2], seen[3])

-- case: math-random-type
print(math.type(math.random(5)), math.type(math.random()))

-- case: math-random-single-value
print(math.random(1), math.random(3, 3), math.random(-2, -2))

-- case: math-random-negative-range
local seen = {}
for i = 1, 2000 do seen[math.random(-2, 0)] = "seen" end
print(seen[-3], seen[-2], seen[-1], seen[0])

-- case: math-random-empty
print(math.random(0))

-- case: math-random-reversed
print(math.random(5, 4))

-- case: math-randomseed
math.randomseed(7)
local first = { math.random(1000), math.random(1000), math.random() }
math.randomseed(7)
print(tostring(first[1] == math.random(1000)), tostring(first[2] == math.random(1000)), tostring(first[3] == math.random()))

-- case: table-concat
print(table.concat({ 1, 2.5, "x" }, ","), table.concat({}, ","), table.concat({ "a", "b", "c" }, "-", 2, 3))

-- case: table-concat-nested
print(table.concat({ 1, {} }, ","))

-- case: table-insert-remove
local t = { "a", "b", "c" }
table.insert(t, "d")
table.insert(t, 1, "z")
local removed = table.remove(t)
local first = table.remove(t, 1)
print(table.concat(t, ","), removed, first, table.remove({}))

-- case: table-insert-float-position
local t = { "a", "b" }
table.insert(t, 1.5, "x")
print(table.concat(t, ","))

-- case: table-insert-out-of-range
local t = { "a" }
table.insert(t, 5, "x")
print(#t)

-- case: table-sort
local t = { 3, 1, 2 }
table.sort(t)
local s = { "b", "C", "a" }
table.sort(s)
local d = { 1, 3, 2 }
table.sort(d, function(a, b) return a > b end)
print(table.concat(t, ","), table.concat(s, ","), table.concat(d, ","))

-- case: table-unpack
print(table.unpack({ 1, 2, 3 }))
print(table.unpack({ 1, 2, 3 }, 2))
print(table.unpack({ 1, 2, 3 }, 2, 3))

-- case: table-pack
local p = table.pack(1, nil, 3)
print(p.n, p[1], p[2], p[3])

-- case: table-move
local t = table.move({ 1, 2, 3 }, 1, 3, 2)
print(table.concat(t, ","))

-- case: table-length
local t = { 1, 2, 3 }
t[#t + 1] = 4
print(#t, #{}, #{ n = 1 })

-- case: table-length-hole
local t = { 1, 2, nil, 4 }
print(#t)

-- case: table-keys
local t = {}
t[1] = "int"
t[1.0] = "float"
t["1"] = "text"
print(t[1], t["1"])

-- case: table-library-extras
print(tostring(table.ToJson ~= nil), tostring(table.FromJson ~= nil), tostring(table.merge ~= nil), tostring(table.collect ~= nil))

-- case: pairs-order
-- Teh's Trails's menu order, and enough keys after it that Lua's order, which changes between
-- runs, never comes out as NeoLua's by chance.
local out = {}
local t = { main = 1, hp = 2, heart = 3, tos = 4 }
for i = 1, 8 do t["k" .. i] = i end
for k in pairs(t) do out[#out + 1] = k end
print(table.concat(out, ","))

-- case: pairs-mixed
local out = {}
for k, v in pairs({ 10, 20, x = "a", 30 }) do out[#out + 1] = tostring(k) .. "=" .. tostring(v) end
print(table.concat(out, ","))

-- case: ipairs-stops-at-nil
local out = {}
for i, v in ipairs({ 1, 2, nil, 4 }) do out[#out + 1] = v end
print(table.concat(out, ","))

-- case: next
local t = { x = 1 }
print(next(t), next({}))

-- case: raw-access
local t = setmetatable({}, { __index = function() return "meta" end, __newindex = function() end })
t.a = 1
rawset(t, "b", 2)
print(t.a, rawget(t, "a"), t.b, tostring(rawequal(t, t)), rawlen({ 1, 2 }), rawlen("abc"))

-- case: metatable-index
local base = { greet = function() return "hi" end }
local obj = setmetatable({}, { __index = base })
local fn = setmetatable({}, { __index = function(_, k) return k .. "!" end })
print(obj.greet(), fn.key, tostring(getmetatable(obj).__index == base))

-- case: metatable-newindex
local log = {}
local t = setmetatable({}, { __newindex = function(t, k, v) rawset(t, k, v * 2) end })
t.x = 5
print(t.x)

-- case: metatable-call
local callable = setmetatable({}, { __call = function(self, a, b) return a + b end })
print(callable(2, 3))

-- case: metatable-arithmetic
local mt = {}
mt.__add = function(a, b) return a.v + b.v end
mt.__unm = function(a) return -a.v end
mt.__concat = function(a, b) return "cat" end
mt.__len = function() return 42 end
local a = setmetatable({ v = 1 }, mt)
local b = setmetatable({ v = 2 }, mt)
print(a + b, -a, a .. "x", #a)

-- case: metatable-comparison
local mt = {}
mt.__eq = function(a, b) return a.v == b.v end
mt.__lt = function(a, b) return a.v < b.v end
mt.__le = function(a, b) return a.v <= b.v end
local a = setmetatable({ v = 1 }, mt)
local b = setmetatable({ v = 1 }, mt)
local c = setmetatable({ v = 2 }, mt)
print(tostring(a == b), tostring(a < c), tostring(c <= a), tostring(a ~= c))

-- case: metatable-tostring
local obj = setmetatable({}, { __tostring = function() return "custom" end })
print(tostring(obj), obj)

-- case: metatable-of-string
print(tostring(getmetatable("abc") ~= nil))

-- case: coroutine-basic
local co = coroutine.create(function(a, b)
  local c = coroutine.yield(a + b)
  local d, e = coroutine.yield(c * 2)
  return d + e
end)
print(coroutine.resume(co, 1, 2))
print(coroutine.resume(co, 10))
print(coroutine.resume(co, 3, 4))
print(coroutine.status(co), coroutine.resume(co))

-- case: coroutine-wrap
local gen = coroutine.wrap(function()
  for i = 1, 3 do coroutine.yield(i) end
end)
print(gen(), gen(), gen())

-- case: coroutine-status
local co
co = coroutine.create(function() print(coroutine.status(co)) coroutine.yield() end)
print(coroutine.status(co))
coroutine.resume(co)
print(coroutine.status(co))
coroutine.resume(co)
print(coroutine.status(co))

-- case: coroutine-error
local co = coroutine.create(function() error("inside") end)
local ok = coroutine.resume(co)
print(tostring(ok), coroutine.status(co))

-- case: coroutine-library-absent
print(tostring(coroutine.close == nil), tostring(coroutine.isyieldable == nil))

-- case: bit32
print(bit32.band(6, 3), bit32.bor(6, 3), bit32.bxor(6, 3), bit32.lshift(1, 4), bit32.rshift(256, 4), tostring(bit32.btest(6, 1)))

-- case: globals-absent
print(tostring(pcall), tostring(error), tostring(assert), tostring(unpack), tostring(load), tostring(dofile))

-- case: globals-g
print(tostring(_G ~= nil))

-- case: globals-g-is-the-environment
_G.answer = 42
other = "set"
print(answer, _G.other, tostring(_G._G == _G), tostring(_G.print == print))

-- case: globals-neolua
print(tostring(rawmembers ~= nil), tostring(rawarray ~= nil), tostring(mpairs ~= nil), tostring(nextKey ~= nil), tostring(_VERSION ~= nil))

-- case: goto
local i = 1
::top::
i = i + 1
if i < 3 then goto top end
print(i)

-- case: integer-for-loop-type
local out = {}
for i = 1, 2 do out[#out + 1] = math.type(i) end
print(table.concat(out, ","))

-- case: nil-counter
-- What a script counting into an unset global relies on.
for i = 1, 3 do hits = hits + 1 end
print(hits)

-- case: nil-field-counter
local t = {}
t.n = t.n + 1
print(t.n)

-- case: nil-call
local x
x()

-- case: nil-index
local x
print(x.y)

-- case: print-trailing-nil
local function none() end
local function one_nil() return nil end
print(1, nil)
print(1, one_nil())
print(1, none())
print(1, math.type("x"))

-- case: math-type-of-text
print(select("#", math.type("x")), select("#", math.type(nil)))

-- case: nil-metatable
print(tostring(getmetatable(nil)), tostring(getmetatable(false)))

-- case: nil-bitwise-float
local x
print(x | 3.5, x | -2.5, x | 2.7)

-- case: nil-floor-division-type
local x
print(math.type(x // 2.5), math.type(7.5 // 2), math.type(7.5 // 2.5), math.type(7 // 2.0))

-- case: nil-unset-global-counter-float
total = total + 0.5
print(total, math.type(total))

-- case: boolean-and-boolean-add
print(true + false)

-- case: boolean-and-boolean-compare
print(false < true)

-- case: boolean-and-boolean-concat
print(true .. false)

-- case: nil-and-boolean-add
local x
print(x + true)

-- case: boolean-and-nil-add
local x
print(true + x)

-- case: nil-concat-number-text
local x
print(x .. (10 / 2), (10 / 2) .. x)

-- case: nil-with-table-metamethod
local t = setmetatable({}, { __add = function() return "table's" end, __concat = function() return "table's" end })
local x
print(x .. t)

-- case: nil-with-table-metamethod-add
local t = setmetatable({}, { __add = function() return "table's" end, __lt = function() return true end })
local x
print(x + t, t + x, tostring(x < t))

-- Every operator against nil and the booleans, on each side: NeoLua converts what it can to the
-- other operand's type, nil and false to 0, but not always on both sides.
-- case: op nil + 1
local a, b = nil, 1
local v = a + b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 + nil
local a, b = 1, nil
local v = a + b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil + 2.5
local a, b = nil, 2.5
local v = a + b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 2.5 + nil
local a, b = 2.5, nil
local v = a + b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil + nil
local a, b = nil, nil
local v = a + b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil + "1"
local a, b = nil, "1"
local v = a + b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "1" + nil
local a, b = "1", nil
local v = a + b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil + "a"
local a, b = nil, "a"
local v = a + b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" + nil
local a, b = "a", nil
local v = a + b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil + {}
local a, b = nil, {}
local v = a + b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false + 1
local a, b = false, 1
local v = a + b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 + false
local a, b = 1, false
local v = a + b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op true + 1
local a, b = true, 1
local v = a + b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 + true
local a, b = 1, true
local v = a + b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false + "a"
local a, b = false, "a"
local v = a + b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" + true
local a, b = "a", true
local v = a + b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil - 1
local a, b = nil, 1
local v = a - b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 - nil
local a, b = 1, nil
local v = a - b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil - 2.5
local a, b = nil, 2.5
local v = a - b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 2.5 - nil
local a, b = 2.5, nil
local v = a - b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil - nil
local a, b = nil, nil
local v = a - b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil - "1"
local a, b = nil, "1"
local v = a - b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "1" - nil
local a, b = "1", nil
local v = a - b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil - "a"
local a, b = nil, "a"
local v = a - b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" - nil
local a, b = "a", nil
local v = a - b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil - {}
local a, b = nil, {}
local v = a - b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false - 1
local a, b = false, 1
local v = a - b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 - false
local a, b = 1, false
local v = a - b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op true - 1
local a, b = true, 1
local v = a - b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 - true
local a, b = 1, true
local v = a - b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false - "a"
local a, b = false, "a"
local v = a - b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" - true
local a, b = "a", true
local v = a - b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil * 1
local a, b = nil, 1
local v = a * b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 * nil
local a, b = 1, nil
local v = a * b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil * 2.5
local a, b = nil, 2.5
local v = a * b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 2.5 * nil
local a, b = 2.5, nil
local v = a * b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil * nil
local a, b = nil, nil
local v = a * b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil * "1"
local a, b = nil, "1"
local v = a * b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "1" * nil
local a, b = "1", nil
local v = a * b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil * "a"
local a, b = nil, "a"
local v = a * b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" * nil
local a, b = "a", nil
local v = a * b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil * {}
local a, b = nil, {}
local v = a * b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false * 1
local a, b = false, 1
local v = a * b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 * false
local a, b = 1, false
local v = a * b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op true * 1
local a, b = true, 1
local v = a * b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 * true
local a, b = 1, true
local v = a * b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false * "a"
local a, b = false, "a"
local v = a * b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" * true
local a, b = "a", true
local v = a * b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil / 1
local a, b = nil, 1
local v = a / b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 / nil
local a, b = 1, nil
local v = a / b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil / 2.5
local a, b = nil, 2.5
local v = a / b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 2.5 / nil
local a, b = 2.5, nil
local v = a / b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil / nil
local a, b = nil, nil
local v = a / b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil / "1"
local a, b = nil, "1"
local v = a / b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "1" / nil
local a, b = "1", nil
local v = a / b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil / "a"
local a, b = nil, "a"
local v = a / b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" / nil
local a, b = "a", nil
local v = a / b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil / {}
local a, b = nil, {}
local v = a / b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false / 1
local a, b = false, 1
local v = a / b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 / false
local a, b = 1, false
local v = a / b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op true / 1
local a, b = true, 1
local v = a / b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 / true
local a, b = 1, true
local v = a / b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false / "a"
local a, b = false, "a"
local v = a / b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" / true
local a, b = "a", true
local v = a / b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil // 1
local a, b = nil, 1
local v = a // b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 // nil
local a, b = 1, nil
local v = a // b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil // 2.5
local a, b = nil, 2.5
local v = a // b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 2.5 // nil
local a, b = 2.5, nil
local v = a // b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil // nil
local a, b = nil, nil
local v = a // b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil // "1"
local a, b = nil, "1"
local v = a // b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "1" // nil
local a, b = "1", nil
local v = a // b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil // "a"
local a, b = nil, "a"
local v = a // b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" // nil
local a, b = "a", nil
local v = a // b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil // {}
local a, b = nil, {}
local v = a // b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false // 1
local a, b = false, 1
local v = a // b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 // false
local a, b = 1, false
local v = a // b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op true // 1
local a, b = true, 1
local v = a // b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 // true
local a, b = 1, true
local v = a // b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false // "a"
local a, b = false, "a"
local v = a // b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" // true
local a, b = "a", true
local v = a // b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil % 1
local a, b = nil, 1
local v = a % b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 % nil
local a, b = 1, nil
local v = a % b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil % 2.5
local a, b = nil, 2.5
local v = a % b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 2.5 % nil
local a, b = 2.5, nil
local v = a % b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil % nil
local a, b = nil, nil
local v = a % b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil % "1"
local a, b = nil, "1"
local v = a % b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "1" % nil
local a, b = "1", nil
local v = a % b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil % "a"
local a, b = nil, "a"
local v = a % b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" % nil
local a, b = "a", nil
local v = a % b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil % {}
local a, b = nil, {}
local v = a % b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false % 1
local a, b = false, 1
local v = a % b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 % false
local a, b = 1, false
local v = a % b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op true % 1
local a, b = true, 1
local v = a % b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 % true
local a, b = 1, true
local v = a % b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false % "a"
local a, b = false, "a"
local v = a % b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" % true
local a, b = "a", true
local v = a % b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil ^ 1
local a, b = nil, 1
local v = a ^ b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 ^ nil
local a, b = 1, nil
local v = a ^ b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil ^ 2.5
local a, b = nil, 2.5
local v = a ^ b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 2.5 ^ nil
local a, b = 2.5, nil
local v = a ^ b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil ^ nil
local a, b = nil, nil
local v = a ^ b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil ^ "1"
local a, b = nil, "1"
local v = a ^ b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "1" ^ nil
local a, b = "1", nil
local v = a ^ b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil ^ "a"
local a, b = nil, "a"
local v = a ^ b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" ^ nil
local a, b = "a", nil
local v = a ^ b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil ^ {}
local a, b = nil, {}
local v = a ^ b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false ^ 1
local a, b = false, 1
local v = a ^ b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 ^ false
local a, b = 1, false
local v = a ^ b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op true ^ 1
local a, b = true, 1
local v = a ^ b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 ^ true
local a, b = 1, true
local v = a ^ b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false ^ "a"
local a, b = false, "a"
local v = a ^ b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" ^ true
local a, b = "a", true
local v = a ^ b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil & 1
local a, b = nil, 1
local v = a & b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 & nil
local a, b = 1, nil
local v = a & b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil & 2.5
local a, b = nil, 2.5
local v = a & b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 2.5 & nil
local a, b = 2.5, nil
local v = a & b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil & nil
local a, b = nil, nil
local v = a & b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil & "1"
local a, b = nil, "1"
local v = a & b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "1" & nil
local a, b = "1", nil
local v = a & b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil & "a"
local a, b = nil, "a"
local v = a & b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" & nil
local a, b = "a", nil
local v = a & b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil & {}
local a, b = nil, {}
local v = a & b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false & 1
local a, b = false, 1
local v = a & b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 & false
local a, b = 1, false
local v = a & b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op true & 1
local a, b = true, 1
local v = a & b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 & true
local a, b = 1, true
local v = a & b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false & "a"
local a, b = false, "a"
local v = a & b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" & true
local a, b = "a", true
local v = a & b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil | 1
local a, b = nil, 1
local v = a | b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 | nil
local a, b = 1, nil
local v = a | b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil | 2.5
local a, b = nil, 2.5
local v = a | b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 2.5 | nil
local a, b = 2.5, nil
local v = a | b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil | nil
local a, b = nil, nil
local v = a | b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil | "1"
local a, b = nil, "1"
local v = a | b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "1" | nil
local a, b = "1", nil
local v = a | b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil | "a"
local a, b = nil, "a"
local v = a | b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" | nil
local a, b = "a", nil
local v = a | b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil | {}
local a, b = nil, {}
local v = a | b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false | 1
local a, b = false, 1
local v = a | b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 | false
local a, b = 1, false
local v = a | b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op true | 1
local a, b = true, 1
local v = a | b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 | true
local a, b = 1, true
local v = a | b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false | "a"
local a, b = false, "a"
local v = a | b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" | true
local a, b = "a", true
local v = a | b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil ~ 1
local a, b = nil, 1
local v = a ~ b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 ~ nil
local a, b = 1, nil
local v = a ~ b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil ~ 2.5
local a, b = nil, 2.5
local v = a ~ b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 2.5 ~ nil
local a, b = 2.5, nil
local v = a ~ b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil ~ nil
local a, b = nil, nil
local v = a ~ b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil ~ "1"
local a, b = nil, "1"
local v = a ~ b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "1" ~ nil
local a, b = "1", nil
local v = a ~ b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil ~ "a"
local a, b = nil, "a"
local v = a ~ b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" ~ nil
local a, b = "a", nil
local v = a ~ b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil ~ {}
local a, b = nil, {}
local v = a ~ b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false ~ 1
local a, b = false, 1
local v = a ~ b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 ~ false
local a, b = 1, false
local v = a ~ b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op true ~ 1
local a, b = true, 1
local v = a ~ b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 ~ true
local a, b = 1, true
local v = a ~ b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false ~ "a"
local a, b = false, "a"
local v = a ~ b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" ~ true
local a, b = "a", true
local v = a ~ b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil << 1
local a, b = nil, 1
local v = a << b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 << nil
local a, b = 1, nil
local v = a << b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil << 2.5
local a, b = nil, 2.5
local v = a << b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 2.5 << nil
local a, b = 2.5, nil
local v = a << b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil << nil
local a, b = nil, nil
local v = a << b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil << "1"
local a, b = nil, "1"
local v = a << b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "1" << nil
local a, b = "1", nil
local v = a << b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil << "a"
local a, b = nil, "a"
local v = a << b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" << nil
local a, b = "a", nil
local v = a << b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil << {}
local a, b = nil, {}
local v = a << b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false << 1
local a, b = false, 1
local v = a << b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 << false
local a, b = 1, false
local v = a << b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op true << 1
local a, b = true, 1
local v = a << b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 << true
local a, b = 1, true
local v = a << b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false << "a"
local a, b = false, "a"
local v = a << b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" << true
local a, b = "a", true
local v = a << b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil >> 1
local a, b = nil, 1
local v = a >> b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 >> nil
local a, b = 1, nil
local v = a >> b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil >> 2.5
local a, b = nil, 2.5
local v = a >> b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 2.5 >> nil
local a, b = 2.5, nil
local v = a >> b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil >> nil
local a, b = nil, nil
local v = a >> b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil >> "1"
local a, b = nil, "1"
local v = a >> b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "1" >> nil
local a, b = "1", nil
local v = a >> b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil >> "a"
local a, b = nil, "a"
local v = a >> b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" >> nil
local a, b = "a", nil
local v = a >> b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil >> {}
local a, b = nil, {}
local v = a >> b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false >> 1
local a, b = false, 1
local v = a >> b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 >> false
local a, b = 1, false
local v = a >> b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op true >> 1
local a, b = true, 1
local v = a >> b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 >> true
local a, b = 1, true
local v = a >> b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false >> "a"
local a, b = false, "a"
local v = a >> b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" >> true
local a, b = "a", true
local v = a >> b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil < 1
local a, b = nil, 1
local v = a < b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 < nil
local a, b = 1, nil
local v = a < b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil < 2.5
local a, b = nil, 2.5
local v = a < b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 2.5 < nil
local a, b = 2.5, nil
local v = a < b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil < nil
local a, b = nil, nil
local v = a < b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil < "1"
local a, b = nil, "1"
local v = a < b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "1" < nil
local a, b = "1", nil
local v = a < b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil < "a"
local a, b = nil, "a"
local v = a < b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" < nil
local a, b = "a", nil
local v = a < b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil < {}
local a, b = nil, {}
local v = a < b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false < 1
local a, b = false, 1
local v = a < b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 < false
local a, b = 1, false
local v = a < b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op true < 1
local a, b = true, 1
local v = a < b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 < true
local a, b = 1, true
local v = a < b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false < "a"
local a, b = false, "a"
local v = a < b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" < true
local a, b = "a", true
local v = a < b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil <= 1
local a, b = nil, 1
local v = a <= b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 <= nil
local a, b = 1, nil
local v = a <= b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil <= 2.5
local a, b = nil, 2.5
local v = a <= b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 2.5 <= nil
local a, b = 2.5, nil
local v = a <= b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil <= nil
local a, b = nil, nil
local v = a <= b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil <= "1"
local a, b = nil, "1"
local v = a <= b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "1" <= nil
local a, b = "1", nil
local v = a <= b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil <= "a"
local a, b = nil, "a"
local v = a <= b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" <= nil
local a, b = "a", nil
local v = a <= b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil <= {}
local a, b = nil, {}
local v = a <= b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false <= 1
local a, b = false, 1
local v = a <= b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 <= false
local a, b = 1, false
local v = a <= b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op true <= 1
local a, b = true, 1
local v = a <= b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 <= true
local a, b = 1, true
local v = a <= b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false <= "a"
local a, b = false, "a"
local v = a <= b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" <= true
local a, b = "a", true
local v = a <= b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil > 1
local a, b = nil, 1
local v = a > b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 > nil
local a, b = 1, nil
local v = a > b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil > 2.5
local a, b = nil, 2.5
local v = a > b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 2.5 > nil
local a, b = 2.5, nil
local v = a > b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil > nil
local a, b = nil, nil
local v = a > b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil > "1"
local a, b = nil, "1"
local v = a > b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "1" > nil
local a, b = "1", nil
local v = a > b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil > "a"
local a, b = nil, "a"
local v = a > b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" > nil
local a, b = "a", nil
local v = a > b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil > {}
local a, b = nil, {}
local v = a > b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false > 1
local a, b = false, 1
local v = a > b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 > false
local a, b = 1, false
local v = a > b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op true > 1
local a, b = true, 1
local v = a > b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 > true
local a, b = 1, true
local v = a > b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false > "a"
local a, b = false, "a"
local v = a > b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" > true
local a, b = "a", true
local v = a > b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil >= 1
local a, b = nil, 1
local v = a >= b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 >= nil
local a, b = 1, nil
local v = a >= b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil >= 2.5
local a, b = nil, 2.5
local v = a >= b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 2.5 >= nil
local a, b = 2.5, nil
local v = a >= b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil >= nil
local a, b = nil, nil
local v = a >= b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil >= "1"
local a, b = nil, "1"
local v = a >= b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "1" >= nil
local a, b = "1", nil
local v = a >= b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil >= "a"
local a, b = nil, "a"
local v = a >= b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" >= nil
local a, b = "a", nil
local v = a >= b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil >= {}
local a, b = nil, {}
local v = a >= b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false >= 1
local a, b = false, 1
local v = a >= b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 >= false
local a, b = 1, false
local v = a >= b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op true >= 1
local a, b = true, 1
local v = a >= b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 >= true
local a, b = 1, true
local v = a >= b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false >= "a"
local a, b = false, "a"
local v = a >= b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" >= true
local a, b = "a", true
local v = a >= b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil .. 1
local a, b = nil, 1
local v = a .. b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 .. nil
local a, b = 1, nil
local v = a .. b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil .. 2.5
local a, b = nil, 2.5
local v = a .. b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 2.5 .. nil
local a, b = 2.5, nil
local v = a .. b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil .. nil
local a, b = nil, nil
local v = a .. b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil .. "1"
local a, b = nil, "1"
local v = a .. b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "1" .. nil
local a, b = "1", nil
local v = a .. b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil .. "a"
local a, b = nil, "a"
local v = a .. b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" .. nil
local a, b = "a", nil
local v = a .. b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil .. {}
local a, b = nil, {}
local v = a .. b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false .. 1
local a, b = false, 1
local v = a .. b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 .. false
local a, b = 1, false
local v = a .. b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op true .. 1
local a, b = true, 1
local v = a .. b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 .. true
local a, b = 1, true
local v = a .. b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false .. "a"
local a, b = false, "a"
local v = a .. b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" .. true
local a, b = "a", true
local v = a .. b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil == 1
local a, b = nil, 1
local v = a == b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 == nil
local a, b = 1, nil
local v = a == b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil == 2.5
local a, b = nil, 2.5
local v = a == b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 2.5 == nil
local a, b = 2.5, nil
local v = a == b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil == nil
local a, b = nil, nil
local v = a == b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil == "1"
local a, b = nil, "1"
local v = a == b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "1" == nil
local a, b = "1", nil
local v = a == b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil == "a"
local a, b = nil, "a"
local v = a == b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" == nil
local a, b = "a", nil
local v = a == b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op nil == {}
local a, b = nil, {}
local v = a == b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false == 1
local a, b = false, 1
local v = a == b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 == false
local a, b = 1, false
local v = a == b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op true == 1
local a, b = true, 1
local v = a == b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op 1 == true
local a, b = 1, true
local v = a == b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op false == "a"
local a, b = false, "a"
local v = a == b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op "a" == true
local a, b = "a", true
local v = a == b
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op -nil
local a = nil
local v = -a
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op -false
local a = false
local v = -a
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op -true
local a = true
local v = -a
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op ~nil
local a = nil
local v = ~a
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op ~false
local a = false
local v = ~a
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op ~true
local a = true
local v = ~a
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op #nil
local a = nil
local v = #a
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op #false
local a = false
local v = #a
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: op #true
local a = true
local v = #a
print(v, type(v) == "number" and math.type(v) or type(v))

-- case: concat-table
print({} .. "x")

-- case: error-call-nil
local f
f()

-- case: error-index-nil
local t
print(t.x)

-- case: error-compare-mixed
print(1 < "x")

-- case: error-string-arg-not-number
print(("abc"):sub("x"))

-- case: error-setmetatable-number
setmetatable(1, {})

-- case: error-after-print
print("before")
local t
t.x = 1
print("after")
