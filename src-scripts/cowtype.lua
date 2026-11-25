-- SPDX-License-Identifier: GPL-3.0-or-later
-- Fcitx imeapi bridge. User code is compiled as text in a fresh environment.
local compile, protected, hook = load, pcall, debug and debug.sethook
local date, clock = os.date, os.time
local limit_error = {}
local function failure(reason, line)
    return {Status='error', Reason=reason, Line=tostring(line or 0)}
end
local function copy(source)
    local target = {}
    for key, value in pairs(source) do target[key] = value end
    return target
end
function cowtype_script(request)
    if type(request) ~= 'string' or #request > 65605 or type(hook) ~= 'function' then
        return failure('protocol')
    end
    local mode, independent, trigger, source = request:match('^(%d)\n([01])\n([a-z]+)\n(.*)$')
    if mode ~= '1' and mode ~= '2' then return failure('protocol') end
    local env
    if mode == '1' then
        env = {assert=assert, error=error, ipairs=ipairs, pairs=pairs, next=next,
               select=select, tonumber=tonumber, tostring=tostring, type=type,
               math=copy(math), string=copy(string), table=copy(table), utf8=copy(utf8 or {}),
               os={date=date, time=clock}, input=trigger}
        -- No dynamic loading, I/O, module loader or debug API.
    else
        env = setmetatable({input=trigger}, {__index=_G})
    end
    local chunk, compile_error = compile(source, '@cowtype-script', 't', env)
    if not chunk then return failure('syntax', tonumber(compile_error:match('cowtype%-script:(%d+):'))) end
    hook(function() error(limit_error, 0) end, '', 100000)
    local ok, values = protected(chunk)
    hook()
    if not ok then
        if values == limit_error then return failure('limit') end
        local message = type(values) == 'string' and values or ''
        local line = tonumber(message:match('cowtype%-script:(%d+):'))
        if mode == '1' and (message:find("global 'io'",1,true) or message:find("global 'require'",1,true)
           or message:find("global 'debug'",1,true) or message:find("field 'execute'",1,true)
           or message:find("field 'getenv'",1,true) or message:find("global 'load'",1,true)) then
            return failure('permission', line)
        end
        if message:find('assertion failed',1,true) then return failure('assertion', line) end
        if message:find('nil value',1,true) then return failure('undefined', line) end
        if message:find('bad argument',1,true) or message:find('attempt to',1,true) then return failure('type', line) end
        return failure('runtime', line)
    end
    if type(values) == 'string' then
        if independent ~= '1' then return failure('rank_required') end
        values = {values}
    end
    if type(values) == 'table' and type(rawget(values, 1)) == 'number' then values = {values} end
    if type(values) ~= 'table' then return failure('return_type') end
    local result = {Status='ok', Length='0'}
    local length = 0
    for i = 1, 12 do
        local value = rawget(values, i)
        if value == nil then break end
        local priority = i
        if type(value) == 'table' then
            priority, value = rawget(value, 1), rawget(value, 2)
            if type(priority) ~= 'number' or priority ~= priority or math.abs(priority) == math.huge then
                return failure('priority')
            end
        elseif independent ~= '1' then return failure('rank_required') end
        if type(value) ~= 'string' then return failure('text_type') end
        if #value > 4096 then return failure('text_size') end
        if value:find('%z') then return failure('text_nul') end
        if #value > 0 then
            result[tostring(length)] = {Text=value, Priority=tostring(priority)}
            length = length + 1
        end
    end
    result.Length = tostring(length)
    return result
end
