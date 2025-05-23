# Run "make all" to update
# - the list of diagnostics
# - the list of authors
# - the shell script installing prerequisites (for the manual)

all: update_diagnostics update_authorship installation/code/install_libraries.sh

INTERMEDIATE=diag.json diag-2.json dummy.nc pism_config.nc surface_input.nc

.INTERMEDIATE: ${INTERMEDIATE}

dummy.nc:
	# use a thermal bedrock layer model and run for 1 year to complete the
	# bootstrapping
	pism -eisII A -y 1 -Mbz 5 -Lbz 1000 -Mz 5 -Mx 5 -My 5 -o $@ -verbose 1

	# rename climatic_mass_balance: we need precipitation for an atmosphere model
	ncrename -v climatic_mass_balance,precipitation -O $@ $@

	# create dummy firn and snow depth fields
	ncap2 -s "firn_depth=thk" -O $@ $@
	ncatted -a standard_name,firn_depth,d,, -O $@ $@
	ncap2 -s "snow_depth=firn_depth" -O $@ $@
	# create a dummy (zero) basal water thickness field
	ncap2 -s "bwat=firn_depth" -O $@ $@

# - enthalpy-based energy balance
# - point-wise isostasy bed deformation model
# - PDD surface mass balance model
# - cosine yearly cycle for air temperature (SeaRISE Greenland)
# - SSA+SIA hybrid stress balance
# - routing hydrology
# - all supported calving models
# - constant frontal melt model
diag-2.json: dummy.nc
	pism -i $^ \
	      -bootstrap \
	      -energy enthalpy \
	      -bed_def iso \
	      -surface pdd \
	      -atmosphere searise_greenland \
	      -stress_balance ssa+sia \
	      -yield_stress mohr_coulomb \
	      -topg_to_phi -phi_min 10 -phi_max 30 -topg_min -50 -topg_max 0 \
	      -hydrology routing \
	      -calving eigen_calving,thickness_calving,vonmises_calving \
	      -frontal_melt constant \
	      -verbose 1 -o_size none \
	      -list_diagnostics json > $@

# dummy surface input rate to use with the steady hydrology model
surface_input.nc: dummy.nc
	ncap2 -v -O -s "water_input_rate=precipitation" $^ $@
	ncwa -O -a time $@ $@

# - steady state hydrology
# - Lingle-Clark bed deformation model
# - isochrone tracking
# - debm_simple surface
diag.json: dummy.nc surface_input.nc
	pism -i dummy.nc \
	      -atmosphere.models searise_greenland \
	      -surface.models debm_simple \
	      -bootstrap \
	      -bed_def lc \
	      -hydrology steady \
	      -hydrology.surface_input.file surface_input.nc \
	      -hydrology.add_water_input_to_till_storage false \
	      -isochrones.deposition_times 100 \
	      -verbose 1 -o_size none \
	      -list_diagnostics json > $@

# entries from diag-2.json will override ones in diag.json (in other words, the order
# matters here)
update_diagnostics: diag.json diag-2.json
	python3 ./list_diagnostics.py $^ > manual/diagnostics/diagnostics-list.txt

update_authorship: ../../CITATION.cff
	sed "s/# contrib/contrib/g" $^ | python3 authorship.py > authorship.rst

pism_config.nc: ../../src/pism_config.cdl
	ncgen -o $@ $^

installation/code/install_libraries.sh: installation/debian-packages.csv Makefile
	/bin/echo -n "apt-get install -y --no-install-recommends " > $@
	cut -f1 -d, $< | sed "s/\`//g" | xargs >> $@

clean:
	@rm -f ${INTERMEDIATE}
