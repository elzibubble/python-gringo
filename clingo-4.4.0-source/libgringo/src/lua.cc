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

#ifdef WITH_LUA

#include "gringo/lua.hh"
#include "gringo/version.hh"
#include "gringo/logger.hh"
#include "gringo/control.hh"

#include <lua.hpp>
#include <cstring>

namespace Gringo {

namespace {

// {{{ auxiliary functions

#define VALUE_CMP(TYPE) \
static int eq##TYPE(lua_State *L) { \
    Value *a = static_cast<Value*>(luaL_checkudata(L, 1, "gringo."#TYPE)); \
    Value *b = static_cast<Value*>(luaL_checkudata(L, 2, "gringo."#TYPE)); \
    lua_pushboolean(L, *a == *b); \
    return 1; \
} \
static int lt##TYPE(lua_State *L) { \
    Value *a = static_cast<Value*>(luaL_checkudata(L, 1, "gringo."#TYPE)); \
    Value *b = static_cast<Value*>(luaL_checkudata(L, 2, "gringo."#TYPE)); \
    lua_pushboolean(L, *a <= *b); \
    return 1; \
} \
static int le##TYPE(lua_State *L) { \
    Value *a = static_cast<Value*>(luaL_checkudata(L, 1, "gringo."#TYPE)); \
    Value *b = static_cast<Value*>(luaL_checkudata(L, 2, "gringo."#TYPE)); \
    lua_pushboolean(L, *a < *b); \
    return 1; \
}

template <typename R, typename T>
R protect(lua_State *L, T f) {
    try                             { return f(); }
    catch (std::exception const &e) { luaL_error(L, e.what()); }
    catch (...)                     { luaL_error(L, "unknown error"); }
    throw std::runtime_error("cannot happen");
}

struct AnyWrap {
    template <class T> 
    static T *new_(lua_State *L) {
        void *data = lua_newuserdata(L, sizeof(Gringo::Any));
        Gringo::Any *ret = new (data) Any();
        luaL_getmetatable(L, "gringo._Any");
        lua_setmetatable(L, -2);
        protect<void>(L, [ret] { *ret = Any(T()) ; });
        return ret->get<T>();
    }
    static int gc(lua_State *L) {
        Any* del = (Any*)lua_touserdata(L, 1);
        del->~Any();
        return 0;
    }
    static luaL_Reg const meta[];
};

luaL_Reg const AnyWrap::meta[] = {
    {"__gc", gc},
    {nullptr, nullptr}
};

Value luaToVal(lua_State *L, int idx) {
    int type = lua_type(L, idx);
    switch(type) {
        case LUA_TSTRING: {
            char const *name = lua_tostring(L, idx);
            return protect<Value>(L, [name]() { return Value(name, false); });
        }
        case LUA_TNUMBER: {
            int num = lua_tointeger(L, idx);
            return Value(num);
        }
        case LUA_TUSERDATA: {
            auto check = [L, idx]() -> bool {
                if (lua_getmetatable(L, idx)) {
                    lua_getfield(L, LUA_REGISTRYINDEX, "gringo.Fun");
                    if (lua_rawequal(L, -1, -2)) {
                        lua_pop(L, 2);
                        return true;
                    }
                    lua_pop(L, 1);
                    lua_getfield(L, LUA_REGISTRYINDEX, "gringo.Sup");
                    if (lua_rawequal(L, -1, -2)) {
                        lua_pop(L, 2);
                        return true;
                    }
                    lua_pop(L, 1);
                    lua_getfield(L, LUA_REGISTRYINDEX, "gringo.Inf");
                    if (lua_rawequal(L, -1, -2)) {
                        lua_pop(L, 2);
                        return true;
                    }
                    lua_pop(L, 1);
                }
                return false;
            };
            if (check()) { return *(Value*)lua_touserdata(L, idx); }
        }
        default: { luaL_error(L, "cannot convert to value"); }
    }
    throw std::runtime_error("cannot happen");
}
void valToLua(lua_State *L, Value v) {
    switch (v.type()) {
        case Value::ID:
        case Value::FUNC: {
            *(Value*)lua_newuserdata(L, sizeof(Value)) = v;
            luaL_getmetatable(L, "gringo.Fun");
            lua_setmetatable(L, -2);
            break;
        }
        case Value::SUP: {
            *(Value*)lua_newuserdata(L, sizeof(Value)) = v;
            luaL_getmetatable(L, "gringo.Sup");
            lua_setmetatable(L, -2);
            break;
        }
        case Value::INF: {
            *(Value*)lua_newuserdata(L, sizeof(Value)) = v;
            luaL_getmetatable(L, "gringo.Inf");
            lua_setmetatable(L, -2);
            break;
        }
        case Value::NUM: {
            lua_pushnumber(L, v.num());
            break;
        }
        case Value::STRING: {
            lua_pushstring(L, protect<char const *>(L, [v](){ return (*v.string()).c_str(); }));
            break;
        }
        default: { luaL_error(L, "cannot happen"); }
    }
}

#if LUA_VERSION_NUM < 502

int lua_absindex(lua_State *L, int idx) {
    return (idx < 0) ? lua_gettop(L) + idx + 1 : idx;
}

#endif

ValVec *luaToVals(lua_State *L, int idx) {
    idx = lua_absindex(L, idx);
    luaL_checktype(L, idx, LUA_TTABLE);
    ValVec *vals = AnyWrap::new_<ValVec>(L);
    lua_pushnil(L);
    while (lua_next(L, idx) != 0) {
        Value val = luaToVal(L, -1);
        protect<void>(L, [val,&vals](){ vals->push_back(val); });
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return vals;
}

bool handleError(lua_State *L, Location const &loc, int code, char const *desc) {
    switch (code) {
        case LUA_ERRSYNTAX: {
            std::string msg(lua_tostring(L, -1));
            lua_pop(L, 1);
            GRINGO_REPORT(W_TERM_UNDEFINED)
                << loc << ": warning: " << desc << ":\n"
                << "  SyntaxError: "
                << msg << "\n"
                ;
            return false;
        }
        case LUA_ERRRUN:
        case LUA_ERRERR: {
            std::string msg(lua_tostring(L, -1));
            lua_pop(L, 1);
            GRINGO_REPORT(W_TERM_UNDEFINED)
                << loc << ": warning: " << desc << ":\n"
                << "  RuntimeError: "
                << msg << "\n"
                ;
            return false;
        }
        case LUA_ERRMEM: { throw std::runtime_error("lua interpreter ran out of memory"); }
    }
    return true;
}

static int luaTraceback (lua_State *L);

#if LUA_VERSION_NUM < 502

#define PACKAGE_KEY "gringo._PackageTable"

static void push_package_table (lua_State *L) {
    lua_pushliteral(L, PACKAGE_KEY);
    lua_rawget(L, LUA_REGISTRYINDEX);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        // try to get package table from globals
        lua_pushliteral(L, "package");
        lua_rawget(L, LUA_GLOBALSINDEX);
        if (lua_istable(L, -1)) {
            lua_pushliteral(L, PACKAGE_KEY);
            lua_pushvalue(L, -2);
            lua_rawset(L, LUA_REGISTRYINDEX);
        }
    }
}
void lua_getuservalue (lua_State *L, int i) {
    luaL_checktype(L, i, LUA_TUSERDATA);
    luaL_checkstack(L, 2, "not enough stack slots");
    lua_getfenv(L, i);
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    if (lua_rawequal(L, -1, -2)) {
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_replace(L, -2);
    } else {
        lua_pop(L, 1);
        push_package_table(L);
        if (lua_rawequal(L, -1, -2)) {
            lua_pop(L, 1);
            lua_pushnil(L);
            lua_replace(L, -2);
        } else
            lua_pop(L, 1);
    }
}

void lua_setuservalue (lua_State *L, int i) {
    luaL_checktype(L, i, LUA_TUSERDATA);
    if (lua_isnil(L, -1)) {
        luaL_checkstack(L, 1, "not enough stack slots");
        lua_pushvalue(L, LUA_GLOBALSINDEX);
        lua_replace(L, -2);
    }
    lua_setfenv(L, i);
}

#endif

void lua_unsetuservaluefield(lua_State *L, int i, char const *field) {
    lua_getuservalue(L, i);       // stack +1
    if (!lua_isnil(L, -1)) {
        lua_pushstring(L, field); // stack +2
        lua_pushnil(L);           // stack +3
        lua_rawset(L, -3);        // stack +1
    }
    lua_pop(L, 1);                // stack +0
}

// }}}
// {{{ wrap Inf

struct Inf {
    static int new_(lua_State *L) {
        *(Value*)lua_newuserdata(L, sizeof(Value)) = Value(true);
        luaL_getmetatable(L, "gringo.Inf");
        lua_setmetatable(L, -2);
        return 1;
    }
    static int toString(lua_State *L) {
        luaL_checkudata(L, 1, "gringo.Inf");
        lua_pushliteral(L, "#inf");
        return 1;
    }
    VALUE_CMP(Inf)
    static luaL_Reg const meta[];
};

luaL_Reg const Inf::meta[] = {
    {"__tostring", toString},
    {"__eq", eqInf},
    {"__lt", ltInf},
    {"__le", leInf},
    {nullptr, nullptr}
};

// }}}
// {{{ wrap Sup

struct Sup {
    static int new_(lua_State *L) {
        *(Value*)lua_newuserdata(L, sizeof(Value)) = Value(false);
        luaL_getmetatable(L, "gringo.Sup");
        lua_setmetatable(L, -2);
        return 1;
    }
    VALUE_CMP(Sup)
    static int toString(lua_State *L) {
        luaL_checkudata(L, 1, "gringo.Sup");
        lua_pushliteral(L, "#sup");
        return 1;
    }
    static luaL_Reg const meta[];
};

luaL_Reg const Sup::meta[] = {
    {"__tostring", toString},
    {"__eq", eqSup},
    {"__lt", ltSup},
    {"__le", leSup},
    {nullptr, nullptr}
};

// }}}
// {{{ wrap Fun

struct Fun {
    static int newFun(lua_State *L) {
        char const *name = luaL_checklstring(L, 1, nullptr);
        if (name[0] == '\0') { luaL_argerror(L, 2, "function symbols must have a non-empty name"); }
        if (lua_isnone(L, 2) || lua_isnil(L, 2)) {
            *(Value*)lua_newuserdata(L, sizeof(Value)) = protect<Value>(L, [name](){ return Value(name); });
            luaL_getmetatable(L, "gringo.Fun");
            lua_setmetatable(L, -2);
        }
        else {
            ValVec *vals = luaToVals(L, 2);
            *(Value*)lua_newuserdata(L, sizeof(Value)) = protect<Value>(L, [name, vals](){ return vals->empty() ? Value(name) : Value(name, *vals); });
            luaL_getmetatable(L, "gringo.Fun");
            lua_setmetatable(L, -2);
        }
        return 1;
    }
    static int newTuple(lua_State *L) {
        ValVec *vals = luaToVals(L, 1);
        if (vals->size() < 2) { luaL_argerror(L, 1, "tuples must have at least two values"); }
        *(Value*)lua_newuserdata(L, sizeof(Value)) = protect<Value>(L, [vals](){ return Value("", *vals); });
        luaL_getmetatable(L, "gringo.Fun");
        lua_setmetatable(L, -2);
        return 1;
    }
    VALUE_CMP(Fun)
    static int name(lua_State *L) {
        Value val = *(Value*)luaL_checkudata(L, 1, "gringo.Fun");
        lua_pushstring(L, protect<const char*>(L, [val]() { return (*val.name()).c_str(); }));
        return 1;
    }
    static int args(lua_State *L) {
        Value val = *(Value*)luaL_checkudata(L, 1, "gringo.Fun");
        lua_createtable(L, val.args().size(), 0);
        if (val.type() == Value::FUNC) {
            int i = 1;
            for (auto &x : val.args()) {
                valToLua(L, x);
                lua_rawseti(L, -2, i++);
            }
        }
        return 1;
    }
    static int toString(lua_State *L) {
        std::string *rep = AnyWrap::new_<std::string>(L);
        Value val = *(Value*)luaL_checkudata(L, 1, "gringo.Fun");
        lua_pushstring(L, protect<const char*>(L, [val, rep]() {
            std::ostringstream oss;
            oss << val;
            *rep = oss.str();
            return rep->c_str();
        }));
        return 1;
    }
    static luaL_Reg const meta[];
};

luaL_Reg const Fun::meta[] = {
    {"__tostring", toString},
    {"name", name},
    {"args", args},
    {"__eq", eqFun},
    {"__lt", ltFun},
    {"__le", leFun},
    {nullptr, nullptr}
};

// }}}
// {{{ wrap SolveControl

struct SolveControl {
    static int getClause(lua_State *L, bool invert) {
        Model const *& model =  *(Model const **)luaL_checkudata(L, 1, "gringo.SolveControl");
        Gringo::Model::LitVec *lits = AnyWrap::new_<Gringo::Model::LitVec>(L);
        luaL_checktype(L, 2, LUA_TTABLE);
        lua_pushnil(L);
        while (lua_next(L, 2)) {
            luaL_checktype(L, -1, LUA_TTABLE);
            lua_pushnil(L);
            if (!lua_next(L, -2)) { luaL_error(L, "atom/boolean pair expected"); }
            Value atom = luaToVal(L, -1);
            lua_pop(L, 1);
            if (!lua_next(L, -2)) { luaL_error(L, "atom/boolean pair expected"); }
            bool truth = lua_toboolean(L, -1);
            lua_pop(L, 3);
            protect<void>(L, [invert, atom, truth, lits](){ lits->emplace_back(truth ^ invert, atom); });
        }
        lua_pop(L, 1);
        protect<void>(L, [model, lits](){ model->addClause(*lits); });
        return 0;
    }
    static int add_clause(lua_State *L) {
        return getClause(L, false);
    }
    static int add_nogood(lua_State *L) {
        return getClause(L, true);
    }
    static luaL_Reg const meta[];
};

luaL_Reg const SolveControl::meta[] = {
    {"add_clause", add_clause},
    {"add_nogood", add_nogood},
    {nullptr, nullptr}
};

// }}}
// {{{ wrap Model

struct Model {
    static int contains(lua_State *L) {
        Gringo::Model const *& model =  *(Gringo::Model const **)luaL_checkudata(L, 1, "gringo.Model");
        Value val = luaToVal(L, 2);
        lua_pushboolean(L, protect<bool>(L, [val, model]() { return model->contains(val); }));
        return 1;
    }
    static int atoms(lua_State *L) {
        Gringo::Model const *& model = *(Gringo::Model const **)luaL_checkudata(L, 1, "gringo.Model");
        int atomset = Gringo::Model::SHOWN;
        if (lua_isnumber (L, 2)) { atomset = luaL_checkinteger(L, 2); }
        ValVec *atoms = AnyWrap::new_<ValVec>(L);
        protect<void>(L, [&model, atoms, atomset]() { *atoms = model->atoms(atomset); });
        lua_createtable(L, atoms->size(), 0);
        int i = 1;
        for (auto x : *atoms) {
            valToLua(L, x);
            lua_rawseti(L, -2, i++);
        }
        return 1;
    }
    static int optimization(lua_State *L) {
        Gringo::Model const *& model = *(Gringo::Model const **)luaL_checkudata(L, 1, "gringo.Model");
        Int64Vec *values = AnyWrap::new_<Int64Vec>(L);
        protect<void>(L, [&model, values]() { *values = model->optimization(); });
        lua_createtable(L, values->size(), 0);
        int i = 1;
        for (auto x : *values) {
            lua_pushinteger(L, x);
            lua_rawseti(L, -2, i++);
        }
        return 1;
    }
    static int toString(lua_State *L) {
        Gringo::Model const *& model =  *(Gringo::Model const **)luaL_checkudata(L, 1, "gringo.Model");
        std::string *rep = AnyWrap::new_<std::string>(L);
        lua_pushstring(L, protect<char const *>(L, [model, rep]() {
            auto printAtom = [](std::ostream &out, Value val) {
                if (val.type() == Value::FUNC && *val.sig() == Signature("$", 2)) { out << val.args().front() << "=" << val.args().back(); }
                else { out << val; }
            };
            std::ostringstream oss;
            print_comma(oss, model->atoms(Gringo::Model::SHOWN), " ", printAtom);
            *rep = oss.str();
            return rep->c_str();
        }));
        return 1;
    }
    static int index(lua_State *L) {
        auto &model = *(Gringo::Model const **)luaL_checkudata(L, 1, "gringo.Model");
        char const *name = luaL_checkstring(L, 2);
        if (strcmp(name, "context") == 0) {
            *(Gringo::Model const **)lua_newuserdata(L, sizeof(Gringo::Model*)) = model;
            luaL_getmetatable(L, "gringo.SolveControl");
            lua_setmetatable(L, -2);
        }
        else {
            lua_getmetatable(L, 1);
            lua_getfield(L, -1, name);
        }
        return 1;
    }
    static luaL_Reg const meta[];
};

luaL_Reg const Model::meta[] = {
    {"__tostring", toString},
    {"atoms", atoms},
    {"optimization", optimization},
    {"contains", contains},
    {nullptr, nullptr}
};

// }}}
// {{{ wrap Statistics

int newStatistics(lua_State *L, Statistics const *stats) {
    char const *prefix = lua_tostring(L, -1); // stack + 1
    auto ret           = protect<Statistics::Quantity>(L, [stats, prefix]{ return stats->getStat(prefix); });
    switch (ret.error()) {
        case Statistics::error_none: {
            lua_pop(L, 1);
            lua_pushnumber(L, (double)ret);
            return 1;
        }
        case Statistics::error_not_available: {
            return luaL_error(L, "error_not_available: %s", prefix);
        }
        case Statistics::error_unknown_quantity: {
            return luaL_error(L, "error_unknown_quantity: %s", prefix);
        }
        case Statistics::error_ambiguous_quantity: {
            char const *keys = protect<char const *>(L, [stats, prefix]() { return stats->getKeys(prefix); });
            if (!keys) { luaL_error(L, "error zero keys string: %s", prefix); }
            lua_newtable(L); // stack + 2
            for (char const *it = keys; *it; it+= strlen(it) + 1) {
                if (strcmp(it, "__len") == 0) {
                    char const *lenPrefix = lua_tostring(L, -1);
                    int len = (int)protect<double>(L, [stats, lenPrefix]{ return stats->getStat(lenPrefix); });
                    for (int i = 1; i <= len; ++i) {
                        lua_pushvalue(L, -2);
                        lua_pushinteger(L, i-1);
                        lua_pushliteral(L, ".");
                        lua_concat(L, 3);        // stack + 3
                        newStatistics(L, stats); // stack + 3
                        lua_rawseti(L, -2, i);   // stack + 2
                    }
                    break;
                }
                else {
                    int len = strlen(it);
                    lua_pushlstring(L, it, len - (it[len-1] == '.')); // stack + 3
                    lua_pushvalue(L, -3);
                    lua_pushstring(L, it);
                    lua_concat(L, 2);        // stack + 4
                    newStatistics(L, stats); // stack + 4
                    lua_rawset(L, -3);       // stack + 2
                }
            }
            lua_replace(L, -2);
            return 1;
        }

    }
    assert(false);
    return 1;
}

// }}}
// {{{ wrap SolveFuture

struct SolveFuture {
    static int get(lua_State *L) {
        Gringo::SolveFuture *& future = *(Gringo::SolveFuture **)luaL_checkudata(L, 1, "gringo.SolveFuture");
        lua_pushnumber(L, protect<int>(L, [future]() { return (int)future->get(); }));
        return 1;
    }
    static int wait(lua_State *L) {
        Gringo::SolveFuture *& future = *(Gringo::SolveFuture **)luaL_checkudata(L, 1, "gringo.SolveFuture");
        if (lua_isnone(L, 2) == 0) {
            double timeout = luaL_checknumber(L, 2);
            lua_pushboolean(L, protect<bool>(L, [future, timeout]() { return future->wait(timeout); }));
            return 1;
        }
        else {
            protect<void>(L, [future]() { future->wait(); });
            return 0;
        }
    }
    static int interrupt(lua_State *L) {
        Gringo::SolveFuture *& future = *(Gringo::SolveFuture **)luaL_checkudata(L, 1, "gringo.SolveFuture");
        protect<void>(L, [future]() { future->interrupt(); });
        return 0;
    }
    static luaL_Reg const meta[];
};

luaL_Reg const SolveFuture::meta[] = {
    {"get",  get},
    {"wait", wait},
    {"interrupt", interrupt},
    {nullptr, nullptr}
};

// }}}
// {{{ wrap SolveIter

struct SolveIter {
    static int close(lua_State *L) {
        Gringo::SolveIter *& iter = *(Gringo::SolveIter **)luaL_checkudata(L, 1, "gringo.SolveIter");
        protect<void>(L, [iter]() { iter->close(); });
        return 0;
    }
    static int next(lua_State *L) {
        Gringo::SolveIter *& iter = *(Gringo::SolveIter **)luaL_checkudata(L, lua_upvalueindex(1), "gringo.SolveIter");
        Gringo::Model const *m = protect<Gringo::Model const *>(L, [iter]() { return iter->next(); });
        if (m) {
            *(Gringo::Model const **)lua_newuserdata(L, sizeof(Gringo::Model*)) = m;
            luaL_getmetatable(L, "gringo.Model");
            lua_setmetatable(L, -2);
        }
        else   { lua_pushnil(L); }
        return 1;
    }
    static int iter(lua_State *L) {
        luaL_checkudata(L, 1, "gringo.SolveIter");
        lua_pushvalue(L,1);
        lua_pushcclosure(L, next, 1);
        return 1;
    }
    static int get(lua_State *L) {
        Gringo::SolveIter *& iter = *(Gringo::SolveIter **)luaL_checkudata(L, 1, "gringo.SolveIter");
        lua_pushnumber(L, protect<int>(L, [iter]() { return (int)iter->get(); }));
        return 1;
    }
    static luaL_Reg const meta[];
};

luaL_Reg const SolveIter::meta[] = {
    {"iter",  iter},
    {"close", close},
    {"get", get},
    {nullptr, nullptr}
};

// }}}
// {{{ wrap ConfigProxy

struct ConfigProxy {
    unsigned key;
    int nSubkeys;
    int arrLen;
    int nValues;
    char const* help;
    Gringo::ConfigProxy *proxy;

    static int new_(lua_State *L, unsigned key, Gringo::ConfigProxy &proxy) {
        ConfigProxy &self = *(ConfigProxy*)lua_newuserdata(L, sizeof(ConfigProxy));
        self.proxy = &proxy;
        self.key   = key;
        protect<void>(L, [&self] { self.proxy->getKeyInfo(self.key, &self.nSubkeys, &self.arrLen, &self.help, &self.nValues); });
        luaL_getmetatable(L, "gringo.ConfigProxy");
        lua_setmetatable(L, -2);
        return 1;
    }

    static int keys(lua_State *L) {
        auto &self = *(ConfigProxy *)luaL_checkudata(L, 1, "gringo.ConfigProxy");
        if (self.nSubkeys < 0) { return 0; }
        else {
            lua_createtable(L, self.nSubkeys, 0);
            for (int i = 0; i < self.nSubkeys; ++i) {
                char const *key = protect<char const *>(L, [&self, i] { return self.proxy->getSubKeyName(self.key, i); });
                lua_pushstring(L, key);
                lua_rawseti(L, -2, i+1);
            }
            return 1;
        }
    }

    static int index(lua_State *L) {
        auto &self = *(ConfigProxy *)luaL_checkudata(L, 1, "gringo.ConfigProxy");
        char const *name = luaL_checkstring(L, 2);
        bool desc = strncmp("__desc_", name, 7) == 0;
        if (desc) { name += 7; }
        unsigned key;
        bool hasSubKey = protect<bool>(L, [self, name, &key] { return self.proxy->hasSubKey(self.key, name, &key); });
        if (hasSubKey) {
            new_(L, key, *self.proxy);
            auto &sub = *(ConfigProxy *)lua_touserdata(L, -1);
            if (desc) {
                lua_pushstring(L, sub.help);
                return 1;
            }
            else if (sub.nValues < 0) { return 1; }
            else {
                std::string *value = AnyWrap::new_<std::string>(L);
                bool ret = protect<bool>(L, [&sub, value]() { return sub.proxy->getKeyValue(sub.key, *value); });
                if (ret) {
                    lua_pushstring(L, value->c_str());
                    return 1;
                }
                return 0;
            }
        }
        lua_getmetatable(L, 1);
        lua_getfield(L, -1, name);
        return 1;
    }

    static int newindex(lua_State *L) {
        auto &self = *(ConfigProxy *)luaL_checkudata(L, 1, "gringo.ConfigProxy");
        char const *name = luaL_checkstring(L, 2);
        unsigned key;
        bool hasSubKey = protect<bool>(L, [self, name, &key] { return self.proxy->hasSubKey(self.key, name, &key); });
        if (hasSubKey) {
            const char *value = lua_tostring(L, 3);
            protect<void>(L, [self, key, value]() { self.proxy->setKeyValue(key, value); });
            lua_pushstring(L, value);
            return 1;
        }
        return luaL_error(L, "unknown field: %s", name);
    }

    static int next(lua_State *L) {
        auto &self = *(ConfigProxy *)luaL_checkudata(L, lua_upvalueindex(1), "gringo.ConfigProxy");
        int index = luaL_checkinteger(L, lua_upvalueindex(2));
        lua_pushnumber(L, index + 1);
        lua_replace(L, lua_upvalueindex(2));
        if (index < self.arrLen) {
            unsigned key = protect<unsigned>(L, [&self, index]() { return self.proxy->getArrKey(self.key, index); });
            return new_(L, key, *self.proxy);
        }
        else {
            lua_pushnil(L);
            return 1;
        }
    }

    static int iter(lua_State *L) {
        luaL_checkudata(L, 1, "gringo.ConfigProxy");
        lua_pushvalue(L, 1);
        lua_pushnumber(L, 0);
        lua_pushcclosure(L, next, 2);
        return 1;
    }

    static int len(lua_State *L) {
        auto &self = *(ConfigProxy *)luaL_checkudata(L, 1, "gringo.ConfigProxy");
        lua_pushnumber(L, self.arrLen);
        return 1;
    }
    static luaL_Reg const meta[];
};

luaL_Reg const ConfigProxy::meta[] = {
    {"keys", keys},
    {"__len", len},
    {"iter", iter},
    {nullptr, nullptr}
};

// }}}
// {{{ wrap ControlWrap

struct LuaClear {
    LuaClear(lua_State *L) : L(L), n(lua_gettop(L)) { }
    ~LuaClear() { lua_settop(L, n); }
    lua_State *L;
    int n;
};

struct LuaContext {
    int idx;
};

struct ControlWrap {
    static Control::NewControlFunc  newControl;
    static Control::FreeControlFunc freeControl;
    static void checkBlocked(lua_State *L, Control *ctl, char const *function) {
        if (protect<bool>(L, [ctl]() { return ctl->blocked(); })) { luaL_error(L, "Control.%s must not be called during solve call", function); }
    }
    static int ground(lua_State *L) {
        auto &ctl =  *(Control **)luaL_checkudata(L, 1, "gringo.Control");
        checkBlocked(L, ctl, "ground");
        luaL_checktype(L, 2, LUA_TTABLE);
        LuaContext context = { !lua_isnone(L, 3) && !lua_isnil(L, 3) ? 3 : 0 };
        if (context.idx) { luaL_checktype(L, context.idx, LUA_TTABLE); }
        Control::GroundVec *vec = AnyWrap::new_<Control::GroundVec>(L);
        lua_pushnil(L);
        while (lua_next(L, 2) != 0) {
            luaL_checktype(L, -1, LUA_TTABLE);
            lua_pushnil(L);
            if (lua_next(L, -2) == 0) { luaL_error(L, "tuple of name and arguments expected"); }
            char const *name = luaL_checkstring(L, -1);
            lua_pop(L, 1);
            if (lua_next(L, -2) == 0) { luaL_error(L, "tuple of name and arguments expected"); }
            ValVec *args = luaToVals(L, -1);
            lua_pop(L, 1);
            if (lua_next(L, -2) != 0) { luaL_error(L, "tuple of name and arguments expected"); }
            protect<void>(L, [name, args, vec](){ vec->emplace_back(name, *args); });
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        protect<void>(L, [ctl, vec, context]() { ctl->ground(*vec, context.idx ? Gringo::Any(context) : Any()); });
        return 0;
    }
    static int add(lua_State *L) {
        auto &ctl = *(Control **)luaL_checkudata(L, 1, "gringo.Control");
        checkBlocked(L, ctl, "add");
        char const *name = luaL_checkstring(L, 2);
        luaL_checktype(L, 3, LUA_TTABLE);
        char const *prg = luaL_checkstring(L, 4);
        FWStringVec *vals = AnyWrap::new_<FWStringVec>(L);
        lua_pushnil(L);
        while (lua_next(L, 3) != 0) {
            char const *val = luaL_checkstring(L, -1);
            protect<void>(L, [val,&vals](){ vals->push_back(val); });
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        protect<void>(L, [ctl, name, vals, prg]() { ctl->add(name, *vals, prg); });
        return 0;
    }
    static int load(lua_State *L) {
        auto &ctl = *(Control **)luaL_checkudata(L, 1, "gringo.Control");
        checkBlocked(L, ctl, "load");
        char const *filename = luaL_checkstring(L, 2);
        protect<void>(L, [ctl, filename]() { ctl->load(filename); });
        return 0;
    }
    static int get_const(lua_State *L) {
        auto &ctl = *(Control **)luaL_checkudata(L, 1, "gringo.Control");
        checkBlocked(L, ctl, "get_const");
        char const *name = luaL_checkstring(L, 2);
        Value ret = protect<Value>(L, [ctl, name]() { return ctl->getConst(name); });
        if (ret.type() == Value::SPECIAL) { lua_pushnil(L); }
        else                              { valToLua(L, ret); }
        return 1;
    }

    static Control::Assumptions *getAssumptions(lua_State *L, int assIdx) {
        Control::Assumptions *ass = AnyWrap::new_<Control::Assumptions>(L);
        if (assIdx) {
            luaL_checktype(L, assIdx, LUA_TTABLE);
            lua_pushnil(L);
            while (lua_next(L, assIdx)) {
                luaL_checktype(L, -1, LUA_TTABLE);
                lua_pushnil(L);
                if (!lua_next(L, -2)) { luaL_error(L, "atom/boolean pair expected"); }
                Value atom = luaToVal(L, -1);
                lua_pop(L, 1);
                if (!lua_next(L, -2)) { luaL_error(L, "atom/boolean pair expected"); }
                bool truth = lua_toboolean(L, -1);
                lua_pop(L, 3);
                protect<void>(L, [atom, truth, ass](){ ass->emplace_back(atom, truth); });
            }
            lua_pop(L, 1);
        }
        return ass;
    }

    static int solve(lua_State *L) {
        auto &ctl = *(Control **)luaL_checkudata(L, 1, "gringo.Control");
        checkBlocked(L, ctl, "solve");
        lua_unsetuservaluefield(L, 1, "stats");
        int assIdx  = !lua_isnone(L, 2) && !lua_isnil(L, 2) ? 2 : 0;
        int mhIndex = !lua_isnone(L, 3) && !lua_isnil(L, 3) ? 3 : 0;
        Gringo::Model const **model = nullptr;
        int mIndex  = 0;
        if (mhIndex) {
            model = (Gringo::Model const **)lua_newuserdata(L, sizeof(Gringo::Model*));
            luaL_getmetatable(L, "gringo.Model");
            lua_setmetatable(L, -2);
            mIndex = lua_gettop(L);
        }
        Control::Assumptions *ass = getAssumptions(L, assIdx);
        lua_pushinteger(L, protect<int>(L, [L, ctl, model, ass, mhIndex, mIndex]() {
            return (int)ctl->solve(!model ? Control::ModelHandler(nullptr) : [L, model, mhIndex, mIndex](Gringo::Model const &m) -> bool {
                LuaClear lc(L);
                lua_pushcfunction(L, luaTraceback);
                lua_pushvalue(L, mhIndex);
                lua_pushvalue(L, mIndex);
                *model = &m;
                int code = lua_pcall(L, 1, 1, -3);
                Location loc("<on_model>", 1, 1, "<on_model>", 1, 1);
                if (!handleError(L, loc, code, "error in model callback")) { throw std::runtime_error("error in model callback"); }
                return lua_type(L, -1) == LUA_TNIL || lua_toboolean(L, -1);
            }, std::move(*ass));
        }));
        return 1;
    }
    static int solve_async(lua_State *L) {
        auto &ctl = *(Control **)luaL_checkudata(L, 1, "gringo.Control");
        checkBlocked(L, ctl, "solve_async");
        lua_unsetuservaluefield(L, 1, "stats");
        int assIdx  = !lua_isnone(L, 2) && !lua_isnil(L, 2) ? 2 : 0;
        int mhIndex = !lua_isnone(L, 3) && !lua_isnil(L, 3) ? 3 : 0;
        int fhIndex = !lua_isnone(L, 4) && !lua_isnil(L, 4) ? 4 : 0;
        Control::Assumptions *ass = getAssumptions(L, assIdx);
        auto &future = *(Gringo::SolveFuture **)lua_newuserdata(L, sizeof(Gringo::SolveFuture*));
        lua_State *M = nullptr;
        if (mhIndex || fhIndex) {
            lua_getfield(L, LUA_REGISTRYINDEX, "gringo._SolveThread");
            M = (lua_State*)lua_tothread(L, -1);
            lua_pop(L, 1);
            lua_settop(M, 0);
        }
        Gringo::Model const ** model = nullptr;
        if (mhIndex) {
            lua_pushvalue(L, mhIndex);
            lua_xmove(L, M, 1);
            mhIndex = lua_gettop(M);
            model   = (Gringo::Model const **)lua_newuserdata(M, sizeof(Gringo::Model*));
            luaL_getmetatable(M, "gringo.Model");
            lua_setmetatable(M, -2);
        }
        if (fhIndex) {
            lua_pushvalue(L, fhIndex);
            lua_xmove(L, M, 1);
            fhIndex = lua_gettop(M);
        }
        future = protect<Gringo::SolveFuture*>(L, [ctl, model, mhIndex, fhIndex, ass, M]() {
            auto mh = !mhIndex ? Control::ModelHandler(nullptr) : [M, mhIndex, model](Gringo::Model const &m) -> bool {
                LuaClear lc(M);
                lua_pushcfunction(M, luaTraceback);
                lua_pushvalue(M, mhIndex);
                lua_pushvalue(M, mhIndex+1);
                *model = &m;
                int code = lua_pcall(M, 1, 1, -3);
                Location loc("<on_model>", 1, 1, "<on_model>", 1, 1);
                if (!handleError(M, loc, code, "error in model callback")) { throw std::runtime_error("error in model callback"); }
                return lua_type(M, -1) == LUA_TNIL || lua_toboolean(M, -1);
            };
            auto fh = !fhIndex ? Control::FinishHandler(nullptr) : [M, fhIndex](SolveResult ret, bool interrupted) -> void {
                LuaClear lc(M);
                lua_pushcfunction(M, luaTraceback);
                lua_pushvalue(M, fhIndex);
                lua_pushinteger(M, (int)ret);
                lua_pushboolean(M, interrupted);
                int code = lua_pcall(M, 2, 1, -4);
                Location loc("<on_finish>", 1, 1, "<on_finish>", 1, 1);
                if (!handleError(M, loc, code, "error in model callback")) { throw std::runtime_error("error in model callback"); }
            };
            return ctl->solveAsync(mh, fh, std::move(*ass));
        });
        luaL_getmetatable(L, "gringo.SolveFuture");
        lua_setmetatable(L, -2);
        return 1;
    }
    static int solve_iter(lua_State *L) {
        auto &ctl = *(Control **)luaL_checkudata(L, 1, "gringo.Control");
        checkBlocked(L, ctl, "solve_iter");
        lua_unsetuservaluefield(L, 1, "stats");
        int assIdx  = !lua_isnone(L, 2) && !lua_isnil(L, 2) ? 2 : 0;
        Control::Assumptions *ass = getAssumptions(L, assIdx);
        auto &iter = *(Gringo::SolveIter **)lua_newuserdata(L, sizeof(Gringo::SolveIter*));
        iter = protect<Gringo::SolveIter*>(L, [ctl, ass]() { return ctl->solveIter(std::move(*ass)); });
        luaL_getmetatable(L, "gringo.SolveIter");
        lua_setmetatable(L, -2);
        return 1;
    }
    static int assign_external(lua_State *L) {
        auto &ctl = *(Control **)luaL_checkudata(L, 1, "gringo.Control");
        checkBlocked(L, ctl, "assign_external");
        Value atom = luaToVal(L, 2);
        luaL_checkany(L, 3);
        TruthValue truth;
        if (lua_isnil (L, 3)) { truth = TruthValue::Open; }
        else {
            luaL_checktype(L, 3, LUA_TBOOLEAN);
            truth = lua_toboolean(L, 3) ? TruthValue::True : TruthValue::False;
        }
        protect<void>(L, [ctl, atom, truth]() { ctl->assignExternal(atom, truth); });
        return 0;
    }
    static int release_external(lua_State *L) {
        auto &ctl = *(Control **)luaL_checkudata(L, 1, "gringo.Control");
        checkBlocked(L, ctl, "release_external");
        Value atom = luaToVal(L, 2);
        protect<void>(L, [ctl, atom]() { ctl->assignExternal(atom, TruthValue::Free); });
        return 0;
    }

    static int newindex(lua_State *L) {
        auto &self = *(Control **)luaL_checkudata(L, 1, "gringo.Control");
        char const *name = luaL_checkstring(L, 2);
        bool enabled = lua_toboolean(L, 3);
        if (strcmp(name, "useEnumAssumption") == 0) {
            checkBlocked(L, self, "useEnumAssumption");
            protect<void>(L, [self, enabled]() { self->useEnumAssumption(enabled); });
            return 0;
        }
        return luaL_error(L, "unknown field: %s", name);
    }

    static int index(lua_State *L) {
        auto &ctl = *(Control **)luaL_checkudata(L, 1, "gringo.Control");
        char const *name = luaL_checkstring(L, 2);
        if (strcmp(name, "useEnumAssumption") == 0) {
            bool enabled = protect<bool>(L, [ctl]() { return ctl->useEnumAssumption(); });
            lua_pushboolean(L, enabled);     // stack +1
            return 1;
        }
        else if (strcmp(name, "stats") == 0) {
            checkBlocked(L, ctl, "stats");
            lua_getuservalue(L, 1);          // stack +1
            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);               // stack +0
                lua_newtable(L);             // stack +1
                lua_pushvalue(L, -1);        // stack +2
                lua_setuservalue(L, 1);      // stack +1
            }
            lua_pushstring(L, "stats");      // stack +2
            lua_rawget(L, -2);               // stack +2
            if (lua_isnil(L, -1)) {
                auto stats = protect<Statistics *const>(L, [ctl](){ return ctl->getStats(); });
                lua_pop(L, 1);               // stack +1
                lua_pushliteral(L, "");      // stack +2
                newStatistics(L, stats);     // stack +2
                lua_pushstring(L, "stats");  // stack +3
                lua_pushvalue(L, -2);        // stack +4
                lua_rawset(L, -4);           // stack +2
            }
            lua_replace(L, -2);              // stack +1
            return 1;
        }
        else if (strcmp(name, "conf") == 0) {
            checkBlocked(L, ctl, "conf");
            Gringo::ConfigProxy *proxy;
            unsigned key;
            protect<void>(L, [ctl, &proxy, &key]() -> void {
                proxy = &ctl->getConf();
                key   = proxy->getRootKey();
            });
            return ConfigProxy::new_(L, key, *proxy);
        }
        lua_getmetatable(L, 1);
        lua_getfield(L, -1, name);
        return 1;
    }
    static int new_(lua_State *L) {
        bool hasArg = !lua_isnone(L, 1);
        std::vector<std::string> *args = AnyWrap::new_<std::vector<std::string>>(L);
        if (hasArg) {
            luaL_checktype(L, 1, LUA_TTABLE);
            lua_pushnil(L);
            while (lua_next(L, 1) != 0) {
                char const *arg = luaL_checkstring(L, -1);
                protect<void>(L, [arg, &args](){ args->push_back(arg); });
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
        }
        std::vector<char const *> *cargs = AnyWrap::new_<std::vector<char const*>>(L);
        protect<void>(L, [&cargs](){ cargs->push_back("clingo"); });
        for (auto &arg : *args) {
            protect<void>(L, [&arg, &cargs](){ cargs->push_back(arg.c_str()); });
        }
        protect<void>(L, [&cargs](){ cargs->push_back(nullptr); });
        *(Gringo::Control **)lua_newuserdata(L, sizeof(Gringo::Control*)) = newControl(cargs->size(), cargs->data());
        luaL_getmetatable(L, "gringo.Control");
        lua_setmetatable(L, -2);
        return 1;
    }
    static int gc(lua_State *L) {
        if (freeControl) {
            auto &ctl = *(Control **)luaL_checkudata(L, 1, "gringo.Control");
            freeControl(ctl);
        }
        return 0;
    }
    static luaL_Reg meta[];
};

Control::NewControlFunc  ControlWrap::newControl  = nullptr;
Control::FreeControlFunc ControlWrap::freeControl = nullptr;

luaL_Reg ControlWrap::meta[] = {
    {"ground",  ground},
    {"add", add},
    {"load", load},
    {"solve", solve},
    {"solve_async", solve_async},
    {"solve_iter", solve_iter},
    {"get_const", get_const},
    {"assign_external", assign_external},
    {"release_external", release_external},
    {nullptr, nullptr}, 
    {nullptr, nullptr}, 
    {nullptr, nullptr}
};

// }}}
// {{{ wrap cmp

int cmpVal(lua_State *L) {
    Value a(luaToVal(L, 1));
    Value b(luaToVal(L, 2));
    if (a < b)      { lua_pushnumber(L, -1); }
    else if (b < a) { lua_pushnumber(L, 1); }
    else            { lua_pushnumber(L, 0); }
    return 1;
}

// }}}
// {{{ gringo library

void lua_regMeta(lua_State *L, char const *name, luaL_Reg const * funs, lua_CFunction indexfun = nullptr, lua_CFunction newindexfun = nullptr) {
#if LUA_VERSION_NUM < 502
	luaL_newmetatable(L, name);
	luaL_register(L, 0, funs);
#else
	luaL_newmetatable(L, name);
	luaL_setfuncs(L, funs, 0);
#endif
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -2);
	lua_rawset(L, -3);
	lua_pushliteral(L, "__index");
	if (indexfun) {
		lua_pushcfunction(L, indexfun);
	}
	else {
		lua_pushvalue(L, -2);
	}
	lua_rawset(L, -3);
	if (newindexfun) {
		lua_pushliteral(L, "__newindex");
		lua_pushcfunction(L, newindexfun);
		lua_rawset(L, -3);
	}
}

int luaopen_gringo(lua_State* L) {
    static luaL_Reg gringoLib[] = {
        {"Inf",   Inf::new_},
        {"Sup",   Sup::new_},
        {"Fun",   Fun::newFun},
        {"Tuple", Fun::newTuple},
        {"cmp",   cmpVal},
        {nullptr, nullptr},
        {nullptr, nullptr}
    };

    if (ControlWrap::newControl) {
        gringoLib[sizeof(gringoLib) / sizeof(luaL_Reg) - 2].name = "Control";
        gringoLib[sizeof(gringoLib) / sizeof(luaL_Reg) - 2].func = ControlWrap::new_;
    }
    if (ControlWrap::freeControl) {
        ControlWrap::meta[sizeof(ControlWrap::meta) / sizeof(luaL_Reg) - 2].name = "__gc";
        ControlWrap::meta[sizeof(ControlWrap::meta) / sizeof(luaL_Reg) - 2].func = ControlWrap::gc;
    }

    lua_newthread(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "gringo._SolveThread");

    lua_regMeta(L, "gringo.Inf",          Inf::meta);
    lua_regMeta(L, "gringo.Sup",          Sup::meta);
    lua_regMeta(L, "gringo.Fun",          Fun::meta);
    lua_regMeta(L, "gringo.Model",        Model::meta, Model::index);
    lua_regMeta(L, "gringo.SolveControl", SolveControl::meta);
    lua_regMeta(L, "gringo.SolveFuture",  SolveFuture::meta);
    lua_regMeta(L, "gringo.SolveIter",    SolveIter::meta);
    lua_regMeta(L, "gringo.Control",      ControlWrap::meta, ControlWrap::index, ControlWrap::newindex);
    lua_regMeta(L, "gringo.ConfigProxy",  ConfigProxy::meta, ConfigProxy::index, ConfigProxy::newindex);
    lua_regMeta(L, "gringo._Any",         AnyWrap::meta);

#if LUA_VERSION_NUM < 502
    luaL_register(L, "gringo", gringoLib);
#else
    luaL_newlib(L, gringoLib);
#endif

    lua_pushstring(L, GRINGO_VERSION);
    lua_setfield(L, -2, "__version__");

    lua_createtable(L, 0, 3);
    lua_pushinteger(L, (int)Gringo::SolveResult::SAT);
    lua_setfield(L, -2, "SAT");
    lua_pushinteger(L, (int)Gringo::SolveResult::UNSAT);
    lua_setfield(L, -2, "UNSAT");
    lua_pushinteger(L, (int)Gringo::SolveResult::UNKNOWN);
    lua_setfield(L, -2, "UNKNOWN");
    lua_setfield(L, -2, "SolveResult");

    lua_createtable(L, 0, 4);
    lua_pushinteger(L, (int)Gringo::Model::ATOMS);
    lua_setfield(L, -2, "ATOMS");
    lua_pushinteger(L, (int)Gringo::Model::TERMS);
    lua_setfield(L, -2, "TERMS");
    lua_pushinteger(L, (int)Gringo::Model::SHOWN);
    lua_setfield(L, -2, "SHOWN");
    lua_pushinteger(L, (int)Gringo::Model::CSP);
    lua_setfield(L, -2, "CSP");
    lua_pushinteger(L, (int)Gringo::Model::COMP);
    lua_setfield(L, -2, "COMP");
    lua_setfield(L, -2, "Model");

    return 1;
}

int luarequire_gringo(lua_State *L) {
    luaL_openlibs(L);
#if LUA_VERSION_NUM < 502
    lua_pushcfunction(L, luaopen_gringo);
    lua_call(L, 0, 1);
#else
    luaL_requiref(L, "gringo", luaopen_gringo, true);
#endif
    return 1;
}

// }}}
// {{{ lua C functions

using LuaCallArgs = std::tuple<char const *, ValVec const &, ValVec>;

static int luaTraceback (lua_State *L) {
    if (!lua_isstring(L, 1)) { return 1; }
    lua_getglobal(L, "debug");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return 1;
    }
    lua_getfield(L, -1, "traceback");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 2);
        return 1;
    }
    lua_pushvalue(L, 1);
    lua_pushinteger(L, 2);
    lua_call(L, 2, 1);
    lua_getglobal(L, "string");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return 1;
    }
    lua_getfield(L, -1, "gsub");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return 1;
    }
    lua_pushvalue(L, -3);
    lua_pushliteral(L, "\t");
    lua_pushliteral(L, "  ");
    lua_call(L, 3, 1);
    return 1;
}

int luaCall(lua_State *L) {
    auto &args = *(LuaCallArgs*)lua_touserdata(L, 1);
    bool hasContext = !lua_isnil(L, 2);
    if (hasContext) { 
        lua_getfield(L, 2, std::get<0>(args));
        lua_pushvalue(L, 2);
    }
    else { lua_getglobal(L, std::get<0>(args)); }
    for (auto &x : std::get<1>(args)) { valToLua(L, x); }
    lua_call(L, std::get<1>(args).size() + hasContext, 1);
    if (lua_type(L, -1) == LUA_TTABLE) {
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            Value val = luaToVal(L, -1);
            protect<void>(L, [val, &args]() { std::get<2>(args).emplace_back(val); });
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    else {
        Value val = luaToVal(L, -1);
        protect<void>(L, [val, &args]() { std::get<2>(args).emplace_back(val); });
    }
    return 0;
}

int luaMain(lua_State *L) {
    auto ctl = (Control*)lua_touserdata(L, 1);
    lua_getglobal(L, "main");
    *(Control **)lua_newuserdata(L, sizeof(Control*)) = ctl;
    luaL_getmetatable(L, "gringo.Control");
    lua_setmetatable(L, -2);
    lua_call(L, 1, 0);
    return 0;
}

// }}}

} // namespace

// {{{ definition of LuaImpl

struct LuaImpl {
    LuaImpl() : L(luaL_newstate()) {
        if (!L) { throw std::runtime_error("could not open lua state"); }
        int n = lua_gettop(L);
        lua_pushcfunction(L, luaTraceback);
        lua_pushcfunction(L, luarequire_gringo);
        int ret = lua_pcall(L, 0, 0, -2);
        Location loc("<LuaImpl>", 1, 1, "<LuaImpl>", 1, 1);
        if (!handleError(L, loc, ret, "running lua script failed")) {
            throw std::runtime_error("could not initialize gringo module");
        }
        lua_settop(L, n);
    }
    ~LuaImpl() {
        if (L) { lua_close(L); }
    }
    lua_State *L;
};

// }}}
// {{{ definition of Lua

Lua::Lua() = default;

bool Lua::exec(Location const &loc, FWString code) {
    if (!impl) { impl = make_unique<LuaImpl>(); }
    LuaClear lc(impl->L);
    std::stringstream oss;
    oss << loc;
    lua_pushcfunction(impl->L, luaTraceback);
    int ret = luaL_loadbuffer(impl->L, (*code).c_str(), (*code).size(), oss.str().c_str());
    if (!handleError(impl->L, loc, ret, "parsing lua script failed")) { return false; }
    ret = lua_pcall(impl->L, 0, 0, -2);
    if (!handleError(impl->L, loc, ret, "running lua script failed")) { return false; }
    return true;
}

ValVec Lua::call(Any const &context, Location const &loc, FWString name, ValVec const &args) {
    assert(impl);
    LuaClear lc(impl->L);
    LuaContext const *ctx = context.get<LuaContext>();
    LuaCallArgs arg((*name).c_str(), args, {});
    lua_pushcfunction(impl->L, luaTraceback);
    lua_pushcfunction(impl->L, luaCall);
    lua_pushlightuserdata(impl->L, (void*)&arg);
    if (ctx) { lua_pushvalue(impl->L, ctx->idx); }
    else { lua_pushnil(impl->L); }
    int ret = lua_pcall(impl->L, 2, 0, -4);
    if (!handleError(impl->L, loc, ret, "operation undefined, a zero is substituted")) { return {0}; }
    return std::move(std::get<2>(arg));
}

bool Lua::callable(Any const &context, FWString name) {
    if (!impl) { return false; }
    LuaClear lc(impl->L);
    LuaContext const *ctx = context.get<LuaContext>();
    if (ctx) { lua_getfield(impl->L, ctx->idx, (*name).c_str()); } 
    else { lua_getglobal(impl->L, (*name).c_str()); }
    bool ret = lua_type(impl->L, -1) == LUA_TFUNCTION;
    return ret;
}

void Lua::main(Control &ctl) {
    assert(impl);
    LuaClear lc(impl->L);
    lua_pushcfunction(impl->L, luaTraceback);
    lua_pushcfunction(impl->L, luaMain);
    lua_pushlightuserdata(impl->L, (void*)&ctl);
    switch (lua_pcall(impl->L, 1, 0, -3)) {
        case LUA_ERRRUN:
        case LUA_ERRERR: {
            std::ostringstream oss;
            oss << lua_tostring(impl->L, -1);
            lua_pop(impl->L, 1);
            throw std::runtime_error(oss.str());
        }
        case LUA_ERRMEM: { throw std::runtime_error("lua interpreter ran out of memory"); }
    }
}
void Lua::initlib(lua_State *L, Control::NewControlFunc newControl, Control::FreeControlFunc freeControl) {
    ControlWrap::newControl  = newControl;
    ControlWrap::freeControl = freeControl;
    luarequire_gringo(L);
}
Lua::~Lua() = default;

// }}}

} // namespace Gringo

#else // WITH_LUA

#include "gringo/lua.hh"
#include "gringo/logger.hh"

namespace Gringo {

// {{{ definition of LuaImpl

struct LuaImpl { };

// }}}
// {{{ definition of Lua

Lua::Lua() = default;
bool Lua::exec(Location const &loc, FWString) {
    GRINGO_REPORT(W_TERM_UNDEFINED)
        << loc << ": warning: gringo has been build without lua support, code is ignored\n"
        ;
    return false;
}
bool Lua::callable(Any const &, FWString) {
    return false;
}
ValVec Lua::call(Any const &, Location const &, FWString, ValVec const &) {
    return {0};
}
void Lua::main(Control &) { }
void Lua::initlib(lua_State *L, Control::NewControlFunc, Control::FreeControlFunc) {
    throw std::runtime_error("gringo lib has been build without lua support");
}
Lua::~Lua() = default;

// }}}

} // namespace Gringo

#endif // WITH_LUA

