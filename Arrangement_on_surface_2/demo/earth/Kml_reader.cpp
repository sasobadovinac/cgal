
#include "Kml_reader.h"

#include <iostream>
#include <unordered_map>

#include <qdebug.h>
#include <qfile.h>
#include <qxmlstream.h>


bool Kml::Node::operator == (const Node& r) const
{
  return  (lon == r.lon) && (lat == r.lat);
}
Kml::Vec3d Kml::Node::get_coords_3d(const double r) const
{
  const auto phi = qDegreesToRadians(lat);
  const auto theta = qDegreesToRadians(lon);
  const auto z = r * std::sin(phi);
  const auto rxy = r * std::cos(phi);
  const auto x = rxy * std::cos(theta);
  const auto y = rxy * std::sin(theta);

  return Vec3d{ x, y, z };
}
QVector3D Kml::Node::get_coords_3f(const double r) const
{
  const auto v = get_coords_3d(r);
  return QVector3D(v.x, v.y, v.z);
}
std::ostream& operator << (std::ostream& os, const Kml::Node& n)
{
  os << n.lon << ", " << n.lat;
  return os;
}


Kml::Placemarks  Kml::read(const std::string& file_name)
{
  LinearRing    lring;
  Polygon       polygon;
  Placemark     placemark;
  Placemarks    placemarks;

  QFile file(file_name.c_str());
  if (file.open(QIODevice::ReadOnly))
  {
    QXmlStreamReader xmlReader;
    xmlReader.setDevice(&file);

    xmlReader.readNext();

    // Reading from the file
    while (!xmlReader.isEndDocument())
    {
      QString name = xmlReader.name().toString();

      if (xmlReader.isStartElement())
      {
        if (name == "Placemark")
        {
          placemark = Placemark{};
        }
        else if (name == "Polygon")
        {
          polygon = Polygon{};
        }
        else if (name == "LinearRing")
        {
          lring = LinearRing{};
        }
        else if (name == "coordinates")
        {
          xmlReader.readNext();
          auto str = xmlReader.text().toString();
          auto node_strs = str.split(" ");
          for (const auto& node_str : node_strs)
          {
            if (node_str.isEmpty())
              continue;

            auto coord_strs = node_str.split(",");
            const auto lon = coord_strs[0].toDouble();
            const auto lat = coord_strs[1].toDouble();
            lring.nodes.push_back(Node{ lon, lat });
          }
        }
        else if (name == "SimpleData")
        {
          auto attributes = xmlReader.attributes();
          auto attr_name = attributes[0].name().toString();
          auto attr_value = attributes[0].value().toString();
          if ((attr_name == "name") && (attr_value == "ADMIN"))
          {
            xmlReader.readNext();
            placemark.name = xmlReader.text().toString().toStdString();;
          }
        }
      }
      else if (xmlReader.isEndElement())
      {
        if (name == "Placemark")
        {
          placemarks.push_back(std::move(placemark));
        }
        else if (name == "Polygon")
        {
          placemark.polygons.push_back(std::move(polygon));
        }
        else if (name == "outerBoundaryIs")
        {
          polygon.outer_boundary = std::move(lring);
        }
        else if (name == "innerBoundaryIs")
        {
          polygon.inner_boundaries.push_back(std::move(lring));
        }
        else if (name == "LinearRing")
        {
          // LinearRing is moved to the correct locations via other tags above
        }
        else if (name == "coordinates")
        {
          // no need to do anything here: the coordinates are read above!
        }
      }

      xmlReader.readNext();
    }

    if (xmlReader.hasError())
    {
      std::cout << "XML error: " << xmlReader.errorString().data() << std::endl;
    }
  }

  return placemarks;
}

Kml::Nodes Kml::get_duplicates(const Placemarks& placemarks)
{
  // collect all nodes into a single vector
  int polygon_count = 0;
  std::vector<Kml::Node> nodes;
  for (const auto& pm : placemarks)
  {
    for (const auto& polygon : pm.polygons)
    {
      polygon_count++;
      
      Kml::LinearRings linear_rings;
      linear_rings.push_back(polygon.outer_boundary);
      for (const auto& inner_boundary : polygon.inner_boundaries)
        linear_rings.push_back(inner_boundary);
      
      for(const auto& lring : linear_rings)
      {
        for (const auto& node : lring.nodes)
          nodes.push_back(node);
      }
    }
  }
  qDebug() << "polygon count = " << polygon_count;

  int count = nodes.size();
  std::vector<int> num_duplicates(count, 0);
  qDebug() << "node count (with duplicates) = " << count;
  int dup_count = 0;

  // this keeps track of how many nodes there are with certain dup-count
  std::unordered_map<int, int> dup_count_map;

  Nodes duplicate_nodes;
  for (int i = 0; i < count; ++i)
  {
    // if the current node has been detected as duplicate skip it
    if (num_duplicates[i] > 0)
      continue;

    const auto& curr_node = nodes[i];
    std::vector<int> curr_dup; // current set of duplicates
    for (int j = i + 1; j < count; ++j)
    {
      if (curr_node == nodes[j])
      {
        curr_dup.push_back(j);
      }
    }

    // if duplicates found
    if (!curr_dup.empty())
    {
      dup_count++;
      int num_dup = curr_dup.size() + 1; // +1 for the i'th node
      num_duplicates[i] = num_dup;
      for (const auto di : curr_dup)
        num_duplicates[di] = num_dup;

      duplicate_nodes.push_back(curr_node);
      dup_count_map[num_dup]++;
    }
  }
  qDebug() << "dup count = " << dup_count;
  for (const auto& p : dup_count_map)
  {
    const auto dup_count = p.first;
    const auto num_nodes_with_this_dup_count = p.second;
    qDebug() << dup_count << ": " << num_nodes_with_this_dup_count;
  }

  return duplicate_nodes;
}

Kml::Nodes Kml::Polygon::get_all_nodes() const
{
  Nodes all_nodes;
  auto source_first = outer_boundary.nodes.begin();
  auto source_last = outer_boundary.nodes.end();
  all_nodes.insert(all_nodes.begin(), source_first, source_last);

  for (const auto& inner_boundary : inner_boundaries)
  {
    auto source_first = inner_boundary.nodes.begin();
    auto source_last = inner_boundary.nodes.end();
    all_nodes.insert(all_nodes.begin(), source_first, source_last);
  }

  return all_nodes;
}

Kml::Nodes Kml::Placemark::get_all_nodes() const
{
  Nodes all_nodes;
  for (const auto& polygon : polygons)
  {
    auto polygon_nodes = polygon.get_all_nodes();
    auto first = std::make_move_iterator(polygon_nodes.begin());
    auto last = std::make_move_iterator(polygon_nodes.end());
    all_nodes.insert(all_nodes.end(), first, last);
  }

  return all_nodes;
}

Kml::Arcs Kml::LinearRing::get_arcs() const
{
  Arcs arcs;
  const int num_nodes = nodes.size();
  for (int i = 0; i < num_nodes - 1; ++i)
  {
    const auto from = nodes[i];
    const auto to = nodes[i + 1];
    arcs.push_back(Arc{ from, to });
  }
  return arcs;
}
void Kml::LinearRing::get_arcs(Arcs& arcs) const
{
  auto a = get_arcs();
  arcs.insert(arcs.end(), a.begin(), a.end());
}

Kml::Arcs Kml::Placemark::get_all_arcs() const
{
  Arcs all_arcs;

  for (const auto& polygon : polygons)
  {
    polygon.outer_boundary.get_arcs(all_arcs);
    for (const auto& inner_boundary : polygon.inner_boundaries)
      inner_boundary.get_arcs(all_arcs);
  }

  return all_arcs;
}

