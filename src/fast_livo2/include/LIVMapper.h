/* 
This file is part of FAST-LIVO2: Fast, Direct LiDAR-Inertial-Visual Odometry.

Developer: Chunran Zheng <zhengcr@connect.hku.hk>

For commercial use, please contact me at <zhengcr@connect.hku.hk> or
Prof. Fu Zhang at <fuzhang@hku.hk>.

This file is subject to the terms and conditions outlined in the 'LICENSE' file,
which is included as part of this source code package.
*/

#ifndef LIV_MAPPER_H
#define LIV_MAPPER_H

#include "IMU_Processing.h"
#include "vio.h"
#include "preprocess.h"
#include <cv_bridge/cv_bridge.hpp>
#include <functional>
#include <geometry_msgs/msg/pose_stamped.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <nav_msgs/msg/path.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/compressed_image.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>
#include <tf2_ros/transform_broadcaster.h>
#include <visualization_msgs/msg/marker.hpp>
#include <visualization_msgs/msg/marker_array.hpp>

class LIVMapper
{
public:
  using PointCloud2 = sensor_msgs::msg::PointCloud2;
  template <typename T> using Pub = typename rclcpp::Publisher<T>::SharedPtr;

  explicit LIVMapper(const rclcpp::Node::SharedPtr &node);
  ~LIVMapper();
  void initializeSubscribersAndPublishers();
  void initializeComponents();
  void initializeFiles();
  void run();
  void gravityAlignment();
  void handleFirstFrame();
  void stateEstimationAndMapping();
  void handleVIO();
  void handleLIO();
  void savePCD();
  void processImu();
  
  bool sync_packages(LidarMeasureGroup &meas);
  void prop_imu_once(StatesGroup &imu_prop_state, const double dt, V3D acc_avr, V3D angvel_avr);
  void imu_prop_callback();
  void transformLidar(const Eigen::Matrix3d rot, const Eigen::Vector3d t, const PointCloudXYZI::Ptr &input_cloud, PointCloudXYZI::Ptr &trans_cloud);
  void pointBodyToWorld(const PointType &pi, PointType &po);
  void RGBpointBodyLidarToIMU(PointType const *const pi, PointType *const po);
  void RGBpointBodyToWorld(PointType const *const pi, PointType *const po);
  void standard_pcl_cbk(const PointCloud2::ConstSharedPtr &msg);
  void imu_cbk(const sensor_msgs::msg::Imu::ConstSharedPtr &msg_in);
  void img_cbk(const sensor_msgs::msg::Image::ConstSharedPtr &msg_in);
  void compressed_img_cbk(const sensor_msgs::msg::CompressedImage::ConstSharedPtr &msg_in);
  void push_image(double stamp, const std::function<cv::Mat()> &decode);
  void crop_body(PointCloudXYZI::Ptr &cloud) const;
  void publish_img_rgb(const Pub<sensor_msgs::msg::Image> &pubImage, VIOManagerPtr vio_manager);
  void publish_frame_world(const Pub<PointCloud2> &pubLaserCloudFullRes, VIOManagerPtr vio_manager);
  void publish_visual_sub_map(const Pub<PointCloud2> &pubSubVisualMap);
  void publish_effect_world(const Pub<PointCloud2> &pubLaserCloudEffect, const std::vector<PointToPlane> &ptpl_list);
  void publish_odometry(const Pub<nav_msgs::msg::Odometry> &pubOdomAftMapped);
  void publish_mavros(const Pub<geometry_msgs::msg::PoseStamped> &mavros_pose_publisher);
  void publish_path(const Pub<nav_msgs::msg::Path> &pubPath);
  void readParameters(rclcpp::Node &nh);
  void loadCamera(rclcpp::Node &nh);
  template <typename T> void set_posestamp(T &out);
  template <typename T> void pointBodyToWorld(const Eigen::Matrix<T, 3, 1> &pi, Eigen::Matrix<T, 3, 1> &po);
  template <typename T> Eigen::Matrix<T, 3, 1> pointBodyToWorld(const Eigen::Matrix<T, 3, 1> &pi);
  cv::Mat getImageFromMsg(const sensor_msgs::msg::Image::ConstSharedPtr &img_msg);

  std::mutex mtx_buffer, mtx_buffer_imu_prop;
  std::condition_variable sig_buffer;

  SLAM_MODE slam_mode_;
  std::unordered_map<VOXEL_LOCATION, VoxelOctoTree *> voxel_map;
  
  string root_dir;
  string lid_topic, imu_topic, seq_name, img_topic;
  V3D extT;
  M3D extR;

  int feats_down_size = 0, max_iterations = 0;

  double res_mean_last = 0.05;
  double gyr_cov = 0, acc_cov = 0, inv_expo_cov = 0;
  double blind_rgb_points = 0.0;
  double last_timestamp_lidar = -1.0, last_timestamp_imu = -1.0, last_timestamp_img = -1.0;
  double filter_size_surf_min = 0;
  double filter_size_pcd = 0;
  double _first_lidar_time = 0.0;
  double match_time = 0, solve_time = 0, solve_const_H_time = 0;

  bool lidar_map_inited = false, pcd_save_en = false, img_save_en = false, pub_effect_point_en = false, pose_output_en = false, ros_driver_fix_en = false, hilti_en = false;
  int img_save_interval = 1, pcd_save_interval = -1, pcd_save_type = 0;
  int pub_scan_num = 1;

  StatesGroup imu_propagate, latest_ekf_state;

  bool new_imu = false, state_update_flg = false, imu_prop_enable = true, ekf_finish_once = false;
  deque<sensor_msgs::msg::Imu> prop_imu_buffer;
  sensor_msgs::msg::Imu newest_imu;
  double latest_ekf_time;
  nav_msgs::msg::Odometry imu_prop_odom;
  Pub<nav_msgs::msg::Odometry> pubImuPropOdom;
  double imu_time_offset = 0.0;
  double lidar_time_offset = 0.0;

  bool gravity_align_en = false, gravity_align_finished = false;

  bool sync_jump_flag = false;

  bool lidar_pushed = false, imu_en, gravity_est_en, flg_reset = false, ba_bg_est_en = true;
  bool dense_map_en = false;
  int img_en = 1, imu_int_frame = 3;
  bool normal_en = true;
  bool exposure_estimate_en = false;
  double exposure_time_init = 0.0;
  bool inverse_composition_en = false;
  bool raycast_en = false;
  int lidar_en = 1;
  bool is_first_frame = false;
  int grid_size = 5, patch_size = 8, grid_n_width = 0, grid_n_height = 17, patch_pyrimid_level = 3;
  double outlier_threshold = 1000.0;
  double plot_time = -10.0;
  int frame_cnt = 6;
  double img_time_offset = 0.0;
  deque<PointCloudXYZI::Ptr> lid_raw_data_buffer;
  deque<double> lid_header_time_buffer;
  deque<ImuConstPtr> imu_buffer;
  deque<cv::Mat> img_buffer;
  deque<double> img_time_buffer;
  vector<pointWithVar> _pv_list;
  vector<double> extrinT;
  vector<double> extrinR;
  vector<double> cameraextrinT;
  vector<double> cameraextrinR;
  double IMG_POINT_COV;

  PointCloudXYZI::Ptr visual_sub_map;
  PointCloudXYZI::Ptr feats_undistort;
  PointCloudXYZI::Ptr feats_down_body;
  PointCloudXYZI::Ptr feats_down_world;
  PointCloudXYZI::Ptr pcl_w_wait_pub;
  PointCloudXYZI::Ptr pcl_wait_pub;
  PointCloudXYZRGB::Ptr pcl_wait_save;
  PointCloudXYZI::Ptr pcl_wait_save_intensity;

  ofstream fout_pre, fout_out, fout_visual_pos, fout_lidar_pos, fout_points;

  pcl::VoxelGrid<PointType> downSizeFilterSurf;

  V3D euler_cur;

  LidarMeasureGroup LidarMeasures;
  StatesGroup _state;
  StatesGroup  state_propagat;

  nav_msgs::msg::Path path;
  nav_msgs::msg::Odometry odomAftMapped;
  geometry_msgs::msg::Quaternion geoQuat;
  geometry_msgs::msg::PoseStamped msg_body_pose;

  PreprocessPtr p_pre;
  ImuProcessPtr p_imu;
  VoxelMapManagerPtr voxelmap_manager;
  VIOManagerPtr vio_manager;

  rclcpp::Node::SharedPtr node_;
  std::unique_ptr<tf2_ros::TransformBroadcaster> tf_broadcaster;
  std::string world_frame = "camera_init", body_frame = "aft_mapped";
  bool tf_en = true, img_compressed = false, reliable_sensors = false, debug_log_en = true;
  bool crop_en = false;
  V3D crop_min, crop_max;
  rclcpp::SubscriptionBase::SharedPtr sub_pcl;
  rclcpp::SubscriptionBase::SharedPtr sub_imu;
  rclcpp::SubscriptionBase::SharedPtr sub_img;
  Pub<PointCloud2> pubLaserCloudFullRes;
  Pub<PointCloud2> pubLaserCloudRGB;
  Pub<PointCloud2> pubSubVisualMap;
  Pub<PointCloud2> pubLaserCloudEffect;
  Pub<nav_msgs::msg::Odometry> pubOdomAftMapped;
  Pub<nav_msgs::msg::Path> pubPath;
  Pub<sensor_msgs::msg::Image> pubImage;
  Pub<geometry_msgs::msg::PoseStamped> mavros_pose_publisher;
  rclcpp::TimerBase::SharedPtr imu_prop_timer;

  int frame_num = 0;
  double aver_time_consu = 0;
  double aver_time_icp = 0;
  double aver_time_map_inre = 0;
  bool colmap_output_en = false;
};
#endif