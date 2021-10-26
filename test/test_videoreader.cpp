#include <videoreader/videoreader.h>
#include <string>
#include <gtest/gtest.h>
#include <stdexcept>

TEST(TestVedeoreader, TestVideoFile) {
  auto video_reader = VideoReader::create(TEST_VIDEOPATH, {
      // "analyzeduration", "0",
      // "rtsp_transport", "http",
      // "reorder_queue_size", "13",
      // "probesize", "32",
      // "fflags", "+nobuffer +igndts",
      // "rtbuffsize", "64738",
      // "flags", "low_delay",

      // "analyzeduration", "100000",
      // "probesize", "100000"
    }
  );
  uint64_t const frames_count = video_reader->size();
  EXPECT_EQ(frames_count, 145UL);
  EXPECT_EQ(video_reader->is_seekable(), true);

  uint64_t read_frame_count = 0;
  while (auto frame = video_reader->next_frame())
  {
    EXPECT_EQ(frame->number, read_frame_count);
    EXPECT_EQ(frame->image.width, 640);
    EXPECT_EQ(frame->image.height, 480);
    EXPECT_EQ(frame->image.channels, 3);
    double const expected_timestamp = read_frame_count * 0.04;
    EXPECT_EQ(frame->timestamp_s, expected_timestamp);
    ++read_frame_count;
  }
  EXPECT_EQ(read_frame_count, 145UL);
}

void test_create_invalid_path() {
  VideoReader::create("invalid_path.mp4");
}

TEST(TestVedeoreader, InvalidPath) {
  ASSERT_THROW(test_create_invalid_path(), std::runtime_error);
}

void test_create_argument() {
  VideoReader::create(TEST_VIDEOPATH, {"single"});
}

TEST(TestVedeoreader, Arguments) {
  ASSERT_THROW(test_create_argument(), std::runtime_error);
  VideoReader::create(TEST_VIDEOPATH, {"single", "1"});
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
