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

#ifndef _GRINGO_CLINGOLIB_HH
#define _GRINGO_CLINGOLIB_HH

#include <program_opts/typed_value.h>
#include <program_opts/application.h>
#include <clasp/cli/clasp_options.h>
#include "gringo/version.hh"
#include "clingocontrol.hh"

class ClingoLib : public Clasp::EventHandler, public ClingoControl {
    using StringVec    = std::vector<std::string>;
public:
    ClingoLib(int argc, char const **argv);
    virtual ~ClingoLib();
protected:
    void initOptions(ProgramOptions::OptionContext& root);
	static bool parsePositional(const std::string& s, std::string& out);
    // -------------------------------------------------------------------------------------------
    // Event handler
    virtual void onEvent(const Clasp::Event& ev);
    virtual bool onModel(const Clasp::Solver& s, const Clasp::Model& m);
private:
    ClingoLib(const ClingoLib&);
    ClingoLib& operator=(const ClingoLib&);
    ClingoOptions                       grOpts_;
    Clasp::Cli::ClaspCliConfig          claspConfig_;
    Clasp::ClaspFacade                  clasp_;
};

#endif // _GRINGO_CLINGOLIB_HH
