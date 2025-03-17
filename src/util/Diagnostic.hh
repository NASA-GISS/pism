// Copyright (C) 2010--2025 PISM Authors
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

#ifndef PISM_DIAGNOSTIC_HH
#define PISM_DIAGNOSTIC_HH

#include <map>
#include <memory>
#include <string>

#include "pism/util/Config.hh"
#include "pism/util/VariableMetadata.hh"
#include "pism/util/io/IO_Flags.hh"
#include "pism/util/array/Scalar.hh"
#include "pism/util/error_handling.hh"
#include "pism/util/io/File.hh"
#include "pism/util/io/io_helpers.hh"

namespace pism {

class Grid;

//! @brief Class representing diagnostic computations in PISM.
/*!
 * The main goal of this abstraction is to allow accessing metadata
 * corresponding to a diagnostic quantity *before* it is computed.
 *
 * Another goal is to create an interface for computing diagnostics *without*
 * knowing which PISM module is responsible for the computation.
 *
 * Technical note: to compute some diagnostic quantities we need access to
 * protected members of classes. C++ forbids obtaining pointers to non-static
 * methods of a class, but it is possible to define a (friend) function
 *
 * @code
 * std::shared_ptr<array::Array> compute_bar(Foo* model, ...);
 * @endcode
 *
 * which is the same as creating a method `Foo::compute_bar()`, but you *can*
 * get a pointer to it.
 *
 * Diagnostic creates a common interface for all these compute_bar
 * functions.
 */
class Diagnostic {
public:
  Diagnostic(std::shared_ptr<const Grid> g);
  virtual ~Diagnostic() = default;

  typedef std::shared_ptr<Diagnostic> Ptr;

  // defined below
  template <typename T>
  static Ptr wrap(const T &input);

  void update(double dt);
  void reset();

  //! @brief Compute a diagnostic quantity and return a pointer to a newly-allocated Array.
  std::shared_ptr<array::Array> compute() const;

  unsigned int n_variables() const;

  SpatialVariableMetadata &metadata(unsigned int N = 0);

  void define(const File &file, io::Type default_type) const;

  void init(const File &input, unsigned int time);
  void define_state(const File &output) const;
  void write_state(const File &output) const;

protected:
  virtual void define_impl(const File &file, io::Type default_type) const;
  virtual void init_impl(const File &input, unsigned int time);
  virtual void define_state_impl(const File &output) const;
  virtual void write_state_impl(const File &output) const;

  virtual void update_impl(double dt);
  virtual void reset_impl();

  virtual std::shared_ptr<array::Array> compute_impl() const = 0;

  double to_internal(double x) const;
  double to_external(double x) const;

  /*!
   * Allocate storage for an array of type `T` and copy metadata from `m_vars`.
   */
  template<typename T>
  std::shared_ptr<T> allocate(const std::string &name) const {
    auto result = std::make_shared<T>(m_grid, name);
    for (unsigned int k = 0; k < result->ndof(); ++k) {
      result->metadata(k) = m_vars.at(k);
    }
    return result;
  }

  //! the grid
  std::shared_ptr<const Grid> m_grid;
  //! the unit system
  const units::System::Ptr m_sys;
  //! Configuration flags and parameters
  std::shared_ptr<const Config> m_config;
  //! metadata corresponding to NetCDF variables
  std::vector<SpatialVariableMetadata> m_vars;
  //! fill value (used often enough to justify storing it)
  double m_fill_value;
};

typedef std::map<std::string, Diagnostic::Ptr> DiagnosticList;

/*!
 * Helper template wrapping quantities with dedicated storage in diagnostic classes.
 *
 * Note: Make sure that that created diagnostics don't outlast fields that they wrap (or you'll have
 * dangling pointers).
 */
template <class T>
class DiagWithDedicatedStorage : public Diagnostic {
public:
  DiagWithDedicatedStorage(const T &input) : Diagnostic(input.grid()), m_input(input) {
    for (unsigned int j = 0; j < input.ndof(); ++j) {
      m_vars.emplace_back(input.metadata(j));
    }
  }

protected:
  std::shared_ptr<array::Array> compute_impl() const {
    auto result = m_input.duplicate();

    result->set_name(m_input.get_name());
    for (unsigned int k = 0; k < m_vars.size(); ++k) {
      result->metadata(k) = m_vars[k];
    }

    result->copy_from(m_input);

    return result;
  }

  const T &m_input;
};

template <typename T>
Diagnostic::Ptr Diagnostic::wrap(const T &input) {
  return Ptr(new DiagWithDedicatedStorage<T>(input));
}

//! A template derived from Diagnostic, adding a "Model".
template <class Model>
class Diag : public Diagnostic {
public:
  Diag(const Model *m) : Diagnostic(m->grid()), model(m) {
  }

protected:
  const Model *model;
};

/*!
 * Report a time-averaged rate of change of a quantity by accumulating changes over several time
 * steps.
 */
template <class M>
class DiagAverageRate : public Diag<M> {
public:
  enum InputKind { TOTAL_CHANGE = 0, RATE = 1 };

  DiagAverageRate(const M *m, const std::string &name, InputKind kind)
      : Diag<M>(m),
        m_factor(1.0),
        m_input_kind(kind),
        m_accumulator(Diagnostic::m_grid, name + "_accumulator"),
        m_interval_length(0.0),
        m_time_since_reset(name + "_time_since_reset", Diagnostic::m_sys) {

    m_time_since_reset["units"]     = "seconds";
    m_time_since_reset["long_name"] = "time since " + m_accumulator.get_name() + " was reset to 0";

    m_accumulator.metadata()["long_name"] = "accumulator for the " + name + " diagnostic";

    m_accumulator.set(0.0);
  }

protected:
  void init_impl(const File &input, unsigned int time) {
    if (input.variable_exists(m_accumulator.get_name())) {
      m_accumulator.read(input, time);
    } else {
      m_accumulator.set(0.0);
    }

    if (input.variable_exists(m_time_since_reset.get_name())) {
      input.read_variable(m_time_since_reset.get_name(), { time }, { 1 }, // start, count
                          &m_interval_length);
    } else {
      m_interval_length = 0.0;
    }
  }

  void define_state_impl(const File &output) const {
    m_accumulator.define(output, io::PISM_DOUBLE);
    io::define_timeseries(m_time_since_reset,
                          Diagnostic::m_config->get_string("time.dimension_name"), output,
                          io::PISM_DOUBLE);
  }

  void write_state_impl(const File &output) const {
    m_accumulator.write(output);

    auto time_name = Diagnostic::m_config->get_string("time.dimension_name");

    unsigned int time_length = output.dimension_length(time_name);
    unsigned int t_start     = time_length > 0 ? time_length - 1 : 0;
    io::write_timeseries(output, m_time_since_reset, t_start, { m_interval_length });
  }

  virtual void update_impl(double dt) {
    // Here the "factor" is used to convert units (from m to kg m^-2, for example) and (possibly)
    // integrate over the time interval using the rectangle method.

    double factor = m_factor * (m_input_kind == TOTAL_CHANGE ? 1.0 : dt);

    m_accumulator.add(factor, this->model_input());

    m_interval_length += dt;
  }

  virtual void reset_impl() {
    m_accumulator.set(0.0);
    m_interval_length = 0.0;
  }

  virtual std::shared_ptr<array::Array> compute_impl() const {
    auto result = Diagnostic::allocate<array::Scalar>("diagnostic");

    if (m_interval_length > 0.0) {
      result->copy_from(m_accumulator);
      result->scale(1.0 / m_interval_length);
    } else {
      result->set(Diagnostic::to_internal(Diagnostic::m_fill_value));
    }

    return result;
  }

  // constants initialized in the constructor
  double m_factor;
  InputKind m_input_kind;
  // the state (read from and written to files)
  array::Scalar m_accumulator;
  // length of the reporting interval, accumulated along with the cumulative quantity
  double m_interval_length;
  VariableMetadata m_time_since_reset;

  // it should be enough to implement the constructor and this method
  virtual const array::Scalar &model_input() {
    throw RuntimeError::formatted(PISM_ERROR_LOCATION, "no default implementation");
  }
};

//! @brief PISM's scalar time-series diagnostics.
class TSDiagnostic {
public:
  typedef std::shared_ptr<TSDiagnostic> Ptr;

  TSDiagnostic(std::shared_ptr<const Grid> g, const std::string &name);
  virtual ~TSDiagnostic();

  void update(double t0, double t1);

  void flush();

  void init(const File &output_file, std::shared_ptr<std::vector<double> > requested_times);

  const VariableMetadata &metadata() const;

protected:
  virtual void update_impl(double t0, double t1) = 0;

  /*!
   * Compute the diagnostic. Regular (snapshot) quantity should be computed here; for rates of
   * change, compute() should return the total change during the time step from t0 to t1. The rate
   * itself is computed in evaluate_rate().
   */
  virtual double compute() = 0;

  /*!
   * Set internal (MKS) and "output" units.
   *
   * output_units is ignored if output.use_MKS is set.
   */
  void set_units(const std::string &units, const std::string &output_units);

  //! the grid
  std::shared_ptr<const Grid> m_grid;
  //! Configuration flags and parameters
  std::shared_ptr<const Config> m_config;
  //! the unit system
  const units::System::Ptr m_sys;

  //! time series object used to store computed values and metadata
  std::string m_time_name;

  VariableMetadata m_variable;
  VariableMetadata m_dimension;
  VariableMetadata m_time_bounds;

  // buffer for diagnostic time series
  std::vector<double> m_time;
  std::vector<double> m_bounds;
  std::vector<double> m_values;

  //! requested times
  std::shared_ptr<std::vector<double> > m_requested_times;
  //! index into m_times
  unsigned int m_current_time;

  //! the name of the file to save to (stored here because it is used by flush(), which is called
  //! from update())
  std::string m_output_filename;
  //! starting index used when flushing the buffer
  unsigned int m_start;
  //! size of the buffer used to store data
  size_t m_buffer_size;
};

typedef std::map<std::string, TSDiagnostic::Ptr> TSDiagnosticList;

//! Scalar diagnostic reporting a snapshot of a quantity modeled by PISM.
/*!
 * The method compute() should return the instantaneous "snapshot" value.
 */
class TSSnapshotDiagnostic : public TSDiagnostic {
public:
  TSSnapshotDiagnostic(std::shared_ptr<const Grid> g, const std::string &name);

private:
  void update_impl(double t0, double t1);
  void evaluate(double t0, double t1, double v);
};

//! Scalar diagnostic reporting the rate of change of a quantity modeled by PISM.
/*!
 * The rate of change is averaged in time over reporting intervals.
 *
 * The method compute() should return the instantaneous "snapshot" value of a quantity.
 */
class TSRateDiagnostic : public TSDiagnostic {
public:
  TSRateDiagnostic(std::shared_ptr<const Grid> g, const std::string &name);

protected:
  //! accumulator of changes (used to compute rates of change)
  double m_accumulator;
  void evaluate(double t0, double t1, double change);

private:
  void update_impl(double t0, double t1);

  //! last two values, used to compute the change during a time step
  double m_v_previous;
  bool m_v_previous_set;
};

//! Scalar diagnostic reporting a "flux".
/*!
 * The flux is averaged over reporting intervals.
 *
 * The method compute() should return the change due to a flux over a time step.
 *
 * Fluxes can be computed using TSRateDiagnostic, but that would require keeping track of the total
 * change due to a flux. It is possible for the magnitude of the total change to grow indefinitely,
 * leading to the loss of precision; this is why we use changes over individual time steps instead.
 *
 * (The total change due to a flux can grow in magnitude even it the amount does not change. For
 * example: if calving removes as much ice as we have added due to the SMB, the total mass is
 * constant, but total SMB will grow.)
 */
class TSFluxDiagnostic : public TSRateDiagnostic {
public:
  TSFluxDiagnostic(std::shared_ptr<const Grid> g, const std::string &name);

private:
  void update_impl(double t0, double t1);
};

template <class D, class M>
class TSDiag : public D {
public:
  TSDiag(const M *m, const std::string &name)
    : D(m->grid(), name), model(m) {
  }
protected:
  const M *model;
};

} // end of namespace pism

#endif /* PISM_DIAGNOSTIC_HH */
