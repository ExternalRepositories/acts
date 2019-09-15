// This file is part of the Acts project.
//
// Copyright (C) 2019 CERN for the benefit of the Acts project
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "Acts/EventData/TrackParameters.hpp"
#include "Acts/Utilities/Logger.hpp"
#include "Acts/Utilities/Result.hpp"
#include "Acts/Vertexing/ImpactPoint3dEstimator.hpp"
#include "Acts/Vertexing/KalmanVertexUpdater.hpp"
#include "Acts/Vertexing/LinearizerConcept.hpp"
#include "Acts/Vertexing/MAVFInfo.hpp"
#include "Acts/Vertexing/SequentialVertexSmoother.hpp"
#include "Acts/Vertexing/VertexAnnealingTool.hpp"
#include "Acts/Vertexing/VertexFitterOptions.hpp"

#include <functional>

namespace Acts {

/// @class MultiAdaptiveVertexFitter
/// @brief Implements an adaptive multi-vertex fitter as described
///   in detail in Section 5.3.5 in:
///   Ref. (1): CERN-THESIS-2010-027, Author: Piacquadio, Giacinto:
///   `Identification of b-jets and investigation of the discovery potential
///   of a Higgs boson in the WH−−>lvbb¯ channel with the ATLAS experiment`
///
///////////////////////////////////////////////////////////////////////////
///
/// @tparam input_track_t Track object type
/// @tparam linearizer_t Track linearizer type
template <typename input_track_t, typename linearizer_t>
class MultiAdaptiveVertexFitter {
  static_assert(LinearizerConcept<linearizer_t>,
                "Linearizer does not fulfill linearizer concept.");

  using InputTrack_t = input_track_t;
  using Propagator_t = typename linearizer_t::Propagator_t;
  using BField_t = typename linearizer_t::BField_t;
  using IPEstimator =
      ImpactPoint3dEstimator<BField_t, InputTrack_t, Propagator_t>;

 public:
  /// @brief The fitter state
  struct State {
    // Vertex collection to be fitted
    std::vector<Vertex<InputTrack_t>*> vertexCollection;

    // Annealing state
    VertexAnnealingTool::State annealingState;

    // Map to store vertices information
    std::map<Vertex<InputTrack_t>*, MAVFVertexInfo<InputTrack_t>> vtxInfoMap;

    // Map to store tracks information
    std::map<unsigned long, MAVFTrackAtVtxInfo<InputTrack_t>> trkInfoMap;
  };

  struct Config {
    /// @brief Config constructor
    ///
    /// @param est ImpactPoint3dEstimator
    Config(IPEstimator est) : ipEst(std::move(est)) {}

    // ImpactPoint3dEstimator
    IPEstimator ipEst;

    // Vertex updater
    KalmanVertexUpdater<InputTrack_t> vertexUpdater;

    // SequentialVertexSmoother
    SequentialVertexSmoother<InputTrack_t> vertexSmoother;

    // Annealing tool
    VertexAnnealingTool annealingTool;

    // Number of max iterations
    unsigned int maxIterations{50};

    // Max distance to linearization point allowed
    // without relinearization
    double maxDistToLinPoint{0.5};

    // Minimum track weight needed for track to be considered
    double minWeight{0.001};

    // Max relative shift of vertex during one iteration
    double maxRelativeShift{0.01};

    // Do smoothing after multivertex fit
    bool doSmoothing{false};
  };

  /// @brief Constructor used if InputTrack_t type == BoundParameters
  ///
  /// @param cfg Configuration object
  /// @param logger The logging instance
  template <typename T = InputTrack_t,
            std::enable_if_t<std::is_same<T, BoundParameters>::value, int> = 0>
  MultiAdaptiveVertexFitter(Config& cfg,
                            std::unique_ptr<const Logger> logger =
                                getDefaultLogger("MultiAdaptiveVertexFitter",
                                                 Logging::INFO))
      : m_cfg(std::move(cfg)),
        m_extractParameters([](T params) { return params; }),
        m_logger(std::move(logger)) {}

  /// @brief Constructor for user-defined InputTrack_t type =! BoundParameters
  ///
  /// @param cfg Configuration object
  /// @param func Function extracting BoundParameters from InputTrack_t object
  /// @param logger The logging instance
  MultiAdaptiveVertexFitter(Config& cfg,
                            std::function<BoundParameters(InputTrack_t)> func,
                            std::unique_ptr<const Logger> logger =
                                getDefaultLogger("MultiAdaptiveVertexFitter",
                                                 Logging::INFO))
      : m_cfg(std::move(cfg)),
        m_extractParameters(func),
        m_logger(std::move(logger)) {}

  /// @brief Adds new vertex to an existing multi-vertex fit
  /// and fits everything together (by invoking the fit_impl method):
  /// 1. The new vertex is added to the fit: all associated tracks get
  /// initialized, i.e. ParamsAtIP3d are created (from ImpactPoint3dEstimator)
  /// to be later able to estimate in a fast way the compatibility of the tracks
  /// to their respective vertices.
  /// 2. All tracks belonging to the new vertex are scanned and all the vertices
  ///  which share tracks with the new vertex to be fit are also added to the
  ///  fit.
  /// 3. The multivertex fit is performed with all involved vertices.
  ///
  /// This has the advantage that only vertices that are affected by adding the
  /// new vertex are refitted.
  ///
  /// Note: newVertex has to be properly initialized (seed vertex,
  /// constraint vertex, list of MAV)
  ///
  /// @param state The state object
  /// @param newVertex New vertex to be added to fit
  /// @param linearizer The track linearizer
  /// @param vFitterOptions Vertex fitter options
  ///
  /// @return Result<void> object
  Result<void> fit(
      State& state, Vertex<InputTrack_t>& newVertex,
      const linearizer_t& linearizer,
      const VertexFitterOptions<InputTrack_t>& vFitterOptions) const;

 private:
  /// Configuration object
  const Config m_cfg;

  /// @brief Function to extract track parameters,
  /// InputTrack_t objects are BoundParameters by default, function to be
  /// overwritten to return BoundParameters for other InputTrack_t objects.
  ///
  /// @param InputTrack_t object to extract track parameters from
  const std::function<BoundParameters(InputTrack_t)> m_extractParameters;

  /// Logging instance
  std::unique_ptr<const Logger> m_logger;

  /// Private access to logging instance
  const Logger& logger() const { return *m_logger; }

  /// @brief The actual fit function, performs a simulaneous
  ///   fit of all vertices in state.vertexCollection
  ///
  /// @param state The state object
  /// @param linearizer The track linearizer
  /// @param vFitterOptions Vertex fitter options
  ///
  /// @return Result<void> object
  Result<void> fit_impl(
      State& state, const linearizer_t& linearizer,
      const VertexFitterOptions<InputTrack_t>& vFitterOptions) const;

  /// @brief Tests if vertex is already in list of vertices or not
  ///
  /// @param vtx Vertex to test
  /// @param verticesVec Vector of vertices to search
  ///
  /// @return True if vtx is already in verticesVec
  bool isAlreadyInList(
      Vertex<InputTrack_t>* vtx,
      const std::vector<Vertex<InputTrack_t>*>& verticesVec) const;

  /// @brief Prepares vertex object for the actual fit, i.e.
  /// all TrackAtVertex objects at current vertex will obtain
  /// `ip3dParams` from ImpactPoint3dEstimator::getParamsAtIP3d
  /// in order to later faster estimate compatibilities of track
  /// with different vertices
  ///
  /// @param vtx The vertex object
  /// @param vFitterOptions Vertex fitter options
  Result<void> prepareVtxForFit(
      State& state, Vertex<InputTrack_t>* vtx,
      const VertexFitterOptions<InputTrack_t>& vFitterOptions) const;

  /// @brief Sets vertexCompatibility for all TrackAtVertex objects
  /// at current vertex
  ///
  /// @param state The state object
  /// @param geoContext The geometry context
  /// @param mfContext The magnetic field context
  /// @param currentVtx Current vertex
  Result<void> setAllVtxCompatibilities(State& state,
                                        const GeometryContext& geoContext,
                                        const MagneticFieldContext& mfContext,
                                        Vertex<InputTrack_t>* currentVtx) const;

  /// @brief Sets weights to the track according to Eq.(5.46) in Ref.(1)
  ///  and updates the vertices by calling the VertexUpdater
  ///
  /// @param state The state object
  /// @param linearizer The track linearizer
  Result<void> setWeightsAndUpdate(State& state,
                                   const linearizer_t& linearizer) const;

  /// @brief Collects all compatibility values of the track `trk`
  /// at all vertices it is currently attached to and outputs
  /// these values in a vector
  ///
  /// @param state The state object
  /// @param trk The track
  ///
  /// @return Vector of compatibility values
  Result<std::vector<double>> collectTrkToVtxCompatibilities(
      State& state, const TrackAtVertex<InputTrack_t>& trk) const;

  /// @brief Determines if vertex position has shifted more than
  /// m_cfg.maxRelativeShift in last iteration
  ///
  /// @param state The state object
  ///
  /// @return False if shift was larger than maxRelativeShift
  bool checkSmallShift(State& state) const;
};

}  // namespace Acts

#include "Acts/Vertexing/MultiAdaptiveVertexFitter.ipp"
