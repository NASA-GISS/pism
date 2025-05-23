// Copyright (C) 2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018, 2021, 2023, 2024 PISM Authors
//
// This file is part of PISM.
//
// PISM is free software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation; either version 3 of the License, or (at your option) any later
// version.
//
// PISM is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License
// along with PISM; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

#ifndef PISM_OPTIONS_H
#define PISM_OPTIONS_H

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "pism/util/options.hh"

namespace pism {

namespace units {
class System;
} // end of namespace units

class Config;
class Logger;

void show_usage(const Logger &log, const std::string &execname, const std::string &usage);

//! @brief Returns true if PISM should terminate after printing some
//! messages to stdout.
bool show_usage_check_req_opts(const Logger &log,
                               const std::string &execname,
                               const std::vector<std::string> &required_options,
                               const std::string &usage);


//! Utilities for processing command-line options.
namespace options {

typedef enum {ALLOW_EMPTY, DONT_ALLOW_EMPTY} ArgumentFlag;

class String : public Option<std::string> {
public:
  // there is no reasonable default; if the option is set, it has to
  // have a non-empty argument
  String(const std::string& option,
         const std::string& description);
  // there is a reasonable default
  String(const std::string& option,
         const std::string& description,
         const std::string& default_value,
         ArgumentFlag flag = DONT_ALLOW_EMPTY);
private:
  int process(const std::string& option,
              const std::string& description,
              const std::string& default_value,
              ArgumentFlag flag);
};

class Keyword : public Option<std::string> {
public:
  Keyword(const std::string& option,
          const std::string& description,
          const std::string& choices,
          const std::string& default_value);
};

class Integer : public Option<int> {
public:
  Integer(const std::string& option,
          const std::string& description,
          int default_value);
};

class Real : public Option<double> {
public:
  Real(std::shared_ptr<units::System> system,
       const std::string& option,
       const std::string& description,
       const std::string& units,
       double default_value);
};

bool Bool(const std::string& option,
          const std::string& description);

void deprecated(const std::string &old_name, const std::string &new_name);
void ignored(const Logger &log, const std::string &name);
void forbidden(const std::string &name);
} // end of namespace options

} // end of namespace pism

#endif /* PISM_OPTIONS_H */
