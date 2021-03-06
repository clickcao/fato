/*****************************************************************************/
/*  Copyright (c) 2015, Alessandro Pieropan                                  */
/*  All rights reserved.                                                     */
/*                                                                           */
/*  Redistribution and use in source and binary forms, with or without       */
/*  modification, are permitted provided that the following conditions       */
/*  are met:                                                                 */
/*                                                                           */
/*  1. Redistributions of source code must retain the above copyright        */
/*  notice, this list of conditions and the following disclaimer.            */
/*                                                                           */
/*  2. Redistributions in binary form must reproduce the above copyright     */
/*  notice, this list of conditions and the following disclaimer in the      */
/*  documentation and/or other materials provided with the distribution.     */
/*                                                                           */
/*  3. Neither the name of the copyright holder nor the names of its         */
/*  contributors may be used to endorse or promote products derived from     */
/*  this software without specific prior written permission.                 */
/*                                                                           */
/*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS      */
/*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT        */
/*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR    */
/*  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT     */
/*  HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,   */
/*  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT         */
/*  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,    */
/*  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY    */
/*  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT      */
/*  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE    */
/*  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.     */
/*****************************************************************************/

#include "../include/tracker_offline.h"
#include "../../utilities/include/profiler.h"
#include "../../utilities/include/draw_functions.h"

using namespace std;
using namespace cv;

namespace fato {

TrackerOffline::TrackerOffline()
    : nh_(),
      rgb_video_path_(),
      depth_video_path_(),
      top_left_pt_(),
      bottom_righ_pt_(),
      use_depth_(false),
      rgb_video_(),
      depth_video_(),
      spinner_(0),
      tracker_() {
  publisher_ = nh_.advertise<sensor_msgs::Image>("pinot_tracker/output", 1);

  init();
}

void TrackerOffline::init() {
  getParameters();
  if (rgb_video_path_.size() == 0) {
    ROS_INFO("No input video set in parameters!");
    return;
  }

  bool is_video_open = false;

  if (!rgb_video_.open(rgb_video_path_)) {
    ROS_WARN("TRACKER: invalid user defined video path, trying default...");
    is_video_open = false;
  }

  if (!rgb_video_.open(rgb_default_path_)) {
    ROS_WARN("TRACKER: invalid default video path. Quitting...");
    return;
  }

  if (use_depth_) {
    depth_video_.open(depth_video_path_);
  }

  if (top_left_pt_ == bottom_righ_pt_) {
    ROS_INFO("Initial bounding box not defined!");
    return;
  }

  cvStartWindowThread();
  namedWindow("Init");
  Mat rgb_image, tmp;
  rgb_video_.read(rgb_image);
  rgb_image.copyTo(tmp);
  rectangle(tmp, top_left_pt_, bottom_righ_pt_, Scalar(255, 0, 0), 1);
  imshow("Init", tmp);
  waitKey(0);
  destroyWindow("Init");
  waitKey(1);

  tracker_.init(rgb_image, top_left_pt_, bottom_righ_pt_);

  start();
}

void TrackerOffline::start() {
  ros::Rate r(30);

  Mat rgb_image;

  auto& profiler = Profiler::getInstance();

  while (ros::ok()) {
    if (!rgb_video_.read(rgb_image)) break;

    profiler->start("frame");
    tracker_.computeNext(rgb_image);
    profiler->stop("frame");

    Point2f p = tracker_.getCentroid();
    circle(rgb_image, p, 5, Scalar(255, 0, 0), -1);
    vector<Point2f> bbox = tracker_.getBoundingBox();
    drawBoundingBox(bbox, Scalar(255, 0, 0), 2, rgb_image);

    cv_bridge::CvImage cv_img;
    cv_img.image = rgb_image;
    cv_img.encoding = sensor_msgs::image_encodings::BGR8;
    publisher_.publish(cv_img.toImageMsg());

    r.sleep();
  }

  stringstream ss;
  ss << "Average tracking time: " << profiler->getTime("frame");
  ROS_INFO(ss.str().c_str());
}

void TrackerOffline::getParameters() {
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
  if (!ros::param::get("pinot/clustering/eps", params_.eps))
    ss << "failed \n";
  else
    ss << params_.eps << "\n";

  ss << "min_points: ";
  if (!ros::param::get("pinot/clustering/min_points", params_.min_points))
    ss << "failed \n";
  else
    ss << params_.min_points << "\n";

  ss << "ransac_iterations: ";
  if (!ros::param::get("pinot/pose_estimation/ransac_iterations",
                       params_.ransac_iterations))
    ss << "failed \n";
  else
    ss << params_.ransac_iterations << "\n";

  int x, y, w, h;
  x = y = w = h = 0;

  ss << "box_x: ";
  if (!ros::param::get("pinot/offline/box_x", x))
    ss << "failed \n";
  else
    ss << x << "\n";

  ss << "box_y: ";
  if (!ros::param::get("pinot/offline/box_y", y))
    ss << "failed \n";
  else
    ss << y << "\n";

  ss << "box_w: ";
  if (!ros::param::get("pinot/offline/box_w", w))
    ss << "failed \n";
  else
    ss << w << "\n";

  ss << "box_h: ";
  if (!ros::param::get("pinot/offline/box_h", h))
    ss << "failed \n";
  else
    ss << h << "\n";

  top_left_pt_ = Point2f(x, y);
  bottom_righ_pt_ = Point2f(x + w, y + h);

  string default_path, user_path, video_name;

  ss << "default_path: ";
  if (!ros::param::get("pinot/offline/default_path", default_path))
    ss << "failed \n";
  else
    ss << default_path << "\n";

  ss << "data_path: ";
  if (!ros::param::get("pinot/offline/data_path", user_path))
    ss << "failed \n";
  else
    ss << user_path << "\n";

  ss << "rgb_input: ";
  if (!ros::param::get("pinot/offline/rgb_input", video_name))
    ss << "failed \n";
  else
    ss << video_name << "\n";

  rgb_default_path_ = default_path + video_name;
  rgb_video_path_ = user_path + video_name;

  ss << "path_to_video \n";
  ss << rgb_video_path_ << "\n";

  ss << "depth_input: ";
  if (!ros::param::get("pinot/offline/depth_input", depth_video_path_))
    ss << "failed \n";
  else
    ss << depth_video_path_ << "\n";

  ROS_INFO(ss.str().c_str());
}

}  // end namespace

int main(int argc, char* argv[]) {
  ROS_INFO("Starting tracker in offline mode");
  ros::init(argc, argv, "pinot_tracker_offline_node");

  fato::TrackerOffline offline;

  ros::shutdown();

  return 0;
}
