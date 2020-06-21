// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/viz/service/display/overlay_strategy_underlay.h"

#include "components/viz/common/quads/draw_quad.h"
#include "components/viz/common/quads/solid_color_draw_quad.h"
#include "components/viz/service/display/display_resource_provider.h"

namespace viz {

OverlayStrategyUnderlay::OverlayStrategyUnderlay(
    OverlayProcessorUsingStrategy* capability_checker,
    OpaqueMode opaque_mode)
    : capability_checker_(capability_checker), opaque_mode_(opaque_mode) {
  DCHECK(capability_checker);
}

OverlayStrategyUnderlay::~OverlayStrategyUnderlay() {}

bool OverlayStrategyUnderlay::Attempt(
    const SkMatrix44& output_color_matrix,
    const OverlayProcessorInterface::FilterOperationsMap&
        render_pass_backdrop_filters,
    DisplayResourceProvider* resource_provider,
    RenderPassList* render_pass_list,
    const PrimaryPlane* primary_plane,
    OverlayCandidateList* candidate_list,
    std::vector<gfx::Rect>* content_bounds) {
  // Before we attempt an overlay strategy, the candidate list should be empty.
  DCHECK(candidate_list->empty());
  RenderPass* render_pass = render_pass_list->back().get();
  QuadList& quad_list = render_pass->quad_list;

  for (auto it = quad_list.begin(); it != quad_list.end(); ++it) {
    OverlayCandidate candidate;
    if (!OverlayCandidate::FromDrawQuad(resource_provider, output_color_matrix,
                                        *it, &candidate) ||
        (opaque_mode_ == OpaqueMode::RequireOpaqueCandidates &&
         !candidate.is_opaque)) {
      continue;
    }

    // Filters read back the framebuffer to get the pixel values that need to
    // be filtered.  This is a problem when there are hardware planes because
    // the planes are not composited until they are on the display controller.
    if (OverlayCandidate::IsOccludedByFilteredQuad(
            candidate, quad_list.begin(), it, render_pass_backdrop_filters)) {
      continue;
    }

    // Add the overlay.
    OverlayCandidateList new_candidate_list = *candidate_list;
    new_candidate_list.push_back(candidate);
    new_candidate_list.back().plane_z_order = -1;

    if (primary_plane) {
      // Since there is a list of strategies to go through, each strategy should
      // not change the input parameters. In this case, we need to keep the
      // |primary_plane| unchanged. The underlay strategy only works when the
      // |primary_plane| supports blending. In order to check the hardware
      // support, make a copy of the |primary_plane| with blending enabled.
      PrimaryPlane new_plane_candidate(*primary_plane);
      new_plane_candidate.enable_blending = true;
      // Check for support.
      capability_checker_->CheckOverlaySupport(&new_plane_candidate,
                                               &new_candidate_list);
    } else {
      capability_checker_->CheckOverlaySupport(nullptr, &new_candidate_list);
    }

    // If the candidate can be handled by an overlay, create a pass for it. We
    // need to switch out the video quad with a black transparent one.
    if (new_candidate_list.back().overlay_handled) {
      new_candidate_list.back().is_unoccluded =
          !OverlayCandidate::IsOccluded(candidate, quad_list.cbegin(), it);
      quad_list.ReplaceExistingQuadWithOpaqueTransparentSolidColor(it);
      candidate_list->swap(new_candidate_list);

      return true;
    }
  }

  return false;
}

// Turn on blending for the output surface plane so the underlay could show
// through.
void OverlayStrategyUnderlay::AdjustOutputSurfaceOverlay(
    OverlayProcessorInterface::OutputSurfaceOverlayPlane*
        output_surface_plane) {
  if (output_surface_plane)
    output_surface_plane->enable_blending = true;
}

OverlayStrategy OverlayStrategyUnderlay::GetUMAEnum() const {
  return OverlayStrategy::kUnderlay;
}

}  // namespace viz
