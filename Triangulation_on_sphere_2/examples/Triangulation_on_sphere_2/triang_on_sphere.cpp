#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>

#include <CGAL/Delaunay_triangulation_on_sphere_2.h>
#include <CGAL/Delaunay_triangulation_sphere_traits_2.h> // @todo '_on_' ?

typedef CGAL::Exact_predicates_inexact_constructions_kernel K;

typedef CGAL::Delaunay_triangulation_sphere_traits_2<K>     Traits;
typedef CGAL::Delaunay_triangulation_on_sphere_2<Traits>    DToS2;

typedef Traits::Point_3                                     Point;

int main()
{
  std::vector<Point> points;
  points.emplace_back( 2, 1, 1);
  points.emplace_back(-2, 1, 1);
  points.emplace_back( 0, 1, 1);
  points.emplace_back( 1, 2, 1);
  points.emplace_back( 0, 1, 1);
  points.emplace_back( 1, 0, 1);
  points.emplace_back( 1, 1, 2);

  Traits traits(Point(1, 1, 1));
  DToS2 dtos(traits);

  for(const Point& pt : points)
  {
    std::cout << "Inserting: " << pt
              << " at distance: " << CGAL::squared_distance(pt, traits.center())
              << " from center, is on sphere? " << traits.is_on_sphere(pt) << std::endl;
    dtos.insert(pt);

    std::cout << "dimension: " << dtos.dimension() << std::endl;
    std::cout << dtos.number_of_vertices() << " nv" << std::endl;
    std::cout << dtos.number_of_faces() << " nf" << std::endl;
    std::cout << dtos.number_of_ghost_faces() << " gf" << std::endl;
  }
}
