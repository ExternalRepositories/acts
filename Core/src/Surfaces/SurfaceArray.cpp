// This file is part of the ACTS project.
//
// Copyright (C) 2018 ACTS project team
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "ACTS/Surfaces/SurfaceArray.hpp"
#include "ACTS/Utilities/Definitions.hpp"
#include "ACTS/Utilities/InstanceFactory.hpp"
#include "ACTS/Utilities/ThrowAssert.hpp"
#include "ACTS/Utilities/VariantData.hpp"

Acts::variant_data
Acts::SurfaceArray::toVariantData() const
{
  using namespace std::string_literals;
  variant_map payload;

  throw_assert(m_gridLookup->dimensions() != 0,
               "0-dim SurfaceGridLookups cannot currently be serialized");

  payload["surfacegridlookup"] = surfaceGridLookupToVariantData(*m_gridLookup);

  variant_vector surfaces;

  for (const auto& srf : m_surfaces) {
    surfaces.push_back(srf->toVariantData());
  }

  payload["surfaces"] = surfaces;

  variant_map data;
  data["type"]    = "SurfaceArray"s;
  data["payload"] = payload;
  return data;
}

Acts::variant_data
Acts::SurfaceArray::surfaceGridLookupToVariantData(
    const ISurfaceGridLookup& sgl) const
{
  using namespace std::string_literals;
  variant_map payload;

  payload["dimensions"] = int(sgl.dimensions());
  variant_vector axes;

  for (const auto& axis : sgl.getAxes()) {
    variant_map ax_pl;

  ax_pl["axistype"] = axis->isEquidistant() ? "equidistant"s : "variable"s;

    if (axis->isEquidistant()) {
      ax_pl["min"]   = axis->getMin();
      ax_pl["max"]   = axis->getMax();
      ax_pl["nbins"] = int(axis->getNBins());
    } else {
      variant_vector bin_edges;
      for (const auto& bin_edge : axis->getBinEdges()) {
        bin_edges.push_back(bin_edge);
      }
      ax_pl["bin_edges"] = bin_edges;
    }

    if (axis->getBoundaryType() == detail::AxisBoundaryType::Open) {
      ax_pl["axisboundarytype"] = "open"s;
    } else if (axis->getBoundaryType() == detail::AxisBoundaryType::Bound) {
      ax_pl["axisboundarytype"] = "bound"s;
    } else if (axis->getBoundaryType() == detail::AxisBoundaryType::Closed) {
      ax_pl["axisboundarytype"] = "closed"s;
    }

    variant_map ax_data;
    ax_data["type"]    = "Axis"s;
    ax_data["payload"] = ax_pl;
    axes.push_back(ax_data);
  }
  payload["axes"] = axes;

  variant_map data;
  data["type"]    = "SurfaceGridLookup"s;
  data["payload"] = payload;
  return data;
}

Acts::SurfaceArray::SurfaceArray(const variant_data&                      data_,
                                 std::function<Vector2D(const Vector3D&)> g2l,
                                 std::function<Vector3D(const Vector2D&)> l2g,
                                 std::shared_ptr<const Transform3D> transform)
  : m_gridLookup(nullptr), m_transform(transform)
{
  const variant_map& data = boost::get<variant_map>(data_);
  throw_assert(data.get<std::string>("type") == "SurfaceArray",
               "Type must be SurfaceArray");

  const variant_map& payload = data.get<variant_map>("payload");
  const variant_map& var_sgl = payload.get<variant_map>("surfacegridlookup");
  throw_assert(var_sgl.get<std::string>("type") == "SurfaceGridLookup",
               "Currently only SurfaceGridLookup can be deserialized");
  const variant_vector& var_surfaces = payload.get<variant_vector>("surfaces");

  InstanceFactory factory;

  std::vector<const Surface*> surfaces;
  for (const auto& var_srf_ : var_surfaces) {
    const variant_map& var_srf = boost::get<variant_map>(var_srf_);
    const Surface*     surface
        = factory.surface(var_srf.get<std::string>("type"), var_srf);
    surfaces.push_back(surface);
  }

  m_surfaces = surfaces;

  // reproduce axes
  const variant_vector& var_axes
      = var_sgl.get<variant_map>("payload").get<variant_vector>("axes");

  throw_assert(var_axes.size() == 2,
               "This constructor cannot deserialize DIM!=2 data");

  // two dimensional
  const variant_map& var_axis_a
      = var_axes.get<variant_map>(0).get<variant_map>("payload");
  const variant_map& var_axis_b
      = var_axes.get<variant_map>(1).get<variant_map>("payload");

  std::string axistype_a = var_axis_a.get<std::string>("axistype");
  std::string axisbdt_a  = var_axis_a.get<std::string>("axisboundarytype");
  std::string axistype_b = var_axis_b.get<std::string>("axistype");
  std::string axisbdt_b  = var_axis_b.get<std::string>("axisboundarytype");

  auto makePAxis = [](const std::string& axistype,
                      const variant_map& var_axis) -> ProtoAxis {
    ProtoAxis pAxis;
    if (axistype == "equidistant") {
      pAxis.bType = equidistant;
      pAxis.min   = var_axis.get<double>("min");
      pAxis.max   = var_axis.get<double>("max");
      pAxis.nBins = var_axis.get<int>("nbins");
    } else {
      std::vector<double>   bin_edges;
      const variant_vector& var_bin_edges
          = var_axis.get<variant_vector>("bin_edges");
      for (size_t i = 0; i < var_bin_edges.size(); i++) {
        bin_edges.push_back(var_bin_edges.get<double>(i));
      }

      pAxis.bType    = arbitrary;
      pAxis.binEdges = bin_edges;
    }

    return pAxis;
  };

  ProtoAxis pAxisA = makePAxis(axistype_a, var_axis_a);
  ProtoAxis pAxisB = makePAxis(axistype_b, var_axis_b);

  if (axisbdt_a == "closed" && axisbdt_b == "closed") {
    m_gridLookup = SurfaceArray::
        makeSurfaceGridLookup2D<detail::AxisBoundaryType::Closed,
                                detail::AxisBoundaryType::Closed>(
            g2l, l2g, pAxisA, pAxisB);
  } else if (axisbdt_a == "closed" && axisbdt_b == "bound") {
    m_gridLookup = SurfaceArray::
        makeSurfaceGridLookup2D<detail::AxisBoundaryType::Closed,
                                detail::AxisBoundaryType::Bound>(
            g2l, l2g, pAxisA, pAxisB);
  } else if (axisbdt_a == "closed" && axisbdt_b == "open") {
    m_gridLookup = SurfaceArray::
        makeSurfaceGridLookup2D<detail::AxisBoundaryType::Closed,
                                detail::AxisBoundaryType::Open>(
            g2l, l2g, pAxisA, pAxisB);
  } else if (axisbdt_a == "open" && axisbdt_b == "closed") {
    m_gridLookup = SurfaceArray::
        makeSurfaceGridLookup2D<detail::AxisBoundaryType::Open,
                                detail::AxisBoundaryType::Closed>(
            g2l, l2g, pAxisA, pAxisB);
  } else if (axisbdt_a == "open" && axisbdt_b == "bound") {
    m_gridLookup = SurfaceArray::
        makeSurfaceGridLookup2D<detail::AxisBoundaryType::Open,
                                detail::AxisBoundaryType::Bound>(
            g2l, l2g, pAxisA, pAxisB);
  } else if (axisbdt_a == "open" && axisbdt_b == "open") {
    m_gridLookup
        = SurfaceArray::makeSurfaceGridLookup2D<detail::AxisBoundaryType::Open,
                                                detail::AxisBoundaryType::Open>(
            g2l, l2g, pAxisA, pAxisB);
  } else if (axisbdt_a == "bound" && axisbdt_b == "closed") {
    m_gridLookup = SurfaceArray::
        makeSurfaceGridLookup2D<detail::AxisBoundaryType::Bound,
                                detail::AxisBoundaryType::Closed>(
            g2l, l2g, pAxisA, pAxisB);
  } else if (axisbdt_a == "bound" && axisbdt_b == "bound") {
    m_gridLookup = SurfaceArray::
        makeSurfaceGridLookup2D<detail::AxisBoundaryType::Bound,
                                detail::AxisBoundaryType::Bound>(
            g2l, l2g, pAxisA, pAxisB);
  } else if (axisbdt_a == "bound" && axisbdt_b == "open") {
    m_gridLookup
        = SurfaceArray::makeSurfaceGridLookup2D<detail::AxisBoundaryType::Bound,
                                                detail::AxisBoundaryType::Open>(
            g2l, l2g, pAxisA, pAxisB);
  }

  m_gridLookup->fill(surfaces);
}

Acts::SurfaceArray::SurfaceArray(
    const variant_data& data_,
    std::function<std::array<double, 1>(const Vector3D&)> g2l,
    std::function<Vector3D(const std::array<double, 1>&)> l2g)
  : m_gridLookup(nullptr)
{
  const variant_map& data = boost::get<variant_map>(data_);
  throw_assert(data.get<std::string>("type") == "SurfaceArray",
               "Type must be SurfaceArray");

  const variant_map& payload = data.get<variant_map>("payload");
  const variant_map& var_sgl = payload.get<variant_map>("surfacegridlookup");
  throw_assert(var_sgl.get<std::string>("type") == "SurfaceGridLookup",
               "Currently only SurfaceGridLookup can be deserialized");
  const variant_vector& var_surfaces = payload.get<variant_vector>("surfaces");

  InstanceFactory factory;

  std::vector<const Surface*> surfaces;
  for (const auto& var_srf_ : var_surfaces) {
    const variant_map& var_srf = boost::get<variant_map>(var_srf_);
    const Surface*     surface
        = factory.surface(var_srf.get<std::string>("type"), var_srf);
    surfaces.push_back(surface);
  }

  m_surfaces = surfaces;

  // reproduce axes
  const variant_vector& var_axes
      = var_sgl.get<variant_map>("payload").get<variant_vector>("axes");

  throw_assert(var_axes.size() == 1,
               "This constructor cannot deserialize DIM!=2 data");

  const variant_map& var_axis
      = var_axes.get<variant_map>(0).get<variant_map>("payload");

  std::string axistype = var_axis.get<std::string>("axistype");
  std::string axisbdt  = var_axis.get<std::string>("axisboundarytype");

  if (axistype == "equidistant" && axisbdt == "bound") {
    detail::Axis<detail::AxisType::Equidistant, detail::AxisBoundaryType::Bound>
    axis(var_axis.get<double>("min"),
         var_axis.get<double>("max"),
         var_axis.get<int>("nbins"));

    m_gridLookup
        = std::make_unique<SurfaceGridLookup<decltype(axis)>>(g2l, l2g, std::make_tuple(axis));
  } else if (axistype == "equidistant" && axisbdt == "closed") {
    detail::Axis<detail::AxisType::Equidistant,
                 detail::AxisBoundaryType::Closed>
    axis(var_axis.get<double>("min"),
         var_axis.get<double>("max"),
         var_axis.get<int>("nbins"));

    m_gridLookup
        = std::make_unique<SurfaceGridLookup<decltype(axis)>>(g2l, l2g, std::make_tuple(axis));
  } else if (axistype == "equidistant" && axisbdt == "open") {
    detail::Axis<detail::AxisType::Equidistant, detail::AxisBoundaryType::Open>
    axis(var_axis.get<double>("min"),
         var_axis.get<double>("max"),
         var_axis.get<int>("nbins"));

    m_gridLookup
        = std::make_unique<SurfaceGridLookup<decltype(axis)>>(g2l, l2g, std::make_tuple(axis));
  } else if (axistype == "variable") {

    std::vector<double>   bin_edges;
    const variant_vector& var_bin_edges
        = var_axis.get<variant_vector>("bin_edges");
    for (size_t i = 0; i < var_bin_edges.size(); i++) {
      bin_edges.push_back(var_bin_edges.get<double>(i));
    }

    if (axisbdt == "bound") {
      detail::Axis<detail::AxisType::Variable, detail::AxisBoundaryType::Bound>
          axis(bin_edges);
      m_gridLookup
          = std::make_unique<SurfaceGridLookup<decltype(axis)>>(g2l, l2g, std::make_tuple(axis));
    } else if (axisbdt == "closed") {
      detail::Axis<detail::AxisType::Variable, detail::AxisBoundaryType::Closed>
          axis(bin_edges);
      m_gridLookup
          = std::make_unique<SurfaceGridLookup<decltype(axis)>>(g2l, l2g, std::make_tuple(axis));
    } else if (axisbdt == "open") {
      detail::Axis<detail::AxisType::Variable, detail::AxisBoundaryType::Open>
          axis(bin_edges);
      m_gridLookup
          = std::make_unique<SurfaceGridLookup<decltype(axis)>>(g2l, l2g, std::make_tuple(axis));
    }
  }

  m_gridLookup->fill(surfaces);
}
