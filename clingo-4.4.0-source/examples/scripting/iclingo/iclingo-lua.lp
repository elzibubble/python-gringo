#script (lua) 

function map(func, array)
    local new_array = {}
    for i,v in ipairs(array) do
        new_array[i] = func(v)
    end
    return new_array
end

function on_model(m)
    print("Model: {"..table.concat(map(tostring, m:atoms()), ",").."}")
end

function get(val, default)
    if val ~= nil then 
        return val 
    else 
        return default 
    end
end

function main(prg)
    imin   = get(prg:get_const("imin"), 1)
    imax   = prg:get_const("imax")
    istop  = get(prg:get_const("istop"), "SAT")
    iquery = get(prg:get_const("iquery"), 1)
    step   = 1

    parts = {}
    parts[#parts+1] = {"base", {}}
    while true do
        if imax ~= nil and step > imax then break end
        parts[#parts+1] = {"cumulative", {step}}
        ret = gringo.SolveResult.UNKNOWN
        if step >= iquery then
            parts[#parts+1] = {"volatile", {step}}
        end
        prg:ground(parts)
        prg:release_external(gringo.Fun("query", {step-1}))
        if step >= iquery then
            prg:assign_external(gringo.Fun("query", {step}), true)
            parts = {}
        end
        ret = prg:solve()
        if step >= imin and ((istop == "SAT" and ret == gringo.SolveResult.SAT) or (istop == "UNSAT" and ret == gringo.SolveResult.UNSAT)) then
            break
        end
        step = step+1
    end
end

#end.
