-- SPDX-License-Identifier: GPL-3.0-or-later
-- Requires Full permission mode: io.popen executes local system commands.
-- 需要“完全”权限模式：io.popen 会执行本机系统命令。
local results = {}
for _, option in ipairs({'-s', '-r', '-m', '-srm'}) do
  local pipe = assert(io.popen('uname ' .. option, 'r'))
  local text = pipe:read('*l')
  local ok = pipe:close()
  assert(ok and text and #text > 0, 'uname failed')
  results[#results + 1] = text
end
return results
