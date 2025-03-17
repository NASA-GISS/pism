// Copyright (C) 2008-2020, 2023, 2024, 2025 Ed Bueler, Constantine Khroulev, Ricarda Winkelmann,
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

// Implementation of the atmosphere model using constant-in-time precipitation
// and a cosine yearly cycle for near-surface air temperatures.

#include <gsl/gsl_math.h>       // M_PI

#include "pism/coupler/atmosphere/YearlyCycle.hh"
#include "pism/util/Time.hh"
#include "pism/util/Grid.hh"
#include "pism/util/Config.hh"
#include "pism/util/io/io_helpers.hh"
#include "pism/util/Logger.hh"

namespace pism {
namespace atmosphere {

YearlyCycle::YearlyCycle(std::shared_ptr<const Grid> g)
  : AtmosphereModel(g),
    m_air_temp_mean_annual(m_grid, "air_temp_mean_annual"),
    m_air_temp_mean_summer(m_grid, "air_temp_mean_summer"),
    m_precipitation(m_grid, "precipitation") {

  m_snow_temp_summer_day = m_config->get_number("atmosphere.fausto_air_temp.summer_peak_day");

  m_air_temp_mean_annual.metadata(0)
      .long_name(
          "mean annual near-surface air temperature (without sub-year time-dependence or forcing)")
      .units("kelvin");
  m_air_temp_mean_annual.metadata()["source"] = m_reference;

  m_air_temp_mean_summer.metadata(0)
      .long_name(
          "mean summer (NH: July/ SH: January) near-surface air temperature (without sub-year time-dependence or forcing)")
      .units("kelvin");
  m_air_temp_mean_summer.metadata()["source"] = m_reference;

  m_precipitation.metadata(0)
      .long_name("precipitation rate")
      .units("kg m^-2 second^-1")
      .output_units("kg m^-2 year^-1")
      .standard_name("precipitation_flux")
      .set_time_independent(true);
}

//! Reads in the precipitation data from the input file.
void YearlyCycle::init_impl(const Geometry &geometry) {
  (void) geometry;

  InputOptions opts = process_input_options(m_grid->com, m_config);
  init_internal(opts.filename, opts.type == INIT_BOOTSTRAP, opts.record);
}

//! Read precipitation data from a given file.
void YearlyCycle::init_internal(const std::string &input_filename, bool do_regrid,
                                unsigned int start) {
  // read precipitation rate from file
  m_log->message(2,
             "    reading mean annual ice-equivalent precipitation rate 'precipitation'\n"
             "      from %s ... \n",
             input_filename.c_str());
  if (do_regrid == true) {
    m_precipitation.regrid(input_filename, io::Default::Nil()); // fails if not found!
  } else {
    m_precipitation.read(input_filename, start); // fails if not found!
  }
}

void YearlyCycle::define_model_state_impl(const File &output) const {
  m_precipitation.define(output, io::PISM_DOUBLE);
}

void YearlyCycle::write_model_state_impl(const File &output) const {
  m_precipitation.write(output);
}

//! Copies the stored precipitation field into result.
const array::Scalar& YearlyCycle::precipitation_impl() const {
  return m_precipitation;
}

//! Copies the stored mean annual near-surface air temperature field into result.
const array::Scalar& YearlyCycle::air_temperature_impl() const {
  return m_air_temp_mean_annual;
}

//! Copies the stored mean summer near-surface air temperature field into result.
const array::Scalar& YearlyCycle::mean_summer_temp() const {
  return m_air_temp_mean_summer;
}

void YearlyCycle::init_timeseries_impl(const std::vector<double> &ts) const {
  // constants related to the standard yearly cycle
  const double
    summerday_fraction = time().day_of_the_year_to_year_fraction(m_snow_temp_summer_day);

  size_t N = ts.size();

  m_ts_times.resize(N);
  m_cosine_cycle.resize(N);
  for (unsigned int k = 0; k < m_ts_times.size(); k++) {
    double tk = time().year_fraction(ts[k]) - summerday_fraction;

    m_ts_times[k] = ts[k];
    m_cosine_cycle[k] = cos(2.0 * M_PI * tk);
  }
}

void YearlyCycle::precip_time_series_impl(int i, int j, std::vector<double> &result) const {
  result.resize(m_ts_times.size());
  for (unsigned int k = 0; k < m_ts_times.size(); k++) {
    result[k] = m_precipitation(i,j);
  }
}

void YearlyCycle::temp_time_series_impl(int i, int j, std::vector<double> &result) const {
  result.resize(m_ts_times.size());
  for (unsigned int k = 0; k < m_ts_times.size(); ++k) {
    result[k] = m_air_temp_mean_annual(i,j) + (m_air_temp_mean_summer(i,j) - m_air_temp_mean_annual(i,j)) * m_cosine_cycle[k];
  }
}

void YearlyCycle::begin_pointwise_access_impl() const {
  m_air_temp_mean_annual.begin_access();
  m_air_temp_mean_summer.begin_access();
  m_precipitation.begin_access();
}

void YearlyCycle::end_pointwise_access_impl() const {
  m_air_temp_mean_annual.end_access();
  m_air_temp_mean_summer.end_access();
  m_precipitation.end_access();
}

namespace diagnostics {

/*! @brief Mean summer near-surface air temperature. */
class MeanSummerTemperature : public Diag<YearlyCycle>
{
public:
  MeanSummerTemperature(const YearlyCycle *m) : Diag<YearlyCycle>(m) {
    m_vars = { { m_sys, "air_temp_mean_summer" } };
    m_vars[0]
        .long_name("mean summer near-surface air temperature used in the cosine yearly cycle")
        .units("kelvin");
  }

private:
  std::shared_ptr<array::Array> compute_impl() const {
    auto result = allocate<array::Scalar>("air_temp_mean_summer");

    result->copy_from(model->mean_summer_temp());

    return result;
  }
};
} // end of namespace diagnostics

DiagnosticList YearlyCycle::diagnostics_impl() const {
  DiagnosticList result = AtmosphereModel::diagnostics_impl();

  result["air_temp_mean_summer"] = Diagnostic::Ptr(new diagnostics::MeanSummerTemperature(this));

  return result;
}

} // end of namespace atmosphere
} // end of namespace pism
