// Copyright (C) 2008-2018, 2021, 2022 Ed Bueler, Constantine Khroulev, Ricarda Winkelmann,
// Gudfinna Adalgeirsdottir and Andy Aschwanden
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

#ifndef __AtmosphereModel
#define __AtmosphereModel

#include <vector>

#include "pism/util/Component.hh"

namespace pism {

//! @brief Atmosphere models and modifiers: provide precipitation and
//! temperature to a surface::SurfaceModel below
namespace atmosphere {
//! A purely virtual class defining the interface of a PISM Atmosphere Model.
class AtmosphereModel : public Component {
public:
  AtmosphereModel(std::shared_ptr<const Grid> g);
  AtmosphereModel(std::shared_ptr<const Grid> g, std::shared_ptr<AtmosphereModel> input);
  virtual ~AtmosphereModel() = default;

  void init(const Geometry &geometry);

  void update(const Geometry &geometry, double t, double dt);

  //! @brief Sets result to the mean precipitation, in "kg m-2 second-1".
  const array::Scalar& precipitation() const;

  //! @brief Sets result to the mean near-surface air temperature, in kelvin.
  const array::Scalar& air_temperature() const;

  void begin_pointwise_access() const;
  void end_pointwise_access() const;
  void init_timeseries(const std::vector<double> &ts) const;
  //! \brief Sets a pre-allocated N-element array "result" to the time-series of
  //! ice-equivalent precipitation (m/s) at the point i,j on the grid.
  //!
  //! See temp_time_series() for more.
  void precip_time_series(int i, int j, std::vector<double> &result) const;

  //! \brief Sets a pre-allocated N-element array "result" to the time-series
  //! of near-surface air temperature (kelvin) at the point i,j on the
  //! grid. Times (in years) are specified in ts. NB! Has to be surrounded by
  //! begin_pointwise_access() and end_pointwise_access()
  void temp_time_series(int i, int j, std::vector<double> &result) const;
protected:
  virtual void init_impl(const Geometry &geometry) = 0;
  virtual void update_impl(const Geometry &geometry, double t, double dt) = 0;
  virtual void define_model_state_impl(const File &output) const;
  virtual void write_model_state_impl(const File &output) const;

  virtual MaxTimestep max_timestep_impl(double my_t) const;

  virtual const array::Scalar& precipitation_impl() const;
  virtual const array::Scalar& air_temperature_impl() const;

  virtual void begin_pointwise_access_impl() const;
  virtual void end_pointwise_access_impl() const;
  virtual void init_timeseries_impl(const std::vector<double> &ts) const;
  virtual void precip_time_series_impl(int i, int j, std::vector<double> &result) const;
  virtual void temp_time_series_impl(int i, int j, std::vector<double> &result) const;

  virtual DiagnosticList diagnostics_impl() const;
  virtual TSDiagnosticList ts_diagnostics_impl() const;
protected:
  mutable std::vector<double> m_ts_times;

  std::shared_ptr<AtmosphereModel> m_input_model;

  static std::shared_ptr<array::Scalar> allocate_temperature(std::shared_ptr<const Grid> grid);
  static std::shared_ptr<array::Scalar> allocate_precipitation(std::shared_ptr<const Grid> grid);
};

} // end of namespace atmosphere
} // end of namespace pism

#endif  // __AtmosphereModel
