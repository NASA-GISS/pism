// Copyright (C) 2012, 2014, 2015, 2016, 2017, 2021, 2022, 2023  David Maxwell and Constantine Khroulev
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

#ifndef IP_SSATAUCTIKHONOVLCL_HH_39UGM4S2
#define IP_SSATAUCTIKHONOVLCL_HH_39UGM4S2

#include <memory>

#include <petscsys.h>

#include "pism/inverse/TaoUtil.hh"
#include "pism/inverse/IPTwoBlockVec.hh"
#include "pism/inverse/IP_SSATaucForwardProblem.hh"
#include "pism/inverse/functional/IPFunctional.hh"

namespace pism {
namespace inverse {

class IP_SSATaucTaoTikhonovProblemLCL;

//! Iteration callback class for IP_SSATaucTaoTikhonovProblemLCL
/*! A class for objects receiving iteration callbacks from a
  IP_SSATaucTaoTikhonovProblemLCL. These callbacks can be used to
  monitor the solution, plot iterations, print diagnostic messages,
  etc. IP_SSATaucTaoTikhonovProblemLCLListeners are ususally used via
  a reference counted pointer
  IP_SSATaucTaoTikhonovProblemLCLListeners::Ptr to allow for good
  memory management when Listeners are created as subclasses of Python
  classes.*/
class IP_SSATaucTaoTikhonovProblemLCLListener {
public:

  typedef std::shared_ptr<IP_SSATaucTaoTikhonovProblemLCLListener> Ptr;

  typedef array::Scalar DesignVec;
  typedef array::Vector StateVec;

  IP_SSATaucTaoTikhonovProblemLCLListener() {}
  virtual ~IP_SSATaucTaoTikhonovProblemLCLListener() {}

  //! Callback called after each iteration.
  //
  // @param problem,  The class calling the callback.
  // @param eta Tikhonov penalty parameter.
  // @param iter Current iteration count.
  // @param objectiveValue Value of the state functional.
  // @param designValue Value of the design functional.
  // @param &d Value of the design variable.
  // @param &diff_d Diference between design variable and a priori estimate.
  // @param &grad_d Gradient of design functional
  // @param &u Value of state variable
  // @param &diff_u Difference between state variable and desired value.
  // @param &grad_u Gradient of state functional
  // @param constraints Residual for state variable being a solution of the %SSA
  virtual void iteration(IP_SSATaucTaoTikhonovProblemLCL &problem,
                         double eta,
                         int iter,
                         double objectiveValue,
                         double designValue,
                         const std::shared_ptr<DesignVec> &d,
                         const std::shared_ptr<DesignVec> &diff_d,
                         const std::shared_ptr<DesignVec> &grad_d,
                         const std::shared_ptr<StateVec> &u,
                         const std::shared_ptr<StateVec> &diff_u,
                         const std::shared_ptr<StateVec> &grad_u,
                         const std::shared_ptr<StateVec> &constraints) = 0;
};

//! \brief Defines a Tikhonov minimization problem of determining \f$\tau_c\f$ from %SSA velocities to be solved with a TaoBasicSolver using the tao_lcl algorithm.
/*! Experimental and not particularly functional. */
class IP_SSATaucTaoTikhonovProblemLCL {
public:
  typedef array::Scalar DesignVec;
  typedef array::Scalar1 DesignVecGhosted;

  typedef array::Vector StateVec;
  typedef array::Vector1 StateVec1;

  typedef IP_SSATaucTaoTikhonovProblemLCLListener Listener;

  IP_SSATaucTaoTikhonovProblemLCL(IP_SSATaucForwardProblem &ssaforward, DesignVec &d0, StateVec &u_obs, double eta,
                                  IPFunctional<DesignVec> &designFunctional, IPFunctional<StateVec> &stateFunctional);

  virtual ~IP_SSATaucTaoTikhonovProblemLCL() = default;

  virtual void addListener(Listener::Ptr listener) {
    m_listeners.push_back(listener);
  }

  virtual std::shared_ptr<StateVec> stateSolution();
  virtual std::shared_ptr<DesignVec> designSolution();

  virtual void setInitialGuess(DesignVec &d0);

  void connect(Tao tao);

  void monitorTao(Tao tao);

  virtual void evaluateObjectiveAndGradient(Tao tao, Vec x, double *value, Vec gradient);

  virtual std::shared_ptr<TerminationReason> formInitialGuess(Vec *x);

  virtual void evaluateConstraints(Tao tao, Vec x, Vec r);

  virtual void evaluateConstraintsJacobianState(Tao tao, Vec x, Mat Jstate, Mat Jpc, Mat Jinv,
                                                MatStructure *s);

  virtual void evaluateConstraintsJacobianDesign(Tao tao, Vec x, Mat Jdesign);

  virtual void applyConstraintsJacobianDesign(Vec x, Vec y);
  virtual void applyConstraintsJacobianDesignTranspose(Vec x, Vec y);

protected:

  IP_SSATaucForwardProblem &m_ssaforward;

  std::unique_ptr<IPTwoBlockVec> m_x;

  DesignVec m_dGlobal;
  std::shared_ptr<DesignVecGhosted> m_d;
  DesignVec &m_d0;
  std::shared_ptr<DesignVecGhosted> m_d_diff;
  DesignVecGhosted m_dzeta;            // ghosted

  std::shared_ptr<StateVec> m_uGlobal;
  StateVec1 m_u;                 // ghosted
  StateVec1 m_du;                // ghosted
  StateVec &m_u_obs;
  std::shared_ptr<StateVec> m_u_diff;

  std::shared_ptr<DesignVec> m_grad_design;
  std::shared_ptr<StateVec>  m_grad_state;

  double m_eta;

  double m_val_design;
  double m_val_state;

  std::shared_ptr<StateVec> m_constraints;
  petsc::Mat m_Jstate;
  petsc::Mat m_Jdesign;

  array::Scalar1 m_d_Jdesign;   // ghosted
  array::Vector1 m_u_Jdesign;        // ghosted

  double m_constraintsScale;
  double m_velocityScale;

  IPFunctional<array::Scalar> &m_designFunctional;
  IPFunctional<array::Vector> &m_stateFunctional;

  std::vector<Listener::Ptr> m_listeners;

  static PetscErrorCode jacobian_design_callback(Mat A, Vec x, Vec y);
  static PetscErrorCode jacobian_design_transpose_callback(Mat A, Vec x, Vec y);
};

} // end of namespace inverse
} // end of namespace pism

#endif /* end of include guard: IP_SSATAUCTIKHONOVLCL_HH_39UGM4S2 */
