// This file is part of OpenMVG, an Open Multiple View Geometry C++ library.

// Copyright (c) 2012, 2013 openMVG authors.

// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

#include "openMVG/features/sift/SIFT_Anatomy_Image_Describer.hpp"
#include "openMVG/features/sift/SIFT_Anatomy_Image_Describer_io.hpp"
#include "openMVG/features/svg_features.hpp"
#include "openMVG/image/image_concat.hpp"
#include "openMVG/image/image_io.hpp"
#include "openMVG/matching/kvld/kvld.h"
#include "openMVG/matching/kvld/kvld_draw.h"
#include "openMVG/matching/regions_matcher.hpp"
#include "openMVG/matching/svg_matches.hpp"
#include "third_party/cmdLine/cmdLine.h"
#include "third_party/stlplus3/filesystemSimplified/file_system.hpp"
#include "third_party/vectorGraphics/svgDrawer.hpp"

using namespace openMVG;
using namespace openMVG::image;
using namespace openMVG::matching;
using namespace openMVG::features;
using namespace std;

#include "openMVG/features/regions_factory_io.hpp"

int main(int argc, char** argv) {
  CmdLine cmd;

  std::string sImg1;
  std::string sImg2;
  std::string sOutDir = ".";
  std::string debug_ = "0";
  std::string feature_ = "default";
  // std::cout << sImg1 << std::endl << sImg2 << std::endl;
  cmd.add(make_option('i', sImg1, "img1"));
  cmd.add(make_option('j', sImg2, "img2"));
  cmd.add(make_option('o', sOutDir, "outdir"));
  cmd.add(make_option('d', debug_, "debug"));
  cmd.add(make_option('f', feature_, "feature"));

  if (argc > 1) {
    try {
      if (argc == 1) throw std::string("Invalid command line parameter.");
      cmd.process(argc, argv);
    } catch (const std::string& s) {
      std::cerr << "Usage: " << argv[0] << ' ' << "[-i|--img1 file] "
                << "[-j|--img2 file] "
                << "[-o|--outdir path] " << std::endl;

      std::cerr << s << std::endl;
      return EXIT_FAILURE;
    }
  }

  int debug = stoi(debug_);

  // -----------------------------
  // a. List images
  // b. Compute features and descriptor
  // c. Compute putatives descriptor matches
  // d. Geometric filtering of putatives matches
  // e. Export some statistics
  // -----------------------------

  const string jpg_filenameL = sImg1;
  const string jpg_filenameR = sImg2;

  Image<unsigned char> imageL, imageR;
  ReadImage(jpg_filenameL.c_str(), &imageL);
  ReadImage(jpg_filenameR.c_str(), &imageR);

  auto start = std::chrono::system_clock::now();

  //--
  // Detect regions thanks to an image_describer
  //--
  using namespace openMVG::features;

  std::unique_ptr<Image_describer> image_describer;
  image_describer = std::unique_ptr<Image_describer>(new SIFT_Anatomy_Image_describer(SIFT_Anatomy_Image_describer::Params(-1)));
  std::map<IndexT, std::unique_ptr<features::Regions>> regions_perImage;
  image_describer->Describe(imageL, regions_perImage[0]);
  image_describer->Describe(imageR, regions_perImage[1]);

  const SIFT_Regions* regionsL = dynamic_cast<SIFT_Regions*>(regions_perImage.at(0).get());
  const SIFT_Regions* regionsR = dynamic_cast<SIFT_Regions*>(regions_perImage.at(1).get());

  const PointFeatures featsL = regions_perImage.at(0)->GetRegionsPositions(), featsR = regions_perImage.at(1)->GetRegionsPositions();

  // cout << featsL.size() << " " << featsR.size() << endl;

  std::vector<IndMatch> vec_PutativeMatches;
  //-- Perform matching -> find Nearest neighbor, filtered with Distance ratio
  {
    // Find corresponding points
    matching::DistanceRatioMatch(0.8, matching::BRUTE_FORCE_L2, *regions_perImage.at(0).get(), *regions_perImage.at(1).get(), vec_PutativeMatches);
  }

  // K-VLD filter
  Image<float> imgA(imageL.GetMat().cast<float>());
  Image<float> imgB(imageR.GetMat().cast<float>());

  std::vector<Pair> matchesFiltered;
  std::vector<Pair> matchesPair;

  for (const auto& match_it : vec_PutativeMatches) {
    matchesPair.emplace_back(match_it.i_, match_it.j_);
  }
  std::vector<double> vec_score;

  // In order to illustrate the gvld(or vld)-consistant neighbors,
  // the following two parameters has been externalized as inputs of the function KVLD.
  openMVG::Mat E = openMVG::Mat::Ones(vec_PutativeMatches.size(), vec_PutativeMatches.size()) * (-1);
  // gvld-consistancy matrix, intitialized to -1,  >0 consistancy value, -1=unknow, -2=false
  std::vector<bool> valid(vec_PutativeMatches.size(), true);  // indices of match in the initial matches, if true at the end of KVLD, a match is kept.

  size_t it_num = 0;
  KvldParameters kvldparameters;  // initial parameters of KVLD
  while (it_num < 5 && kvldparameters.inlierRate > KVLD(imgA, imgB, regionsL->Features(), regionsR->Features(), matchesPair, matchesFiltered, vec_score, E, valid, kvldparameters)) {
    kvldparameters.inlierRate /= 2;
    // std::cout<<"low inlier rate, re-select matches with new rate="<<kvldparameters.inlierRate<<std::endl;
    kvldparameters.K = 2;
    it_num++;
  }

  std::vector<IndMatch> vec_FilteredMatches;
  for (std::vector<Pair>::const_iterator i_matchFilter = matchesFiltered.begin(); i_matchFilter != matchesFiltered.end(); ++i_matchFilter) {
    vec_FilteredMatches.push_back(IndMatch(i_matchFilter->first, i_matchFilter->second));
  }

  // Export
  if (!vec_FilteredMatches.empty()) {
    // std::string input;
    for (const auto match_it : vec_FilteredMatches) {
      // Get back linked features
      const features::PointFeature& L = featsL[match_it.i_];
      const features::PointFeature& R = featsR[match_it.j_];
      auto output_str = std::to_string(L.x()) + "," + std::to_string(L.y()) + "," + std::to_string(R.x()) + "," + std::to_string(R.y());
      cout << "m:" << output_str << endl;
    }
  }

  return EXIT_SUCCESS;
}
