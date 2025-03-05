.. include:: ../global.txt

.. _sec-release-checklist:

Release checklist
=================

#. Run ``make manual_linkcheck`` and fix any broken links in the manual.
#. Run ``make`` in the ``doc/sphinx`` directory to update lists of diagnostics and
   configuration parameters.
#. Run ``make`` in the ``doc`` directory to update funding sources.
#. Create a "pre-release" branch starting from the "``dev``" branch and remove code that
   should not be a part of the release.
#. Update PISM version in ``VERSION``.
#. Update ``CHANGES.rst``.
#. Tag.

   ::

      git tag -a v2.X -m "The v2.X release. See CHANGES.rst for the list of changes since v2.X-1."

#. Push.

   ::

      git push -u origin HEAD

#. Push tags.

   ::

      git push origin tag v2.X

#. Write a news item for ``pism.io``.
#. Update the current PISM version on ``pism.io``.
#. Send an e-mail to CRYOLIST.
#. Tell more people, if desired.
#. Create a new "release" on https://github.com/pism/pism/releases

   Add the phrase "Follow https://doi.org/10.5281/zenodo.1199019 to the zenodo DOI for
   this release." to the release notes on GitHub to make it easier to find the right DOI.
