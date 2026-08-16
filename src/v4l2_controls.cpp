#include "v4l2_controls.hpp"
#include <iostream>
#include <cstring>
#include <sys/ioctl.h>
#include <cerrno>

namespace gudcam {

V4L2Controls::V4L2Controls() {}
V4L2Controls::~V4L2Controls() {}

std::string V4L2Controls::control_type_to_string(ControlType type) {
    switch (type) {
        case ControlType::Integer:   return "Integer";
        case ControlType::Boolean:   return "Boolean";
        case ControlType::Menu:      return "Menu";
        case ControlType::Button:    return "Button";
        case ControlType::Integer64: return "Integer64";
        case ControlType::String:    return "String";
        case ControlType::Bitmask:   return "Bitmask";
        default:                     return "Unknown";
    }
}

std::vector<ControlInfo> V4L2Controls::query_all_controls(int fd) {
    std::vector<ControlInfo> controls;
    if (fd < 0) return controls;

    struct v4l2_queryctrl qctrl;
    memset(&qctrl, 0, sizeof(qctrl));

    // V4L2_CTRL_FLAG_NEXT_CTRL allows iterating through all driver controls dynamically
    qctrl.id = V4L2_CTRL_FLAG_NEXT_CTRL;

    while (ioctl(fd, VIDIOC_QUERYCTRL, &qctrl) == 0) {
        if (!(qctrl.flags & V4L2_CTRL_FLAG_DISABLED)) {
            ControlInfo info;
            info.id = qctrl.id;
            info.name = reinterpret_cast<const char*>(qctrl.name);
            info.min = qctrl.minimum;
            info.max = qctrl.maximum;
            info.step = qctrl.step;
            info.default_val = qctrl.default_value;
            info.flags = qctrl.flags;
            info.disabled = (qctrl.flags & V4L2_CTRL_FLAG_DISABLED);

            switch (qctrl.type) {
                case V4L2_CTRL_TYPE_INTEGER:
                    info.type = ControlType::Integer;
                    break;
                case V4L2_CTRL_TYPE_BOOLEAN:
                    info.type = ControlType::Boolean;
                    break;
                case V4L2_CTRL_TYPE_MENU:
                case V4L2_CTRL_TYPE_INTEGER_MENU:
                    info.type = ControlType::Menu;
                    break;
                case V4L2_CTRL_TYPE_BUTTON:
                    info.type = ControlType::Button;
                    break;
                case V4L2_CTRL_TYPE_INTEGER64:
                    info.type = ControlType::Integer64;
                    break;
                case V4L2_CTRL_TYPE_STRING:
                    info.type = ControlType::String;
                    break;
                case V4L2_CTRL_TYPE_BITMASK:
                    info.type = ControlType::Bitmask;
                    break;
                default:
                    info.type = ControlType::Unknown;
                    break;
            }

            // Enumerate menu items if control type is Menu
            if (info.type == ControlType::Menu) {
                struct v4l2_querymenu qmenu;
                memset(&qmenu, 0, sizeof(qmenu));
                qmenu.id = qctrl.id;

                for (int i = qctrl.minimum; i <= qctrl.maximum; ++i) {
                    qmenu.index = i;
                    if (ioctl(fd, VIDIOC_QUERYMENU, &qmenu) == 0) {
                        MenuItem item;
                        item.index = i;
                        item.name = reinterpret_cast<const char*>(qmenu.name);
                        info.menu_items.push_back(item);
                    }
                }
            }

            // Get current control value
            int32_t val = 0;
            if (get_control(fd, qctrl.id, val)) {
                info.current_val = val;
            } else {
                info.current_val = qctrl.default_value;
            }

            controls.push_back(info);
        }

        qctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
    }

    // Fallback: If V4L2_CTRL_FLAG_NEXT_CTRL wasn't supported, query standard IDs
    if (controls.empty()) {
        static const uint32_t standard_ids[] = {
            V4L2_CID_BRIGHTNESS, V4L2_CID_CONTRAST, V4L2_CID_SATURATION, V4L2_CID_HUE,
            V4L2_CID_AUDIO_VOLUME, V4L2_CID_AUDIO_BALANCE, V4L2_CID_AUDIO_BASS, V4L2_CID_AUDIO_TREBLE,
            V4L2_CID_AUDIO_MUTE, V4L2_CID_AUDIO_LOUDNESS, V4L2_CID_BLACK_LEVEL, V4L2_CID_AUTO_WHITE_BALANCE,
            V4L2_CID_DO_WHITE_BALANCE, V4L2_CID_RED_BALANCE, V4L2_CID_BLUE_BALANCE, V4L2_CID_GAMMA,
            V4L2_CID_EXPOSURE, V4L2_CID_AUTOGAIN, V4L2_CID_GAIN, V4L2_CID_HFLIP, V4L2_CID_VFLIP,
            V4L2_CID_POWER_LINE_FREQUENCY, V4L2_CID_HUE_AUTO, V4L2_CID_WHITE_BALANCE_TEMPERATURE,
            V4L2_CID_SHARPNESS, V4L2_CID_BACKLIGHT_COMPENSATION, V4L2_CID_CHROMA_AGC, V4L2_CID_COLOR_KILLER
        };

        for (uint32_t id : standard_ids) {
            memset(&qctrl, 0, sizeof(qctrl));
            qctrl.id = id;
            if (ioctl(fd, VIDIOC_QUERYCTRL, &qctrl) == 0 && !(qctrl.flags & V4L2_CTRL_FLAG_DISABLED)) {
                ControlInfo info;
                info.id = qctrl.id;
                info.name = reinterpret_cast<const char*>(qctrl.name);
                info.min = qctrl.minimum;
                info.max = qctrl.maximum;
                info.step = qctrl.step;
                info.default_val = qctrl.default_value;
                info.type = (qctrl.type == V4L2_CTRL_TYPE_BOOLEAN) ? ControlType::Boolean : ControlType::Integer;
                get_control(fd, id, info.current_val);
                controls.push_back(info);
            }
        }
    }

    std::cout << "[V4L2Controls] Enumerated " << controls.size() << " dynamic camera controls\n";
    return controls;
}

bool V4L2Controls::get_control(int fd, uint32_t id, int32_t& value) {
    if (fd < 0) return false;

    struct v4l2_control ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = id;

    if (ioctl(fd, VIDIOC_G_CTRL, &ctrl) == 0) {
        value = ctrl.value;
        return true;
    }

    // Try extended control if standard G_CTRL fails
    struct v4l2_ext_control ext_ctrl;
    struct v4l2_ext_controls ext_ctrls;
    memset(&ext_ctrl, 0, sizeof(ext_ctrl));
    memset(&ext_ctrls, 0, sizeof(ext_ctrls));

    ext_ctrl.id = id;
    ext_ctrls.ctrl_class = V4L2_CTRL_ID2CLASS(id);
    ext_ctrls.count = 1;
    ext_ctrls.controls = &ext_ctrl;

    if (ioctl(fd, VIDIOC_G_EXT_CTRLS, &ext_ctrls) == 0) {
        value = ext_ctrl.value;
        return true;
    }

    return false;
}

bool V4L2Controls::set_control(int fd, uint32_t id, int32_t value) {
    if (fd < 0) return false;

    struct v4l2_control ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = id;
    ctrl.value = value;

    if (ioctl(fd, VIDIOC_S_CTRL, &ctrl) == 0) {
        return true;
    }

    // Fallback to VIDIOC_S_EXT_CTRLS
    std::map<uint32_t, int32_t> single_map;
    single_map[id] = value;
    return set_controls_atomic(fd, single_map);
}

// Atomic Multi-Register Update using VIDIOC_S_EXT_CTRLS
bool V4L2Controls::set_controls_atomic(int fd, const std::map<uint32_t, int32_t>& control_map) {
    if (fd < 0 || control_map.empty()) return false;

    std::vector<struct v4l2_ext_control> ext_ctrl_vec;
    ext_ctrl_vec.reserve(control_map.size());

    for (const auto& kv : control_map) {
        struct v4l2_ext_control c;
        memset(&c, 0, sizeof(c));
        c.id = kv.first;
        c.value = kv.second;
        ext_ctrl_vec.push_back(c);
    }

    struct v4l2_ext_controls ext_ctrls;
    memset(&ext_ctrls, 0, sizeof(ext_ctrls));
    ext_ctrls.ctrl_class = V4L2_CTRL_ID2CLASS(ext_ctrl_vec[0].id);
    ext_ctrls.count = static_cast<uint32_t>(ext_ctrl_vec.size());
    ext_ctrls.controls = ext_ctrl_vec.data();

    if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &ext_ctrls) < 0) {
        // If ext ctrls fails, fall back to individual S_CTRL
        bool all_ok = true;
        for (const auto& kv : control_map) {
            struct v4l2_control ctrl;
            memset(&ctrl, 0, sizeof(ctrl));
            ctrl.id = kv.first;
            ctrl.value = kv.second;
            if (ioctl(fd, VIDIOC_S_CTRL, &ctrl) < 0) {
                all_ok = false;
            }
        }
        return all_ok;
    }

    return true;
}

} // namespace gudcam
