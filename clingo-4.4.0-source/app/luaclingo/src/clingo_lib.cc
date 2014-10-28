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

#include "clingo_lib.hh"
#include <climits>
#include <unistd.h>

using namespace Clasp;
using namespace std::placeholders;

// {{{ declaration of ClingoLib

ClingoLib::ClingoLib(int argc, char const **argv) 
        : ClingoControl(true, &clasp_, claspConfig_, nullptr, nullptr) { 
    using namespace ProgramOptions;
    OptionContext allOpts("<pyclingo>");
    initOptions(allOpts);
    ParsedValues values = parseCommandLine(argc, const_cast<char**>(argv), allOpts, false, parsePositional);
    ParsedOptions parsed;
    parsed.assign(values);
    allOpts.assignDefaults(parsed);
    claspConfig_.finalize(parsed, Clasp::Problem_t::ASP, true);
    clasp_.ctx.setEventHandler(this);
    Asp::LogicProgram* lp = &clasp_.startAsp(claspConfig_, true);
    incremental = true;
    parse({}, grOpts_, lp, false);
}


static bool parseConst(const std::string& str, std::vector<std::string>& out) {
    out.push_back(str);
    return true;
}

void ClingoLib::initOptions(ProgramOptions::OptionContext& root) {
    using namespace ProgramOptions;
    grOpts_.defines.clear();
    grOpts_.verbose = false;
    OptionGroup gringo("Gringo Options");
    grOpts_.text = false;
    gringo.addOptions()
        ("verbose,V"                , flag(grOpts_.verbose = false), "Enable verbose output")
        ("const,c"                  , storeTo(grOpts_.defines, parseConst)->composing()->arg("<id>=<term>"), "Replace term occurences of <id> with <term>")
        ("lparse-rewrite"           , flag(grOpts_.lpRewrite = false), "Use together with --text to inspect lparse rewriting")
        ("lparse-debug"             , storeTo(grOpts_.lparseDebug = Gringo::Output::LparseDebug::NONE, values<Gringo::Output::LparseDebug>()
          ("none"  , Gringo::Output::LparseDebug::NONE)
          ("plain" , Gringo::Output::LparseDebug::PLAIN)
          ("lparse", Gringo::Output::LparseDebug::LPARSE)
          ("all"   , Gringo::Output::LparseDebug::ALL)), "Debug information during lparse rewriting:\n"
         "      none  : no additional info\n"
         "      plain : print rules as in plain output (prefix %%)\n"
         "      lparse: print rules as in lparse output (prefix %%%%)\n"
         "      all   : combines plain and lparse\n")
        ("warn,W"                   , storeTo(grOpts_, parseWarning)->arg("<warn>")->composing(), "Enable/disable warnings:\n"
         "      [no-]atom-undefined:        a :- b.\n"
         "      [no-]define-cyclic:         #const a=b. #const b=a.\n"
         "      [no-]define-redefinition:   #const a=1. #const a=2.\n"
         "      [no-]file-included:         #include \"a.lp\". #include \"a.lp\".\n"
         "      [no-]nonmonotone-aggregate: a :- #sum { 1:a; -1:a } >= 0.\n"
         "      [no-]term-undefined       : p(1/0).\n")
        ;
    root.add(gringo);
    claspConfig_.addOptions(root);
}

bool ClingoLib::onModel(Clasp::Solver const&, Clasp::Model const& m) {
    return ClingoControl::onModel(m);
}
void ClingoLib::onEvent(Event const& ev) {
#if WITH_THREADS
    Clasp::ClaspFacade::StepReady const *r = Clasp::event_cast<Clasp::ClaspFacade::StepReady>(ev);
    if (r && finishHandler) { onFinish(r->summary->result); }
#endif
    const LogEvent* log = event_cast<LogEvent>(ev);
    if (log && log->isWarning()) {
        fflush(stdout);
        fprintf(stderr, "*** %-5s: (%s): %s\n", "Warn", "pyclingo", log->msg);
        fflush(stderr);
    }
}
bool ClingoLib::parsePositional(const std::string& t, std::string& out) {
    int num;
    if (bk_lib::string_cast(t, num)) { 
        out = "number";
        return true;
    }
    return false;
}
ClingoLib::~ClingoLib() {
    // TODO: can be removed after bennies next update...
#if WITH_THREADS
    solveIter_   = nullptr;
    solveFuture_ = nullptr;
#endif
    clasp_.shutdown();
}

// }}}

