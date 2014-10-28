#script (python) 

from gringo import *

def on_model(model):
    print "Model: ", model.atoms()

def get(val, default):
    return val if val != None else default

def main(prg):
    imin   = get(prg.get_const("imin"), 1)
    imax   = prg.get_const("imax")
    istop  = get(prg.get_const("istop"), "SAT")
    iquery = get(prg.get_const("iquery"), 1)
    
    step = 1
    
    parts = []
    parts.append(("base", []))
    while True:
        if imax != None and step > imax: break
        parts.append(("cumulative", [step]))
        if step >= iquery: 
            parts.append(("volatile", [step]))
        prg.ground(parts)
        parts = []
        prg.release_external(Fun("query", [step-1]))
        if step >= iquery: 
            prg.assign_external(Fun("query", [step]), True)
        ret = prg.solve(None, on_model)
        if step >= imin and ((istop == "SAT" and ret == SolveResult.SAT) or (istop == "UNSAT" and ret != SolveResult.SAT)): break
        step = step+1
#end.
