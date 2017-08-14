
local yaml = require 'yaml'

return function (name, ...)
    local val1, val2, val3 = assert(yaml.load [[---
- foo: ~
  baz: y
- boz: 1
  biz: true
  bees: false
  buzz: "true"
- [01, 0b10]: [0xAB_ef_03, 0_11, -12, 1.0, -3.9, 0]
---
~
---
null
...]])

    print(val2, val3)
    for i, m in ipairs(val1) do
        print(i)
        for k, v in pairs(m) do
            if type(k) == 'string' then
                print('', k, v, type(v))
            else
                print('', unpack(k))
                for i, vx in ipairs(v) do
                    print('', '', vx, type(vx))
                end
            end
        end
    end
end
