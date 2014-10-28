// {{{ GPL License 

// This file is part of gringo - a grounder for logic programs.
// Copyright (C) 2013  Roland Kaminski

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

// }}}

#include "gringo/bug.hh"
#include "gringo/input/statement.hh"
#include "gringo/input/literals.hh"
#include "gringo/input/aggregates.hh"
#include "gringo/ground/statements.hh"
#include "gringo/safetycheck.hh"

namespace Gringo { namespace Input {

// {{{ helpers for weak constraints

namespace {
    
UTermVec wc_to_args(UTerm &&weight, UTerm &&prioriy, UTermVec &&tuple) {
    UTermVec args;
    args.emplace_back(std::move(weight));
    args.emplace_back(std::move(prioriy));
    std::move(tuple.begin(), tuple.end(), std::back_inserter(args));
    return args;
}

UHeadAggr wc_to_head(UTermVec &&args) {
    assert(args.size() > 1);
    Location loc(args.front()->loc() + args.back()->loc());
    return make_locatable<SimpleHeadLiteral>(loc, make_locatable<PredicateLiteral>(loc, NAF::POS, make_locatable<FunctionTerm>(loc, "#wc", std::move(args))));
}

Term const &wc_term_from_head(UHeadAggr const &head) {
    assert(dynamic_cast<SimpleHeadLiteral*>(head.get()));
    assert(dynamic_cast<PredicateLiteral*>(static_cast<SimpleHeadLiteral&>(*head).lit.get()));
    return *static_cast<PredicateLiteral&>(*static_cast<SimpleHeadLiteral&>(*head).lit).repr;
}

UTermVec wc_args_from_term(Term const &term) {
    if (term.getInvertibility() == Term::CONSTANT) {
        Value x = term.eval();
        assert(x.type() == Value::FUNC);
        UTermVec ret;
        for (Value y : x.args()) { ret.emplace_back(make_locatable<ValTerm>(term.loc(), y)); }
        return ret;
    }
    else {
        assert(dynamic_cast<FunctionTerm const*>(&term));
        return get_clone(dynamic_cast<FunctionTerm const &>(term).args);
    }
}

} // namespace

// }}}

// {{{ definition of Statement::Statement

Statement::Statement(UHeadAggr &&head, UBodyAggrVec &&body, bool external)
    : Statement(std::move(head), std::move(body), external ? Type::EXTERNAL : Type::RULE) { }

Statement::Statement(UHeadAggr &&head, UBodyAggrVec &&body, Type type)
    : head(std::move(head))
    , body(std::move(body))
    , type(type) { }

Statement::Statement(UTerm &&weight, UTerm &&priority, UTermVec &&tuple, UBodyAggrVec &&body)
    : Statement(wc_to_args(std::move(weight), std::move(priority), std::move(tuple)), std::move(body)) {
}

Statement::Statement(UTermVec &&tuple, UBodyAggrVec &&body)
    : Statement(wc_to_head(std::move(tuple)), std::move(body), Type::WEAKCONSTRAINT) { }
// }}}
// {{{ definition of Statement::add

void Statement::add(ULit &&lit) {
    Location loc(lit->loc());
    body.emplace_back(make_locatable<SimpleBodyLiteral>(loc, std::move(lit)));
}

// }}}
// {{{ definition of Statement::print

void Statement::print(std::ostream &out) const {
    if (type != Type::WEAKCONSTRAINT) {
        if (type == Type::EXTERNAL) { out << "#external "; }
        if (head) { out << *head; }
        if (!body.empty()) { 
            out << (type == Type::EXTERNAL ? ":" : ":-");
            auto f = [](std::ostream &out, UBodyAggr const &x) { out << *x; };
            print_comma(out, body, ";", f);
        }
        out << ".";
    }
    else {
        out << ":~"; 
        print_comma(out, body, ";", [](std::ostream &out, UBodyAggr const &x) { out << *x; });
        out << ".[";
        Term const &term = wc_term_from_head(head);
        if (term.getInvertibility() == Term::CONSTANT) {
            FWValVec args = term.eval().args();
            out << args.front() << "@" << *(args.begin() + 1);
            for (auto it = args.begin() + 2, ie = args.end(); it != ie; ++it) { out << "," << *it; }
        }
        else {
            UTermVec const &args = static_cast<FunctionTerm const&>(term).args;
            out << *args.front() << "@" << **(args.begin() + 1);
            for (auto it = args.begin() + 2, ie = args.end(); it != ie; ++it) { out << "," << **it; }
        }
        out << "]";
    }
}

// }}}
// {{{ definition of Statement::isEDB

Value Statement::isEDB() const { return type == Type::RULE && body.empty() ? head->isEDB() : Value(); }

// }}}
// {{{ definition of Statement::unpool

UStmVec Statement::unpool(bool beforeRewrite) {
    UStmVec x;
    UBodyAggrVec body;
    for (auto &y : this->body) { y->unpool(body, beforeRewrite); }
    UHeadAggrVec head;
    this->head->unpool(head, beforeRewrite);
    for (auto &y : head) { x.emplace_back(make_locatable<Statement>(loc(), std::move(y), get_clone(body), type)); }
    return x;
}

// }}}
// {{{ definition of Statement::hasPool

bool Statement::hasPool(bool beforeRewrite) const {
    for (auto &x : body) { if (x->hasPool(beforeRewrite)) { return true; } }
    return head->hasPool(beforeRewrite);
}

// }}}
// {{{ definition of Statement::rewrite1

void Statement::rewrite1(Projections &project) {
    unsigned auxVars = 0;
    Term::DotsMap dots;
    Term::ScriptMap scripts;
    head->simplify(project, dots, scripts, auxVars);
    Term::replace(head, head->shiftHead(body));
    for (auto &y : body) { y->simplify(project, dots, scripts, auxVars); }
    for (auto &y : dots) { body.emplace_back(make_unique<SimpleBodyLiteral>(RangeLiteral::make(y))); }
    for (auto &y : scripts) { body.emplace_back(make_unique<SimpleBodyLiteral>(ScriptLiteral::make(y))); }
}

// }}}
// {{{ definition of Statement::rewrite2

namespace {

void _rewriteAggregates(UBodyAggrVec &body) {
    UBodyAggrVec aggr;
    auto jt = body.begin();
    for (auto it = jt, ie = body.end(); it != ie; ++it) {
        if ((*it)->rewriteAggregates(aggr)) {
            if (it != jt) { *jt = std::move(*it); }
            ++jt;
        }
    }
    body.erase(jt, body.end());
    std::move(aggr.begin(), aggr.end(), std::back_inserter(body));
}

void _rewriteAssignments(UBodyAggrVec &body, Location const &loc, Statement::SplitVec &splits) {
    using LitDep = SafetyChecker<VarTerm*, UBodyAggr>;
    using VarMap = std::unordered_map<FWString, LitDep::VarNode*>;
    unsigned numAssign = 0;
    for (auto &y : body) {
        if (y->isAssignment()) { numAssign++; }
    }
    if (numAssign) {
        LitDep dep;
        VarMap map;
        for (auto &y : body) { 
            auto &ent = dep.insertEnt(std::move(y));
            VarTermBoundVec vars;
            ent.data->collect(vars);
            for (auto &occ : vars) {
                if (occ.first->level == 0) {
                    auto &var(map[occ.first->name]);
                    if (!var)  { var = &dep.insertVar(occ.first); }
                    if (occ.second) { dep.insertEdge(ent, *var); }
                    else            { dep.insertEdge(*var, ent); }
                }
            }
        }
        LitDep::VarVec bound;
        LitDep::EntVec open;
        open.reserve(body.size()); // Note: keeps iterators valid
        body.clear();
        dep.init(open);
        UBodyAggrVec sorted;
        for (auto it = open.begin(); it != open.end(); ) {
            LitDep::EntVec assign;
            for (; it != open.end(); ++it) {
                if (!(*it)->data->isAssignment()) { 
                    dep.propagate(*it, open, &bound);
                    sorted.emplace_back(std::move((*it)->data));
                }
                else { assign.emplace_back(*it); }
            }
            LitDep::EntVec nextAssign;
            while (!assign.empty()) {
                for (auto &x : assign) { 
                    bool allBound = true;
                    for (auto &y : x->provides) {
                        if (!y->bound) {
                            allBound = false;
                            break;
                        }
                    }
                    if (!allBound) { nextAssign.emplace_back(x); }
                    else {
                        x->data->removeAssignment();
                        dep.propagate(x, open, &bound); 
                        sorted.emplace_back(std::move(x->data));
                    }
                }
                if (!nextAssign.empty()) {
                    dep.propagate(nextAssign.back(), open, &bound); 
                    sorted.emplace_back(std::move(nextAssign.back()->data));
                    nextAssign.pop_back();
                }
                assign = std::move(nextAssign);
            }
        }
        UBodyAggrVec done;
        for (auto it = sorted.begin(); it != sorted.end(); ++it) {
            done.emplace_back(std::move(*it));
            if (done.back()->isAssignment() && it + 1 != sorted.end()) { 
                VarTermBoundVec boundVec;
                for (auto &x : done) { x->collect(boundVec); }
                auto lvl = [](VarTermBoundVec::value_type const &a) { return a.first->level != 0; };
                boundVec.erase(std::remove_if(boundVec.begin(), boundVec.end(), lvl), boundVec.end());
                auto cmp = [](VarTermBoundVec::value_type const &a, VarTermBoundVec::value_type const &b) { return *a.first->name < *b.first->name; };
                std::sort(boundVec.begin(), boundVec.end(), cmp);
                auto eq  = [](VarTermBoundVec::value_type const &a, VarTermBoundVec::value_type const &b) { return a.first->name == b.first->name; };
                boundVec.erase(std::unique(boundVec.begin(), boundVec.end(), eq), boundVec.end());
                UTermVec args;
                for (auto &x : boundVec) { args.emplace_back(x.first->clone()); }
                FWString name = "#split" + std::to_string(splits.size());
                ULit split(make_locatable<PredicateLiteral>(loc, NAF::POS, make_locatable<FunctionTerm>(loc, name, std::move(args))));
                splits.emplace_back(get_clone(split), std::move(done));
                done.emplace_back(make_unique<SimpleBodyLiteral>(std::move(split)));
            }
        }
        body = std::move(done);
    }
}

} // namespace

void Statement::rewrite2(SplitVec &splits) {
    unsigned auxVars = 0;
    { // rewrite aggregates
        Term::replace(head, head->rewriteAggregates(body));
        _rewriteAggregates(body);
    }
    { // assign levels
        AssignLevel c;
        head->assignLevels(c);
        for (auto &y : body) { y->assignLevels(c); }
        c.assignLevels();
    }
    { // rewrite arithmetics
        Term::ArithmeticsMap arith;
        Literal::AssignVec assign;
        arith.emplace_back();
        head->rewriteArithmetics(arith, auxVars);
        for (auto &y : body) { y->rewriteArithmetics(arith, assign, auxVars); }
        for (auto &y : arith.back()) { body.emplace_back(make_unique<SimpleBodyLiteral>(RelationLiteral::make(y))); }
        for (auto &y : assign) { body.emplace_back(make_unique<SimpleBodyLiteral>(RelationLiteral::make(y))); }
        arith.pop_back();
    }
    _rewriteAssignments(body, loc(), splits);
}

// }}}
// {{{ definition of Statement::check

bool Statement::check() const {
    bool ret = true;
    ChkLvlVec levels;
    levels.emplace_back(loc(), *this);
    ret = head->check(levels) && ret;
    for (auto &y : body) { ret = y->check(levels) && ret; }
    return levels.back().check() && ret;
}

// }}}
// {{{ definition of Statement::replace

void Statement::replace(Defines &x) { 
    head->replace(x);
    for (auto &y : body) { y->replace(x); }
}

// }}}
// {{{ definition of Statement::toGround

namespace {
    
void toGround(CreateHead &&head, UBodyAggrVec const &body, ToGroundArg &x, Ground::UStmVec &stms) {
    // TODO: remove auxLits during head creation
    CreateBodyVec createVec;
    createVec.emplace_back(std::move(head.second));
    for (auto &y : body) { createVec.emplace_back(y->toGround(x, stms)); }
    Ground::ULitVec lits;
    for (auto current = createVec.begin(), end = createVec.end(); current != end; ++current) {
        current->first(lits, true);
        for (auto &z : current->second) {
            Ground::ULitVec splitLits;
            for (auto it = createVec.begin(); it != end; ++it) {
                if (it != current) { it->first(splitLits, false); }
            }
            stms.emplace_back(z(std::move(splitLits)));
        }
    }
    stms.emplace_back(head.first(std::move(lits)));
}

} // namespace

void Statement::toGround(ToGroundArg &x, Ground::UStmVec &stms) const {
    if (type == Type::WEAKCONSTRAINT) {
            CreateHead hd
            {[this](Ground::ULitVec &&lits) -> Ground::UStm { return make_unique<Ground::WeakConstraint>(wc_args_from_term(wc_term_from_head(head)), std::move(lits)); },
            { [](Ground::ULitVec &, bool) { }, { } }};
        Gringo::Input::toGround(std::move(hd), body, x, stms);
    }
    else {
        Gringo::Input::toGround(head->toGround(x, type==Type::EXTERNAL), body, x, stms);
    }
}

// }}}
// {{{ definition of Statement::~Statement

Statement::~Statement()                     { }

// }}}

} } // namespace Input Gringo



