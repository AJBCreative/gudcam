#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

int main() {
    int fd = open("/dev/video0", O_RDWR);
    if (fd < 0) {
        std::cerr << "Failed to open /dev/video0\n";
        return 1;
    }

    struct v4l2_fmtdesc fmtdesc;
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmtdesc.index = 0;

    std::cout << "Supported Camera Formats & Resolutions:\n";
    while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
        std::cout << "Format [" << fmtdesc.index << "]: " << fmtdesc.description 
                  << " (FourCC: " << (char)(fmtdesc.pixelformat & 0xFF)
                  << (char)((fmtdesc.pixelformat >> 8) & 0xFF)
                  << (char)((fmtdesc.pixelformat >> 16) & 0xFF)
                  << (char)((fmtdesc.pixelformat >> 24) & 0xFF) << ")\n";

        struct v4l2_frmsizeenum frmsize;
        frmsize.pixel_format = fmtdesc.pixelformat;
        frmsize.index = 0;

        while (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0) {
            if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                std::cout << "  - Resolution: " << frmsize.discrete.width << "x" << frmsize.discrete.height << "\n";
            }
            frmsize.index++;
        }
        fmtdesc.index++;
    }

    close(fd);
    return 0;
}
