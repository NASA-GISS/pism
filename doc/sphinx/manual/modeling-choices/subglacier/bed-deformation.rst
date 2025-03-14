.. include:: ../../../global.txt

.. _sec-bed-def:

Earth deformation models
------------------------

The option :opt:`-bed_def` ``[iso, lc, given]`` (flag :config:`bed_deformation.model`) turns one
of the three available bed deformation models.

Bed deformation timescale is longer than that of ice flow; this makes it possible to use
*asynchronous* coupling between bed deformation and ice dynamics sub-models of PISM. The
parameter :config:`bed_deformation.update_interval` controls the coupling interval. Update
interval of zero corresponds to synchronous coupling.

When performing a step of a bed deformation model PISM uses the *average* of the load over
the time interval since the last update. This is necessary to avoid temporal aliasing of
"high-frequency" variations in the ice thickness due to the annual cycle of the surface
mass balance when sampled at a lower frequency of bed deformation updates.

Here only *grounded* ice contributes to the load on the bed. We ignore the contribution of
floating shelves and the pressure of the column of water in the ocean.

.. _sec-bed-def-iso:

Point-wise isostasy
===================

The first model ``-bed_def iso``, is instantaneous pointwise isostasy. This model assumes
that the bed at the starting time is in equilibrium with the load. Then, as the ice
geometry evolves, the bed elevation is equal to the starting bed elevation minus a
multiple of the increase in ice thickness from the starting time:

.. math::

   b(t,x,y) = b(0,x,y) - f \left[H(t,x,y) - H(0,x,y)\right].

Here `f` is the density of ice divided by the density of the mantle, so its value is
determined by the values of :config:`bed_deformation.mantle_density` and
:config:`constants.ice.density` in the configuration file.
For an example and verification, see Test H in :ref:`sec-verif`.

.. _sec-bed-def-lc:

Lingle-Clark
============

The second model ``-bed_def lc`` is much more physical. It is based on papers by Lingle
and Clark :cite:`LingleClark` and Bueler and others :cite:`BLKfastearth`. It generalizes
and improves the most widely-used earth deformation model in ice sheet modeling, the flat
earth Elastic Lithosphere Relaxing Asthenosphere (ELRA) model :cite:`Greve2001`. It
imposes essentially no computational burden because the Fast Fourier Transform is used to
solve the linear differential equation :cite:`BLKfastearth`. When using this model in
PISM, the rate of bed movement (uplift) and the viscous plate displacement are stored in
the PISM output file and then used to initialize the next part of the run. In fact, if
gridded "observed" uplift data is available, for instance from a combination of actual
point observations and/or paleo ice load modeling, and if that uplift field is put in a
NetCDF variable with standard name ``tendency_of_bedrock_altitude`` in the input file,
then this model will initialize so that it starts with the given uplift rate.

All parameters (except for :config:`constants.ice.density`) controlling the Lingle-Clark
model are listed below (they all have the prefix ``bed_deformation.``).

.. rubric:: Parameters

Prefix: ``bed_deformation.``

.. pism-parameters::
   :prefix: bed_deformation.
   :exclude: bed_deformation.(model|bed_|given)

Here are minimal example runs to compare these models:

.. code-block:: none

   mpiexec -n 4 pism -eisII A -Mx 61 -My 61 -y 8000 -o eisIIA_nobd.nc
   mpiexec -n 4 pism -eisII A -Mx 61 -My 61 -bed_def iso -y 8000 -o eisIIA_bdiso.nc
   mpiexec -n 4 pism -eisII A -Mx 61 -My 61 -bed_def lc -y 8000 -o eisIIA_bdlc.nc

Compare the :var:`topg`, :var:`usurf`, and :var:`dbdt` variables in the resulting output
files. See also the comparison done in :cite:`BLKfastearth`.

To include "measured" uplift rates during initialization, use the option
:opt:`-uplift_file` (parameter :config:`bed_deformation.bed_uplift_file`) to specify the
name of the file containing the field :var:`dbdt` (CF standard name:
``tendency_of_bedrock_altitude``).

Use the option :opt:`-topg_delta_file` (parameter
:config:`bed_deformation.bed_topography_delta_file`) to apply a correction to the bed
topography field read in from an input file. This sets the bed topography `b` at the
beginning of a run as follows:

.. math::
   :label: eq-bedcorrection

   b = b_{0} + \Delta b.

Here `b_{0}` is the bed topography (:var:`topg`) read in from an input file and `\Delta b`
is the :var:`topg_delta` field read in from the file specified using this option.

A correction like this can be used to get a bed topography field at the end of a
paleo-climate run that is closer to observed present day topography. The correction is
computed by performing a "preliminary" run and subtracting modeled bed topography from
present day observations. A subsequent run with this correction should produce bed
elevations that are closer to observed values.

.. warning::

   The variable :var:`viscous_bed_displacement` does not correspond to any measured
   physical quantity. Do not even attempt to analyze it without a careful reading of
   :cite:`BLKfastearth`.

   Trying to provide a "hand-crafted" :var:`viscous_bed_displacement` field to PISM is not
   a good idea.

   Keep in mind that zero :var:`viscous_bed_displacement` does *not* mean that the bed
   deformation model is in equilibrium.

.. FIXME: Document regridding viscous_bed_displacement when switching to a finer grid in a
   long bootstrapping simulation. This is the one and only reasonable example of using a
   viscous_bed_displacement field provided by the user. (Re-starting from a file created
   by PISM does not count.)



.. _sec-bed-def-given:

Given bed deformation history
=============================

The last option ``-bed_def given`` can be used if a bed deformation history (i.e. bed
elevation changes relative to a reference topography) is known from an external
solid-Earth model\ [#RSL]_. This can be useful when running simulations using offline
coupling to such a model.

The bed topography `b` is set to

.. math::

   b(t,x,y) = b_{\text{ref}}(x,y) + \Delta b(t,x,y),

which is a time-dependent version of :eq:`eq-bedcorrection`.

This class uses two input files:

1. Reference topography `b_{\text{ref}}(x,y)` (variable :var:`topg`, in meters).
2. Time-dependent history of bed elevation changes `\Delta b(t,x,y)` relative to the 
   reference topography (variable :var:`topg_delta`, in meters).

Use the following configuration parameters (prefix: ``bed_deformation.given.``) to set
them.

.. pism-parameters::
   :prefix: bed_deformation.given.

.. note::

   It is possible to combine high-resolution reference bed topography with
   low-spatial-frequency bed elevation changes: both files have to use the same grid
   projection and cover the modeling domain but they **do not** have to use the same grid.

.. rubric:: Footnotes

.. [#RSL] E.g. a relative sea level model.
