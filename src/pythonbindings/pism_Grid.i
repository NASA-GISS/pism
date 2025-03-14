%{
#include "util/Grid.hh"
%}

%extend pism::Grid
{
    %pythoncode {
    def __getitem__(self, key):
      if key == "x":
        return self.x()
      elif key == "y":
        return self.y()
      elif key == "z":
        return self.z()
      else:
        raise KeyError(f"Key {key} not found")
          
    def points(self):
        """Iterate over tuples ``(i,j)`` of nodes owned by the current processor."""
        for i in range(self.xs(),self.xs()+self.xm()):
            for j in range(self.ys(),self.ys()+self.ym()):
                yield (i,j)
    def points_with_ghosts(self,nGhosts=0):
        for i in range(self.xs()-nGhosts,self.xs()+self.xm()+nGhosts):
            for j in range(self.ys()-nGhosts,self.ys()+self.ym()+nGhosts):
                yield (i,j)
    def coords(self):
        for i in range(self.xs(),self.xs()+self.xm()):
            for j in range(self.ys(),self.ys()+self.ym()):
                yield (i,j,self.x(i),self.y(j))
    }
}

%rename("GridParameters") "pism::grid::Parameters";
%shared_ptr(pism::Grid);
%include "util/Grid.hh"
