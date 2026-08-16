#ifndef V4L2_CONTROLS_HPP
#define V4L2_CONTROLS_HPP

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <linux/videodev2.h>

namespace gudcam {

enum class ControlType {
    Integer,
    Boolean,
    Menu,
    Button,
    Integer64,
    String,
    Bitmask,
    Unknown
};

struct MenuItem {
    uint32_t index;
    std::string name;
};

struct ControlInfo {
    uint32_t id{0};
    std::string name;
    ControlType type{ControlType::Unknown};
    int32_t min{0};
    int32_t max{0};
    int32_t step{1};
    int32_t default_val{0};
    int32_t current_val{0};
    uint32_t flags{0};
    bool disabled{false};
    std::vector<MenuItem> menu_items;
};

class V4L2Controls {
public:
    V4L2Controls();
    ~V4L2Controls();

    // Query and enumerate all camera hardware controls dynamically
    std::vector<ControlInfo> query_all_controls(int fd);

    // Get current value of a single control
    bool get_control(int fd, uint32_t id, int32_t& value);

    // Set a single control value
    bool set_control(int fd, uint32_t id, int32_t value);

    // Commit atomic multi-register parameter updates using VIDIOC_S_EXT_CTRLS
    bool set_controls_atomic(int fd, const std::map<uint32_t, int32_t>& control_map);

    // Helper to format control type
    static std::string control_type_to_string(ControlType type);
};

} // namespace gudcam

#endif // V4L2_CONTROLS_HPP
