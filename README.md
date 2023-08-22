# Videoreader
c++/c/python wrapper for

* ffmpeg
* galaxy camera (daheng-imaging)
* pylon camera


## Usage:

### python
```python
from videoreader import VideoReader

videoreader = VideoReader("test/big_buck_bunny_480p_1mb.mp4", [])
for img, frame_no, timestamp_s in videoreader:
    print(img, frame_no, timestamp_s)
```

### c++
```cpp
#include <videoreader/videoreader.h>

auto video_reader = VideoReader::create(
    "test/big_buck_bunny_480p_1mb.mp4", {});
while (auto frame = video_reader->next_frame()) {
    std::cout << frame->number << ", " << frame->timestamp_s << "\n";
}
```
