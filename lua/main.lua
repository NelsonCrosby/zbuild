
local yaml = require 'yaml'

return function (name, ...)
    local val1 = assert(yaml.load [[---
- foo: bar
  baz: qux
- boz: buzz
  biz: bees
- [1, 2]: [3, 4]
...]])

    for i, m in ipairs(val1) do
        print(i)
        for k, v in pairs(m) do
            if type(k) == 'string' then
                print('', k, v)
            else
                print('', k[1]..','..k[2], ':', unpack(v))
            end
        end
    end
end
