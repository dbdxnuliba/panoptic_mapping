#include "panoptic_mapping/tools/serialization.h"

#include <fstream>
#include <queue>
#include <random>
#include <vector>
#include <string> 

#include <experimental/filesystem>
#include <gtest/gtest.h>

#include "panoptic_mapping/Submap.pb.h"
#include "panoptic_mapping/common/common.h"
#include "panoptic_mapping/map/classification/binary_count.h"
#include "panoptic_mapping/test/comparison_utils.h"
#include "panoptic_mapping/test/randomization_utils.h"

namespace panoptic_mapping {
namespace test {

struct SerializationConfig {
  // Layer structure.
  const size_t voxels_per_side = 16;
  const FloatingPoint voxel_size = 0.1;

  // Various params.
  const Point origin = Point(0, 0, 0);

  // Number of random tests to run.
  const size_t num_voxel_tests = 100;
  const size_t num_block_tests = 5;
  const size_t num_layer_tests = 5;
  const size_t num_blocks_per_layer = 10;

  // Cached downstream quantities.
  const size_t voxels_per_block =
      voxels_per_side * voxels_per_side * voxels_per_side;
  const FloatingPoint block_size_inv = 1.f / (voxel_size * voxels_per_side);
} config;

// Serialize and deserialize a given voxel type.
template <typename VoxelT>
inline void testVoxelSerialization() {
  for (size_t i = 0; i < config.num_voxel_tests; ++i) {
    VoxelT before, after;
    randomizeVoxel(&before);
    const std::vector<uint32_t> data = before.serializeVoxelToInt();
    size_t index = 0;
    after.deseriliazeVoxelFromInt(data, &index);
    checkVoxelEqual(before, after);
  }
}

// Serialize and deserialize a given voxel block from within a layer.
template <typename VoxelT, typename LayerT>
inline void testBlockSerialization() {
  for (size_t i = 0; i < config.num_block_tests; ++i) {
    LayerT before(typename LayerT::Config(), config.voxel_size,
                  config.voxels_per_side);

    // Create a random block.
    auto block = before.allocateNewBlockByCoordinates(config.origin);
    for (size_t i = 0; i < config.voxels_per_block; ++i) {
      randomizeVoxel(static_cast<VoxelT*>(&block->getVoxelByLinearIndex(i)));
    }

    // Save and load via temporary filestream.
    const std::string file_name = testing::internal::TempDir() +
                                  "panoptic_mapping_serialization_test.tmp";
    std::fstream f(file_name, std::fstream::in | std::fstream::out |
                                  std::fstream::trunc | std::fstream::binary);
    EXPECT_TRUE(f.is_open());
    EXPECT_TRUE(before.saveBlocksToStream(true, voxblox::BlockIndexList(), &f));
    size_t tmp_byte_offset = 0;
    voxblox::BlockProto block_proto;
    EXPECT_TRUE(voxblox::utils::readProtoMsgFromStream(&f, &block_proto,
                                                       &tmp_byte_offset));
    f.close();
    std::experimental::filesystem::remove(file_name);

    // De-serialize the block and check.
    LayerT after(typename LayerT::Config(), config.voxel_size,
                 config.voxels_per_side);
    after.addBlockFromProto(block_proto);
    checkLayerEqual(before.getLayer(), after.getLayer());
  }
}

// Serialize and deserialize a given voxel block from within a layer.
template <typename VoxelT, typename LayerT>
inline void testLayerSerialization() {
  for (size_t i = 0; i < config.num_layer_tests; ++i) {
    LayerT before(typename LayerT::Config(), config.voxel_size,
                  config.voxels_per_side);

    // Create up to N random blocks depending on number of duplicates.
    for (size_t i = 0; i < config.num_blocks_per_layer; ++i) {
      // Get random positions of the blocks.
      const Point position(#include <string> getRandomReal(-10.f, 10.f),
                           getRandomReal(-10.f, 10.f),
                           getRandomReal(-10.f, 10.f));
      auto block = before.allocateNewBlockByCoordinates(position);
      for (size_t i = 0; i < config.voxels_per_block; ++i) {
        randomizeVoxel(static_cast<VoxelT*>(&block->getVoxelByLinearIndex(i)));
      }
    }

    // Save and load via temporary filestream.
    const std::string file_name = testing::internal::TempDir() +
                                  "panoptic_mapping_serialization_test.tmp";
    std::fstream f(file_name, std::fstream::in | std::fstream::out |
                                  std::fstream::trunc | std::fstream::binary);
    // TODO(schmluk): it might be neater to move submap serialization also to
    // the serialization header.
    SubmapProto submap_proto;
    submap_proto.set_class_voxel_type(static_cast<int>(before.getVoxelType()));
    submap_proto.set_num_class_blocks(before.getNumberOfAllocatedBlocks());
    submap_proto.set_voxel_size(config.voxel_size);
    submap_proto.set_voxels_per_side(config.voxels_per_side);
    EXPECT_TRUE(f.is_open());
    EXPECT_TRUE(voxblox::utils::writeProtoMsgToStream(submap_proto, &f));
    EXPECT_TRUE(before.saveBlocksToStream(true, voxblox::BlockIndexList(), &f));
    size_t tmp_byte_offset = 0;

    SubmapProto submap_proto_after;
    EXPECT_TRUE(voxblox::utils::readProtoMsgFromStream(&f, &submap_proto_after,
                                                       &tmp_byte_offset));
    auto loaded =
        loadClassLayerFromStream(submap_proto_after, &f, &tmp_byte_offset);
    if (!loaded) {
      FAIL() << "Could not loadClassLayerFromStream";
      return;
    }

    f.close();
    std::experimental::filesystem::remove(file_name);

    // Check the layers for type and content.
    EXPECT_EQ(before.getVoxelType(), loaded->getVoxelType());
    LayerT* after = dynamic_cast<LayerT*>(loaded.get());
    if (after == nullptr) {
      FAIL() << "Could not cast loaded layer to LayerT.";
      return;
    }
    checkLayerEqual(before.getLayer(), after->getLayer());
  }
}

TEST(BinaryCount, SerializeVoxel) {
  testVoxelSerialization<BinaryCountVoxel>();
}

TEST(BinaryCount, SerializeBlock) {
  testBlockSerialization<BinaryCountVoxel, BinaryCountLayer>();
}

TEST(BinaryCount, SerializeLayer) {
  testLayerSerialization<BinaryCountVoxel, BinaryCountLayer>();
}

}  // namespace test
}  // namespace panoptic_mapping

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
