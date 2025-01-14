// This file is part of OpenMVG, an Open Multiple View Geometry C++ library.
// Copyright (c) 2012, 2013, 2015 Pierre MOULON.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#include "openMVG/matching/indMatch.hpp"
#include "openMVG/matching/indMatch_utils.hpp"
#include "openMVG/matching/svg_matches.hpp"
#include "openMVG/image/image_io.hpp"
#include "openMVG/sfm/pipelines/sfm_features_provider.hpp"
#include "openMVG/sfm/pipelines/sfm_matches_provider.hpp"
#include "openMVG/sfm/sfm_data.hpp"
#include "openMVG/sfm/sfm_data_io.hpp"
#include "openMVG/system/loggerprogress.hpp"
#include "third_party/cmdLine/cmdLine.h"
#include "third_party/stlplus3/filesystemSimplified/file_system.hpp"
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>
#include <map>
using namespace openMVG;
using namespace openMVG::features;
using namespace openMVG::matching;
using namespace openMVG::sfm;
int main(int argc, char ** argv)
{
  CmdLine cmd;
  std::string sSfM_Data_Filename;
  std::string sMatchesDir;
  std::string sMatchFile;
  std::string sOutDir = "";
  cmd.add( make_option('i', sSfM_Data_Filename, "input_file") );
  cmd.add( make_option('d', sMatchesDir, "matchdir") );
  cmd.add( make_option('m', sMatchFile, "matchfile") );
  cmd.add( make_option('o', sOutDir, "outdir") );
  try {
    if (argc == 1) throw std::string("Invalid command line parameter.");
    cmd.process(argc, argv);
  } catch (const std::string& s) {
    OPENMVG_LOG_INFO << "Export pairwise matches.\nUsage: " << argv[0] << "\n"
      << "[-i|--input_file file] path to a SfM_Data scene\n"
      << "[-d|--matchdir path]\n"
      << "[-m|--sMatchFile filename]\n"
      << "[-o|--outdir path]";
    OPENMVG_LOG_ERROR << s;
    return EXIT_FAILURE;
  }
  if (sOutDir.empty())  {
    OPENMVG_LOG_ERROR << "It is an invalid output directory";
    return EXIT_FAILURE;
  }
  if (sMatchesDir.empty()) {
    OPENMVG_LOG_ERROR << "matchdir cannot be an empty option";
    return EXIT_FAILURE;
  }
  if (sMatchFile.empty()) {
    OPENMVG_LOG_ERROR << "matchfile cannot be an empty option";
    return EXIT_FAILURE;
  }
  //---------------------------------------
  // Read SfM Scene (image view names)
  //---------------------------------------
  SfM_Data sfm_data;
  if (!Load(sfm_data, sSfM_Data_Filename, ESfM_Data(VIEWS|INTRINSICS))) {
    OPENMVG_LOG_ERROR << "The input SfM_Data file \""<< sSfM_Data_Filename << "\" cannot be read.";
    return EXIT_FAILURE;
  }
  //---------------------------------------
  // Load SfM Scene regions
  //---------------------------------------
  // Init the regions_type from the image describer file (used for image regions extraction)
  const std::string sImage_describer = stlplus::create_filespec(sMatchesDir, "image_describer", "json");
  std::unique_ptr<Regions> regions_type = Init_region_type_from_file(sImage_describer);
  if (!regions_type)
  {
    OPENMVG_LOG_ERROR << "Invalid: " << sImage_describer << " regions type file.";
    return EXIT_FAILURE;
  }
  // Read the features
  std::shared_ptr<Features_Provider> feats_provider = std::make_shared<Features_Provider>();
  if (!feats_provider->load(sfm_data, sMatchesDir, regions_type)) {
    OPENMVG_LOG_ERROR << "Cannot load view corresponding features in directory: " << sMatchesDir << ".";
    return EXIT_FAILURE;
  }
  std::shared_ptr<Matches_Provider> matches_provider = std::make_shared<Matches_Provider>();
  if (!matches_provider->load(sfm_data, sMatchFile)) {
    OPENMVG_LOG_ERROR << "Cannot load the match file: " << sMatchFile << ".";
    return EXIT_FAILURE;
  }
  // ------------
  // For each pair, export the matches
  // ------------
  std::string input;
  int num_matches = 0;
  stlplus::folder_create(sOutDir);
  // OPENMVG_LOG_INFO << "Export pairwise matches";
  const Pair_Set pairs = matches_provider->getPairs();
  system::LoggerProgress my_progress_bar( pairs.size() );
  for (Pair_Set::const_iterator iter = pairs.begin();
    iter != pairs.end();
    ++iter, ++my_progress_bar)
  {
    const size_t I = iter->first;
    const size_t J = iter->second;
    const View * view_I = sfm_data.GetViews().at(I).get();
    const std::string sView_I= stlplus::create_filespec(sfm_data.s_root_path,
      view_I->s_Img_path);
    const View * view_J = sfm_data.GetViews().at(J).get();
    const std::string sView_J= stlplus::create_filespec(sfm_data.s_root_path,
      view_J->s_Img_path);
    // Get corresponding matches
    const std::vector<IndMatch> & vec_FilteredMatches =
      matches_provider->pairWise_matches_.at(*iter);
    if (!vec_FilteredMatches.empty()) {
      const auto& left_features = feats_provider->getFeatures(view_I->id_view);
      const auto& right_features = feats_provider->getFeatures(view_J->id_view);
      
      for (const auto match_it : vec_FilteredMatches) {
          // Get back linked features
          const features::PointFeature & L = left_features[match_it.i_];
          const features::PointFeature & R = right_features[match_it.j_];
          auto output_str = view_I->s_Img_path + ","  + std::to_string(L.x()) + "," + std::to_string(L.y()) + "," + view_J->s_Img_path + "," + std::to_string(R.x()) + "," + std::to_string(R.y());

          input += output_str +  "\n";
          num_matches++;
          std::cout << "m:" << output_str << std::endl;
      }
      
      
      // // Draw corresponding features
      // const bool bVertical = false;
      // std::ostringstream os;
      // os << stlplus::folder_append_separator(sOutDir)
      //   << iter->first << "_" << iter->second
      //   << "_" << vec_FilteredMatches.size() << "_.svg";
      // Matches2SVG
      // (
      //   sView_I,
      //   {view_I->ui_width, view_I->ui_height},
      //   feats_provider->getFeatures(view_I->id_view),
      //   sView_J,
      //   {view_J->ui_width, view_J->ui_height},
      //   feats_provider->getFeatures(view_J->id_view),
      //   vec_FilteredMatches,
      //   os.str(),
      //   bVertical
      // );
    }
  }
  std::ofstream out(sOutDir + "/matches.txt");
  out << input;
  out.close();
  // std::cout << num_matches << " matches found" << std::endl;
  return EXIT_SUCCESS;
}