#ifndef GO2_TERRAIN_PLANNER__ELEVATION_PLANNER_HPP_
#define GO2_TERRAIN_PLANNER__ELEVATION_PLANNER_HPP_

#include <Eigen/Geometry>

#include <functional>
#include <vector>

namespace go2_terrain_planner
{

struct PlanLimits
{
  // Largest height change accepted between neighboring cells. This is a robot
  // parameter, not a measured property of a particular stair.
  double max_step_height{0.16};
  // Start and goal snap to a surface cell inside this radius.
  double snap_radius{0.45};
  // Extra cost for climbing or descending, relative to horizontal distance.
  double step_weight{2.0};
  // Body radius that must also be a surface. Zero checks only the center cell.
  double footprint_radius{0.0};
  // When set, the start cell must lie this far below the base pose. The base
  // frame is the IMU, so the floor is below it; a cell above the base is not
  // a stance. Goal snap ignores this, because a 2D click has no useful z.
  bool constrain_start_height{false};
  double start_z{0.0};
  double min_stance_drop{0.02};
  double max_stance_drop{0.80};
};

struct SurfacePath
{
  bool success{false};
  std::vector<Eigen::Vector3d> path;
};

// Fixed 2.5D elevation grid. Each cell keeps a low cluster and an optional
// high cluster. The walkable height is the floor, or the upper tread when the
// two clusters are only one step apart. A tall dense column is not a surface.
// Unknown cells are not surfaces.
class ElevationMap
{
public:
  ElevationMap(double resolution, double half_size);

  void set_min_count(int min_count);
  void set_obstacle_rise(double rise);
  void begin_scan();
  void insert(const Eigen::Vector3d & point);
  void clear();
  // Move every stored cluster by delta. Used when map → odom jumps.
  void reproject(const Eigen::Isometry3d & delta);

  SurfacePath plan(
    const Eigen::Vector2d & start, const Eigen::Vector2d & goal, const PlanLimits & limits) const;

  void visit_surfaces(
    const std::function<void(double x, double y, double z)> & visitor) const;

  double resolution() const;
  int width() const;

private:
  struct Cell
  {
    float low_z{0.0F};
    float high_z{0.0F};
    int low_n{0};
    int high_n{0};
    int mid_n{0};
    int gen{0};
  };

  enum class Stance
  {
    kNone,
    kLow,
    kUpper
  };

  void decay_cell(Cell & cell);
  void accumulate(float & mean, int & count, double z);
  Stance stance_of(const Cell & cell) const;
  bool in_bounds(int ix, int iy) const;
  bool world_to_cell(double x, double y, int & ix, int & iy) const;
  bool snap(
    const Eigen::Vector2d & query, double radius, int & ix, int & iy,
    bool limit_height, double z_min, double z_max) const;
  bool surface(int ix, int iy) const;
  bool stance_fits(int ix, int iy, const PlanLimits & limits) const;
  double height(int ix, int iy) const;
  Eigen::Vector2d center(int ix, int iy) const;
  int index_of(int ix, int iy) const;

  double resolution_{0.1};
  double origin_{0.0};
  double band_{0.08};
  double obstacle_rise_{0.16};
  double overhead_{1.5};
  int width_{0};
  int min_count_{1};
  int generation_{0};
  std::vector<Cell> cells_;
};

}  // namespace go2_terrain_planner

#endif  // GO2_TERRAIN_PLANNER__ELEVATION_PLANNER_HPP_
