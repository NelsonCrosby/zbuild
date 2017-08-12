

return function (name, ...)
    print(name)
    for i, arg in ipairs({...}) do
        print(i, arg)
    end
end
