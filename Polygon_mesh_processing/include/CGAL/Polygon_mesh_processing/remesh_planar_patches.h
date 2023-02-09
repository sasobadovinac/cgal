// Copyright (c) 2018 GeometryFactory (France).
// All rights reserved.
//
// This file is part of CGAL (www.cgal.org).
//
// $URL$
// $Id$
// SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-Commercial
//
//
// Author(s)     : Sebastien Loriot

#ifndef CGAL_POLYGON_MESH_PROCESSING_REMESH_PLANAR_PATCHES_H
#define CGAL_POLYGON_MESH_PROCESSING_REMESH_PLANAR_PATCHES_H

#include <CGAL/license/Polygon_mesh_processing/meshing_hole_filling.h>

#include <CGAL/Polygon_mesh_processing/connected_components.h>
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Polygon_mesh_processing/manifoldness.h>
#include <CGAL/Constrained_Delaunay_triangulation_2.h>
#include <CGAL/Projection_traits_3.h>
#include <CGAL/Triangulation_vertex_base_with_info_2.h>
#include <CGAL/Triangulation_face_base_with_info_2.h>

#include <CGAL/Named_function_parameters.h>
#include <CGAL/boost/graph/properties.h>
#include <unordered_map>
#include <boost/dynamic_bitset.hpp>

#include <algorithm>

/// @todo remove Kernel_traits
/// @todo function to move in PMP: retriangulate_planar_patches(in, out, vci, ecm, fccid, np) (pca is a np option)


namespace CGAL{

namespace Polygon_mesh_processing {

namespace Planar_segmentation{

inline std::size_t init_id()
{
  return std::size_t(-1);
}

inline std::size_t default_id()
{
  return std::size_t(-2);
}

inline bool is_init_id(std::size_t i)
{
  return i == init_id();
}

inline bool is_corner_id(std::size_t i)
{
  return i < default_id();
}

template <typename Vector_3>
bool is_vector_positive(const Vector_3& normal)
{
  if (normal.x()==0)
  {
    if (normal.y()==0)
      return normal.z() > 0;
    else
      return normal.y() > 0;
  }
  else
    return normal.x() > 0;
}

struct FaceInfo2
{
  FaceInfo2():m_in_domain(-1){}
  int m_in_domain;

  void set_in_domain()   { m_in_domain=1; }
  void set_out_domain()  { m_in_domain=0; }
  bool visited() const   { return m_in_domain!=-1; }
  bool in_domain() const { return m_in_domain==1; }
};

template <typename TriangleMesh,
          typename VertexPointMap,
          typename edge_descriptor>
bool is_edge_between_coplanar_faces(edge_descriptor e,
                                    const TriangleMesh& tm,
                                    double coplanar_cos_threshold,
                                    const VertexPointMap& vpm)
{
  typedef typename boost::property_traits<VertexPointMap>::value_type Point_3;
  typedef typename Kernel_traits<Point_3>::type K;
  typedef typename boost::property_traits<VertexPointMap>::reference Point_ref_3;
  if (is_border(e, tm)) return false;
  typename boost::graph_traits<TriangleMesh>::halfedge_descriptor
    h =  halfedge(e, tm);
  Point_ref_3 p = get(vpm, source(h, tm) );
  Point_ref_3 q = get(vpm, target(h, tm) );
  Point_ref_3 r = get(vpm, target(next(h, tm), tm) );
  Point_ref_3 s = get(vpm, target(next(opposite(h, tm), tm), tm) );

  if (coplanar_cos_threshold==-1)
    return coplanar(p, q, r, s);
  else
  {
    typename K::Compare_dihedral_angle_3 pred;
    return pred(p, q, r, s, typename K::FT(coplanar_cos_threshold)) == CGAL::LARGER;
  }
}

template <typename TriangleMesh,
          typename VertexPointMap,
          typename halfedge_descriptor,
          typename EdgeIsConstrainedMap>
bool is_target_vertex_a_corner(halfedge_descriptor h,
                               EdgeIsConstrainedMap edge_is_constrained,
                               const TriangleMesh& tm,
                               double coplanar_cos_threshold,
                               const VertexPointMap& vpm)
{
  typedef typename boost::property_traits<VertexPointMap>::value_type Point_3;
  typedef typename Kernel_traits<Point_3>::type K;
  typedef typename boost::graph_traits<TriangleMesh> graph_traits;

  halfedge_descriptor h2 = graph_traits::null_halfedge();
  for(halfedge_descriptor h_loop : halfedges_around_target(h, tm))
  {
    if (h_loop==h) continue;
    if (get(edge_is_constrained, edge(h_loop, tm)))
    {
      if (h2 != graph_traits::null_halfedge()) return true;
      h2=h_loop;
    }
  }

  // handle case when the graph of constraints does not contains only cycle
  // (for example when there is a tangency between surfaces and is shared)
  if (h2 == graph_traits::null_halfedge()) return true;

  const Point_3& p = get(vpm, source(h, tm));
  const Point_3& q = get(vpm, target(h, tm));
  const Point_3& r = get(vpm, source(h2, tm));

  if (coplanar_cos_threshold==-1)
    return !collinear(p, q, r);
  else
  {
    typename K::Compare_angle_3 pred;
    return pred(p, q, r, typename K::FT(coplanar_cos_threshold))==CGAL::SMALLER;
  }
}

template <typename TriangleMesh,
          typename EdgeIsConstrainedMap,
          typename VertexPointMap>
void
mark_constrained_edges(
  TriangleMesh& tm,
  EdgeIsConstrainedMap edge_is_constrained,
  double coplanar_cos_threshold,
  const VertexPointMap& vpm)
{
  for(typename boost::graph_traits<TriangleMesh>::edge_descriptor e : edges(tm))
  {
    if (!get(edge_is_constrained,e))
      if (!is_edge_between_coplanar_faces(e, tm, coplanar_cos_threshold, vpm))
        put(edge_is_constrained, e, true);
  }
}

template <typename TriangleMesh,
          typename VertexPointMap,
          typename EdgeIsConstrainedMap,
          typename VertexCornerIdMap>
std::size_t
mark_corner_vertices(
  TriangleMesh& tm,
  EdgeIsConstrainedMap& edge_is_constrained,
  VertexCornerIdMap& vertex_corner_id,
  double coplanar_cos_threshold,
  const VertexPointMap& vpm)
{
  typedef boost::graph_traits<TriangleMesh> graph_traits;
  std::size_t corner_id = 0;
  for(typename graph_traits::edge_descriptor e : edges(tm))
  {
    if (!get(edge_is_constrained, e)) continue;
    typename graph_traits::halfedge_descriptor h = halfedge(e, tm);

    if (is_init_id(get(vertex_corner_id, target(h, tm))))
    {
      if (is_target_vertex_a_corner(h, edge_is_constrained, tm, coplanar_cos_threshold, vpm))
        put(vertex_corner_id, target(h, tm), corner_id++);
      else
        put(vertex_corner_id, target(h, tm), default_id());
    }
    if (is_init_id(get(vertex_corner_id, source(h, tm))))
    {
      if (is_target_vertex_a_corner(opposite(h, tm), edge_is_constrained, tm, coplanar_cos_threshold, vpm))
        put(vertex_corner_id, source(h, tm), corner_id++);
      else
        put(vertex_corner_id, source(h, tm), default_id());
    }
  }

  return corner_id;
}

template <typename CDT>
void mark_face_triangles(CDT& cdt)
{
  //look for a triangle inside the domain of the face
  typename CDT::Face_handle fh = cdt.infinite_face();
  fh->info().set_out_domain();
  std::vector<typename CDT::Edge> queue;
  for (int i=0; i<3; ++i)
    queue.push_back(typename CDT::Edge(fh, i) );
  while(true)
  {
    typename CDT::Edge e = queue.back();
    queue.pop_back();
    e=cdt.mirror_edge(e);
    if (e.first->info().visited()) continue;
    if (cdt.is_constrained(e))
    {
      queue.clear();
      queue.push_back(e);
      break;
    }
    else
    {
      for(int i=1; i<3; ++i)
      {
        typename CDT::Edge candidate(e.first, (e.second+i)%3);
        if (!candidate.first->neighbor(candidate.second)->info().visited())
          queue.push_back( candidate );
      }
      e.first->info().set_out_domain();
    }
  }
  // now extract triangles inside the face
  while(!queue.empty())
  {
    typename CDT::Edge e = queue.back();
    queue.pop_back();
    if (e.first->info().visited()) continue;
    e.first->info().set_in_domain();

    for(int i=1; i<3; ++i)
    {
      typename CDT::Edge candidate(e.first, (e.second+i)%3);
      if (!cdt.is_constrained(candidate) &&
          !candidate.first->neighbor(candidate.second)->info().visited())
      {
        queue.push_back( cdt.mirror_edge(candidate) );
      }
    }
  }
}

template < typename GT,
           typename Vb = Triangulation_vertex_base_2<GT> >
class Triangulation_vertex_base_with_id_2
  : public Vb
{
  std::size_t _id;

public:
  typedef typename Vb::Face_handle                   Face_handle;
  typedef typename Vb::Point                         Point;

  template < typename TDS2 >
  struct Rebind_TDS {
    typedef typename Vb::template Rebind_TDS<TDS2>::Other          Vb2;
    typedef Triangulation_vertex_base_with_id_2<GT, Vb2>   Other;
  };

  Triangulation_vertex_base_with_id_2()
    : Vb(), _id(-1) {}

  Triangulation_vertex_base_with_id_2(const Point & p)
    : Vb(p), _id(-1) {}

  Triangulation_vertex_base_with_id_2(const Point & p, Face_handle c)
    : Vb(p, c), _id(-1) {}

  Triangulation_vertex_base_with_id_2(Face_handle c)
    : Vb(c), _id(-1) {}

  const std::size_t& corner_id() const { return _id; }
        std::size_t& corner_id()       { return _id; }
};


template <typename Kernel>
bool add_triangle_faces(const std::vector< std::pair<std::size_t, std::size_t> >& csts,
                        typename Kernel::Vector_3 normal,
                        const std::vector<typename Kernel::Point_3>& corners,
                        std::vector<cpp11::array<std::size_t, 3> >& triangles)
{
  typedef Projection_traits_3<Kernel>                            P_traits;
  typedef Triangulation_vertex_base_with_id_2<P_traits>                Vb;
  typedef Triangulation_face_base_with_info_2<FaceInfo2,P_traits>     Fbb;
  typedef Constrained_triangulation_face_base_2<P_traits,Fbb>          Fb;
  typedef Triangulation_data_structure_2<Vb,Fb>                       TDS;
  typedef No_constraint_intersection_requiring_constructions_tag     Itag;
  typedef Constrained_Delaunay_triangulation_2<P_traits, TDS, Itag>   CDT;
  typedef typename CDT::Vertex_handle                       Vertex_handle;
  typedef typename CDT::Face_handle                           Face_handle;

  typedef typename Kernel::Point_3 Point_3;

  std::size_t expected_nb_pts = csts.size()/2;
  std::vector<std::size_t> corner_ids;
  corner_ids.reserve(expected_nb_pts);

  typedef std::pair<std::size_t, std::size_t> Id_pair;
  for(const Id_pair& p : csts)
  {
    CGAL_assertion(p.first<corners.size());
    corner_ids.push_back(p.first);
  }

  bool reverse_face_orientation = is_vector_positive(normal);
  if (reverse_face_orientation)
    normal=-normal;

  // create cdt and insert points
  P_traits p_traits(normal);
  CDT cdt(p_traits);

  // now do the point insert and info set
  typedef typename Pointer_property_map<Point_3>::const_type Pmap;
  typedef Spatial_sort_traits_adapter_2<P_traits,Pmap> Search_traits;

  spatial_sort(corner_ids.begin(), corner_ids.end(),
               Search_traits(make_property_map(corners),p_traits));

  Vertex_handle v_hint;
  Face_handle hint;
  for (std::size_t corner_id : corner_ids)
  {
    v_hint = cdt.insert(corners[corner_id], hint);
    if (v_hint->corner_id()!=std::size_t(-1) && v_hint->corner_id()!=corner_id)
      return false; // handle case of points being identical upon projection
    v_hint->corner_id()=corner_id;
    hint=v_hint->face();
  }

  // note that nbv might be different from points.size() in case of hole
  // tangent to the principal CCB
  CGAL_assertion_code(std::size_t nbv=cdt.number_of_vertices();)

  // insert constrained edges
  std::unordered_map<std::size_t, typename CDT::Vertex_handle> vertex_map;
  for(typename CDT::Finite_vertices_iterator vit = cdt.finite_vertices_begin(),
                                             end = cdt.finite_vertices_end(); vit!=end; ++vit)
  {
    vertex_map[vit->corner_id()]=vit;
  }

  std::vector< std::pair<typename CDT::Vertex_handle, typename CDT::Vertex_handle> > local_csts;
  local_csts.reserve(csts.size());
  try{
    for(const Id_pair& p : csts)
    {
      CGAL_assertion(vertex_map.count(p.first)!=0 && vertex_map.count(p.second)!=0);
      cdt.insert_constraint(vertex_map[p.first], vertex_map[p.second]);
    }
  }catch(typename CDT::Intersection_of_constraints_exception&)
  {
    // intersection of constraints probably due to the projection
    return false;
  }
  CGAL_assertion(cdt.number_of_vertices() == nbv);

  mark_face_triangles(cdt);

  for (typename CDT::Finite_faces_iterator fit=cdt.finite_faces_begin(),
                                           end=cdt.finite_faces_end(); fit!=end; ++fit)
  {
    if (!fit->info().in_domain()) continue;
    if (cdt.is_infinite(fit)) return false;

    if (reverse_face_orientation)
      triangles.push_back( make_array(fit->vertex(1)->corner_id(),
                                      fit->vertex(0)->corner_id(),
                                      fit->vertex(2)->corner_id()) );
    else
      triangles.push_back( make_array(fit->vertex(0)->corner_id(),
                                      fit->vertex(1)->corner_id(),
                                      fit->vertex(2)->corner_id()) );
  }

  return true;
}

template <typename TriangleMesh,
          typename VertexCornerIdMap,
          typename EdgeIsConstrainedMap,
          typename FaceCCIdMap,
          typename VertexPointMap>
std::pair<std::size_t, std::size_t>
tag_corners_and_constrained_edges(TriangleMesh& tm,
                                  double coplanar_cos_threshold,
                                  VertexCornerIdMap& vertex_corner_id,
                                  EdgeIsConstrainedMap& edge_is_constrained,
                                  FaceCCIdMap& face_cc_ids,
                                  const VertexPointMap& vpm)
{
  typedef typename boost::graph_traits<TriangleMesh> graph_traits;
  // mark constrained edges
  mark_constrained_edges(tm, edge_is_constrained, coplanar_cos_threshold, vpm);

  // mark connected components (cc) delimited by constrained edges
  std::size_t nb_cc = Polygon_mesh_processing::connected_components(
    tm, face_cc_ids, parameters::edge_is_constrained_map(edge_is_constrained));

  if (coplanar_cos_threshold!=-1)
  {
    for(typename graph_traits::edge_descriptor e : edges(tm))
    {
      if (get(edge_is_constrained, e) && !is_border(e, tm))
      {
        typename graph_traits::halfedge_descriptor h = halfedge(e, tm);
        if ( get(face_cc_ids, face(h, tm))==get(face_cc_ids, face(opposite(h, tm), tm)) )
          put(edge_is_constrained, e, false);
      }
    }
  }

  std::size_t nb_corners =
    mark_corner_vertices(tm, edge_is_constrained, vertex_corner_id, coplanar_cos_threshold, vpm);

  return std::make_pair(nb_corners, nb_cc);
}

template <typename TriangleMesh,
          typename VertexCornerIdMap,
          typename EdgeIsConstrainedMap,
          typename FaceCCIdMap,
          typename VertexPointMap,
          typename Point_3>
bool decimate_impl(const TriangleMesh& tm,
                   std::pair<std::size_t, std::size_t>& nb_corners_and_nb_cc,
                   VertexCornerIdMap& vertex_corner_id,
                   EdgeIsConstrainedMap& edge_is_constrained,
                   FaceCCIdMap& face_cc_ids,
                   const VertexPointMap& vpm,
                   std::vector< Point_3 >& corners,
                   std::vector< cpp11::array<std::size_t, 3> >& out_triangles)
{
  typedef typename Kernel_traits<Point_3>::type K;
  typedef typename boost::graph_traits<TriangleMesh> graph_traits;
  typedef typename graph_traits::halfedge_descriptor halfedge_descriptor;
  typedef typename graph_traits::vertex_descriptor vertex_descriptor;
  typedef typename graph_traits::face_descriptor face_descriptor;
  typedef std::pair<std::size_t, std::size_t> Id_pair;
  std::vector< typename K::Vector_3 > face_normals(nb_corners_and_nb_cc.second, NULL_VECTOR);


  /// @TODO this is rather drastic in particular if the mesh has almost none simplified faces
/// TODO use add_faces?

  // compute the new mesh
  std::vector< std::vector< cpp11::array<std::size_t, 3> > > triangles_per_cc(nb_corners_and_nb_cc.second);
  boost::dynamic_bitset<> cc_to_handle(nb_corners_and_nb_cc.second);
  cc_to_handle.set();

  bool all_patches_successfully_remeshed = true;
  do
  {
    std::vector< std::vector<Id_pair> > face_boundaries(nb_corners_and_nb_cc.second);
    std::vector<bool> face_boundaries_valid(nb_corners_and_nb_cc.second, true);

    std::vector<vertex_descriptor> corner_id_to_vd(nb_corners_and_nb_cc.first, graph_traits::null_vertex());
    std::vector<bool> duplicated_corners(nb_corners_and_nb_cc.first, false);
    auto check_corner = [&corner_id_to_vd, &duplicated_corners](std::size_t corner_id, vertex_descriptor vd)
    {
      if (corner_id_to_vd[corner_id]!=graph_traits::null_vertex() && corner_id_to_vd[corner_id]!=vd)
        duplicated_corners[corner_id]=true;
    };

    // collect maximal constrained edges per cc
    for(halfedge_descriptor h : halfedges(tm))
    {
      if (!get(edge_is_constrained, edge(h, tm)) || is_border(h, tm)) continue;

      std::size_t i1 = get(vertex_corner_id, source(h, tm));
      if ( is_corner_id(i1) )
      {
        check_corner(i1, source(h, tm));
        halfedge_descriptor h_init = h;
        std::size_t cc_id = get(face_cc_ids, face(h_init, tm));
        if (!cc_to_handle.test(cc_id)) continue;
        do{
          std::size_t i2 = get(vertex_corner_id, target(h_init, tm));
          if ( is_corner_id(i2) )
          {
            check_corner(i2, target(h_init, tm));
            face_boundaries[ cc_id ].push_back( Id_pair(i1,i2) );
            if (face_normals[ cc_id ] == NULL_VECTOR)
            {
              face_normals[ cc_id ] = normal(get(vpm, source(h, tm)),
                                             get(vpm, target(h, tm)),
                                             get(vpm, target(next(h, tm), tm)));
            }
            break;
          }

          do{
            h_init=opposite(next(h_init, tm), tm);
          } while( !get(edge_is_constrained, edge(h_init, tm)) );
          h_init=opposite(h_init, tm);
        }
        while(true);
      }
    }

    for (std::size_t cc_id = cc_to_handle.find_first();
                         cc_id < cc_to_handle.npos;
                         cc_id = cc_to_handle.find_next(cc_id))
    {
      std::vector< cpp11::array<std::size_t, 3> >& triangles = triangles_per_cc[cc_id];
      triangles.clear();

      std::vector< Id_pair >& csts = face_boundaries[cc_id];

      if (!face_boundaries_valid[cc_id]) continue;

      // do not remesh a patch containing duplicated vertices
      for (auto c : csts)
        if (duplicated_corners[c.first] || duplicated_corners[c.second])
        {
          csts.clear(); // this will trigger the copy of the current patch rather than a remeshing
          break;
        }
#ifdef CGAL_DEBUG_DECIMATION
      std::cout << "csts.size() " <<  csts.size() << "\n";
#endif
      if (csts.size()==3)
      {
        triangles.push_back( make_array(csts[0].first,
                                        csts[0].second,
                                        csts[0].first==csts[1].first ||
                                        csts[0].second==csts[1].first ?
                                        csts[1].second:csts[1].first) );
        cc_to_handle.set(cc_id, 0);
      }
      else
      {
        if (csts.size() > 3 && add_triangle_faces<K>(csts, face_normals[cc_id], corners, triangles))
          cc_to_handle.set(cc_id, 0);
        else
        {
          //TODO: shall we try to plug pseudo-cdt?
#ifdef CGAL_DEBUG_DECIMATION
          std::cout << "  DEBUG: Failed to remesh a patch" << std::endl;
#endif
          all_patches_successfully_remeshed = false;
          // make all vertices of the patch a corner
          CGAL::Face_filtered_graph<TriangleMesh> ffg(tm, cc_id, face_cc_ids);
          std::vector<vertex_descriptor> new_corners;
          for (vertex_descriptor v : vertices(ffg))
          {
            std::size_t i = get(vertex_corner_id, v);
            if ( !is_corner_id(i) )
            {
              put(vertex_corner_id, v, nb_corners_and_nb_cc.first++);
              corners.push_back(get(vpm, v));
              new_corners.push_back(v);
            }
          }
          // add all the faces of the current patch
          for (face_descriptor f : faces(ffg))
          {
            halfedge_descriptor h = halfedge(f, tm);
            triangles.push_back({ get(vertex_corner_id, source(h,tm)),
                                  get(vertex_corner_id, target(h,tm)),
                                  get(vertex_corner_id, target(next(h,tm), tm)) });
          }
          // reset flag for neighbor connected components only if interface has changed
          for (vertex_descriptor v : new_corners)
          {
            for (halfedge_descriptor h : halfedges_around_target(halfedge(v, tm), tm))
            {
              if (!is_border(h, tm))
              {
                std::size_t other_cc_id = get(face_cc_ids, face(h, tm));
                cc_to_handle.set(other_cc_id, 1);
                face_boundaries_valid[ other_cc_id ]=false;
              }
            }
          }
          cc_to_handle.set(cc_id, 0);
        }
      }
    }
  }
  while(cc_to_handle.any());

  for (const std::vector<cpp11::array<std::size_t, 3>>& cc_trs : triangles_per_cc)
    out_triangles.insert(out_triangles.end(), cc_trs.begin(), cc_trs.end());

  return all_patches_successfully_remeshed;
}

template <typename TriangleMesh,
          typename VertexCornerIdMap,
          typename EdgeIsConstrainedMap,
          typename FaceCCIdMap,
          typename VertexPointMap>
bool decimate_impl(TriangleMesh& tm,
                   std::pair<std::size_t, std::size_t> nb_corners_and_nb_cc,
                   VertexCornerIdMap& vertex_corner_id,
                   EdgeIsConstrainedMap& edge_is_constrained,
                   FaceCCIdMap& face_cc_ids,
                   const VertexPointMap& vpm)
{
  typedef typename boost::graph_traits<TriangleMesh> graph_traits;
  typedef typename graph_traits::vertex_descriptor vertex_descriptor;
  typedef typename boost::property_traits<VertexPointMap>::value_type Point_3;

  //collect corners
  std::vector< Point_3 > corners(nb_corners_and_nb_cc.first);
  for(vertex_descriptor v : vertices(tm))
  {
    std::size_t i = get(vertex_corner_id, v);
    if ( is_corner_id(i) )
      corners[i]=get(vpm, v);
  }

  std::vector< cpp11::array<std::size_t, 3> > triangles;
  bool remeshing_failed = decimate_impl(tm,
                                        nb_corners_and_nb_cc,
                                        vertex_corner_id,
                                        edge_is_constrained,
                                        face_cc_ids,
                                        vpm,
                                        corners,
                                        triangles);

  if (!is_polygon_soup_a_polygon_mesh(triangles))
    return false;

  //clear(tm);
  tm.clear_without_removing_property_maps();
  polygon_soup_to_polygon_mesh(corners, triangles, tm, parameters::all_default(), parameters::vertex_point_map(vpm));
  return remeshing_failed;
}

template <typename vertex_descriptor,
          typename Point_3,
          typename OutputIterator>
void extract_meshes_containing_a_point(
  const Point_3& pt,
  const std::map<Point_3, std::multimap<std::size_t, vertex_descriptor> >& point_to_vertex_maps,
  OutputIterator out)
{
  typedef std::pair<const std::size_t, vertex_descriptor> Pair_type;
  for(const Pair_type& p : point_to_vertex_maps.find(pt)->second)
    *out++=p.first;
}

template <typename TriangleMesh,
          typename Point_3,
          typename vertex_descriptor,
          typename EdgeIsConstrainedMap,
          typename VertexIsSharedMap,
          typename VertexPointMap>
void mark_boundary_of_shared_patches_as_constrained_edges(
  std::vector<TriangleMesh*>& mesh_ptrs,
  std::map<Point_3, std::multimap<std::size_t, vertex_descriptor> >& point_to_vertex_maps,
  std::vector<EdgeIsConstrainedMap>& edge_is_constrained_maps,
  std::vector<VertexIsSharedMap>& vertex_shared_maps,
  const std::vector<VertexPointMap>& vpms)
{
  typedef boost::graph_traits<TriangleMesh> graph_traits;
  typedef typename graph_traits::edge_descriptor edge_descriptor;
  typedef typename graph_traits::halfedge_descriptor halfedge_descriptor;

  std::size_t mesh_id = 0;
  for(TriangleMesh* tm_ptr : mesh_ptrs)
  {
    TriangleMesh& tm=*tm_ptr;
    EdgeIsConstrainedMap& edge_is_constrained = edge_is_constrained_maps[mesh_id];
    VertexIsSharedMap& is_vertex_shared = vertex_shared_maps[mesh_id];

    for(edge_descriptor e : edges(tm))
    {
      if (is_border(e, tm)) continue; //border edges will be automatically marked as constrained

      halfedge_descriptor h = halfedge(e, tm);
      vertex_descriptor src = source(h, tm), tgt = target(h, tm);
      if (get(is_vertex_shared, src) && get(is_vertex_shared, tgt))
      {
        //extract the set of meshes having both vertices
        std::set<std::size_t> src_set, tgt_set, inter_set;
        extract_meshes_containing_a_point(get(vpms[mesh_id], src),
                                          point_to_vertex_maps,
                                          std::inserter(src_set, src_set.begin()));
        extract_meshes_containing_a_point(get(vpms[mesh_id], tgt),
                                          point_to_vertex_maps,
                                          std::inserter(tgt_set, tgt_set.begin()));

        std::set_intersection(src_set.begin(), src_set.end(),
                              tgt_set.begin(), tgt_set.end(),
                              std::inserter(inter_set, inter_set.begin()));

        std::multimap<std::size_t, vertex_descriptor>& mesh_to_vertex_src =
          point_to_vertex_maps[get(vpms[mesh_id], src)];
        std::multimap<std::size_t, vertex_descriptor>& mesh_to_vertex_tgt =
          point_to_vertex_maps[get(vpms[mesh_id], tgt)];

        std::set<Point_3> incident_face_points;
        incident_face_points.insert(get(vpms[mesh_id], target(next(h, tm), tm)));
        h=opposite(h, tm);
        incident_face_points.insert(get(vpms[mesh_id], target(next(h, tm), tm)));

        // we mark as constrained edge, any edge that is shared between more than 2 meshes
        // such that at least one of the two incident faces to the edge are not present in
        // all the meshes containing the edge
        for(std::size_t other_mesh_id : inter_set)
        {
          TriangleMesh* other_tm_ptr = mesh_ptrs[other_mesh_id];
          if (other_tm_ptr==&tm) continue;

          std::vector<vertex_descriptor> srcs, tgts;
          auto it = mesh_to_vertex_src.find(other_mesh_id);
          while (it!=mesh_to_vertex_src.end() && it->first==other_mesh_id)
            srcs.push_back(it++->second);
          it = mesh_to_vertex_tgt.find(other_mesh_id);
          while (it!=mesh_to_vertex_tgt.end() && it->first==other_mesh_id)
            tgts.push_back(it++->second);

          for (vertex_descriptor other_src : srcs)
          {
            for (vertex_descriptor other_tgt : tgts)
            {
              std::pair<halfedge_descriptor, bool> hres = halfedge(other_src, other_tgt, *other_tm_ptr);
              if (hres.second)
              {
                if (is_border_edge(hres.first, *other_tm_ptr))
                {
                  put(edge_is_constrained, e, true);
                  break;
                }
                if (incident_face_points.count(
                      get(vpms[other_mesh_id], target(next(hres.first, *other_tm_ptr), *other_tm_ptr)))==0)
                {
                  put(edge_is_constrained, e, true);
                  break;
                }
                hres.first=opposite(hres.first, *other_tm_ptr);
                if (incident_face_points.count(
                      get(vpms[other_mesh_id], target(next(hres.first, *other_tm_ptr), *other_tm_ptr)))==0)
                {
                  put(edge_is_constrained, e, true);
                  break;
                }
              }
            }
          }
        }
      }
    }
    ++mesh_id;
  }
}

template<typename Point_3,
         typename vertex_descriptor,
         typename VertexIsSharedMap>
void propagate_corner_status(
  std::vector<VertexIsSharedMap>& vertex_corner_id_maps,
  std::map<Point_3, std::multimap<std::size_t, vertex_descriptor> >& point_to_vertex_maps,
  std::vector< std::pair<std::size_t, std::size_t> >& nb_corners_and_nb_cc_all)
{
  typedef std::pair<const Point_3, std::multimap<std::size_t, vertex_descriptor> > Pair_type;
  for(Pair_type& p : point_to_vertex_maps)
  {
    // if one vertex is a corner, all should be
    typedef std::pair<const std::size_t, vertex_descriptor> Map_pair_type;
    bool is_corner=false;
    for (Map_pair_type& mp : p.second)
    {
      std::size_t mesh_id = mp.first;
      if ( is_corner_id( get(vertex_corner_id_maps[mesh_id], mp.second) ))
      {
        is_corner=true;
        break;
      }
    }
    if (is_corner)
    {
      for(Map_pair_type& mp : p.second)
      {
        std::size_t mesh_id = mp.first;
        if ( !is_corner_id(get(vertex_corner_id_maps[mesh_id], mp.second)) )
        {
          put(vertex_corner_id_maps[mesh_id], mp.second,
            nb_corners_and_nb_cc_all[mesh_id].first++);
        }
      }
    }
  }
}

template <typename TriangleMeshRange,
          typename MeshMap,
          typename VertexPointMap>
bool decimate_meshes_with_common_interfaces_impl(TriangleMeshRange& meshes,
                                                 MeshMap mesh_map,
                                                 double coplanar_cos_threshold,
                                                 const std::vector<VertexPointMap>& vpms)
{
  typedef typename boost::property_traits<MeshMap>::value_type Triangle_mesh;
  typedef typename std::iterator_traits<typename TriangleMeshRange::iterator>::value_type Mesh_descriptor;
  typedef typename boost::property_traits<VertexPointMap>::value_type Point_3;
  typedef typename boost::graph_traits<Triangle_mesh> graph_traits;
  typedef typename graph_traits::vertex_descriptor vertex_descriptor;
  typedef typename graph_traits::edge_descriptor edge_descriptor;
  typedef typename graph_traits::face_descriptor face_descriptor;
  CGAL_assertion(coplanar_cos_threshold<0);
  typedef typename graph_traits::halfedge_descriptor halfedge_descriptor;

  // declare and init all property maps
  typedef typename boost::property_map<Triangle_mesh, CGAL::dynamic_face_property_t<std::size_t> >::type Face_cc_map;
  typedef typename boost::property_map<Triangle_mesh, CGAL::dynamic_edge_property_t<bool> >::type Edge_is_constrained_map;
  typedef typename boost::property_map<Triangle_mesh, CGAL::dynamic_vertex_property_t<std::size_t> >::type Vertex_corner_id_map;
  typedef typename boost::property_map<Triangle_mesh, CGAL::dynamic_vertex_property_t<bool> >::type Vertex_is_shared_map;

  std::vector<Vertex_is_shared_map > vertex_shared_maps;
  std::vector<Edge_is_constrained_map> edge_is_constrained_maps;
  std::vector<Vertex_corner_id_map> vertex_corner_id_maps;
  std::vector<Face_cc_map> face_cc_ids_maps;
  const std::size_t nb_meshes = meshes.size();
  std::vector<bool> mesh_has_non_manifold_vertices(nb_meshes, false);
  vertex_corner_id_maps.reserve(nb_meshes);
  vertex_shared_maps.reserve(nb_meshes);
  edge_is_constrained_maps.reserve(nb_meshes);
  face_cc_ids_maps.reserve(nb_meshes);

  std::vector<Triangle_mesh*> mesh_ptrs;
  mesh_ptrs.reserve(nb_meshes);
  for(Mesh_descriptor& md : meshes)
    mesh_ptrs.push_back( &(mesh_map[md]) );

  auto has_non_manifold_vertices = [](const Triangle_mesh& tm)
  {
    std::vector<halfedge_descriptor> nmvs;
    non_manifold_vertices(tm, std::back_inserter(nmvs));
    return !nmvs.empty();
  };

  std::size_t mesh_id=0;
  for(Triangle_mesh* tm_ptr : mesh_ptrs)
  {
    Triangle_mesh& tm = *tm_ptr;
    mesh_has_non_manifold_vertices[mesh_id] = has_non_manifold_vertices(tm);
    vertex_shared_maps.push_back( get(CGAL::dynamic_vertex_property_t<bool>(), tm) );
    for(vertex_descriptor v : vertices(tm))
      put(vertex_shared_maps.back(), v, false);

    edge_is_constrained_maps.push_back( get(CGAL::dynamic_edge_property_t<bool>(), tm) );
    for(edge_descriptor e : edges(tm))
      put(edge_is_constrained_maps.back(), e, false);

    vertex_corner_id_maps.push_back( get(CGAL::dynamic_vertex_property_t<std::size_t>(), tm) );
    for(vertex_descriptor v : vertices(tm))
      put(vertex_corner_id_maps.back(), v, Planar_segmentation::init_id());

    face_cc_ids_maps.push_back( get(CGAL::dynamic_face_property_t<std::size_t>(), tm) );
    for(face_descriptor f : faces(tm))
      put(face_cc_ids_maps.back(), f, -1);
    ++mesh_id;
  }

  std::map<Point_3, std::multimap<std::size_t, vertex_descriptor> > point_to_vertex_maps;

  //start by detecting and marking all shared vertices
  mesh_id = 0;
  for(Triangle_mesh* tm_ptr : mesh_ptrs)
  {
    Triangle_mesh& tm = *tm_ptr;

    for(vertex_descriptor v : vertices(tm))
    {
      std::multimap<std::size_t, vertex_descriptor>& mesh_id_to_vertex =
        point_to_vertex_maps[get(vpms[mesh_id], v)];
      if (!mesh_id_to_vertex.empty())
        put(vertex_shared_maps[mesh_id], v, true);
      if (mesh_id_to_vertex.size()==1)
      {
        std::pair<std::size_t, vertex_descriptor> other=*mesh_id_to_vertex.begin();
        put(vertex_shared_maps[other.first], other.second, true);
      }
      mesh_id_to_vertex.insert( std::make_pair(mesh_id, v) );
    }

    ++mesh_id;
  }

  //then detect edge on the boundary of shared patches and mark them as constrained
  mark_boundary_of_shared_patches_as_constrained_edges(mesh_ptrs, point_to_vertex_maps, edge_is_constrained_maps, vertex_shared_maps, vpms);

  // first tag corners and constrained edges
  std::vector< std::pair<std::size_t, std::size_t> > nb_corners_and_nb_cc_all(nb_meshes);
  mesh_id=0;
  for(Triangle_mesh* tm_ptr : mesh_ptrs)
  {
    Triangle_mesh& tm = *tm_ptr;

    //reset face cc ids as it was set by coplanarity_segmentation_with_pca
    for(face_descriptor f : faces(tm))
      put(face_cc_ids_maps[mesh_id], f, -1);

    if (!mesh_has_non_manifold_vertices[mesh_id])
      nb_corners_and_nb_cc_all[mesh_id] =
        tag_corners_and_constrained_edges(tm,
                                          coplanar_cos_threshold,
                                          vertex_corner_id_maps[mesh_id],
                                          edge_is_constrained_maps[mesh_id],
                                          face_cc_ids_maps[mesh_id],
                                          vpms[mesh_id]);
    else
    {
      nb_corners_and_nb_cc_all[mesh_id]={0,1};
      for (vertex_descriptor vd : vertices(tm))
      {
        if (get(vertex_shared_maps[mesh_id], vd))
        {
          put(vertex_corner_id_maps[mesh_id], vd, true);
          ++nb_corners_and_nb_cc_all[mesh_id].first;
        }
      }
    }
    ++mesh_id;
  }

  // extra step to propagate is_corner to all meshes to make sure shared vertices are kept
  propagate_corner_status(vertex_corner_id_maps, point_to_vertex_maps, nb_corners_and_nb_cc_all);

  /// @TODO: make identical patches normal identical (up to the sign). Needed only in the approximate case

// now call the decimation
  // storage of all new triangles and all corners
  std::vector< std::vector< Point_3 > > all_corners(nb_meshes);
  std::vector< std::vector< cpp11::array<std::size_t, 3> > > all_triangles(nb_meshes);
  bool res = true;
  std::vector<bool> to_be_processed(nb_meshes, true);
  bool loop_again;
  bool no_remeshing_issue = true;
  do{
    loop_again = false;
    for(std::size_t mesh_id=0; mesh_id<nb_meshes; ++mesh_id)
    {
      if (!to_be_processed[mesh_id]) continue;
#ifdef CGAL_DEBUG_DECIMATION
      std::cout << "Handling mesh #" << mesh_id << "\n";
      if (mesh_has_non_manifold_vertices[mesh_id])
        std::cout << "  mesh has non-manifold vertices, mesh is kept as is.\n";
#endif
      if (mesh_has_non_manifold_vertices[mesh_id])
      {
        to_be_processed[mesh_id]=false;
        continue;
      }
      all_triangles[mesh_id].clear();
      Triangle_mesh& tm = *mesh_ptrs[mesh_id];

      //collect corners
      std::vector< Point_3 >& corners = all_corners[mesh_id];
      if (corners.empty())
      {
        corners.resize(nb_corners_and_nb_cc_all[mesh_id].first);
        for(vertex_descriptor v : vertices(tm))
        {
          std::size_t i = get(vertex_corner_id_maps[mesh_id], v);
          if ( is_corner_id(i) )
            corners[i]=get(vpms[mesh_id], v);
        }
      }
      std::size_t ncid=corners.size();

      bool all_patches_successfully_remeshed =
        decimate_impl(tm,
                      nb_corners_and_nb_cc_all[mesh_id],
                      vertex_corner_id_maps[mesh_id],
                      edge_is_constrained_maps[mesh_id],
                      face_cc_ids_maps[mesh_id],
                      vpms[mesh_id],
                      corners,
                      all_triangles[mesh_id]) &&
        is_polygon_soup_a_polygon_mesh(all_triangles[mesh_id]);
#ifdef CGAL_DEBUG_DECIMATION
      std::cout << "all_patches_successfully_remeshed? " << all_patches_successfully_remeshed << "\n";
#endif
      if (!all_patches_successfully_remeshed)
      {
        no_remeshing_issue=false;
        // iterate over points newly marked as corners
        std::set<std::size_t> mesh_ids;
        for (std::size_t cid=ncid; cid<corners.size(); ++cid)
        {
          typedef std::pair<const std::size_t, vertex_descriptor> Map_pair_type;
          auto find_res = point_to_vertex_maps.find(corners[cid]);
          assert(find_res != point_to_vertex_maps.end());
          for(Map_pair_type& mp : find_res->second)
          {
            std::size_t other_mesh_id = mp.first;
            if ( other_mesh_id!=mesh_id  && !is_corner_id(get(vertex_corner_id_maps[other_mesh_id], mp.second)))
            {
              mesh_ids.insert(other_mesh_id);
              put(vertex_corner_id_maps[other_mesh_id], mp.second,
                nb_corners_and_nb_cc_all[other_mesh_id].first++);
              if (!all_corners[other_mesh_id].empty())
                all_corners[other_mesh_id].push_back(corners[cid]);
            }
          }
        }
        for (std::size_t mid : mesh_ids)
          if (!to_be_processed[mid])
          {
            if (!loop_again)
              std::cout << "setting for another loop\n";
            loop_again=true;
            to_be_processed[mid] = true;
          }
      }
      to_be_processed[mesh_id] = false;
    }
  }
  while(loop_again);

  // now create the new meshes:
  for(std::size_t mesh_id=0; mesh_id<nb_meshes; ++mesh_id)
  {
    Triangle_mesh& tm = *mesh_ptrs[mesh_id];
    if (mesh_has_non_manifold_vertices[mesh_id])
    {
      no_remeshing_issue = false;
      continue;
    }
    CGAL_assertion(is_polygon_soup_a_polygon_mesh(all_triangles[mesh_id]));

    //clear(tm);
    tm.clear_without_removing_property_maps();
    polygon_soup_to_polygon_mesh(all_corners[mesh_id], all_triangles[mesh_id],
                                 tm, parameters::all_default(), parameters::vertex_point_map(vpms[mesh_id]));
  }

  return res;
}

} //end of namespace Planar_segmentation


/*!
 *  \ingroup PMP_meshing_grp
 *  generates a new triangle mesh `tm_out` with the minimal number of triangles while preserving the shape as `tm_in`.
 *  In practice, this means that connected components of edge incident faces belonging the same plane are
 *  first extracted (each such connected component is called a *patch*). Then the connected components of vertex
 *  connected patch border edge belonging to the same line are extracted. Endpoints of such components and
 *  vertices incident to more that two patches (or two patches + one mesh boundary) are called *corners*.
 *  `tm_out` contains the 2D constrained Delaunay triangulation of each patch with bounder defined by
 *  only corner vertices.
 *
 *  \warning if `tm_in` contains a non-manifold vertex, `tm_out` will always be empty. Those vertices must be
 *           duplicated with `duplicate_non_manifold_vertices()` to get an output.
 *
 *  \tparam TriangleMeshIn a model of `HalfedgeListGraph` and `FaceListGraph`
 *  \tparam TriangleMeshOut a model of `MutableFaceGraph`
 *  \tparam NamedParametersIn a sequence of \ref bgl_namedparameters "Named Parameters"
 *  \tparam NamedParametersOut a sequence of \ref bgl_namedparameters "Named Parameters"
 *
 *  \param tm_in input triangulated surface mesh
 *  \param tm_out output mesh
 *  \param np_in an optional sequence of \ref bgl_namedparameters "Named Parameters" among the ones listed below:
 *
 *  \cgalNamedParamsBegin
 *    \cgalParamNBegin{vertex_point_map}
 *      \cgalParamDescription{a property map associating points to the vertices of `tm_in`}
 *      \cgalParamType{a class model of `ReadablePropertyMap` with `boost::graph_traits<TriangleMeshIn>::%vertex_descriptor`
 *                     as key type and `%Point_3` as value type}
 *      \cgalParamDefault{`boost::get(CGAL::vertex_point, tm_in)`}
 *      \cgalParamExtra{If this parameter is omitted, an internal property map for `CGAL::vertex_point_t`
 *                      must be available in `TriangleMeshIn`.}
 *    \cgalParamNEnd
 *    \cgalParamNBegin{geom_traits}
 *      \cgalParamDescription{an instance of a geometric traits class}
 *      \cgalParamType{a class model of `Kernel`}
 *      \cgalParamDefault{a \cgal Kernel deduced from the point type, using `CGAL::Kernel_traits`}
 *      \cgalParamExtra{The geometric traits class must be compatible with the vertex point type.}
 *    \cgalParamNEnd
 *    \cgalParamNBegin{edge_is_constrained_map}
 *      \cgalParamDescription{a property map filled by this function and that will contain `true` if an edge is on the border of a patch and `false` otherwise.}
 *      \cgalParamType{a class model of `ReadWritePropertyMap` with `boost::graph_traits<TriangleMeshIn>::%edge_descriptor`
 *                     as key type and `bool` as value type}
 *      \cgalParamDefault{None}
 *    \cgalParamNEnd
 *    \cgalParamNBegin{face_patch_map}
 *      \cgalParamDescription{a property map filled by this function and that will contain for each face the id
 *                            of its patch in the range `[0, number of patches - 1]`}
 *      \cgalParamType{a class model of `ReadWritePropertyMap` with `boost::graph_traits<TriangleMeshIn>::%face_descriptor`
 *                     as key type and `std::size_t` as value type}
 *      \cgalParamDefault{None}
 *    \cgalParamNEnd
 *    \cgalParamNBegin{vertex_corner_map}
 *      \cgalParamDescription{a property map filled by this function and that will contain for each vertex that is a corner
 *                            an id in the range `[0, number of corners - 1]`, and `std::size_t(-1)` otherwise.}
 *      \cgalParamType{a class model of `ReadWritePropertyMap` with `boost::graph_traits<TriangleMeshIn>::%vertex_descriptor`
 *                     as key type and `std::size_t` as value type}
 *      \cgalParamDefault{None}
 *   \cgalParamNEnd
 *    \cgalParamNBegin{cosinus_threshold}
 *      \cgalParamDescription{the cosinus an angle that is used as the lower bound of both the dihedral angle between two adjacent
                              triangles to consider then as coplanar, and the angle betweem adjacent segments to consider then as collinear.}
 *      \cgalParamType{`FT` type from the `geom_traits` parameter}
 *      \cgalParamDefault{-1, which means exact coplanarity and collinearity}
 *   \cgalParamNEnd
 *  \cgalNamedParamsEnd
 *
 *  \param np_out an optional sequence of \ref bgl_namedparameters "Named Parameters" among the ones listed below:
 *
 *  \cgalNamedParamsBegin
 *    \cgalParamNBegin{vertex_point_map}
 *      \cgalParamDescription{a property map associating points to the vertices of `tm_out`}
 *      \cgalParamType{a class model of `WritablePropertyMap` with `boost::graph_traits<TriangleMeshOut>::%vertex_descriptor`
 *                     as key type and `%Point_3` as value type}
 *      \cgalParamDefault{`boost::get(CGAL::vertex_point, tm_out)`}
 *      \cgalParamExtra{If this parameter is omitted, an internal property map for `CGAL::vertex_point_t`
 *                      must be available in `TriangleMeshOut`.}
 *    \cgalParamNEnd
 *    \cgalParamNBegin{face_patch_map}
 *      \cgalParamDescription{a property map filled by this function and that will contain for each face the id
 *                            of its patch in the range `[0, number of patches - 1]`}
 *      \cgalParamType{a class model of `ReadWritePropertyMap` with `boost::graph_traits<TriangleMeshOut>::%face_descriptor`
 *                     as key type and `std::size_t` as value type}
 *      \cgalParamDefault{None}
 *    \cgalParamNEnd
 *    \cgalParamNBegin{vertex_corner_map}
 *      \cgalParamDescription{a property map filled by this function and that will contain for each vertex its corner
 *                            an id in the range `[0, number of corners - 1]`}
 *      \cgalParamType{a class model of `ReadWritePropertyMap` with `boost::graph_traits<TriangleMeshOut>::%vertex_descriptor`
 *                     as key type and `std::size_t` as value type}
 *      \cgalParamDefault{None}
 *    \cgalParamNEnd
 *  \cgalNamedParamsEnd
 */
template <typename TriangleMeshIn,
          typename TriangleMeshOut,
          typename NamedParametersIn = parameters::Default_named_parameters,
          typename NamedParametersOut = parameters::Default_named_parameters>
void remesh_planar_patches(const TriangleMeshIn& tm_in,
                                 TriangleMeshOut& tm_out,
                           const NamedParametersIn& np_in = parameters::default_values(),
                           const NamedParametersOut& np_out = parameters::default_values())
{
/*
  // typedef typename GetGeomTraits<TriangleMesh, NamedParameters>::type  Traits;
  typedef typename GetVertexPointMap <TriangleMesh, NamedParameters>::type VPM;
  using parameters::choose_parameter;
  using parameters::get_parameter;

  typedef typename boost::graph_traits<TriangleMesh> graph_traits;
  typedef typename graph_traits::edge_descriptor edge_descriptor;
  typedef typename graph_traits::vertex_descriptor vertex_descriptor;
  typedef typename graph_traits::face_descriptor face_descriptor;

  double coplanar_cos_threshold = choose_parameter(get_parameter(np, internal_np::cosinus_threshold), -1);
  CGAL_precondition(coplanar_cos_threshold<0);

  // initialize property maps
  typename boost::property_map<TriangleMesh, CGAL::dynamic_edge_property_t<bool> >::type edge_is_constrained = get(CGAL::dynamic_edge_property_t<bool>(), tm);
  for(edge_descriptor e : edges(tm))
    put(edge_is_constrained, e, false);

  typename boost::property_map<TriangleMesh, CGAL::dynamic_vertex_property_t<std::size_t> >::type vertex_corner_id = get(CGAL::dynamic_vertex_property_t<std::size_t>(), tm);
  for(vertex_descriptor v : vertices(tm))
    put(vertex_corner_id, v, Planar_segmentation::init_id());

  typename boost::property_map<TriangleMesh, CGAL::dynamic_face_property_t<std::size_t> >::type face_cc_ids = get(CGAL::dynamic_face_property_t<std::size_t>(), tm);
  for(face_descriptor f : faces(tm))
    put(face_cc_ids, f, -1);

  VPM vpm = choose_parameter(get_parameter(np, internal_np::vertex_point),
                             get_property_map(vertex_point, tm));

  std::pair<std::size_t, std::size_t> nb_corners_and_nb_cc =
    Planar_segmentation::tag_corners_and_constrained_edges(tm, coplanar_cos_threshold, vertex_corner_id, edge_is_constrained, face_cc_ids, vpm);
  Planar_segmentation::decimate_impl(tm,
                                     nb_corners_and_nb_cc,
                                     vertex_corner_id,
                                     edge_is_constrained,
                                     face_cc_ids,
                                     vpm);
*/
}

/*!
 *  \ingroup PMP_meshing_grp
 *  generates a new triangle mesh `tm_out` with the minimal number of triangles from a partition of `tm_in`.
 *  The terminology used here and the global idea is very similar to what is done by `remesh_planar_patches()`
 *  except that here the partition into patches and corner identification is provided by the user.
 *  It allows to have a remeshing of almost coplanar regiond, detected for example using the region growing algorithm
 *  with the functions `region_growing_of_planes_on_faces()` and `detect_corners_of_regions()`.
 *  If a patch cannot be triangulated, it is left untouched in the output and all its vertices becomes corners
 *  so that the output is still a valid conformal triangle mesh.
 *  \todo: define how triangulation normals are estimated and maybe ask for them?
 *  \returns `true` if all patches could be triangulated and `false` otherwise.
 *
 *  \tparam TriangleMeshIn a model of `HalfedgeListGraph` and `FaceListGraph`
 *  \tparam TriangleMeshOut a model of `MutableFaceGraph`
 *
 *  \tparam FacePatchMap a class model of `ReadablePropertyMap` with `boost::graph_traits<TriangleMeshIn>::%face_descriptor`
 *                       as key type and `std::size_t` as value type
 *  \tparam EdgeIsConstrainedMap a class model of `ReadablePropertyMap` with `boost::graph_traits<TriangleMeshIn>::%edge_descriptor`
 *                               as key type and `bool` as value type
 *  \tparam VertexCornerMap a class model of `ReadablePropertyMap` with `boost::graph_traits<TriangleMeshIn>::%vertex_descriptor`
 *                          as key type and `std::size_t` as value type
 *  \tparam NamedParametersIn a sequence of \ref bgl_namedparameters "Named Parameters"
 *  \tparam NamedParametersOut a sequence of \ref bgl_namedparameters "Named Parameters"
 *
 *  \param tm_in input triangulated surface mesh
 *  \param tm_out output mesh
 *  \param nb_patches the number of patches in the partition
 *  \param nb_corners the number of corners
 *  \param face_patch_map a property map that contains for each face the id of its patch in the range `[0, nb_patches]`
 *  \param vertex_corner_map a property map that contains for each vertex that is a corner an id in the range `[0, nb_corners - 1]`,
                             and `std::size_t(-1)` otherwise.
 *  \param ecm a property map that contains `true` if an edge is on the border of a patch and `false` otherwise.
 *  \param np_in an optional sequence of \ref bgl_namedparameters "Named Parameters" among the ones listed below:
 *
 *  \cgalNamedParamsBegin
 *    \cgalParamNBegin{vertex_point_map}
 *      \cgalParamDescription{a property map associating points to the vertices of `tm_in`}
 *      \cgalParamType{a class model of `ReadablePropertyMap` with `boost::graph_traits<TriangleMeshIn>::%vertex_descriptor`
 *                     as key type and `%Point_3` as value type}
 *      \cgalParamDefault{`boost::get(CGAL::vertex_point, tm_in)`}
 *      \cgalParamExtra{If this parameter is omitted, an internal property map for `CGAL::vertex_point_t`
 *                      must be available in `TriangleMeshIn`.}
 *    \cgalParamNEnd
 *  \cgalNamedParamsEnd
 *
 *  \param np_out an optional sequence of \ref bgl_namedparameters "Named Parameters" among the ones listed below:
 *
 *  \cgalNamedParamsBegin
 *    \cgalParamNBegin{vertex_point_map}
 *      \cgalParamDescription{a property map associating points to the vertices of `tm_out`}
 *      \cgalParamType{a class model of `WritablePropertyMap` with `boost::graph_traits<TriangleMeshOut>::%vertex_descriptor`
 *                     as key type and `%Point_3` as value type}
 *      \cgalParamDefault{`boost::get(CGAL::vertex_point, tm_out)`}
 *      \cgalParamExtra{If this parameter is omitted, an internal property map for `CGAL::vertex_point_t`
 *                      must be available in `TriangleMeshOut`.}
 *    \cgalParamNEnd
 *    \cgalParamNBegin{face_patch_map}
 *      \cgalParamDescription{a property map filled by this function and that will contain for each face the id
 *                            of its patch in the range `[0, number of patches - 1]`}
 *      \cgalParamType{a class model of `ReadWritePropertyMap` with `boost::graph_traits<TriangleMeshOut>::%face_descriptor`
 *                     as key type and `std::size_t` as value type}
 *      \cgalParamDefault{None}
 *    \cgalParamNEnd
 *    \cgalParamNBegin{vertex_corner_map}
 *      \cgalParamDescription{a property map filled by this function and that will contain for each vertex its corner
 *                            an id in the range `[0, number of corners - 1]`}
 *      \cgalParamType{a class model of `ReadWritePropertyMap` with `boost::graph_traits<TriangleMeshOut>::%vertex_descriptor`
 *                     as key type and `std::size_t` as value type}
 *      \cgalParamDefault{None}
 *    \cgalParamNEnd
 *  \cgalNamedParamsEnd
 */
template <typename TriangleMeshIn,
          typename TriangleMeshOut,
          typename FacePatchMap,
          typename EdgeIsConstrainedMap,
          typename VertexCornerMap,
          typename NamedParameters = parameters::Default_named_parameters>
bool remesh_almost_planar_patches(const TriangleMeshIn& tm_in,
                                        TriangleMeshOut& tm_out,
                                   std::size_t nb_patches,
                                   std::size_t nb_corners,
                                   FacePatchMap face_patch_map,
                                   VertexCornerMap vertex_corner_map,
                                   EdgeIsConstrainedMap ecm,
                                   const NamedParametersIn& np_in = parameters::default_values(),
                                   const NamedParametersOut& np_out = parameters::default_values())
{
/*
  typedef typename GetVertexPointMap <TriangleMesh, NamedParameters>::type VPM;
  using parameters::choose_parameter;
  using parameters::get_parameter;

  VPM vpm = choose_parameter(get_parameter(np, internal_np::vertex_point),
                             get_property_map(vertex_point, tm));

  return Planar_segmentation::decimate_impl(tm,
                                            std::make_pair(nb_corners, nb_patches),
                                            vertex_corner_map, ecm, face_patch_map, vpm);
*/
}


#ifndef DOXYGEN_RUNNING
// MeshMap must be a mutable lvalue pmap with Triangle_mesh as value_type
template <typename TriangleMeshRange, typename MeshMap>
bool decimate_meshes_with_common_interfaces(TriangleMeshRange& meshes, double coplanar_cos_threshold, MeshMap mesh_map)
{
  CGAL_assertion(coplanar_cos_threshold<0);
  typedef typename boost::property_traits<MeshMap>::value_type Triangle_mesh;
  typedef typename std::iterator_traits<typename TriangleMeshRange::iterator>::value_type Mesh_descriptor;

  /// @TODO turn into a range of named parameter
  std::vector<typename boost::property_map<Triangle_mesh, boost::vertex_point_t>::type > vpms;
  vpms.reserve(meshes.size());

  for(Mesh_descriptor& md : meshes)
    vpms.push_back( get(boost::vertex_point, mesh_map[md]) );
  return Planar_segmentation::decimate_meshes_with_common_interfaces_impl(meshes, mesh_map, coplanar_cos_threshold, vpms);
}

template <class TriangleMesh>
bool decimate_meshes_with_common_interfaces(std::vector<TriangleMesh>& meshes, double coplanar_cos_threshold=-1)
{
  return decimate_meshes_with_common_interfaces(meshes, coplanar_cos_threshold, CGAL::Identity_property_map<TriangleMesh>());
}
#endif

} } // end of CGAL::Polygon_mesh_processing

#endif // CGAL_POLYGON_MESH_PROCESSING_REMESH_PLANAR_PATCHES_H
