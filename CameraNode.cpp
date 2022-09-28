#include "opencv2/core/mat.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/imgcodecs.hpp"
#include <chrono>
#include <cstring>
#include <iostream>
#include <libcamera/camera.h>
#include <libcamera/camera_manager.h>
#include <libcamera/control_ids.h>
#include <libcamera/formats.h>
#include <libcamera/framebuffer_allocator.h>
#include <libcamera/property_ids.h>
#include <mutex>
#include <sys/mman.h>
#include <thread>


using namespace std::chrono_literals;

class CameraNode
{
public:
  explicit CameraNode();

  ~CameraNode();

private:
  libcamera::CameraManager camera_manager;
  std::shared_ptr<libcamera::Camera> camera;
  std::shared_ptr<libcamera::FrameBufferAllocator> allocator;
  std::vector<std::unique_ptr<libcamera::Request>> requests;

  // parameters that are to be set for every request
  std::unordered_map<unsigned int, libcamera::ControlValue> parameters;
  std::mutex parameters_lock;

  void
  requestComplete(libcamera::Request *request);
};

struct buffer_t
{
  void *data;
  size_t size;
};

CameraNode::CameraNode()
{
  // start camera manager and check for cameras
  camera_manager.start();
  if (camera_manager.cameras().empty())
    throw std::runtime_error("no cameras available");

  std::cout << ">> cameras:" << std::endl;
  for (size_t id = 0; id < camera_manager.cameras().size(); id++) {
    const std::shared_ptr<libcamera::Camera> camera = camera_manager.cameras().at(id);
    const libcamera::ControlList &properties = camera->properties();
    const std::string name = properties.get(libcamera::properties::Model).value_or("UNDEFINED");
    std::cout << id << ": " << name << " (" << camera->id() << ")" << std::endl;
  }

  // get the camera
  camera = camera_manager.get(camera_manager.cameras().at(0)->id());
  if (!camera)
    throw std::runtime_error("failed to find camera");

  if (camera->acquire())
    throw std::runtime_error("failed to acquire camera");

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
  std::string format = "MJPEG";

  scfg.pixelFormat = libcamera::PixelFormat::fromString(format);

  scfg.size.width = 0;
  scfg.size.height = 0;

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

  // format camera name for calibration file
  const libcamera::ControlList &props = camera->properties();
  std::string cname = camera->id() + '_' + scfg.size.toString();
  const std::optional<std::string> model = props.get(libcamera::properties::Model);
  if (model)
    cname = model.value() + '_' + cname;

  // clean camera name of non-alphanumeric characters
  cname.erase(
    std::remove_if(cname.begin(), cname.end(), [](const char &x) { return std::isspace(x); }),
    cname.cend());
  std::replace_if(
    cname.begin(), cname.end(), [](const char &x) { return !std::isalnum(x); }, '_');

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
  camera->requestCompleted.disconnect();
  std::cout << "stopping ..." << std::endl;
  if (camera->stop())
    std::cerr << "failed to stop camera" << std::endl;
  std::cout << "... stopped." << std::endl;
  camera->release();
  camera_manager.stop();
}

void
CameraNode::requestComplete(libcamera::Request *request)
{
  if (request->status() == libcamera::Request::RequestComplete) {
    assert(request->buffers().size() == 1);

    // get the stream and buffer from the request
    const libcamera::Stream *stream;
    libcamera::FrameBuffer *buffer;
    std::tie(stream, buffer) = *request->buffers().begin();

    const libcamera::FrameMetadata &metadata = buffer->metadata();

    // memory-map the frame buffer planes
    assert(buffer->planes().size() == metadata.planes().size());
    std::vector<buffer_t> buffers;
    for (size_t i = 0; i < buffer->planes().size(); i++) {
      buffer_t mem;
      mem.size = metadata.planes()[i].bytesused;
      mem.data =
        mmap(NULL, mem.size, PROT_READ | PROT_WRITE, MAP_SHARED, buffer->planes()[i].fd.get(), 0);
      buffers.push_back(mem);
      if (mem.data == MAP_FAILED)
        std::cerr << "mmap failed: " << std::strerror(errno) << std::endl;
    }

    const std::vector<uint8_t> data((uint8_t *)buffers[0].data,
                                    (uint8_t *)buffers[0].data + buffers[0].size);

    const cv::Mat img = cv::imdecode({data}, cv::IMREAD_UNCHANGED);

    // delay processing
    std::this_thread::sleep_for(100ms);

    cv::imshow("img", img);
    cv::waitKey(1);

    for (const buffer_t &mem : buffers)
      if (munmap(mem.data, mem.size) == -1)
        std::cerr << "munmap failed: " << std::strerror(errno) << std::endl;
  }
  else if (request->status() == libcamera::Request::RequestCancelled) {
    std::cerr << "request '" << request->toString() << "' cancelled" << std::endl;
  }

  // queue the request again for the next frame
  request->reuse(libcamera::Request::ReuseBuffers);

  // update parameters
  parameters_lock.lock();
  for (const auto &[id, value] : parameters)
    request->controls().set(id, value);
  parameters.clear();
  parameters_lock.unlock();

  camera->queueRequest(request);

  std::cout << "DONE" << std::endl;
}

int
main(int /*argc*/, char * /*argv*/[])
{
  CameraNode cam;
  std::this_thread::sleep_for(5s);
  return EXIT_SUCCESS;
}
