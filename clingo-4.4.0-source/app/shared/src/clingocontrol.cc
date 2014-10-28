// {{{ GPL License 

// This file is part of gringo - a grounder for logic programs.
// Copyright (C) 2013  Benjamin Kaufmann
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

#include "clingocontrol.hh"

// {{{ definition of ClingoLpOutput

void ClingoLpOutput::addBody(const LitVec& body) {
    for (auto x : body) {
        prg_.addToBody((Clasp::Var)std::abs(x), x > 0);
    }
}
void ClingoLpOutput::addBody(const LitWeightVec& body) {
    for (auto x : body) {
        prg_.addToBody((Clasp::Var)std::abs(x.first), x.first > 0, x.second);
    }
}
void ClingoLpOutput::printBasicRule(unsigned head, LitVec const &body) {
    prg_.startRule().addHead(head);
    addBody(body);
    prg_.endRule();
}

void ClingoLpOutput::printChoiceRule(AtomVec const &atoms, LitVec const &body) {
    prg_.startRule(Clasp::Asp::CHOICERULE);
    for (auto x : atoms) { prg_.addHead(x); }
    addBody(body);
    prg_.endRule();
}

void ClingoLpOutput::printCardinalityRule(unsigned head, unsigned lower, LitVec const &body) {
    prg_.startRule(Clasp::Asp::CONSTRAINTRULE, lower).addHead(head);
    addBody(body);
    prg_.endRule();
}

void ClingoLpOutput::printWeightRule(unsigned head, unsigned lower, LitWeightVec const &body) {
    prg_.startRule(Clasp::Asp::WEIGHTRULE, lower).addHead(head);
    addBody(body);
    prg_.endRule();
}

void ClingoLpOutput::printMinimize(LitWeightVec const &body) {
    prg_.startRule(Clasp::Asp::OPTIMIZERULE);
    addBody(body);
    prg_.endRule();
}

void ClingoLpOutput::printDisjunctiveRule(AtomVec const &atoms, LitVec const &body) {
    prg_.startRule(Clasp::Asp::DISJUNCTIVERULE);
    for (auto x : atoms) { prg_.addHead(x); }
    addBody(body);
    prg_.endRule();
}

void ClingoLpOutput::printSymbol(unsigned atomUid, Gringo::Value v) {
    if (v.type() == Gringo::Value::ID || v.type() == Gringo::Value::STRING) {
        prg_.setAtomName(atomUid, (*v.string()).c_str());
    }
    else {
        str_.str("");
        v.print(str_);
        prg_.setAtomName(atomUid, str_.str().c_str());
    }
}

void ClingoLpOutput::printExternal(unsigned atomUid, Gringo::TruthValue type) {
    switch (type) {
        case Gringo::TruthValue::False: { prg_.freeze(atomUid, Clasp::value_false); break; }
        case Gringo::TruthValue::True:  { prg_.freeze(atomUid, Clasp::value_true); break; }
        case Gringo::TruthValue::Open:  { prg_.freeze(atomUid, Clasp::value_free); break; }
        case Gringo::TruthValue::Free:  { prg_.unfreeze(atomUid); break; }
    }
}

bool &ClingoLpOutput::disposeMinimize() {
    return disposeMinimize_;
}

// }}}
// {{{ definition of ClingoControl

#define LOG if (verbose_) std::cerr
ClingoControl::ClingoControl(bool clingoMode, Clasp::ClaspFacade *clasp, Clasp::Cli::ClaspCliConfig &claspConfig, PostGroundFunc pgf, PreSolveFunc psf)
    : clasp(clasp)
    , claspConfig_(claspConfig)
    , pgf_(pgf)
    , psf_(psf)
    , clingoMode_(clingoMode) { }

void ClingoControl::parse_() {
    if (!parser->empty()) {
        parser->parse();
        defs.init();
        parsed = true;
    }
}

void ClingoControl::parse(const StringSeq& files, const ClingoOptions& opts, Clasp::Asp::LogicProgram* claspOut, bool addStdIn) {
    using namespace Gringo;
    if (opts.wNoRedef)        { message_printer()->disable(W_DEFINE_REDEFINTION); }
    if (opts.wNoCycle)        { message_printer()->disable(W_DEFINE_CYCLIC);  }
    if (opts.wNoTermUndef)    { message_printer()->disable(W_TERM_UNDEFINED); }
    if (opts.wNoAtomUndef)    { message_printer()->disable(W_ATOM_UNDEFINED); }
    if (opts.wNoNonMonotone)  { message_printer()->disable(W_NONMONOTONE_AGGREGATE); }
    if (opts.wNoFileIncluded) { message_printer()->disable(W_FILE_INCLUDED); }
    verbose_ = opts.verbose;
    Output::OutputPredicates outPreds;
    for (auto &x : opts.foobar) {
        outPreds.emplace_back(Location("<cmd>",1,1,"<cmd>", 1,1), x, false);
    }
    if (opts.text) {
        out.reset(new Output::OutputBase(std::move(outPreds), std::cout, opts.lpRewrite));
    }
    else {  
        if (claspOut) { lpOut.reset(new ClingoLpOutput(*claspOut)); }
        else          { lpOut.reset(new Output::PlainLparseOutputter(std::cout)); }
        out.reset(new Output::OutputBase(std::move(outPreds), *lpOut, opts.lparseDebug));
    }
    pb = make_unique<Input::NongroundProgramBuilder>(scripts, prg, *out, defs);
    parser = make_unique<Input::NonGroundParser>(*pb);
    for (auto &x : opts.defines) {
        LOG << "define: " << x << std::endl;
        parser->parseDefine(x);
    }
    for (auto x : files) {
        LOG << "file: " << x << std::endl;
        parser->pushFile(std::move(x));
    }
    if (files.empty() && addStdIn) {
        LOG << "reading from stdin" << std::endl;
        parser->pushFile("-");
    }
    parse_();
}

bool ClingoControl::ok() {
    return !clingoMode_ || (clasp->program() && clasp->program()->ok());
}

bool ClingoControl::update() {
    if (clingoMode_ && (clasp->program()->frozen() || configUpdate_)) {
        clasp->update(configUpdate_);
        configUpdate_ = false;
    }
    return ok();
}

void ClingoControl::ground(Gringo::Control::GroundVec const &parts, Gringo::Any &&context) {
    if (!update()) { return; }
    if (parsed) {
        LOG << "************** parsed program **************" << std::endl << prg;
        prg.rewrite(defs);
        LOG << "************* rewritten program ************" << std::endl << prg;
        prg.check();
        if (Gringo::message_printer()->hasError()) {
            throw std::runtime_error("grounding stopped because of errors");
        }
        parsed = false;
    }
    if (!grounded) {
        if (incremental) { out->incremental(); }
        grounded = true;
    }
    if (!parts.empty()) {
        Gringo::Ground::Parameters params;
        for (auto &x : parts) { params.add(x.first, x.second); }
        auto gPrg = prg.toGround(out->domains);
        LOG << "*********** intermediate program ***********" << std::endl << gPrg << std::endl;
        LOG << "************* grounded program *************" << std::endl;
        auto exit = Gringo::onExit([this]{ scripts.context = Gringo::Any(); });
        scripts.context = std::move(context);
        gPrg.ground(params, scripts, *out, false);
    }
}

void ClingoControl::main() {
    if (scripts.callable("main")) { 
        incremental = true;
        clasp->enableProgramUpdates();
        scripts.main(*this);
    }
    else {
        claspConfig_.releaseOptions();
        Gringo::Control::GroundVec parts;
        parts.emplace_back("base", Gringo::FWValVec{});
        ground(parts, Gringo::Any());
        solve(nullptr, {});
    }
}
bool ClingoControl::onModel(Clasp::Model const &m) {
    return !modelHandler || modelHandler(ClingoModel(static_cast<Clasp::Asp::LogicProgram&>(*clasp->program()), *out, clasp->ctx, &m));
}
void ClingoControl::onFinish(Clasp::ClaspFacade::Result ret) {
    if (finishHandler) {
        finishHandler(convert(ret), ret.interrupted());
        finishHandler = nullptr;
    }
}
Gringo::Value ClingoControl::getConst(std::string const &name) {
    auto ret = defs.defs().find(name);
    return ret != defs.defs().end()
        ? std::get<2>(ret->second)->eval()
        : Gringo::Value();
}
void ClingoControl::add(std::string const &name, Gringo::FWStringVec const &params, std::string const &part) {
    Gringo::Location loc("<block>", 1, 1, "<block>", 1, 1);
    Gringo::Input::IdVec idVec;
    for (auto &x : params) { idVec.emplace_back(loc, x); }
    parser->pushBlock(name, std::move(idVec), part);
    parse_();
}
void ClingoControl::load(std::string const &filename) {
    parser->pushFile(std::string(filename));
    parse_();
}
bool ClingoControl::hasSubKey(unsigned key, char const *name, unsigned* subKey) {
    *subKey = claspConfig_.getKey(key, name);
    return *subKey != Clasp::Cli::ClaspCliConfig::INVALID_KEY;
}
unsigned ClingoControl::getSubKey(unsigned key, char const *name) {
    unsigned ret = claspConfig_.getKey(key, name);
    if (ret == Clasp::Cli::ClaspCliConfig::INVALID_KEY) {
        throw std::runtime_error("invalid key");
    }
    return ret;
}
unsigned ClingoControl::getArrKey(unsigned key, unsigned idx) {
    unsigned ret = claspConfig_.getArrKey(key, idx);
    if (ret == Clasp::Cli::ClaspCliConfig::INVALID_KEY) {
        throw std::runtime_error("invalid key");
    }
    return ret;
}
void ClingoControl::getKeyInfo(unsigned key, int* nSubkeys, int* arrLen, const char** help, int* nValues) const {
    if (claspConfig_.getKeyInfo(key, nSubkeys, arrLen, help, nValues) < 0) {
        throw std::runtime_error("could not get key info");
    }
}
const char* ClingoControl::getSubKeyName(unsigned key, unsigned idx) const {
    char const *ret = claspConfig_.getSubkey(key, idx);
    if (!ret) {
        throw std::runtime_error("could not get subkey");
    }
    return ret;
}
bool ClingoControl::getKeyValue(unsigned key, std::string &value) {
    int ret = claspConfig_.getValue(key, value);
    if (ret < -1) {
        throw std::runtime_error("could not get option value");
    }
    return ret >= 0;
}
void ClingoControl::setKeyValue(unsigned key, const char *val) {
    configUpdate_ = true;
    if (claspConfig_.setValue(key, val) <= 0) {
        throw std::runtime_error("could not set option value");
    }
}
unsigned ClingoControl::getRootKey() {
    return Clasp::Cli::ClaspCliConfig::KEY_ROOT;
}
Gringo::ConfigProxy &ClingoControl::getConf() {
    return *this;
}
Gringo::SolveIter *ClingoControl::solveIter(Assumptions &&ass) {
    if (!clingoMode_) { throw std::runtime_error("solveIter is not supported in gringo gringo mode"); }
#if WITH_THREADS
    prepare_(nullptr, nullptr, std::move(ass));
    solveIter_ = Gringo::make_unique<ClingoSolveIter>(clasp->startSolveAsync(), static_cast<Clasp::Asp::LogicProgram&>(*clasp->program()), *out, clasp->ctx);
    return solveIter_.get();
#else
    (void)ass;
    throw std::runtime_error("solveIter requires clingo to be build with thread support");
#endif
}
Gringo::SolveFuture *ClingoControl::solveAsync(ModelHandler mh, FinishHandler fh, Assumptions &&ass) {
    if (!clingoMode_) { throw std::runtime_error("solveAsync is not supported in gringo gringo mode"); }
#if WITH_THREADS
    prepare_(mh, fh, std::move(ass));
    solveFuture_ = Gringo::make_unique<ClingoSolveFuture>(clasp->solveAsync());
    return solveFuture_.get();
#else
    (void)mh;
    (void)fh;
    (void)ass;
    throw std::runtime_error("solveAsync requires clingo to be build with thread support");
#endif
}
bool ClingoControl::blocked() {
    return clasp->solving();
}
bool ClingoControl::prepare_(Gringo::Control::ModelHandler mh, Gringo::Control::FinishHandler fh, Gringo::Control::Assumptions &&ass) {
    if (!update()) { return false; }
    grounded = false;
    out->finish();
    if (clingoMode_) {
#if WITH_THREADS
        solveIter_    = nullptr;
        solveFuture_  = nullptr;
#endif
        finishHandler = fh;
        modelHandler  = mh;
        Clasp::ProgramBuilder *prg = clasp->program();
        if (lpOut && lpOut->disposeMinimize()) { prg->disposeMinimizeConstraint(); }
        if (!prg->ok() || (pgf_ && !pgf_(*prg))) { return false; }
        if (!clasp->prepare(enableEnumAssupmption_ ? Clasp::ClaspFacade::enum_volatile : Clasp::ClaspFacade::enum_static) && (!psf_ || psf_(*clasp))) { return false; }
        for (auto &x : ass) {
            auto atm = out->find2(x.first);
            if (atm && atm->second.hasUid()) {
                Clasp::Literal lit = static_cast<Clasp::Asp::LogicProgram*>(prg)->getLiteral(atm->second.uid());
                clasp->assume(x.second ? lit : ~lit);
            }
            else if (x.second) {
                Clasp::Literal lit = static_cast<Clasp::Asp::LogicProgram*>(prg)->getLiteral(1);
                clasp->assume(lit);
                break;
            }
        }
    }
    else if (!ass.empty()) {
        std::cerr << "warning: the lparse format does not support assumptions" << std::endl;
    }
    return true;
}
Gringo::SolveResult ClingoControl::solve(ModelHandler h, Assumptions &&ass) {
    if (!prepare_(h, nullptr, std::move(ass))) { return convert(clasp->result()); }
    return clingoMode_ ? convert(clasp->solve()) : Gringo::SolveResult::UNKNOWN;
}
std::string ClingoControl::str() {
    return "[object:IncrementalControl]";
}
void ClingoControl::assignExternal(Gringo::Value ext, Gringo::TruthValue val) {
    if (update()) {
        Gringo::PredicateDomain::element_type *atm = out->find2(ext);
        if (atm && atm->second.hasUid()) {
            out->external(*atm, val);
        }
    }
}
ClingoStatistics *ClingoControl::getStats() {
    clingoStats.clasp = clasp;
    return &clingoStats;
}
void ClingoControl::useEnumAssumption(bool enable) {
    enableEnumAssupmption_ = enable;
}
bool ClingoControl::useEnumAssumption() {
    return enableEnumAssupmption_;
}

// }}}
// {{{ definition of ClingoStatistics

Gringo::Statistics::Quantity ClingoStatistics::getStat(char const* key) const {
    if (!clasp) { return std::numeric_limits<double>::quiet_NaN(); }
    auto ret = clasp->getStat(key);
    switch (ret.error()) {
        case Clasp::ExpectedQuantity::error_ambiguous_quantity: { return Gringo::Statistics::error_ambiguous_quantity; }
        case Clasp::ExpectedQuantity::error_not_available:      { return Gringo::Statistics::error_not_available; }
        case Clasp::ExpectedQuantity::error_unknown_quantity:   { return Gringo::Statistics::error_unknown_quantity; }
        case Clasp::ExpectedQuantity::error_none:               { return (double)ret; }
    }
    return std::numeric_limits<double>::quiet_NaN();
}
char const *ClingoStatistics::getKeys(char const* key) const {
    if (!clasp) { return ""; }
    return clasp->getKeys(key);
}

// }}}
// {{{ definition of ClingoSolveIter

#if WITH_THREADS
ClingoSolveIter::ClingoSolveIter(Clasp::ClaspFacade::AsyncResult const &future, Clasp::Asp::LogicProgram const &lp, Gringo::Output::OutputBase const &out, Clasp::SharedContext const &ctx) 
    : future(future)
    , model(lp, out, ctx) { }
Gringo::Model const *ClingoSolveIter::next() {
    if (model.model)  { future.next(); }
    if (future.end()) { return nullptr; }
    model.reset(future.model());
    return &model;
}
void ClingoSolveIter::close() {
    if (!future.end()) { future.cancel(); }
}
Gringo::SolveResult ClingoSolveIter::get() {
    return convert(future.get());
}
ClingoSolveIter::~ClingoSolveIter() = default;
#endif

// }}}
// {{{ definition of ClingoSolveFuture

Gringo::SolveResult convert(Clasp::ClaspFacade::Result res) {
    switch (res) {
        case Clasp::ClaspFacade::Result::SAT:     { return Gringo::SolveResult::SAT; }
        case Clasp::ClaspFacade::Result::UNSAT:   { return Gringo::SolveResult::UNSAT; }
        case Clasp::ClaspFacade::Result::UNKNOWN: { return Gringo::SolveResult::UNKNOWN; }
    }
    return Gringo::SolveResult::UNKNOWN;
}

#if WITH_THREADS
ClingoSolveFuture::ClingoSolveFuture(Clasp::ClaspFacade::AsyncResult const &res)
    : future(res) { }
Gringo::SolveResult ClingoSolveFuture::get() {
    if (!done) { 
        bool stop = future.interrupted() == SIGINT;
        ret       = convert(future.get());
        done      = true;
        if (stop) { throw std::runtime_error("solving stopped by signal"); }
    }
    return ret;
}
void ClingoSolveFuture::wait() { get(); }
bool ClingoSolveFuture::wait(double timeout) {
    if (!done) {
        if (timeout == 0 ? !future.ready() : !future.waitFor(timeout)) { return false; }
        wait();
    }
    return true;
}
void ClingoSolveFuture::interrupt() { future.cancel(); }
ClingoSolveFuture::~ClingoSolveFuture() { }
#endif

// }}}
