// Copyright (C) 2004--2017 PISM Authors
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
#include <cassert>
#include <gsl/gsl_math.h>

#include "MohrCoulombYieldStress.hh"

#include "pism/hydrology/Hydrology.hh"
#include "pism/util/IceGrid.hh"
#include "pism/util/Mask.hh"
#include "pism/util/Vars.hh"
#include "pism/util/error_handling.hh"
#include "pism/util/io/PIO.hh"
#include "pism/util/pism_options.hh"
#include "pism/util/MaxTimestep.hh"
#include "pism/util/pism_utilities.hh"
#include "pism/util/IceModelVec2CellType.hh"
#include "pism/geometry/Geometry.hh"

#include "pism/util/Time.hh"

namespace pism {

//! \file PISMMohrCoulombYieldStress.cc  Process model which computes pseudo-plastic yield stress for the subglacial layer.
/*! \file PISMMohrCoulombYieldStress.cc
The output variable of this submodel is `tauc`, the pseudo-plastic yield stress
field that is used in the ShallowStressBalance objects.  This quantity is
computed by the Mohr-Coulomb criterion [\ref SchoofTill], but using an empirical
relation between the amount of water in the till and the effective pressure
of the overlying glacier resting on the till [\ref Tulaczyketal2000].

The "dry" strength of the till is a state variable which is private to
the submodel, namely `tillphi`.  Its initialization is nontrivial: either the
`-topg_to_phi`  heuristic is used or inverse modeling can be used.  (In the
latter case `tillphi` can be read-in at the beginning of the run.  Currently
`tillphi` does not evolve during the run.)

This submodel uses a pointer to a Hydrology instance to get the till (pore)
water amount, the effective water layer thickness.  The effective pressure is
derived from this.  Then the effective pressure is combined with tillphi to
compute an updated `tauc` by the Mohr-Coulomb criterion.

This submodel is inactive in floating areas.
*/


MohrCoulombYieldStress::MohrCoulombYieldStress(IceGrid::ConstPtr g,
                                               hydrology::Hydrology *hydro)
  : YieldStress(g), m_hydrology(hydro) {

  const unsigned int stencil_width = m_config->get_double("grid.max_stencil_width");

  m_till_phi.create(m_grid, "tillphi", WITH_GHOSTS, stencil_width);
  m_till_phi.set_attrs("model_state",
                       "friction angle for till under grounded ice sheet",
                       "degrees", "");
  // in this model; need not be time-independent in general

  // internal working space; stencil width needed because redundant computation
  // on overlaps
  m_tillwat.create(m_grid, "tillwat_for_MohrCoulomb", WITH_GHOSTS, stencil_width);
  m_tillwat.set_attrs("internal",
                      "copy of till water thickness held by MohrCoulombYieldStress",
                      "m", "");

  m_Po.create(m_grid, "overburden_pressure_for_MohrCoulomb",
              WITH_GHOSTS, stencil_width);
  m_Po.set_attrs("internal",
                 "copy of overburden pressure held by MohrCoulombYieldStress",
                 "Pa", "");

  if (m_config->get_boolean("basal_yield_stress.add_transportable_water")) {
    m_bwat.create(m_grid, "bwat_for_MohrCoulomb", WITHOUT_GHOSTS);
    m_bwat.set_attrs("internal",
                     "copy of transportable water thickness held by MohrCoulombYieldStress",
                     "m", "");
  }

  //! Optimzation of till friction angle for given target surface elevation, analogous to 
  // Pollard et al. (2012), TC 6(5), "A simple inverse method for the distribution of basal 
  // sliding coefficients under ice sheets, applied to Antarctica"

  bool iterative_phi = options::Bool("-iterative_phi",
                                   "Turn on the iterative till friction angle computation"
                                   " which uses target surface elevation");
  if (iterative_phi) {

    m_usurf.create(m_grid, "usurf",
              WITH_GHOSTS, stencil_width);
    m_usurf.set_attrs("internal",
                 "external surface elevation",
                 "m", "surface_altitude"); 

    m_target_usurf.create(m_grid, "target_usurf",
              WITH_GHOSTS, stencil_width);
    m_target_usurf.set_attrs("internal",
                 "target surface elevation",
                 "m", "target_surface_altitude"); 
    m_target_usurf.set_time_independent(true);

    m_diff_usurf.create(m_grid, "diff_usurf",
              WITH_GHOSTS, stencil_width);
    m_diff_usurf.set_attrs("internal",
                 "surface elevation anomaly",
                 "m", ""); 

    m_diff_mask.create(m_grid, "diff_mask",
              WITH_GHOSTS, stencil_width);
    m_diff_mask.set_attrs("internal",
                 "mask for till phi iteration",
                 "", ""); 
  } else{
    m_till_phi.set_time_independent(true);
  }

}

MohrCoulombYieldStress::~MohrCoulombYieldStress() {
  // empty
}


//! Initialize the pseudo-plastic till mechanical model.
/*!
The pseudo-plastic till basal resistance model is governed by this power law
equation,
    \f[ \tau_b = - \frac{\tau_c}{|\mathbf{U}|^{1-q} U_{\mathtt{th}}^q} \mathbf{U}, \f]
where \f$\tau_b=(\tau_{(b)x},\tau_{(b)y})\f$ is the basal shear stress and
\f$U=(u,v)\f$ is the sliding velocity.

We call the scalar field \f$\tau_c(t,x,y)\f$ the \e yield \e stress even when
the power \f$q\f$ is not zero; when that power is zero the formula describes
a plastic material with an actual yield stress.  The constant
\f$U_{\mathtt{th}}\f$ is the \e threshold \e speed, and \f$q\f$ is the \e pseudo
\e plasticity \e exponent.  The current class computes this yield stress field.
See also IceBasalResistancePlasticLaw::drag().

The strength of the saturated till material, the yield stress, is modeled by a
Mohr-Coulomb relation [\ref Paterson, \ref SchoofStream],
    \f[   \tau_c = c_0 + (\tan \varphi) N_{til}, \f]
where \f$N_{til}\f$ is the effective pressure of the glacier on the mineral
till.

The determination of the till friction angle \f$\varphi(x,y)\f$  is important.
It is assumed in this default model to be a time-independent factor which
describes the strength of the unsaturated "dry" (mineral) till material.  Thus
it is assumed to change more slowly than the till water pressure, and it follows
that it changes more slowly than the yield stress and the basal shear stress.

Option `-topg_to_phi` causes call to topg_to_phi() at the beginning of the run.
This determines the map of \f$\varphi(x,y)\f$.  If this option is note given,
the current method leaves `tillphi` unchanged, and thus either in its
read-in-from-file state or with a default constant value from the config file.
*/
void MohrCoulombYieldStress::init_impl() {
  {
    std::string hydrology_tillwat_max = "hydrology.tillwat_max";
    bool till_is_present = m_config->get_double(hydrology_tillwat_max) > 0.0;

    if (not till_is_present) {
      throw RuntimeError::formatted(PISM_ERROR_LOCATION, "The Mohr-Coulomb yield stress model cannot be used without till.\n"
                                    "Reset %s or choose a different yield stress model.",
                                    hydrology_tillwat_max.c_str());
    }
  }

  {
    const std::string flag_name = "basal_yield_stress.add_transportable_water";
    hydrology::Routing *hydrology_routing = dynamic_cast<hydrology::Routing*>(m_hydrology);
    if (m_config->get_boolean(flag_name) == true && hydrology_routing == NULL) {
      throw RuntimeError::formatted(PISM_ERROR_LOCATION, "Flag %s is set.\n"
                                    "Thus the Mohr-Coulomb yield stress model needs a hydrology::Routing\n"
                                    "(or derived like hydrology::Distributed) object with transportable water.\n"
                                    "The current Hydrology instance is not suitable.  Set flag\n"
                                    "%s to 'no' or choose a different yield stress model.",
                                    flag_name.c_str(), flag_name.c_str());
    }
  }

  m_log->message(2, "* Initializing the Mohr-Coulomb basal yield stress model...\n");

  const double till_phi_default = m_config->get_double("basal_yield_stress.mohr_coulomb.till_phi_default");

  //Optimization scheme for till friction angle anlogous to Pollard et al. (2012) /////////
  options::String iterative_phi_file("-iterative_phi",
                                   "Turn on the iterative till friction angle computation"
                                   " which uses target surface elevation from file",
                                   "", options::ALLOW_EMPTY);

  m_iterative_phi = iterative_phi_file.is_set();

  if (m_iterative_phi) {
      m_log->message(2, "* Initializing the iterative till friction angle optimization...\n");
      //FIXME: is there a better workaroud to read target surface elevation from external file?!
      //m_usurf.regrid(iterative_phi_file, OPTIONAL,0.0);
      m_usurf.regrid(iterative_phi_file, CRITICAL);
      m_target_usurf.copy_from(m_usurf);
  } else {
    m_log->message(2, "* No file set to read target surface elevation from...\n");
  }
  /////////////////////////////////////////////////////////////////////////////////////////////

  InputOptions opts = process_input_options(m_grid->com);

  if (options::Bool("-topg_to_phi", "compute tillphi as a function of bed elevation")) {

    m_log->message(2, "  option -topg_to_phi seen; creating tillphi map from bed elevation...\n");

    if (opts.type == INIT_RESTART or opts.type == INIT_BOOTSTRAP) {

      PIO nc(m_grid->com, "guess_mode", opts.filename, PISM_READONLY);
      bool tillphi_present = nc.inq_var(m_till_phi.metadata().get_name());

      if (tillphi_present) {
        m_log->message(2,
                       "PISM WARNING: -topg_to_phi computation will override the '%s' field\n"
                       "              present in the input file '%s'!\n",
                       m_till_phi.metadata().get_name().c_str(), opts.filename.c_str());
      }
    }

    topg_to_phi(*m_grid->variables().get_2d_scalar("bedrock_altitude"));

  } else if (opts.type == INIT_RESTART) {
    m_till_phi.read(opts.filename, opts.record);
  } else if (opts.type == INIT_BOOTSTRAP) {
    m_till_phi.regrid(opts.filename, OPTIONAL, till_phi_default);
  } else {
    m_till_phi.set(till_phi_default);
  }

  // regrid if requested, regardless of how initialized
  regrid("MohrCoulombYieldStress", m_till_phi);

  options::String tauc_to_phi_file("-tauc_to_phi",
                                   "Turn on, and specify, the till friction angle computation"
                                   " which uses basal yield stress (tauc) and the rest of the model state",
                                   "", options::ALLOW_EMPTY);

  if (tauc_to_phi_file.is_set()) {

    if (not tauc_to_phi_file->empty()) {
      // "-tauc_to_phi filename.nc" is given
      m_basal_yield_stress.regrid(tauc_to_phi_file, CRITICAL);
    } else {
      // "-tauc_to_phi" is given (without a file name); assume that tauc has to be present in an
      // input file
      m_basal_yield_stress.regrid(opts.filename, CRITICAL);
    }

    m_log->message(2,
                   "  Will compute till friction angle (tillphi) as a function"
                   " of the yield stress (tauc)...\n");

    tauc_to_phi(*m_grid->variables().get_2d_cell_type("mask"));
  } else {
    m_basal_yield_stress.set(0.0);
    // will be set in update_impl()
  }
}

MaxTimestep MohrCoulombYieldStress::max_timestep_impl(double t) const {
  (void) t;
  return MaxTimestep("Mohr-Coulomb yield stress");
}

void MohrCoulombYieldStress::set_till_friction_angle(const IceModelVec2S &input) {
  m_till_phi.copy_from(input);
}


void MohrCoulombYieldStress::define_model_state_impl(const PIO &output) const {
  m_till_phi.define(output);

  if (m_iterative_phi) {
    m_target_usurf.define(output);
    m_diff_usurf.define(output);
    m_diff_mask.define(output);
  }
}


void MohrCoulombYieldStress::write_model_state_impl(const PIO &output) const {
  m_till_phi.write(output);

  if (m_iterative_phi) {
    m_target_usurf.write(output);
    m_diff_usurf.write(output);
    m_diff_mask.write(output);
  }
}

//! Update the till yield stress for use in the pseudo-plastic till basal stress
//! model.  See also IceBasalResistancePlasticLaw.
/*!
Updates yield stress  @f$ \tau_c @f$  based on modeled till water layer thickness
from a Hydrology object.  We implement the Mohr-Coulomb criterion allowing
a (typically small) till cohesion  @f$ c_0 @f$
and by expressing the coefficient as the tangent of a till friction angle
 @f$ \varphi @f$ :
    @f[   \tau_c = c_0 + (\tan \varphi) N_{til}. @f]
See [@ref Paterson] table 8.1 regarding values.

The effective pressure on the till is empirically-related
to the amount of water in the till.  We use this formula derived from
[@ref Tulaczyketal2000] and documented in [@ref BuelervanPeltDRAFT]:

@f[ N_{til} = \min\left\{P_o, N_0 \left(\frac{\delta P_o}{N_0}\right)^s 10^{(e_0/C_c) (1 - s)}\right\} @f]

where  @f$ s = W_{til} / W_{til}^{max} @f$,  @f$ W_{til}^{max} @f$ =`hydrology_tillwat_max`,
@f$ \delta @f$ =`basal_yield_stress.mohr_coulomb.till_effective_fraction_overburden`,  @f$ P_o @f$  is the
overburden pressure,  @f$ N_0 @f$ =`basal_yield_stress.mohr_coulomb.till_reference_effective_pressure` is a
reference effective pressure,   @f$ e_0 @f$ =`basal_yield_stress.mohr_coulomb.till_reference_void_ratio` is the void ratio
at the reference effective pressure, and  @f$ C_c @f$ =`basal_yield_stress.mohr_coulomb.till_compressibility_coefficient`
is the coefficient of compressibility of the till.  Constants  @f$ N_0, e_0, C_c @f$  are
found by [@ref Tulaczyketal2000] from laboratory experiments on samples of
till.

If `basal_yield_stress.add_transportable_water` is yes then @f$ s @f$ in the above formula
becomes @f$ s = (W + W_{til}) / W_{til}^{max} @f$,
that is, the water amount is the sum @f$ W+W_{til} @f$.  This only works
if @f$ W @f$ is present, that is, if `hydrology` points to a
hydrology::Routing (or derived class thereof).
 */
void MohrCoulombYieldStress::update_impl(const YieldStressInputs &inputs) {

  bool slippery_grounding_lines = m_config->get_boolean("basal_yield_stress.slippery_grounding_lines"),
       add_transportable_water  = m_config->get_boolean("basal_yield_stress.add_transportable_water");

  hydrology::Routing* hydrowithtransport = dynamic_cast<hydrology::Routing*>(m_hydrology);

  if (add_transportable_water and not hydrowithtransport) {
    throw RuntimeError::formatted(PISM_ERROR_LOCATION,
                                  "add_transportable_water requires a routing hydrology model");
  }

  const double high_tauc   = m_config->get_double("basal_yield_stress.ice_free_bedrock"),
               tillwat_max = m_config->get_double("hydrology.tillwat_max"),
               c0          = m_config->get_double("basal_yield_stress.mohr_coulomb.till_cohesion"),
               N0          = m_config->get_double("basal_yield_stress.mohr_coulomb.till_reference_effective_pressure"),
               e0overCc    = (m_config->get_double("basal_yield_stress.mohr_coulomb.till_reference_void_ratio") /
                              m_config->get_double("basal_yield_stress.mohr_coulomb.till_compressibility_coefficient")),
               delta       = m_config->get_double("basal_yield_stress.mohr_coulomb.till_effective_fraction_overburden"),
               tlftw       = m_config->get_double("basal_yield_stress.mohr_coulomb.till_log_factor_transportable_water");

  if (m_hydrology) {
    m_hydrology->till_water_thickness(m_tillwat);
    m_hydrology->overburden_pressure(m_Po);
    if (add_transportable_water) {
      hydrowithtransport->subglacial_water_thickness(m_bwat);
    }
  }

  const IceModelVec2CellType &mask           = inputs.geometry->cell_type;
  const IceModelVec2S        &bed_topography = inputs.geometry->bed_elevation;

  IceModelVec::AccessList list{&m_tillwat, &m_till_phi, &m_basal_yield_stress, &mask,
      &bed_topography, &m_Po};
  if (add_transportable_water) {
    list.add(m_bwat);
  }

  // simple inversion method for till friction angle /////////////////////////////
  // FIXME: export as modularized function
  if (m_iterative_phi) {

    const IceModelVec2S &usurf = inputs.geometry->ice_surface_elevation;
    //const IceModelVec2S &usurf = *m_grid->variables().get_2d_scalar("surface_altitude");
    list.add(usurf);
    list.add(m_target_usurf);
    list.add(m_diff_usurf);
    list.add(m_diff_mask);

    double tinv     = 500.0, // yr
           hinv     = 500.0, // m
           phimin   = 1.0, 
           phiminup = 5.0, 
           phihmin  = -300.0, // m
           phihmax  = 700.0, // m
           phimax   = 60.0,
           dphi     = 1.0,
           phimod   = 0.01; // m/yr

    tinv = options::Real("-tphi_inverse", "time step for phi inversion",tinv);
    hinv = options::Real("-hphi_inverse", "relative surface elevation change for phi inversion",hinv);
    phimax = options::Real("-phimax_inverse", "maximum value of phi inversion",phimax);
    phimin = options::Real("-phimin_inverse", "minimum value of phi inversion",phimax);
    phimod = options::Real("-phimod_inverse", "convergence criterion for phi inversion",phimax);

    double slope = (phiminup - phimin) / (phihmax - phihmin);

    double year = units::convert(m_sys, m_grid->ctx()->time()->current(), "seconds", "years");

    bool initstep = ((units::convert(m_sys, m_grid->ctx()->time()->start(), "seconds", "years") - year) == 0.0);

    if (initstep) {
      m_last_time = year,
      m_last_inverse_time = year;
      m_diff_mask.set(1.0);//apply everywhere
    } 

    double dt_years = year - m_last_time,
           dt_inverse = year - m_last_inverse_time;
    bool inverse_step = (dt_inverse > tinv);
    if (initstep)
      inverse_step = true;

    //if (inverse_step) 
    //m_log->message(2,"!!!! time: %f, %f , %d, %d, %f, %f\n",year,(year+dt_years),dt_years,dt_inverse);


    for (Points p(*m_grid); p; p.next()) {
      const int i = p.i(), j = p.j();

      if (inverse_step) {
        if (mask.grounded_ice(i,j)) {

          double diff_usurf_prev = m_diff_usurf(i,j);
          m_diff_usurf(i,j) = usurf(i,j)-m_target_usurf(i,j);

          // Convergence criterion
          double till_phi_old = m_till_phi(i,j);
          double diff_mask_old = m_diff_mask(i,j);
          double diff_diff = std::abs(m_diff_usurf(i,j)-diff_usurf_prev);
 
         if (diff_diff/tinv > phimod) {
            m_diff_mask(i,j)=1.0;
          
            // Do incremental steps of maximum 0.5*dphi down and dphi up reaching the upper limit phimax
            m_till_phi(i,j) -= std::min(dphi,std::max(-dphi*0.5,m_diff_usurf(i,j)/hinv));
            m_till_phi(i,j) = std::min(phimax,m_till_phi(i,j));

            // Different lower constraints for marine (b<phihmin) and continental (b>phihmax) areas)
            if (bed_topography(i,j)>phihmax){
              m_till_phi(i,j) = std::max(phiminup,m_till_phi(i,j));

            // Apply smooth transition between marine and continental areas
            } else if (bed_topography(i,j)<=phihmax && bed_topography(i,j)>=phihmin){
              m_till_phi(i, j) = std::max((phimin + (bed_topography(i,j) - phihmin) * slope),m_till_phi(i,j));

            } else {
              m_till_phi(i,j) = std::max(phimin,m_till_phi(i,j));
            }
          } else {
            m_diff_mask(i,j)=0.0;
          }

        // Floating and ice free ocean
        } else if (mask.ocean(i,j)){
          m_diff_usurf(i,j) = usurf(i,j)-m_target_usurf(i,j);
          m_till_phi(i,j) = phimin;
          m_diff_mask(i,j)=0.0;
        } 
      }
    }
    m_last_time = year;
    if (inverse_step) {
      m_log->message(2,"\n* Perform iterative step for optimization of till friction angle phi!\n\n");
      m_last_inverse_time = year;
    }
  }
  ///////////////////////////////////////////////////////////////////////////////////////////////


  for (Points p(*m_grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    if (mask.ocean(i, j)) {
      m_basal_yield_stress(i, j) = 0.0;
    } else if (mask.ice_free(i, j)) {
      m_basal_yield_stress(i, j) = high_tauc;  // large yield stress if grounded and ice-free
    } else { // grounded and there is some ice
      // user can ask that marine grounding lines get special treatment
      const double sea_level = 0.0; // FIXME: get sea-level from correct PISM source
      double water = m_tillwat(i,j); // usual case
      if (slippery_grounding_lines and
          bed_topography(i,j) <= sea_level and
          (mask.next_to_floating_ice(i,j) or mask.next_to_ice_free_ocean(i,j))) {
        water = tillwat_max;
      } else if (add_transportable_water) {
        water = m_tillwat(i,j) + tlftw * log(1.0 + m_bwat(i,j) / tlftw);
      }
      double
        s    = water / tillwat_max,
        Ntil = N0 * pow(delta * m_Po(i,j) / N0, s) * pow(10.0, e0overCc * (1.0 - s));
      Ntil = std::min(m_Po(i,j), Ntil);

      m_basal_yield_stress(i, j) = c0 + Ntil * tan((M_PI/180.0) * m_till_phi(i, j));
    }
  }

  m_basal_yield_stress.update_ghosts();
}

//! Computes the till friction angle phi as a piecewise linear function of bed elevation, according to user options.
/*!
Computes the till friction angle \f$\phi(x,y)\f$ at a location as the following
increasing, piecewise-linear function of the bed elevation \f$b(x,y)\f$.  Let
        \f[ M = (\phi_{\text{max}} - \phi_{\text{min}}) / (b_{\text{max}} - b_{\text{min}}) \f]
be the slope of the nontrivial part.  Then
        \f[ \phi(x,y) = \begin{cases}
                \phi_{\text{min}}, & b(x,y) \le b_{\text{min}}, \\
                \phi_{\text{min}} + (b(x,y) - b_{\text{min}}) \,M,
                                  &  b_{\text{min}} < b(x,y) < b_{\text{max}}, \\
                \phi_{\text{max}}, & b_{\text{max}} \le b(x,y), \end{cases} \f]
where \f$\phi_{\text{min}}=\f$`phi_min`, \f$\phi_{\text{max}}=\f$`phi_max`,
\f$b_{\text{min}}=\f$`topg_min`, \f$b_{\text{max}}=\f$`topg_max`.

The default values are vaguely suitable for Antarctica.  See src/pism_config.cdl.
*/
void MohrCoulombYieldStress::topg_to_phi(const IceModelVec2S &bed_topography) {

  options::RealList option("-topg_to_phi",
                           "Turn on, and specify, the till friction angle parameterization"
                           " based on bedrock elevation (topg)");

  if (not option.is_set()) {
    throw RuntimeError::formatted(PISM_ERROR_LOCATION, "-topg_to_phi is not set");
  }

  if (option->size() != 4) {
    throw RuntimeError::formatted(PISM_ERROR_LOCATION,
                                  "invalid -topg_to_phi arguments: has to be a list"
                                  " of 4 numbers, got %d", (int)option->size());
  }

  const double
    phi_min  = option[0],
    phi_max  = option[1],
    topg_min = option[2],
    topg_max = option[3];

  // FIXME: these parameters aren't actually used
  // const double
  //   phi_min  = m_config->get_double("basal_yield_stress.mohr_coulomb.topg_to_phi.phi_min"),
  //   phi_max  = m_config->get_double("basal_yield_stress.mohr_coulomb.topg_to_phi.phi_max"),
  //   topg_min = m_config->get_double("basal_yield_stress.mohr_coulomb.topg_to_phi.topg_min"),
  //   topg_max = m_config->get_double("basal_yield_stress.mohr_coulomb.topg_to_phi.topg_max");

  m_log->message(2,
                 "  till friction angle (phi) is piecewise-linear function of bed elev (topg):\n"
                 "            /  %5.2f                                 for   topg < %.f\n"
                 "      phi = |  %5.2f + (topg - (%.f)) * (%.2f / %.f)   for   %.f < topg < %.f\n"
                 "            \\  %5.2f                                 for   %.f < topg\n",
                 phi_min, topg_min,
                 phi_min, topg_min, phi_max - phi_min, topg_max - topg_min, topg_min, topg_max,
                 phi_max, topg_max);

  if (phi_min >= phi_max) {
    throw RuntimeError(PISM_ERROR_LOCATION,
                       "invalid -topg_to_phi arguments: phi_min < phi_max is required");
  }

  if (topg_min >= topg_max) {
    throw RuntimeError(PISM_ERROR_LOCATION,
                       "invalid -topg_to_phi arguments: topg_min < topg_max is required");
  }

  const double slope = (phi_max - phi_min) / (topg_max - topg_min);

  IceModelVec::AccessList list{&bed_topography, &m_till_phi};

  for (Points p(*m_grid); p; p.next()) {
    const int i = p.i(), j = p.j();
    const double bed = bed_topography(i, j);

    if (bed <= topg_min) {
      m_till_phi(i, j) = phi_min;
    } else if (bed >= topg_max) {
      m_till_phi(i, j) = phi_max;
    } else {
      m_till_phi(i, j) = phi_min + (bed - topg_min) * slope;
    }
  }

  // communicate ghosts so that the tauc computation can be performed locally
  // (including ghosts of tauc, that is)
  m_till_phi.update_ghosts();
}


void MohrCoulombYieldStress::tauc_to_phi(const IceModelVec2CellType &mask) {
  const double c0 = m_config->get_double("basal_yield_stress.mohr_coulomb.till_cohesion"),
    N0            = m_config->get_double("basal_yield_stress.mohr_coulomb.till_reference_effective_pressure"),
    e0overCc      = (m_config->get_double("basal_yield_stress.mohr_coulomb.till_reference_void_ratio") /
                     m_config->get_double("basal_yield_stress.mohr_coulomb.till_compressibility_coefficient")),
    delta         = m_config->get_double("basal_yield_stress.mohr_coulomb.till_effective_fraction_overburden"),
    tillwat_max   = m_config->get_double("hydrology.tillwat_max");

  assert(m_hydrology != NULL);

  m_hydrology->till_water_thickness(m_tillwat);
  m_hydrology->overburden_pressure(m_Po);

  // make sure that we have enough ghosts:
  const unsigned int GHOSTS = m_till_phi.get_stencil_width();

  assert(mask.get_stencil_width()                 >= GHOSTS);
  assert(m_basal_yield_stress.get_stencil_width() >= GHOSTS);
  assert(m_tillwat.get_stencil_width()            >= GHOSTS);
  assert(m_Po.get_stencil_width()                 >= GHOSTS);

  IceModelVec::AccessList list{&mask, &m_basal_yield_stress, &m_tillwat, &m_Po, &m_till_phi};

  for (PointsWithGhosts p(*m_grid, GHOSTS); p; p.next()) {
    const int i = p.i(), j = p.j();

    if (mask.ocean(i, j)) {
      // no change
    } else if (mask.ice_free(i, j)) {
      // no change
    } else { // grounded and there is some ice
      const double s = m_tillwat(i, j) / tillwat_max;

      double Ntil = 0.0;
      Ntil = N0 * pow(delta * m_Po(i, j) / N0, s) * pow(10.0, e0overCc * (1.0 - s));
      Ntil = std::min(m_Po(i, j), Ntil);

      m_till_phi(i, j) = 180.0/M_PI * atan((m_basal_yield_stress(i, j) - c0) / Ntil);
    }
  }
}

std::map<std::string, Diagnostic::Ptr> MohrCoulombYieldStress::diagnostics_impl() const {

  if (m_iterative_phi) {
    return combine({{"tillphi", Diagnostic::wrap(m_till_phi)},
                  {"diff_usurf", Diagnostic::wrap(m_diff_usurf)},
                  //{"target_usurf", Diagnostic::wrap(m_target_usurf)},
                  {"diff_mask", Diagnostic::wrap(m_diff_mask)}},
                   YieldStress::diagnostics_impl());
  } else{
    return combine({{"tillphi", Diagnostic::wrap(m_till_phi)}},
                 YieldStress::diagnostics_impl());
  }
}

} // end of namespace pism
