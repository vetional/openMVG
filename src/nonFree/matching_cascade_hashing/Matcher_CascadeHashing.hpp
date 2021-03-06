
// Copyright (c) 2014 Pierre MOULON.

// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.


#pragma once

#include "openMVG/features/features.hpp"
#include "openMVG/matching/indMatchDecoratorXY.hpp"
#include "openMVG/matching/matching_filters.hpp"
#include "openMVG/matching_image_collection/Matcher.hpp"

#include "nonFree/sift/SIFT_describer.hpp"
#include "nonFree/matching_cascade_hashing/CasHash.hpp"

#include "third_party/stlplus3/filesystemSimplified/file_system.hpp"
#include "third_party/progress/progress.hpp"

// [1] Fast and Accurate Image Matching with Cascade Hashing for 3D Reconstruction.
// Authors: Jian Cheng, Cong Leng, Jiaxiang Wu, Hainan Cui, Hanqing Lu.
// Conference: CVPR 2014.

namespace openMVG {

using namespace openMVG::matching;

/// Implementation of an Image Collection Matcher
/// - Cascade Hashing matching [1]
///
class Matcher_CascadeHashing_AllInMemory
{
  public:
  Matcher_CascadeHashing_AllInMemory(float distRatio):
    fDistRatio(distRatio)
  {
  }

  /// Load all features and descriptors in memory
  bool loadData(
    const features::Image_describer & image_describer, // interface to load computed regions
    const std::vector<std::string> & vec_fileNames, // input filenames
    const std::string & sMatchDir) // where the data are saved
  {
    bool bOk = true;
    for (size_t j = 0; j < vec_fileNames.size(); ++j)  {
      // Load regions of Jnth image
      const std::string sFeatJ = stlplus::create_filespec(sMatchDir,
        stlplus::basename_part(vec_fileNames[j]), "feat");
      const std::string sDescJ = stlplus::create_filespec(sMatchDir,
        stlplus::basename_part(vec_fileNames[j]), "desc");

      image_describer.Allocate(regions_perImage[j]);
      bOk &= image_describer.Load(regions_perImage[j].get(), sFeatJ, sDescJ);
    }
    if (bOk)
    {
      nonFree::CASHASH::ImportFeatures(regions_perImage, vec_hashing);
    }
    return bOk;
  }

  void Match(
    const std::vector<std::string> & vec_fileNames, // input filenames,
    const Pair_Set & pairs,
    PairWiseMatches & map_PutativesMatches) // the pairwise photometric corresponding points
  {
    C_Progress_display my_progress_bar( pairs.size() );

    // Sort pairs according the first index to minimize memory exchange
    typedef std::map<size_t, std::vector<size_t> > Map_vectorT;
    Map_vectorT map_Pairs;
    for (Pair_Set::const_iterator iter = pairs.begin(); iter != pairs.end(); ++iter)
    {
      map_Pairs[iter->first].push_back(iter->second);
    }

    // Perform matching between all the pairs
    for (Map_vectorT::const_iterator iter = map_Pairs.begin();
      iter != map_Pairs.end(); ++iter)
    {
      const size_t I = iter->first;
      const features::Regions *regionsI = regions_perImage.at(I).get();
      const std::vector<PointFeature> pointFeaturesI = regionsI->GetRegionsPositions();

      const std::vector<size_t> & indexToCompare = iter->second;
#ifdef OPENMVG_USE_OPENMP
  #pragma omp parallel for schedule(dynamic)
#endif
      for (int j = 0; j < (int)indexToCompare.size(); ++j)
      {
        const size_t J = indexToCompare[j];
        const features::Regions *regionsJ = regions_perImage.at(J).get();

        std::vector<IndMatch> vec_FilteredMatches;
        cascadeHashing.MatchSpFast(
          vec_FilteredMatches,
          vec_hashing[I], ((features::SIFT_Regions*)regionsI)->Descriptors(),
          vec_hashing[J], ((features::SIFT_Regions*)regionsJ)->Descriptors(),
          fDistRatio);

        // Remove duplicates
        IndMatch::getDeduplicated(vec_FilteredMatches);

        // Remove matches that have the same (X,Y) coordinates
        const std::vector<PointFeature> pointFeaturesJ = regionsJ->GetRegionsPositions();
        IndMatchDecorator<float> matchDeduplicator(vec_FilteredMatches, pointFeaturesI, pointFeaturesJ);
        matchDeduplicator.getDeduplicated(vec_FilteredMatches);

#ifdef OPENMVG_USE_OPENMP
  #pragma omp critical
#endif
        {
          if (!vec_FilteredMatches.empty())
            map_PutativesMatches.insert( make_pair( make_pair(I,J), vec_FilteredMatches ));
          ++my_progress_bar;
        }
      }
    }
  }

  private:
  // Sift features & descriptors per View images
  std::map<IndexT, std::unique_ptr<features::Regions> > regions_perImage;

  // Distance ratio used to discard spurious correspondence
  float fDistRatio;

  // CascadeHashing (data)
  nonFree::CASHASH::CasHashMatcher cascadeHashing;
  std::vector<nonFree::CASHASH::ImageFeatures> vec_hashing;
};

}; // namespace openMVG
