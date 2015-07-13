#include "../include/tracker_node_2d.h"
#include <boost/thread.hpp>
#include <mutex>
#include <opencv2/highgui/highgui.hpp>
#include <sstream>
#include <chrono>

using namespace cv;
using namespace std;

namespace pinot_tracker {

TrackerNode2D::TrackerNode2D()
    : nh_(),
      rgb_topic_("/tracker_input/rgb"),
      camera_info_topic_("/tracker_input/camera_info"),
      queue_size(5),
      is_mouse_dragging_(false),
      img_updated_(false),
      init_requested_(false),
      tracker_initialized_(false),
      mouse_start_(0, 0),
      mouse_end_(0, 0),
      spinner_(0),
      params_() {
  namedWindow("Image Viewer");
  setMouseCallback("Image Viewer", TrackerNode2D::mouseCallback, this);

  getTrackerParameters();

  initRGB();

  run();
}

void TrackerNode2D::initRGB() {
  rgb_it_.reset(new image_transport::ImageTransport(nh_));
  sub_camera_info_.subscribe(nh_, camera_info_topic_, 1);
  /** kinect node settings */
  sub_rgb_.subscribe(*rgb_it_, rgb_topic_, 1,
                     image_transport::TransportHints("raw"));

  sync_rgb_.reset(new SynchronizerRGB(SyncPolicyRGB(queue_size), sub_rgb_,
                                      sub_camera_info_));
  sync_rgb_->registerCallback(
      boost::bind(&TrackerNode2D::rgbCallback, this, _1, _2));
}

void TrackerNode2D::rgbCallback(
    const sensor_msgs::ImageConstPtr &rgb_msg,
    const sensor_msgs::CameraInfoConstPtr &camera_info_msg) {
  Mat rgb;

  readImage(rgb_msg, rgb);

  rgb_image_ = rgb;
  img_updated_ = true;
}

void TrackerNode2D::getTrackerParameters() {
  stringstream ss;

  ss << "Tracker Input: \n";

  ss << "filter_border: ";
  if (!ros::param::get("pinot/tracker_2d/filter_border", params_.filter_border))
    ss << "failed \n";
  else
    ss << params_.filter_border << "\n";

  ss << "update_votes: ";
  if (!ros::param::get("pinot/tracker_2d/update_votes", params_.update_votes))
    ss << "failed \n";
  else
    ss << params_.update_votes << "\n";

  ss << "eps: ";
  if (!ros::param::get("pinot/tracker_2d/eps", params_.eps))
    ss << "failed \n";
  else
    ss << params_.eps << "\n";

  ss << "min_points: ";
  if (!ros::param::get("pinot/tracker_2d/min_points", params_.min_points))
    ss << "failed \n";
  else
    ss << params_.min_points << "\n";

  ROS_INFO(ss.str().c_str());
}

void TrackerNode2D::rgbdCallback(
    const sensor_msgs::ImageConstPtr &depth_msg,
    const sensor_msgs::ImageConstPtr &rgb_msg,
    const sensor_msgs::CameraInfoConstPtr &camera_info_msg) {
  ROS_INFO("Tracker2D: rgbd usupported");
}

void TrackerNode2D::mouseCallback(int event, int x, int y) {
  auto set_point = [this](int x, int y) {
    if (x < mouse_start_.x) {
      mouse_end_.x = mouse_start_.x;
      mouse_start_.x = x;
    } else
      mouse_end_.x = x;

    if (y < mouse_start_.y) {
      mouse_end_.y = mouse_start_.y;
      mouse_start_.y = y;
    } else
      mouse_end_.y = y;
  };

  if (event == EVENT_LBUTTONDOWN) {
    mouse_start_.x = x;
    mouse_start_.y = y;
    mouse_end_ = mouse_start_;
    is_mouse_dragging_ = true;
  } else if (event == EVENT_MOUSEMOVE && is_mouse_dragging_) {
    set_point(x, y);
  } else if (event == EVENT_LBUTTONUP) {
    set_point(x, y);
    is_mouse_dragging_ = false;
    init_requested_ = true;
  }
}

void TrackerNode2D::mouseCallback(int event, int x, int y, int flags,
                                  void *userdata) {
  auto manager = reinterpret_cast<TrackerNode2D *>(userdata);
  manager->mouseCallback(event, x, y);
}

void TrackerNode2D::run() {
  ROS_INFO("INPUT: init tracker");

  spinner_.start();


  cout << params_.threshold << " " << params_.octaves << " "
       << params_.pattern_scale << endl;

  Tracker2D tracker(params_);


  while (ros::ok()) {
    // ROS_INFO_STREAM("Main thread [" << boost::this_thread::get_id() << "].");

    if (img_updated_) {
      if (mouse_start_.x != mouse_end_.x && !tracker_initialized_)
        rectangle(rgb_image_, mouse_start_, mouse_end_, Scalar(255, 0, 0), 3);

      if (init_requested_) {
        ROS_INFO("INPUT: tracker intialization requested");
        tracker.init(rgb_image_, mouse_start_, mouse_end_);
        init_requested_ = false;
        tracker_initialized_ = true;
        ROS_INFO("Tracker initialized!");
        waitKey(0);
      }

      if (tracker_initialized_) {
        auto begin = chrono::high_resolution_clock::now();
        Mat out;
        tracker.computeNext(rgb_image_, out);
        auto end = chrono::high_resolution_clock::now();
        auto time_span =
            chrono::duration_cast<chrono::milliseconds>(end - begin).count();
        stringstream ss;
        Point2f p = tracker.getCentroid();
        circle(rgb_image_, p, 5, Scalar(255, 0, 0), -1);
        ss << "Tracker run in ms: " << time_span << "";
        ROS_INFO(ss.str().c_str());
      }

      imshow("Image Viewer", rgb_image_);
      img_updated_ = false;
      waitKey(1);
    }

    // r.sleep();
  }
}

}  // end namespace

int main(int argc, char *argv[]) {
  ROS_INFO("Starting tracker input");
  ros::init(argc, argv, "pinot_tracker_node");

  pinot_tracker::TrackerNode2D manager;

  ros::shutdown();

  return 0;
}