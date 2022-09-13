#include <libcamera/camera.h>
#include <libcamera/camera_manager.h>
#include <libcamera/formats.h>
#include <libcamera/framebuffer_allocator.h>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_components/register_node_macro.hpp>
#include <sensor_msgs/image_encodings.hpp>
#include <sensor_msgs/msg/compressed_image.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sys/mman.h>


namespace camera
{
class CameraNode : public rclcpp::Node
{
public:
  explicit CameraNode(const rclcpp::NodeOptions &options);

  ~CameraNode();

private:
  libcamera::CameraManager camera_manager;
  std::shared_ptr<libcamera::Camera> camera;
  std::shared_ptr<libcamera::FrameBufferAllocator> allocator;
  std::vector<std::unique_ptr<libcamera::Request>> requests;

  // timestamp offset (ns) from camera time to system time
  int64_t time_offset = 0;

  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr pub_image;
  rclcpp::Publisher<sensor_msgs::msg::CompressedImage>::SharedPtr pub_image_compressed;

  void requestComplete(libcamera::Request *request);
};

RCLCPP_COMPONENTS_REGISTER_NODE(camera::CameraNode)


struct buffer_t
{
  void *data;
  size_t size;
};

//mapping of FourCC to ROS image encodings
// see 'include/uapi/drm/drm_fourcc.h' for a full FourCC list

// supported FourCC formats, without conversion
const std::unordered_map<uint32_t, std::string> map_format_raw = {
  // RGB encodings
  {libcamera::formats::R8.fourcc(), sensor_msgs::image_encodings::MONO8},
  {libcamera::formats::RGB888.fourcc(), sensor_msgs::image_encodings::RGB8},
  {libcamera::formats::BGR888.fourcc(), sensor_msgs::image_encodings::BGR8},
  {libcamera::formats::RGBA8888.fourcc(), sensor_msgs::image_encodings::RGBA8},
  {libcamera::formats::BGRA8888.fourcc(), sensor_msgs::image_encodings::BGRA8},
  // YUV encodings
  {libcamera::formats::YUYV.fourcc(), sensor_msgs::image_encodings::YUV422_YUY2},
  {libcamera::formats::YUV422.fourcc(), sensor_msgs::image_encodings::YUV422},
  // Bayer encodings
  {libcamera::formats::SRGGB8.fourcc(), sensor_msgs::image_encodings::BAYER_RGGB8},
  {libcamera::formats::SGRBG8.fourcc(), sensor_msgs::image_encodings::BAYER_GRBG8},
  {libcamera::formats::SGBRG8.fourcc(), sensor_msgs::image_encodings::BAYER_GBRG8},
  {libcamera::formats::SBGGR8.fourcc(), sensor_msgs::image_encodings::BAYER_BGGR8},
  {libcamera::formats::SRGGB16.fourcc(), sensor_msgs::image_encodings::BAYER_RGGB16},
  {libcamera::formats::SGRBG16.fourcc(), sensor_msgs::image_encodings::BAYER_GRBG16},
  {libcamera::formats::SGBRG16.fourcc(), sensor_msgs::image_encodings::BAYER_GBRG16},
  {libcamera::formats::SBGGR16.fourcc(), sensor_msgs::image_encodings::BAYER_BGGR16},
};

// supported FourCC formats, without conversion, compressed
const std::unordered_map<uint32_t, std::string> map_format_compressed = {
  {libcamera::formats::MJPEG.fourcc(), "jpeg"},
};

CameraNode::CameraNode(const rclcpp::NodeOptions &options) : Node("camera", options)
{
  // pixel format
  rcl_interfaces::msg::ParameterDescriptor param_descr_format;
  param_descr_format.description = "pixel format of streaming buffer";
  param_descr_format.read_only = true;
  declare_parameter<std::string>("format", {}, param_descr_format);

  // image dimensions
  rcl_interfaces::msg::ParameterDescriptor param_descr_ro;
  param_descr_ro.read_only = true;
  declare_parameter<int64_t>("width", {}, param_descr_ro);
  declare_parameter<int64_t>("height", {}, param_descr_ro);

  // publisher for raw and compressed image
  pub_image = this->create_publisher<sensor_msgs::msg::Image>("~/image_raw", 1);
  pub_image_compressed =
    this->create_publisher<sensor_msgs::msg::CompressedImage>("~/image_raw/compressed", 1);

  // start camera manager and check for cameras
  camera_manager.start();
  if (camera_manager.cameras().empty())
    throw std::runtime_error("no cameras available");

  // get the first camera
  camera = camera_manager.get(camera_manager.cameras().front()->id());
  if (!camera)
    throw std::runtime_error("failed to find first camera");

  if (camera->acquire())
    throw std::runtime_error("failed to acquire first camera");

  // configure camera stream
  std::unique_ptr<libcamera::CameraConfiguration> cfg =
    camera->generateConfiguration({libcamera::StreamRole::VideoRecording});

  if (!cfg)
    throw std::runtime_error("failed to generate configuration");

  // show all supported stream configurations and pixel formats
  std::cout << ">> stream configurations:" << std::endl;
  for (size_t i = 0; i < cfg->size(); i++) {
    const libcamera::StreamConfiguration &scfg = cfg->at(i);
    const libcamera::StreamFormats &formats = scfg.formats();

    std::cout << i << ": " << scfg.toString() << std::endl;
    for (const libcamera::PixelFormat &pixelformat : formats.pixelformats()) {
      std::cout << "  - Pixelformat: " << pixelformat.toString() << " ("
                << formats.range(pixelformat).min.toString() << " - "
                << formats.range(pixelformat).max.toString() << ")" << std::endl;
      std::cout << "    Sizes:" << std::endl;
      for (const libcamera::Size &size : formats.sizes(pixelformat))
        std::cout << "     - " << size.toString() << std::endl;
    }
  }

  libcamera::StreamConfiguration &scfg = cfg->at(0);
  std::string format;
  get_parameter("format", format);
  if (format.empty()) {
    // find first supported pixel format available by camera
    scfg.pixelFormat = {};
    for (const libcamera::PixelFormat &pixelformat : scfg.formats().pixelformats()) {
      if (map_format_raw.count(pixelformat.fourcc()) ||
          map_format_compressed.count(pixelformat.fourcc())) {
        scfg.pixelFormat = pixelformat;
        break;
      }
    }

    if (!scfg.pixelFormat.isValid())
      throw std::runtime_error("camera does not provide any of the supported pixel formats");
  }
  else {
    // get pixel format from provided string
    scfg.pixelFormat = libcamera::PixelFormat::fromString(format);
  }

  int64_t width = 0, height = 0;
  get_parameter("width", width);
  get_parameter("height", height);
  if (width)
    scfg.size.width = width;
  if (height)
    scfg.size.height = height;

  switch (cfg->validate()) {
  case libcamera::CameraConfiguration::Valid:
    break;
  case libcamera::CameraConfiguration::Adjusted:
    std::cerr << "Stream configuration adjusted" << std::endl;
    break;
  case libcamera::CameraConfiguration::Invalid:
    throw std::runtime_error("failed to valid stream configurations");
    break;
  }

  if (camera->configure(cfg.get()) < 0)
    throw std::runtime_error("failed to configure streams");

  std::cout << "camera \"" << camera->id() << "\" configured with " << scfg.toString() << " stream"
            << std::endl;

  // allocate stream buffers and create one request per buffer
  libcamera::Stream *stream = scfg.stream();

  allocator = std::make_shared<libcamera::FrameBufferAllocator>(camera);
  allocator->allocate(stream);

  for (const std::unique_ptr<libcamera::FrameBuffer> &buffer : allocator->buffers(stream)) {
    std::unique_ptr<libcamera::Request> request = camera->createRequest();
    if (!request)
      throw std::runtime_error("Can't create request");

    if (request->addBuffer(stream, buffer.get()) < 0)
      throw std::runtime_error("Can't set buffer for request");

    requests.push_back(std::move(request));
  }

  // register callback
  camera->requestCompleted.connect(this, &CameraNode::requestComplete);

  // start camera and queue all requests
  if (camera->start())
    throw std::runtime_error("failed to start camera");

  for (std::unique_ptr<libcamera::Request> &request : requests)
    camera->queueRequest(request.get());
}

CameraNode::~CameraNode()
{
  if (camera->stop())
    std::cerr << "failed to stop camera" << std::endl;
  camera->requestCompleted.disconnect();
  camera->release();
  camera_manager.stop();
}

void CameraNode::requestComplete(libcamera::Request *request)
{
  if (request->status() == libcamera::Request::RequestCancelled)
    return;

  assert(request->buffers().size() == 1);

  // get the stream and buffer from the request
  const libcamera::Stream *stream;
  libcamera::FrameBuffer *buffer;
  std::tie(stream, buffer) = *request->buffers().begin();

  const libcamera::FrameMetadata &metadata = buffer->metadata();

  // set time offset once for accurate timing using the device time
  if (time_offset == 0)
    time_offset = this->now().nanoseconds() - metadata.timestamp;

  // memory-map the frame buffer planes
  assert(buffer->planes().size() == metadata.planes().size());
  std::vector<buffer_t> buffers;
  for (size_t i = 0; i < buffer->planes().size(); i++) {
    buffer_t mem;
    mem.size = metadata.planes()[i].bytesused;
    mem.data =
      mmap(NULL, mem.size, PROT_READ | PROT_WRITE, MAP_SHARED, buffer->planes()[i].fd.get(), 0);
    buffers.push_back(mem);
  }

  // send image data
  std_msgs::msg::Header hdr;
  hdr.stamp = rclcpp::Time(time_offset + int64_t(metadata.timestamp));
  hdr.frame_id = "camera";
  const libcamera::StreamConfiguration &cfg = stream->configuration();

  if (map_format_raw.count(cfg.pixelFormat.fourcc())) {
    // raw uncompressed image
    assert(buffers.size() == 1);
    sensor_msgs::msg::Image::UniquePtr msg_img;
    msg_img = std::make_unique<sensor_msgs::msg::Image>();
    msg_img->header = hdr;
    msg_img->width = cfg.size.width;
    msg_img->height = cfg.size.height;
    msg_img->step = cfg.stride;
    msg_img->encoding = map_format_raw.at(cfg.pixelFormat.fourcc());
    msg_img->data.resize(buffers[0].size);
    memcpy(msg_img->data.data(), buffers[0].data, buffers[0].size);
    pub_image->publish(std::move(msg_img));
  }
  else if (map_format_compressed.count(cfg.pixelFormat.fourcc())) {
    // compressed image
    assert(buffers.size() == 1);
    sensor_msgs::msg::CompressedImage::UniquePtr msg_img_compressed;
    msg_img_compressed = std::make_unique<sensor_msgs::msg::CompressedImage>();
    msg_img_compressed->header = hdr;
    msg_img_compressed->format = map_format_compressed.at(cfg.pixelFormat.fourcc());
    msg_img_compressed->data.resize(buffers[0].size);
    memcpy(msg_img_compressed->data.data(), buffers[0].data, buffers[0].size);
    pub_image_compressed->publish(std::move(msg_img_compressed));
  }
  else {
    throw std::runtime_error("unsupported pixel format: " +
                             stream->configuration().pixelFormat.toString());
  }

  // queue the request again for the next frame
  request->reuse(libcamera::Request::ReuseBuffers);
  camera->queueRequest(request);
}

} // namespace camera
