// Copyright (C) 2008-2018, 2020, 2021, 2022, 2023, 2025 Ed Bueler and Constantine Khroulev
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

#ifndef PISM_COMPONENT_H
#define PISM_COMPONENT_H

#include <string>

#include "pism/util/Config.hh"
#include "pism/util/Units.hh"
#include "pism/util/Logger.hh"
#include "pism/util/Diagnostic.hh"

namespace pism {

class MaxTimestep;
class File;
class Geometry;
class Time;
class Profiling;
class Grid;

namespace array {
template<typename T> class Array2D;
class Array3D;
class Array;
class CellType1;
class CellType2;
class CellType;
class Forcing;
class Scalar1;
class Scalar2;
class Scalar;
class Staggered1;
class Staggered;
class Vector1;
class Vector2;
class Vector;
} // end of namespace array

enum InitializationType {INIT_RESTART, INIT_BOOTSTRAP, INIT_OTHER};

struct InputOptions {
  InputOptions(InitializationType t, const std::string &file, unsigned int index);
  //! initialization type
  InitializationType type;
  //! name of the input file (if applicable)
  std::string filename;
  //! index of the record to re-start from
  unsigned int record;
};

InputOptions process_input_options(MPI_Comm com, std::shared_ptr<const Config> config);

//! \brief A class defining a common interface for most PISM sub-models.
/*!
  \section pism_components PISM's model components and their interface

  We've found that many sub-models in PISM share some tasks: they need to be
  "initialized", "updated", asked for diagnostic quantities, asked to write the
  model state...

  Component and its derived classes were created to have a common interface
  for PISM sub-models, such as surface, atmosphere, ocean and bed deformation
  models.

  \subsection pismcomponent_init Initialization

  Component::init() should contain all the initialization code,
  excluding memory-allocation. (We might need to "re-initialize" a
  component.)

  Many PISM sub-models read data from the same file the rest of PISM reads
  from. Component::find_pism_input() checks options `-i` and `-bootstrap`
  options to simplify finding this file.

  \subsection pismcomponent_output Writing to an output file

  A PISM component needs to implement the following I/O methods:

  - define_model_state_impl()
  - write_model_state_impl()

  Why are all these methods needed? In PISM we separate defining and writing
  NetCDF variables because defining all the NetCDF variables before writing
  data is a lot faster than defining a variable, writing it, defining the
  second variable, etc. (See http://www.unidata.ucar.edu/software/netcdf/docs/netcdf/Parts-of-a-NetCDF-Classic-File.html#Parts-of-a-NetCDF-Classic-File for a technical explanation.)

  Within IceModel the following steps are done to write 2D and 3D fields to an
  output file:

  - Assemble the list of variables to be written (see
  IceModel::output_variables()); calls add_vars_to_output()
  - Create a NetCDF file
  - Define all the variables in the file (see IceModel::write_variables());
  calls define_variables()
  - Write all the variables to the file (same method); calls write_variables().

  \subsection pismcomponent_timestep Restricting time-steps

  Implement Component::max_timestep() to affect PISM's adaptive time-stepping mechanism.
*/
class Component {
public:
  /** Create a Component instance given a grid. */
  Component(std::shared_ptr<const Grid> grid);
  virtual ~Component() = default;

  DiagnosticList diagnostics() const;
  TSDiagnosticList ts_diagnostics() const;

  std::shared_ptr<const Grid> grid() const;

  const Time &time() const;

  const Profiling &profiling() const;

  void define_model_state(const File &output) const;
  void write_model_state(const File &output) const;

  //! Reports the maximum time-step the model can take at time t.
  MaxTimestep max_timestep(double t) const;

protected:
  virtual MaxTimestep max_timestep_impl(double t) const;
  virtual void define_model_state_impl(const File &output) const;
  virtual void write_model_state_impl(const File &output) const;

  virtual DiagnosticList diagnostics_impl() const;
  virtual TSDiagnosticList ts_diagnostics_impl() const;

  /** @brief This flag determines whether a variable is read from the
      `-regrid_file` file even if it is not listed among variables in
      `-regrid_vars`.
  */
  enum RegriddingFlag { REGRID_WITHOUT_REGRID_VARS, NO_REGRID_WITHOUT_REGRID_VARS };
  void regrid(const std::string &module_name, array::Array &variable,
              RegriddingFlag flag = NO_REGRID_WITHOUT_REGRID_VARS);

  //! grid used by this component
  const std::shared_ptr<const Grid> m_grid;
  //! configuration database used by this component
  std::shared_ptr<const Config> m_config;
  //! unit system used by this component
  const units::System::Ptr m_sys;
  //! logger (for easy access)
  const Logger::ConstPtr m_log;
};

} // end of namespace pism

#endif // PISM_COMPONENT_H
