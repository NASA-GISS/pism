// Copyright (C) 2007--2015 Jed Brown, Ed Bueler and Constantine Khroulev
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

#include <petsc.h>
#include <petscfix.h>
#include <petsctime.h>
#include <petscsys.h>

#include <sstream>
#include <ctime>
#include <algorithm>
#include <sys/types.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <cassert>

#include "pism_const.hh"
#include "error_handling.hh"

extern FILE *petsc_history;

namespace pism {

//! \brief PISM verbosity level; determines how much gets printed to the
//! standard out.
static int verbosityLevel;

//! \brief Set the PISM verbosity level.
void setVerbosityLevel(int level) {
  if ((level < 0) || (level > 5)) {
    throw RuntimeError::formatted("verbosity level %d is invalid", level);
  }
  verbosityLevel = level;
}

//! \brief Get the verbosity level.
int getVerbosityLevel() {
  return verbosityLevel;
}

//! Print messages to standard out according to verbosity threshhold.
/*!
Verbosity level version of PetscPrintf.  We print according to whether 
(threshold <= verbosityLevel), in which case print, or (threshold > verbosityLevel)
in which case no print.

verbosityLevelFromOptions() actually reads the threshold.

The range 1 <= threshold <= 5  is enforced.

Use this method for messages and warnings which should
- go to stdout and
- appear only once (regardless of number of processors).

For each communicator, rank 0 does the printing. Calls from other
ranks are ignored.

Should not be used for reporting fatal errors.
 */
void verbPrintf(const int threshold, 
                MPI_Comm comm, const char format[], ...) {
  PetscErrorCode ierr;
  int            rank;

  assert(1 <= threshold && threshold <= 5);

  ierr = MPI_Comm_rank(comm, &rank); PISM_C_CHK(ierr, 0, "MPI_Comm_rank");
  if (rank == 0) {
    va_list Argp;
    if (verbosityLevel >= threshold) {
      va_start(Argp, format);
      ierr = PetscVFPrintf(PETSC_STDOUT, format, Argp);
      PISM_CHK(ierr, "PetscVFPrintf");

      va_end(Argp);
    }
    if (petsc_history) { // always print to history
      va_start(Argp, format);
      ierr = PetscVFPrintf(petsc_history, format, Argp);
      PISM_CHK(ierr, "PetscVFPrintf");

      va_end(Argp);
    }
  }
}


//! Returns true if `str` ends with `suffix` and false otherwise.
bool ends_with(const std::string &str, const std::string &suffix) {
  if (str.empty() and (not suffix.empty())) {
    return false;
  }

  if (str.rfind(suffix) + suffix.size() == str.size()) {
    return true;
  }

  return false;
}

//! Concatenate `strings`, inserting `separator` between elements.
std::string join(const std::vector<std::string> &strings, const std::string &separator) {
  std::vector<std::string>::const_iterator j = strings.begin();
  std::string result = *j;
  ++j;
  while (j != strings.end()) {
    result += separator + *j;
    ++j;
  }
  return result;
}

//! Transform a `separator`-separated list (a string) into a vector of strings.
std::vector<std::string> split(const std::string &input, char separator) {
  std::istringstream input_list(input);
  std::string token;
  std::vector<std::string> result;

  while (getline(input_list, token, separator)) {
    result.push_back(token);
  }
  return result;
}

//! Checks if a vector of doubles is strictly increasing.
bool is_increasing(const std::vector<double> &a) {
  int len = (int)a.size();
  for (int k = 0; k < len-1; k++) {
    if (a[k] >= a[k+1]) {
      return false;
    }
  }
  return true;
}

//! Creates a time-stamp used for the history NetCDF attribute.
std::string pism_timestamp() {
  time_t now;
  tm tm_now;
  char date_str[50];
  now = time(NULL);
  localtime_r(&now, &tm_now);
  // Format specifiers for strftime():
  //   %F = ISO date format,  %T = Full 24 hour time,  %Z = Time Zone name
  strftime(date_str, sizeof(date_str), "%F %T %Z", &tm_now);

  return std::string(date_str);
}

//! Creates a string with the user name, hostname and the time-stamp (for history strings).
std::string pism_username_prefix(MPI_Comm com) {
  PetscErrorCode ierr;

  char username[50];
  ierr = PetscGetUserName(username, sizeof(username));
  PISM_CHK(ierr, "PetscGetUserName");
  if (ierr != 0) {
    username[0] = '\0';
  }
  char hostname[100];
  ierr = PetscGetHostName(hostname, sizeof(hostname));
  PISM_CHK(ierr, "PetscGetHostName");
  if (ierr != 0) {
    hostname[0] = '\0';
  }
  
  std::ostringstream message;
  message << username << "@" << hostname << " " << pism_timestamp() << ": ";

  std::string result = message.str();
  unsigned int length = result.size();
  MPI_Bcast(&length, 1, MPI_UNSIGNED, 0, com);

  result.resize(length);
  MPI_Bcast(&result[0], length, MPI_CHAR, 0, com);

  return result;
}

//! \brief Uses argc and argv to create the string with current PISM
//! command-line arguments.
std::string pism_args_string() {
  int argc;
  char **argv;
  PetscErrorCode ierr = PetscGetArgs(&argc, &argv);
  PISM_CHK(ierr, "PetscGetArgs");

  std::string cmdstr, argument;
  for (int j = 0; j < argc; j++) {
    argument = argv[j];

    // enclose arguments containing spaces with double quotes:
    if (argument.find(" ") != std::string::npos) {
      argument = "\"" + argument + "\"";
    }

    cmdstr += std::string(" ") + argument;
  }
  cmdstr += "\n";

  return cmdstr;
}

//! \brief Adds a suffix to a filename.
/*!
 * Returns filename + separator + suffix + .nc if the original filename had the
 * .nc suffix, otherwise filename + separator. If the old filename had the form
 * "name + separator + more stuff + .nc", then removes the string after the
 * separator.
 */
std::string pism_filename_add_suffix(const std::string &filename,
                                     const std::string &separator,
                                     const std::string &suffix) {
  std::string basename = filename, result;

  // find where the separator begins:
  std::string::size_type j = basename.rfind(separator);
  if (j == std::string::npos) {
    j = basename.rfind(".nc");
  }

  // if the separator was not found, find the .nc suffix:
  if (j == std::string::npos) {
    j = basename.size();
  }

  // cut off everything starting from the separator (or the .nc suffix):
  basename.resize(static_cast<int>(j));

  result = basename + separator + suffix;

  if (ends_with(filename, ".nc")) {
    result += ".nc";
  }

  return result;
}

PetscLogDouble GetTime() {
  PetscLogDouble result;
#if PETSC_VERSION_LT(3,4,0)
  PetscErrorCode ierr = PetscGetTime(&result);
  PISM_CHK(ierr, "PetscGetTime");
#else
  PetscErrorCode ierr = PetscTime(&result);
  PISM_CHK(ierr, "PetscTime");
#endif
  return result;
}

// PETSc profiling events

Profiling::Profiling() {
  PetscErrorCode ierr = PetscClassIdRegister("PISM", &m_classid);
  PISM_CHK(ierr, "PetscClassIdRegister");
}

void Profiling::begin(const char * name) const {
  PetscLogEvent event = 0;
  PetscErrorCode ierr;

  if (m_events.find(name) == m_events.end()) {
    // not registered yet
    ierr = PetscLogEventRegister(name, m_classid, &event);
    PISM_CHK(ierr, "PetscLogEventRegister");
    m_events[name] = event;
  } else {
    event = m_events[name];
  }
  ierr = PetscLogEventBegin(event, 0, 0, 0, 0);
  PISM_CHK(ierr, "PetscLogEventBegin");
}

void Profiling::end(const char * name) const {
  PetscLogEvent event = 0;
  if (m_events.find(name) == m_events.end()) {
    abort();                    // should never happen
  } else {
    event = m_events[name];
  }
  PetscErrorCode ierr = PetscLogEventEnd(event, 0, 0, 0, 0);
  PISM_CHK(ierr, "PetscLogEventEnd");
}

void Profiling::stage_begin(const char * name) const {
  PetscLogStage stage = 0;
  PetscErrorCode ierr;

  if (m_stages.find(name) == m_stages.end()) {
    // not registered yet
    ierr = PetscLogStageRegister(name, &stage);
    PISM_CHK(ierr, "PetscLogStageRegister");
    m_stages[name] = stage;
  } else {
    stage = m_stages[name];
  }
  ierr = PetscLogStagePush(stage);
  PISM_CHK(ierr, "PetscLogStagePush");
}

void Profiling::stage_end(const char * name) const {
  (void) name;
  PetscErrorCode ierr = PetscLogStagePop();
  PISM_CHK(ierr, "PetscLogStagePop");
}

} // end of namespace pism
