// Copyright (C) 2004-2024 Jed Brown, Ed Bueler and Constantine Khroulev
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

#include <cmath>
#include <cstring>

#include <algorithm> // required by sort(...) in test L
#include <memory>
#include <vector> // STL vector container; sortable; used in test L

#include "pism/verification/tests/exactTestH.h"
#include "pism/verification/tests/exactTestL.hh"
#include "pism/verification/tests/exactTestsABCD.h"
#include "pism/verification/tests/exactTestsFG.hh"

#include "pism/verification/BTU_Verification.hh"
#include "pism/verification/PSVerification.hh"
#include "pism/verification/TemperatureModel_Verification.hh"
#include "pism/verification/iceCompModel.hh"
#include "pism/coupler/SeaLevel.hh"
#include "pism/coupler/ocean/Constant.hh"
#include "pism/earth/BedDef.hh"
#include "pism/energy/BTU_Minimal.hh"
#include "pism/rheology/PatersonBuddCold.hh"
#include "pism/stressbalance/ShallowStressBalance.hh"
#include "pism/stressbalance/StressBalance.hh"
#include "pism/stressbalance/sia/SIAFD.hh"
#include "pism/util/ConfigInterface.hh"
#include "pism/util/Context.hh"
#include "pism/util/EnthalpyConverter.hh"
#include "pism/util/Grid.hh"
#include "pism/util/Logger.hh"
#include "pism/util/Mask.hh"
#include "pism/util/Time.hh"
#include "pism/util/error_handling.hh"
#include "pism/util/io/File.hh"
#include "pism/util/io/io_helpers.hh"
#include "pism/util/pism_options.hh"
#include "pism/util/pism_utilities.hh"

namespace pism {

using units::convert;

IceCompModel::IceCompModel(std::shared_ptr<Grid> grid, std::shared_ptr<Context> context, int test)
    : IceModel(grid, context),
      m_testname(test),
      m_HexactL(m_grid, "HexactL"),
      m_strain_heating3_comp(m_grid, "strain_heating_comp", array::WITHOUT_GHOSTS, m_grid->z()),
      m_bedrock_is_ice_forK(false) {

  m_log->message(2, "starting Test %c ...\n", m_testname);

  // Override some defaults from parent class
  m_config->set_number("stress_balance.sia.enhancement_factor", 1.0);
  // none use bed smoothing & bed roughness parameterization
  m_config->set_number("stress_balance.sia.bed_smoother.range", 0.0);

  // set values of flags in run()
  m_config->set_flag("geometry.update.enabled", true);
  m_config->set_flag("geometry.update.use_basal_melt_rate", false);

  // flow law settings
  switch (m_testname) {
  case 'A':
  case 'B':
  case 'C':
  case 'D':
  case 'H':
  case 'L': {
    m_config->set_string("stress_balance.sia.flow_law", "isothermal_glen");
    const double year = convert(m_sys, 1.0, "year", "seconds");
    m_config->set_number("flow_law.isothermal_Glen.ice_softness", 1.0e-16 / year);
    break;
  }
  case 'V': {
    m_config->set_string("stress_balance.ssa.flow_law", "isothermal_glen");
    const double hardness = 1.9e8,
                 softness =
                     pow(hardness, -m_config->get_number("stress_balance.ssa.Glen_exponent"));
    m_config->set_number("flow_law.isothermal_Glen.ice_softness", softness);
    break;
  }
  case 'F':
  case 'G':
  case 'K':
  case 'O':
  default: {
    m_config->set_string("stress_balance.sia.flow_law", "arr");
    break;
  }
  }

  if (m_testname == 'H') {
    m_config->set_string("bed_deformation.model", "iso");
  } else {
    m_config->set_string("bed_deformation.model", "none");
  }

  if ((m_testname == 'F') || (m_testname == 'G') || (m_testname == 'K') || (m_testname == 'O')) {
    m_config->set_string("energy.model", "cold");
    // essentially turn off run-time reporting of extremely low computed
    // temperatures; *they will be reported as errors* anyway
    m_config->set_number("energy.minimum_allowed_temperature", 0.0);
    m_config->set_number("energy.max_low_temperature_count", 1000000);
  } else {
    m_config->set_string("energy.model", "none");
  }

  // set sea level to -1e4 to ensure that all ice is grounded
  m_config->set_number("sea_level.constant.value", -1e4);

  // special considerations for K and O wrt thermal bedrock and pressure-melting
  if ((m_testname == 'K') || (m_testname == 'O')) {
    m_config->set_flag("energy.allow_temperature_above_melting", false);
  } else {
    // note temps are generally allowed to go above pressure melting in verify
    m_config->set_flag("energy.allow_temperature_above_melting", true);
  }

  if (m_testname == 'V') {
    // no sub-shelf melting
    m_config->set_flag("geometry.update.use_basal_melt_rate", false);

    // this test is isothermal
    m_config->set_string("energy.model", "none");

    // use the SSA solver
    m_config->set_string("stress_balance_model", "ssa");

    // set sea level to 0.0
    m_config->set_number("sea_level.constant.value", 0.0);

    m_config->set_flag("stress_balance.ssa.dirichlet_bc", true);
  }
}

void IceCompModel::allocate_storage() {

  IceModel::allocate_storage();

  m_strain_heating3_comp.metadata(0)
      .long_name("rate of compensatory strain heating in ice")
      .units("W m^-3");
}

void IceCompModel::allocate_bedrock_thermal_unit() {

  if (m_btu != NULL) {
    return;
  }

  // this switch changes Test K to make material properties for bedrock the same as for ice
  bool biiSet = options::Bool("-bedrock_is_ice", "set bedrock properties to those of ice");
  if (biiSet) {
    if (m_testname == 'K') {
      m_log->message(1, "setting material properties of bedrock to those of ice in Test K\n");
      m_config->set_number("energy.bedrock_thermal.density",
                           m_config->get_number("constants.ice.density"));
      m_config->set_number("energy.bedrock_thermal.conductivity",
                           m_config->get_number("constants.ice.thermal_conductivity"));
      m_config->set_number("energy.bedrock_thermal.specific_heat_capacity",
                           m_config->get_number("constants.ice.specific_heat_capacity"));
      m_bedrock_is_ice_forK = true;
    } else {
      m_log->message(
          1, "IceCompModel WARNING: option -bedrock_is_ice ignored; only applies to Test K\n");
    }
  }

  if (m_testname != 'K') {
    // now make bedrock have same material properties as ice
    // (note Mbz=1 also, by default, but want ice/rock interface to see
    // pure ice from the point of view of applying geothermal boundary
    // condition, especially in tests F and G)
    m_config->set_number("energy.bedrock_thermal.density",
                         m_config->get_number("constants.ice.density"));
    m_config->set_number("energy.bedrock_thermal.conductivity",
                         m_config->get_number("constants.ice.thermal_conductivity"));
    m_config->set_number("energy.bedrock_thermal.specific_heat_capacity",
                         m_config->get_number("constants.ice.specific_heat_capacity"));
  }

  auto bed_vertical_grid = energy::BTUGrid::FromOptions(m_grid->ctx());

  if (bed_vertical_grid.Mbz > 1) {
    m_btu = std::make_shared<energy::BTU_Verification>(m_grid, bed_vertical_grid,
                                                       m_testname, m_bedrock_is_ice_forK);
  } else {
    m_btu = std::make_shared<energy::BTU_Minimal>(m_grid);
  }

  m_submodels["bedrock thermal layer"] = m_btu.get();
}

void IceCompModel::allocate_energy_model() {

  if (m_energy_model != nullptr) {
    return;
  }

  m_log->message(2, "# Allocating an energy balance model...\n");

  // this switch changes Test K to make material properties for bedrock the same as for ice
  bool bedrock_is_ice = options::Bool("-bedrock_is_ice", "set bedrock properties to those of ice");

  m_energy_model = std::make_shared<energy::TemperatureModel_Verification>(
      m_grid, m_stress_balance, m_testname, bedrock_is_ice);

  m_submodels["energy balance model"] = m_energy_model.get();
}


void IceCompModel::allocate_bed_deformation() {

  IceModel::allocate_bed_deformation();

  // for simple isostasy
  m_f = m_config->get_number("constants.ice.density") / m_config->get_number("bed_deformation.mantle_density");

  std::string bed_def_model = m_config->get_string("bed_deformation.model");

  if ((m_testname == 'H') && bed_def_model != "iso") {
    m_log->message(1,
                   "IceCompModel WARNING: Test H should be run with option\n"
                   "  '-bed_def iso'  for the reported errors to be correct.\n");
  }
}

void IceCompModel::allocate_couplers() {
  EnthalpyConverter::Ptr EC = m_ctx->enthalpy_converter();

  // Climate will always come from verification test formulas.
  m_surface.reset(new surface::Verification(m_grid, EC, m_testname));
  m_submodels["surface process model"] = m_surface.get();

  m_ocean.reset(new ocean::Constant(m_grid));
  m_submodels["ocean model"] = m_ocean.get();

  m_sea_level.reset(new ocean::sea_level::SeaLevel(m_grid));
  m_submodels["sea level forcing"] = m_sea_level.get();
}

void IceCompModel::bootstrap_2d(const File &/*input_file*/) {
  throw RuntimeError(PISM_ERROR_LOCATION,
                     "PISM does not support bootstrapping in verification mode.");
}

void IceCompModel::initialize_2d() {
  m_log->message(3, "initializing Test %c from formulas ...\n",m_testname);

  m_geometry.bed_elevation.set(0.0);
  m_geometry.sea_level_elevation.set(0.0);

  array::Scalar uplift(m_grid, "uplift");
  uplift.set(0.0);

  m_beddef->bootstrap(m_geometry.bed_elevation,
                      uplift,
                      m_geometry.ice_thickness,
                      m_geometry.sea_level_elevation);

  // Test-specific initialization:
  switch (m_testname) {
  case 'A':
  case 'B':
  case 'C':
  case 'D':
  case 'H':
    initTestABCDH();
    break;
  case 'F':
  case 'G':
    initTestFG();  // see iCMthermo.cc
    break;
  case 'K':
  case 'O':
    initTestsKO();  // see iCMthermo.cc
    break;
  case 'L':
    initTestL();
    break;
  case 'V':
    test_V_init();
    break;
  default:
    throw RuntimeError(PISM_ERROR_LOCATION, "Desired test not implemented by IceCompModel.");
  }
}

void IceCompModel::initTestABCDH() {

  const double time = m_time->current();

  m_geometry.cell_type.set(MASK_GROUNDED);

  array::AccessScope list(m_geometry.ice_thickness);

  ParallelSection loop(m_grid->com);
  try {
    for (auto p = m_grid->points(); p; p.next()) {
      const int i = p.i(), j = p.j();

      const double r = grid::radius(*m_grid, i, j);
      switch (m_testname) {
      case 'A':
        m_geometry.ice_thickness(i, j) = exactA(r).H;
        break;
      case 'B':
        m_geometry.ice_thickness(i, j) = exactB(time, r).H;
        break;
      case 'C':
        m_geometry.ice_thickness(i, j) = exactC(time, r).H;
        break;
      case 'D':
        m_geometry.ice_thickness(i, j) = exactD(time, r).H;
        break;
      case 'H':
        m_geometry.ice_thickness(i, j) = exactH(m_f, time, r).H;
        break;
      default:
        throw RuntimeError(PISM_ERROR_LOCATION, "test must be A, B, C, D, or H");
      }
    }
  } catch (...) {
    loop.failed();
  }
  loop.check();

  m_geometry.ice_thickness.update_ghosts();

  {
    array::Scalar bed_uplift(m_grid, "uplift");
    bed_uplift.set(0.0);

    if (m_testname == 'H') {
      m_geometry.bed_elevation.copy_from(m_geometry.ice_thickness);
      m_geometry.bed_elevation.scale(-m_f);
    } else {  // flat bed case otherwise
      m_geometry.bed_elevation.set(0.0);
    }
    m_geometry.sea_level_elevation.set(0.0);
    m_beddef->bootstrap(m_geometry.bed_elevation, bed_uplift, m_geometry.ice_thickness,
                        m_geometry.sea_level_elevation);
  }
}

//! Class used initTestL() in generating sorted list for ODE solver.
class rgrid {
public:
  double r;
  int    i,j;
};

//! Comparison used initTestL() in generating sorted list for ODE solver.
struct rgridReverseSort {
  bool operator()(rgrid a, rgrid b) {
    return (a.r > b.r);
  }
};

void IceCompModel::initTestL() {

  m_log->message(2, "* Initializing ice thickness and bed topography for test L...\n");

  // setup to evaluate test L; requires solving an ODE numerically
  //   using sorted list of radii, sorted in decreasing radius order
  const int MM = m_grid->xm() * m_grid->ym();

  std::vector<rgrid> rrv(MM);
  int k = 0;
  for (auto p = m_grid->points(); p; p.next()) {
    const int i = p.i(), j = p.j();

    rrv[k].i = i;
    rrv[k].j = j;
    rrv[k].r = grid::radius(*m_grid, i,j);

    k += 1;
  }

  std::sort(rrv.begin(), rrv.end(), rgridReverseSort()); // so rrv[k].r > rrv[k+1].r

  // get soln to test L at these radii; solves ODE only once (on each processor)
  std::vector<double> rr(MM), HH(MM), bb(MM), aa(MM);

  for (k = 0; k < MM; k++) {
    rr[k] = rrv[k].r;
  }

  ExactLParameters L = exactL(rr);

  {
    array::Scalar bed_uplift(m_grid, "uplift");

    array::AccessScope list{&m_geometry.ice_thickness, &m_geometry.bed_elevation};

    for (k = 0; k < MM; k++) {
      m_geometry.ice_thickness(rrv[k].i, rrv[k].j)  = L.H[k];
      m_geometry.bed_elevation(rrv[k].i, rrv[k].j) = L.b[k];
    }

    m_geometry.ice_thickness.update_ghosts();

    bed_uplift.set(0.0);

    m_beddef->bootstrap(m_geometry.bed_elevation, bed_uplift, m_geometry.ice_thickness,
                        m_geometry.sea_level_elevation);
  }

  // store copy of ice_thickness for evaluating geometry errors
  m_HexactL.copy_from(m_geometry.ice_thickness);
}

//! \brief Tests A and E have a thickness B.C. (ice_thickness == 0 outside a circle of radius 750km).
void IceCompModel::reset_thickness_test_A() {
  const double LforAE = 750e3; // m

  array::AccessScope list(m_geometry.ice_thickness);

  for (auto p = m_grid->points(); p; p.next()) {
    const int i = p.i(), j = p.j();

    if (grid::radius(*m_grid, i, j) > LforAE) {
      m_geometry.ice_thickness(i, j) = 0;
    }
  }

  m_geometry.ice_thickness.update_ghosts();
}

void IceCompModel::computeGeometryErrors(double &gvolexact, double &gareaexact,
                                         double &gdomeHexact, double &volerr,
                                         double &areaerr, double &gmaxHerr,
                                         double &gavHerr, double &gmaxetaerr,
                                         double &centerHerr) {
  // compute errors in thickness, eta=thickness^{(2n+2)/n}, volume, area

  const double time = m_time->current();
  double
    Hexact     = 0.0,
    vol        = 0.0,
    area       = 0.0,
    domeH      = 0.0,
    volexact   = 0.0,
    areaexact  = 0.0,
    domeHexact = 0.0;
  double
    Herr   = 0.0,
    avHerr = 0.0,
    etaerr = 0.0;

  array::AccessScope list(m_geometry.ice_thickness);
  if (m_testname == 'L') {
    list.add(m_HexactL);
  }

  double
    seawater_density = m_config->get_number("constants.sea_water.density"),
    ice_density      = m_config->get_number("constants.ice.density"),
    Glen_n           = m_config->get_number("stress_balance.sia.Glen_exponent"),
    standard_gravity = m_config->get_number("constants.standard_gravity");

  // area of grid square in square km:
  const double   a = m_grid->dx() * m_grid->dy() * 1e-3 * 1e-3;
  const double   m = (2.0 * Glen_n + 2.0) / Glen_n;

  ParallelSection loop(m_grid->com);
  try {
    for (auto p = m_grid->points(); p; p.next()) {
      const int i = p.i(), j = p.j();

      if (m_geometry.ice_thickness(i,j) > 0) {
        area += a;
        vol += a * m_geometry.ice_thickness(i,j) * 1e-3;
      }
      double xx = m_grid->x(i), r = grid::radius(*m_grid, i,j);
      switch (m_testname) {
      case 'A':
        Hexact = exactA(r).H;
        break;
      case 'B':
        Hexact = exactB(time, r).H;
        break;
      case 'C':
        Hexact = exactC(time, r).H;
        break;
      case 'D':
        Hexact = exactD(time, r).H;
        break;
      case 'F':
        if (r > m_LforFG - 1.0) {  // outside of sheet
          Hexact=0.0;
        } else {
          r=std::max(r,1.0);
          std::vector<double> z(1, 0.0);
          Hexact = exactFG(0.0, r, z, 0.0).H;
        }
        break;
      case 'G':
        if (r > m_LforFG -1.0) {  // outside of sheet
          Hexact=0.0;
        } else {
          r=std::max(r,1.0);
          std::vector<double> z(1, 0.0);
          Hexact = exactFG(time, r, z, m_ApforG).H;
        }
        break;
      case 'H':
        Hexact = exactH(m_f, time, r).H;
        break;
      case 'K':
      case 'O':
        Hexact = 3000.0;
        break;
      case 'L':
        Hexact = m_HexactL(i,j);
        break;
      case 'V':
        {
          double
            H0 = 600.0,
            v0 = convert(m_sys, 300.0, "m year-1", "m second-1"),
            Q0 = H0 * v0,
            B0 = m_stress_balance->shallow()->flow_law()->hardness(0, 0),
            C  = pow(ice_density * standard_gravity * (1.0 - ice_density/seawater_density) / (4 * B0), 3);

          Hexact = pow(4 * C / Q0 * xx + 1/pow(H0, 4), -0.25);
        }
        break;
      default:
        throw RuntimeError(PISM_ERROR_LOCATION, "test must be A, B, C, D, F, G, H, K, L, or O");
      }

      if (Hexact > 0) {
        areaexact += a;
        volexact += a * Hexact * 1e-3;
      }
      if (i == ((int)m_grid->Mx() - 1)/2 and
          j == ((int)m_grid->My() - 1)/2) {
        domeH = m_geometry.ice_thickness(i,j);
        domeHexact = Hexact;
      }
      // compute maximum errors
      Herr = std::max(Herr,fabs(m_geometry.ice_thickness(i,j) - Hexact));
      etaerr = std::max(etaerr,fabs(pow(m_geometry.ice_thickness(i,j),m) - pow(Hexact,m)));
      // add to sums for average errors
      avHerr += fabs(m_geometry.ice_thickness(i,j) - Hexact);
    }
  } catch (...) {
    loop.failed();
  }
  loop.check();

  // globalize (find errors over all processors)
  double gvol, garea, gdomeH;
  gvolexact = GlobalSum(m_grid->com, volexact);
  gdomeHexact = GlobalMax(m_grid->com, domeHexact);
  gareaexact = GlobalSum(m_grid->com, areaexact);

  gvol = GlobalSum(m_grid->com, vol);
  garea = GlobalSum(m_grid->com, area);
  volerr = fabs(gvol - gvolexact);
  areaerr = fabs(garea - gareaexact);

  gmaxHerr = GlobalMax(m_grid->com, Herr);
  gavHerr = GlobalSum(m_grid->com, avHerr);
  gavHerr = gavHerr/(m_grid->Mx()*m_grid->My());
  gmaxetaerr = GlobalMax(m_grid->com, etaerr);

  gdomeH = GlobalMax(m_grid->com, domeH);
  centerHerr = fabs(gdomeH - gdomeHexact);
}

void IceCompModel::post_step_hook() {
  if (m_testname == 'A') {
    reset_thickness_test_A();
  }
}


void IceCompModel::print_summary(bool /* tempAndAge */) {
  //   we always show a summary at every step
  IceModel::print_summary(true);
}

static void write(units::System::Ptr sys, const File &file, size_t start, const char *name,
                  const char *units, const char *long_name, double value,
                  io::Type type = io::PISM_DOUBLE) {
  VariableMetadata v(name, sys);
  v["units"]     = units;
  v["long_name"] = long_name;

  io::define_timeseries(v, "N", file, type);
  io::write_timeseries(file, v, start, { value });
}

void IceCompModel::reportErrors() {
  // geometry errors to report (for all tests except K and O):
  //    -- max thickness error
  //    -- average (at each grid point on whole grid) thickness error
  //    -- max (thickness)^(2n+2)/n error
  //    -- volume error
  //    -- area error
  // and temperature errors (for tests F & G & K & O):
  //    -- max T error over 3D domain of ice
  //    -- av T error over 3D domain of ice
  // and basal temperature errors (for tests F & G):
  //    -- max basal temp error
  //    -- average (at each grid point on whole grid) basal temp error
  // and bedrock temperature errors (for tests K & O):
  //    -- max Tb error over 3D domain of bedrock
  //    -- av Tb error over 3D domain of bedrock
  // and strain-heating (Sigma) errors (for tests F & G):
  //    -- max Sigma error over 3D domain of ice (in 10^-3 K a^-1)
  //    -- av Sigma error over 3D domain of ice (in 10^-3 K a^-1)
  // and basal melt rate error (for test O):
  //    -- max bmelt error over base of ice
  // and surface velocity errors (for tests F & G):
  //    -- max |<us,vs> - <usex,vsex>| error
  //    -- av |<us,vs> - <usex,vsex>| error
  //    -- max ws error
  //    -- av ws error
  // and basal sliding errors (for test E):
  //    -- max ub error
  //    -- max vb error
  //    -- max |<ub,vb> - <ubexact,vbexact>| error
  //    -- av |<ub,vb> - <ubexact,vbexact>| error

  bool no_report = options::Bool("-no_report", "Don't report numerical errors");

  if (no_report) {
    return;
  }

  double maxbmelterr, minbmelterr, volexact, areaexact, domeHexact,
    volerr, areaerr, maxHerr, avHerr, maxetaerr, centerHerr;
  double maxTerr, avTerr, basemaxTerr, baseavTerr, basecenterTerr, maxTberr, avTberr;
  double max_strain_heating_error, av_strain_heating_error;
  double maxUerr, avUerr, maxWerr, avWerr;

  const rheology::FlowLaw &flow_law = *m_stress_balance->modifier()->flow_law();
  const double m = (2.0 * flow_law.exponent() + 2.0) / flow_law.exponent();

  EnthalpyConverter::Ptr EC = m_ctx->enthalpy_converter();

  if ((m_testname == 'F' or m_testname == 'G') and m_testname != 'V' and
      not FlowLawIsPatersonBuddCold(flow_law, *m_config, EC)) {
    m_log->message(1,
                   "WARNING: flow law must be cold part of Paterson-Budd ('-siafd_flow_law arr')\n"
                   "   for reported errors in test %c to be meaningful!\n",
                   m_testname);
  }

  m_log->message(1,
             "NUMERICAL ERRORS evaluated at final time (relative to exact solution):\n");


  // geometry (thickness, vol) errors if appropriate; reported in m except for relmaxETA
  if ((m_testname != 'K') && (m_testname != 'O')) {
    computeGeometryErrors(volexact,areaexact,domeHexact,
                          volerr,areaerr,maxHerr,avHerr,maxetaerr,centerHerr);
    m_log->message(1,
               "geometry  :    prcntVOL        maxH         avH   relmaxETA\n");  // no longer reporting centerHerr
    m_log->message(1, "           %12.6f%12.6f%12.6f%12.6f\n",
               100*volerr/volexact, maxHerr, avHerr,
               maxetaerr/pow(domeHexact,m));

  }

  // temperature errors for F and G
  if ((m_testname == 'F') || (m_testname == 'G')) {
    computeTemperatureErrors(maxTerr, avTerr);
    computeBasalTemperatureErrors(basemaxTerr, baseavTerr, basecenterTerr);
    m_log->message(1,
               "temp      :        maxT         avT    basemaxT     baseavT\n");  // no longer reporting   basecenterT
    m_log->message(1, "           %12.6f%12.6f%12.6f%12.6f\n",
               maxTerr, avTerr, basemaxTerr, baseavTerr);

  } else if ((m_testname == 'K') || (m_testname == 'O')) {
    computeIceBedrockTemperatureErrors(maxTerr, avTerr, maxTberr, avTberr);
    m_log->message(1,
               "temp      :        maxT         avT       maxTb        avTb\n");
    m_log->message(1, "           %12.6f%12.6f%12.6f%12.6f\n",
               maxTerr, avTerr, maxTberr, avTberr);

  }

  // strain_heating errors if appropriate; reported in 10^6 J/(s m^3)
  if ((m_testname == 'F') || (m_testname == 'G')) {
    compute_strain_heating_errors(max_strain_heating_error, av_strain_heating_error);
    m_log->message(1,
               "Sigma     :      maxSig       avSig\n");
    m_log->message(1, "           %12.6f%12.6f\n",
               max_strain_heating_error*1.0e6, av_strain_heating_error*1.0e6);
  }

  // surface velocity errors if exact values are available; reported in m year-1
  if ((m_testname == 'F') || (m_testname == 'G')) {
    computeSurfaceVelocityErrors(maxUerr, avUerr, maxWerr, avWerr);
    m_log->message(1,
               "surf vels :     maxUvec      avUvec        maxW         avW\n");
    m_log->message(1,
               "           %12.6f%12.6f%12.6f%12.6f\n",
               convert(m_sys, maxUerr, "m second-1", "m year-1"),
               convert(m_sys, avUerr, "m second-1", "m year-1"),
               convert(m_sys, maxWerr, "m second-1", "m year-1"),
               convert(m_sys, avWerr, "m second-1", "m year-1"));
  }

  // basal melt rate errors if appropriate; reported in m year-1
  if (m_testname == 'O') {
    computeBasalMeltRateErrors(maxbmelterr, minbmelterr);
    if (maxbmelterr != minbmelterr) {
      m_log->message(1,
                 "IceCompModel WARNING: unexpected Test O situation: max and min of bmelt error\n"
                 "  are different: maxbmelterr = %f, minbmelterr = %f\n",
                 convert(m_sys, maxbmelterr, "m second-1", "m year-1"),
                 convert(m_sys, minbmelterr, "m second-1", "m year-1"));
    }
    m_log->message(1,
               "basal melt:  max\n");
    m_log->message(1, "           %11.5f\n",
               convert(m_sys, maxbmelterr, "m second-1", "m year-1"));

  }

  m_log->message(1, "NUM ERRORS DONE\n");

  options::String report_file("-report_file", "NetCDF error report file");
  bool append = options::Bool("-append", "Append the NetCDF error report");

  io::Mode mode = append ? io::PISM_READWRITE : io::PISM_READWRITE_MOVE;

  if (report_file.is_set()) {

    m_log->message(2, "Also writing errors to '%s'...\n", report_file->c_str());

    // Find the number of records in this file:
    File file(m_grid->com, report_file, io::PISM_NETCDF3, mode); // OK to use netcdf3

    size_t start = file.dimension_length("N");

    io::write_attributes(file, m_output_global_attributes, io::PISM_DOUBLE);

    // Write the dimension variable:
    write(m_sys, file, start, "N", "1", "run number", start + 1);

    // Always write grid parameters:
    write(m_sys, file, start, "dx", "meter", "grid spacing", m_grid->dx());
    write(m_sys, file, start, "dy", "meter", "grid spacing", m_grid->dy());
    write(m_sys, file, start, "dz", "meter", "grid spacing", m_grid->dz_max());

    // Always write the test name:
    write(m_sys, file, start, "test", "", "test name", m_testname);

    if ((m_testname != 'K') && (m_testname != 'O')) {
      write(m_sys, file, start, "relative_volume", "1", "relative volume error", 100*volerr/volexact);
      write(m_sys, file, start, "relative_max_eta", "1", "relative $\\eta$ error", maxetaerr/pow(domeHexact,m));
      write(m_sys, file, start, "maximum_thickness", "meters", "maximum ice thickness error", maxHerr);
      write(m_sys, file, start, "average_thickness", "meters", "average ice thickness error", avHerr);
    }

    if ((m_testname == 'F') || (m_testname == 'G')) {
      write(m_sys, file, start, "maximum_temperature", "kelvin", "maximum ice temperature error", maxTerr);
      write(m_sys, file, start, "average_temperature", "kelvin", "average ice temperature error", avTerr);
      write(m_sys, file, start, "maximum_basal_temperature", "kelvin", "maximum basal temperature error", basemaxTerr);
      write(m_sys, file, start, "average_basal_temperature", "kelvin", "average basal temperature error", baseavTerr);

      {
        units::Converter c(m_sys, "J s^-1 m^-3", "1e6 J s^-1 m^-3");
        write(m_sys, file, start, "maximum_sigma", "1e6 J s-1 m-3", "maximum strain heating error", c(max_strain_heating_error));
        write(m_sys, file, start, "average_sigma", "1e6 J s-1 m-3", "average strain heating error", c(av_strain_heating_error));
      }

      {
        units::Converter c(m_sys, "m second^-1", "m year^-1");
        write(m_sys, file, start, "maximum_surface_velocity", "m year-1", "maximum ice surface horizontal velocity error", c(maxUerr));
        write(m_sys, file, start, "average_surface_velocity", "m year-1", "average ice surface horizontal velocity error", c(avUerr));
        write(m_sys, file, start, "maximum_surface_w", "m year-1", "maximum ice surface vertical velocity error", c(maxWerr));
        write(m_sys, file, start, "average_surface_w", "m year-1", "average ice surface vertical velocity error", c(avWerr));
      }
    }

    if ((m_testname == 'K') || (m_testname == 'O')) {
      write(m_sys, file, start, "maximum_temperature", "kelvin", "maximum ice temperature error", maxTerr);
      write(m_sys, file, start, "average_temperature", "kelvin", "average ice temperature error", avTerr);
      write(m_sys, file, start, "maximum_bedrock_temperature", "kelvin", "maximum bedrock temperature error", maxTberr);
      write(m_sys, file, start, "average_bedrock_temperature", "kelvin", "average bedrock temperature error", avTberr);
    }

    if (m_testname == 'O') {
      units::Converter c(m_sys, "m second^-1", "m year^-1");
      write(m_sys, file, start, "maximum_basal_melt_rate", "m year -1", "maximum basal melt rate error", c(maxbmelterr));
    }
  }

}

//! \brief Initialize test V.
/*
 Try

 pism -test V -y 1000 -part_grid -ssa_method fd -cfbc -o fig4-blue.nc
 pism -test V -y 1000 -part_grid -ssa_method fd -o fig4-green.nc

 to try to reproduce Figure 4.

 Try

 pism -test V -y 3000 -ssa_method fd -cfbc -o fig5.nc -thickness_calving_threshold 250 -part_grid

 with -Mx 51, -Mx 101, -Mx 201 for figure 5,

 pism -test V -y 300 -ssa_method fd -o fig6-ab.nc

 for 6a and 6b,

 pism -test V -y 300 -ssa_method fd -cfbc -part_grid -o fig6-cd.nc

 for 6c and 6d,

 pism -test V -y 300 -ssa_method fd -cfbc -part_grid -o fig6-ef.nc

 for 6e and 6f.

 */
void IceCompModel::test_V_init() {

  {
    array::Scalar bed_uplift(m_grid, "uplift");
    bed_uplift.set(0.0);
    m_geometry.bed_elevation.set(-1000);

    m_beddef->bootstrap(m_geometry.bed_elevation, bed_uplift, m_geometry.ice_thickness,
                        m_geometry.sea_level_elevation);
  }

  // set SSA boundary conditions:
  double upstream_velocity = convert(m_sys, 300.0, "m year-1", "m second-1"),
    upstream_thk = 600.0;

  array::AccessScope list
    {&m_ice_thickness_bc_mask, &m_geometry.ice_thickness,
     &m_velocity_bc_mask, &m_velocity_bc_values};

  for (auto p = m_grid->points(); p; p.next()) {
    const int i = p.i(), j = p.j();

    if (i <= 2) {
      m_velocity_bc_mask(i,j) = 1;
      m_velocity_bc_values(i,j)  = {upstream_velocity, 0.0};
      m_geometry.ice_thickness(i, j) = upstream_thk;
      m_ice_thickness_bc_mask(i, j) = 1;
    } else {
      m_velocity_bc_mask(i,j) = 0;
      m_velocity_bc_values(i,j)  = {0.0, 0.0};
      m_geometry.ice_thickness(i, j) = 0;
      m_ice_thickness_bc_mask(i, j) = 0;
    }
  }

  m_velocity_bc_mask.update_ghosts();

  m_velocity_bc_values.update_ghosts();

  m_geometry.ice_thickness.update_ghosts();
}

} // end of namespace pism
