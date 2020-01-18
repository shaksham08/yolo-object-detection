#include "yolo.h"

Yolo::Yolo() {
  model_ = nullptr;
  capturer_ = std::make_unique<cv::VideoCapture>();
  frames_ = std::make_unique<QueueFps<cv::Mat>>();
  processed_frames_ = std::make_unique<QueueFps<cv::Mat>>();
  predictions_ = std::make_unique<QueueFps<std::vector<cv::Mat>>>();
}

Yolo::~Yolo() {}

void Yolo::Run() {
  Window window("YOLO Object Detection", Configuration::THRESHOLD);
  window.Build();

  auto file = std::string(Configuration::CLASSES_PATH);
  std::ifstream ifs(file.c_str());
  if (!ifs.is_open()) {
    CV_Error(cv::Error::StsError, "File " + file + " not found");
  }
  std::string line;
  while (std::getline(ifs, line)) {
    classes_.emplace_back(line);
  }
  ifs.close();

  cv::String model_path(Configuration::MODEL_PATH);
  cv::String config_path(Configuration::CONFIG_PATH);
  Model model = Model::Init(model_path, config_path);
  model_ = std::make_unique<Model>(std::move(model));

  capturer_->open(cv::String(Configuration::INPUT));

  StartFramesCapture();
  StartFramesProcessing();

  while (cv::waitKey(1) < 0) {

    if (predictions_->Empty()) {
      continue;
    }

    std::vector<cv::Mat> outs = predictions_->Get();
    cv::Mat frame = processed_frames_->Get();

    model_->process(frame, outs, classes_);

    window.DisplayInfo(frame, frames_->GetFps(), frames_->GetCounter(), predictions_->GetFps(),
                       predictions_->GetCounter());

    window.Show(frame);
  }

  std::for_each(threads_.begin(), threads_.end(), [](std::thread &t) { t.join(); });
}

void Yolo::StartFramesCapture() { threads_.emplace_back(std::thread(&Yolo::CaptureFrames, this)); }

void Yolo::StartFramesProcessing() { threads_.emplace_back(std::thread(&Yolo::ProcessFrames, this)); }

void Yolo::CaptureFrames() {
  cv::Mat frame;

  while (true) {
    //    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::cout << "Capturing Frames" << std::endl;
    *capturer_ >> frame;

    if (!frame.empty()) {
      frames_->Push(frame.clone());
    } else {
      break;
    }
  }
}

void Yolo::ProcessFrames() {
  std::queue<cv::AsyncArray> futures;
  Configuration::FrameProcessingData data;
  data.mean = cv::Scalar(0, 0, 0);
  data.input_size = cv::Size(Configuration::WIDTH, Configuration::HEIGHT);
  data.rgb = Configuration::RGB;
  data.scale = Configuration::SCALE;

  std::cout << "Processing Frames" << std::endl;
  while (true) {
    cv::Mat frame;

    if (!frames_->Empty()) {
      std::cout << "Popping frame from queue" << std::endl;
      frame = frames_->Get();
    }

    if (!frame.empty()) {
      model_->process(frame, data);
      processed_frames_->Push(frame);

      futures.push(model_->ForwardAsync());
    }

    while (!futures.empty() && futures.front().wait_for(std::chrono::seconds(0))) {
      cv::AsyncArray async = futures.front();
      futures.pop();
      cv::Mat out;
      async.get(out);
      predictions_->Push({out});
    }
  }
}
