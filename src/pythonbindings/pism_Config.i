%{
/* Using directives needed to compile array::Array wrappers. */

#include "util/Config.hh"

using namespace pism;
%}

%extend pism::Config
{
  %pythoncode "Config.py"
}
