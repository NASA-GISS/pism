/* Copyright (C) 2016, 2017, 2018, 2019, 2020, 2021, 2022, 2023, 2024 PISM Authors
 *
 * This file is part of PISM.
 *
 * PISM is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 3 of the License, or (at your option) any later
 * version.
 *
 * PISM is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PISM; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "pism/energy/EnergyModel.hh"
#include "pism/energy/utilities.hh"
#include "pism/stressbalance/StressBalance.hh"
#include "pism/util/MaxTimestep.hh"
#include "pism/util/Profiling.hh"
#include "pism/util/Vars.hh"
#include "pism/util/array/CellType.hh"
#include "pism/util/error_handling.hh"
#include "pism/util/io/File.hh"
#include "pism/util/pism_utilities.hh"

namespace pism {
namespace energy {

static void check_input(const array::Array *ptr, const char *name) {
  if (ptr == NULL) {
    throw RuntimeError::formatted(PISM_ERROR_LOCATION, "energy balance model input %s was not provided", name);
  }
}

Inputs::Inputs() {
  basal_frictional_heating = NULL;
  basal_heat_flux          = NULL;
  cell_type                = NULL;
  ice_thickness            = NULL;
  surface_liquid_fraction  = NULL;
  shelf_base_temp          = NULL;
  surface_temp             = NULL;
  till_water_thickness     = NULL;

  volumetric_heating_rate  = NULL;
  u3                       = NULL;
  v3                       = NULL;
  w3                       = NULL;

  no_model_mask = NULL;
}

void Inputs::check() const {
  check_input(cell_type,                "cell_type");
  check_input(basal_frictional_heating, "basal_frictional_heating");
  check_input(basal_heat_flux,          "basal_heat_flux");
  check_input(ice_thickness,            "ice_thickness");
  check_input(surface_liquid_fraction,  "surface_liquid_fraction");
  check_input(shelf_base_temp,          "shelf_base_temp");
  check_input(surface_temp,             "surface_temp");
  check_input(till_water_thickness,     "till_water_thickness");

  check_input(volumetric_heating_rate, "volumetric_heating_rate");
  check_input(u3, "u3");
  check_input(v3, "v3");
  check_input(w3, "w3");
}

EnergyModelStats::EnergyModelStats() {
  bulge_counter            = 0;
  reduced_accuracy_counter = 0;
  low_temperature_counter  = 0;
  liquified_ice_volume     = 0.0;
}

EnergyModelStats& EnergyModelStats::operator+=(const EnergyModelStats &other) {
  bulge_counter            += other.bulge_counter;
  reduced_accuracy_counter += other.reduced_accuracy_counter;
  low_temperature_counter  += other.low_temperature_counter;
  liquified_ice_volume     += other.liquified_ice_volume;
  return *this;
}


bool marginal(const array::Scalar1 &thickness, int i, int j, double threshold) {
  auto H = thickness.box(i, j);

  return ((H.e < threshold) or (H.ne < threshold) or (H.n < threshold) or (H.nw < threshold) or
          (H.w < threshold) or (H.sw < threshold) or (H.s < threshold) or (H.se < threshold));
}


void EnergyModelStats::sum(MPI_Comm com) {
  bulge_counter            = GlobalSum(com, bulge_counter);
  reduced_accuracy_counter = GlobalSum(com, reduced_accuracy_counter);
  low_temperature_counter  = GlobalSum(com, low_temperature_counter);
  liquified_ice_volume     = GlobalSum(com, liquified_ice_volume);
}


EnergyModel::EnergyModel(std::shared_ptr<const Grid> grid,
                         std::shared_ptr<const stressbalance::StressBalance> stress_balance)
    : Component(grid),
      m_ice_enthalpy(m_grid, "enthalpy", array::WITH_GHOSTS, m_grid->z(),
                     m_config->get_number("grid.max_stencil_width")),
      m_work(m_grid, "work_vector", array::WITHOUT_GHOSTS, m_grid->z()),
      m_basal_melt_rate(m_grid, "basal_melt_rate_grounded"),
      m_stress_balance(stress_balance) {

  // POSSIBLE standard name = land_ice_enthalpy
  m_ice_enthalpy.metadata(0)
      .long_name("ice enthalpy (includes sensible heat, latent heat, pressure)")
      .units("J kg^-1");

  {
    // ghosted to allow the "redundant" computation of tauc
    m_basal_melt_rate.metadata(0)
        .long_name(
            "ice basal melt rate from energy conservation, in ice thickness per time (valid in grounded areas)")
        .units("m s^-1");
    // We could use land_ice_basal_melt_rate, but that way both basal_melt_rate_grounded and bmelt
    // have this standard name.
    m_basal_melt_rate.metadata()["comment"] = "positive basal melt rate corresponds to ice loss";
  }

  // a 3d work vector
  m_work.metadata(0).long_name("usually new values of temperature or enthalpy during time step");
}

void EnergyModel::init_enthalpy(const File &input_file, bool do_regrid, int record) {

  if (input_file.variable_exists("enthalpy")) {
    if (do_regrid) {
      m_ice_enthalpy.regrid(input_file, io::Default::Nil());
    } else {
      m_ice_enthalpy.read(input_file, record);
    }
  } else if (input_file.variable_exists("temp")) {
    array::Array3D &temp = m_work, &liqfrac = m_ice_enthalpy;

    {
      temp.set_name("temp");
      temp.metadata(0).set_name("temp");
      temp.metadata(0)
          .long_name("ice temperature")
          .units("kelvin")
          .standard_name("land_ice_temperature");

      if (do_regrid) {
        temp.regrid(input_file, io::Default::Nil());
      } else {
        temp.read(input_file, record);
      }
    }

    const array::Scalar &ice_thickness = *m_grid->variables().get_2d_scalar("land_ice_thickness");

    if (input_file.variable_exists("liqfrac")) {
      SpatialVariableMetadata enthalpy_metadata = m_ice_enthalpy.metadata();

      liqfrac.set_name("liqfrac");
      liqfrac.metadata(0).set_name("liqfrac");
      liqfrac.metadata(0).long_name("ice liquid water fraction").units("1");

      if (do_regrid) {
        liqfrac.regrid(input_file, io::Default::Nil());
      } else {
        liqfrac.read(input_file, record);
      }

      m_ice_enthalpy.set_name(enthalpy_metadata.get_name());
      m_ice_enthalpy.metadata() = enthalpy_metadata;

      m_log->message(2,
                     " - Computing enthalpy using ice temperature,"
                     "  liquid water fraction and thickness...\n");
      compute_enthalpy(temp, liqfrac, ice_thickness, m_ice_enthalpy);
    } else {
      m_log->message(2,
                     " - Computing enthalpy using ice temperature and thickness "
                     "and assuming zero liquid water fraction...\n");
      compute_enthalpy_cold(temp, ice_thickness, m_ice_enthalpy);
    }
  } else {
    throw RuntimeError::formatted(PISM_ERROR_LOCATION,
                                  "neither enthalpy nor temperature was found in '%s'.\n",
                                  input_file.name().c_str());
  }
}

/*!
 * The `-regrid_file` may contain enthalpy, temperature, or *both* temperature and liquid water
 * fraction.
 */
void EnergyModel::regrid_enthalpy() {

  auto regrid_filename = m_config->get_string("input.regrid.file");
  auto regrid_vars     = set_split(m_config->get_string("input.regrid.vars"), ',');


  if (regrid_filename.empty()) {
    return;
  }

  std::string enthalpy_name = m_ice_enthalpy.metadata().get_name();

  if (regrid_vars.empty() or member(enthalpy_name, regrid_vars)) {
    File regrid_file(m_grid->com, regrid_filename, io::PISM_GUESS, io::PISM_READONLY);
    init_enthalpy(regrid_file, true, 0);
  }
}


void EnergyModel::restart(const File &input_file, int record) {
  this->restart_impl(input_file, record);
}

void EnergyModel::bootstrap(const File &input_file,
                            const array::Scalar &ice_thickness,
                            const array::Scalar &surface_temperature,
                            const array::Scalar &climatic_mass_balance,
                            const array::Scalar &basal_heat_flux) {
  this->bootstrap_impl(input_file,
                       ice_thickness, surface_temperature,
                       climatic_mass_balance, basal_heat_flux);
}

void EnergyModel::initialize(const array::Scalar &basal_melt_rate,
                             const array::Scalar &ice_thickness,
                             const array::Scalar &surface_temperature,
                             const array::Scalar &climatic_mass_balance,
                             const array::Scalar &basal_heat_flux) {
  this->initialize_impl(basal_melt_rate,
                        ice_thickness,
                        surface_temperature,
                        climatic_mass_balance,
                        basal_heat_flux);
}

void EnergyModel::update(double t, double dt, const Inputs &inputs) {
  // reset standard out flags at the beginning of every time step
  m_stdout_flags = "";
  m_stats = EnergyModelStats();

  profiling().begin("ice_energy");
  {
    // this call should fill m_work with new values of enthalpy
    this->update_impl(t, dt, inputs);

    m_ice_enthalpy.copy_from(m_work);
  }
  profiling().end("ice_energy");

  // globalize m_stats and update m_stdout_flags
  {
    m_stats.sum(m_grid->com);

    if (m_stats.reduced_accuracy_counter > 0.0) { // count of when BOMBPROOF switches to lower accuracy
      const double reduced_accuracy_percentage = 100.0 * (m_stats.reduced_accuracy_counter / (m_grid->Mx() * m_grid->My()));
      const double reporting_threshold = 5.0; // only report if above 5%

      if (reduced_accuracy_percentage > reporting_threshold and m_log->get_threshold() > 2) {
        m_stdout_flags = (pism::printf("  [BPsacr=%.4f%%] ", reduced_accuracy_percentage) +
                          m_stdout_flags);
      }
    }

    if (m_stats.bulge_counter > 0) {
      // count of when advection bulges are limited; frequently it is identically zero
      m_stdout_flags = (pism::printf(" BULGE=%d ", m_stats.bulge_counter) +
                        m_stdout_flags);
    }
  }
}

MaxTimestep EnergyModel::max_timestep_impl(double t) const {
  // silence a compiler warning
  (void) t;

  if (m_stress_balance == NULL) {
    throw RuntimeError::formatted(PISM_ERROR_LOCATION,
                                  "EnergyModel: no stress balance provided."
                                  " Cannot compute max. time step.");
  }

  return MaxTimestep(m_stress_balance->max_timestep_cfl_3d().dt_max.value(), "energy");
}

const std::string& EnergyModel::stdout_flags() const {
  return m_stdout_flags;
}

const EnergyModelStats& EnergyModel::stats() const {
  return m_stats;
}

const array::Array3D & EnergyModel::enthalpy() const {
  return m_ice_enthalpy;
}

/*! @brief Basal melt rate in grounded areas. (It is set to zero elsewhere.) */
const array::Scalar & EnergyModel::basal_melt_rate() const {
  return m_basal_melt_rate;
}

/*! @brief Ice loss "flux" due to ice liquefaction. */
class LiquifiedIceFlux : public TSDiag<TSFluxDiagnostic,EnergyModel> {
public:
  LiquifiedIceFlux(const EnergyModel *m)
    : TSDiag<TSFluxDiagnostic, EnergyModel>(m, "liquified_ice_flux") {

    set_units("m^3 / second", "m^3 / year");
    m_variable["long_name"] =
      "rate of ice loss due to liquefaction, averaged over the reporting interval";
    m_variable["comment"]      = "positive means ice loss";
    m_variable["cell_methods"] = "time: mean";
  }
protected:
  double compute() {
    // liquified ice volume during the last time step
    return model->stats().liquified_ice_volume;
  }
};

DiagnosticList EnergyModel::diagnostics_impl() const {
  DiagnosticList result;
  result = {
    {"enthalpy",                 Diagnostic::wrap(m_ice_enthalpy)},
    {"basal_melt_rate_grounded", Diagnostic::wrap(m_basal_melt_rate)}
  };
  return result;
}

TSDiagnosticList EnergyModel::ts_diagnostics_impl() const {
  return {
    {"liquified_ice_flux", TSDiagnostic::Ptr(new LiquifiedIceFlux(this))}
  };
}

} // end of namespace energy

} // end of namespace pism
