#include "panoptic_mapping/tracking/single_tsdf_tracker.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace panoptic_mapping {

config_utilities::Factory::RegistrationRos<IDTrackerBase, SingleTSDFTracker,
                                           std::shared_ptr<Globals>>
    SingleTSDFTracker::registration_("single_tsdf");

void SingleTSDFTracker::Config::checkParams() const {
  checkParamConfig(submap);
}

void SingleTSDFTracker::Config::setupParamsAndPrinting() {
  setupParam("verbosity", &verbosity);
  setupParam("submap", &submap);
  setupParam("use_detectron", &use_detectron);
  setupParam("use_instance_classification", &use_instance_classification);
  setupParam("continue_using_loaded_map", &continue_using_loaded_map);
}

SingleTSDFTracker::SingleTSDFTracker(const Config& config,
                                     std::shared_ptr<Globals> globals)
    : config_(config.checkValid()), IDTrackerBase(std::move(globals)) {
  LOG_IF(INFO, config_.verbosity >= 1) << "\n" << config_.toString();
  addRequiredInput(InputData::InputType::kColorImage);
  addRequiredInput(InputData::InputType::kDepthImage);
  if (config_.submap.useClassLayer()) {
    addRequiredInput(InputData::InputType::kSegmentationImage);
  }
  if (config_.use_detectron) {
    addRequiredInput(InputData::InputType::kDetectronLabels);
  }
}

void SingleTSDFTracker::processInput(SubmapCollection* submaps,
                                     InputData* input) {
  CHECK_NOTNULL(submaps);
  CHECK_NOTNULL(input);
  CHECK(inputIsValid(*input));

  // Check whether the map is already allocated.
  if (submaps->getActiveFreeSpaceSubmapID() < 0) {
    setup(submaps);
  }

  // Check if we should use the ClassID labels
  if (config_.use_instance_classification) {
    parseInstanceToClasses(input);
  }

  // // The id_image is only written with classIDs instead
  // if (config_.use_detectron) {
  //   parseDetectronClasses(input);
  // }
}

void SingleTSDFTracker::parseDetectronClasses(InputData* input) {
  std::unordered_map<int, int> detectron_to_class_id;
  for (auto it = input->idImagePtr()->begin<int>();
       it != input->idImagePtr()->end<int>(); ++it) {
    if (*it == 0) {
      // Zero indicates unknown class / no prediction.
      continue;
    }
    auto class_it = detectron_to_class_id.find(*it);
    if (class_it == detectron_to_class_id.end()) {
      // First time we encounter this ID, write to the map.
      const int class_id = input->detectronLabels().at(*it).category_id;
      detectron_to_class_id[*it] = class_id;
      *it = class_id;
    } else {
      *it = class_it->second;
    }
  }
}

void SingleTSDFTracker::parseInstanceToClasses(InputData* input) {
  std::unordered_map<int, int> instance_to_class_id;
  for (auto it = input->idImagePtr()->begin<int>();
       it != input->idImagePtr()->end<int>(); ++it) {
    if (*it == 0) {
      // Zero indicates unknown class / no prediction
      continue;
    }
    auto class_it = instance_to_class_id.find(*it);
    if (class_it == instance_to_class_id.end()) {
      // First time we encounter this ID, write to the map.
      const int class_id = globals_->labelHandler()->getClassID(*it);
      std::cout << "LOOK HERE class_id from labels " << class_id << std::endl;
      instance_to_class_id[*it] = class_id;
      *it = class_id;
    } else {
      *it = class_it->second;
    }
  }
}

void SingleTSDFTracker::setup(SubmapCollection* submaps) {
  // Check if there is a loaded map.
  if (submaps->size() > 0 && config_.continue_using_loaded_map) {
    Submap& map = *(submaps->begin());
    if (map.getConfig().voxel_size != config_.submap.voxel_size ||
        map.getConfig().voxels_per_side != config_.submap.voxels_per_side ||
        map.getConfig().truncation_distance !=
            config_.submap.truncation_distance ||
        map.getConfig().useClassLayer() != config_.submap.useClassLayer()) {
      LOG(WARNING)
          << "Loaded submap config does not match the specified config.";
    }
    map.setIsActive(true);
    map_id_ = map.getID();
  } else {
    // Allocate the single map.
    Submap* new_submap = submaps->createSubmap(config_.submap);
    new_submap->setLabel(PanopticLabel::kBackground);
    map_id_ = new_submap->getID();
  }
  submaps->setActiveFreeSpaceSubmapID(map_id_);
}

}  // namespace panoptic_mapping
