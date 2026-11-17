#include "go2_terrain_planner/elevation_planner.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace go2_terrain_planner
{
namespace
{

struct OpenNode
{
  int index{0};
  double f{0.0};
  bool operator<(const OpenNode & other) const
  {
    return f > other.f;
  }
};

constexpr int kCountCap = 48;

}  // namespace

ElevationMap::ElevationMap(double resolution, double half_size)
: resolution_(resolution > 1e-3 ? resolution : 0.1)
{
  width_ = std::max(1, static_cast<int>(std::lround((2.0 * std::abs(half_size)) / resolution_)));
  origin_ = -0.5 * static_cast<double>(width_) * resolution_;
  cells_.assign(static_cast<std::size_t>(width_ * width_), Cell{});
}

void ElevationMap::set_min_count(int min_count)
{
  min_count_ = std::max(1, min_count);
}

void ElevationMap::set_obstacle_rise(double rise)
{
  obstacle_rise_ = std::max(band_, rise);
}

void ElevationMap::begin_scan()
{
  ++generation_;
}

void ElevationMap::accumulate(float & mean, int & count, double z)
{
  if (count <= 0) {
    mean = static_cast<float>(z);
    count = 1;
    return;
  }
  const int weight = std::min(count, kCountCap);
  mean = static_cast<float>((static_cast<double>(mean) * weight + z) / (weight + 1));
  if (count < kCountCap) {
    ++count;
  }
}

void ElevationMap::decay_cell(Cell & cell)
{
  if (cell.gen == generation_) {
    return;
  }
  cell.low_n -= cell.low_n / 4;
  cell.high_n -= cell.high_n / 4;
  cell.mid_n -= cell.mid_n / 4;
  cell.gen = generation_;
}

void ElevationMap::insert(const Eigen::Vector3d & point)
{
  if (!point.allFinite()) {
    return;
  }
  int ix = 0;
  int iy = 0;
  if (!world_to_cell(point.x(), point.y(), ix, iy)) {
    return;
  }
  Cell & cell = cells_[static_cast<std::size_t>(index_of(ix, iy))];
  const double z = point.z();
  decay_cell(cell);

  if (cell.low_n > 0 && z > static_cast<double>(cell.low_z) + overhead_) {
    return;
  }

  if (cell.low_n == 0 && cell.high_n == 0) {
    accumulate(cell.low_z, cell.low_n, z);
    return;
  }
  if (cell.low_n > 0 && std::abs(z - cell.low_z) <= band_) {
    accumulate(cell.low_z, cell.low_n, z);
  } else if (cell.high_n > 0 && std::abs(z - cell.high_z) <= band_) {
    accumulate(cell.high_z, cell.high_n, z);
  } else if (cell.low_n > 0 && z < static_cast<double>(cell.low_z) - band_) {
    if (cell.high_n == 0) {
      cell.high_z = cell.low_z;
      cell.high_n = cell.low_n;
    } else {
      cell.mid_n = std::min(kCountCap, cell.mid_n + cell.low_n);
    }
    cell.low_z = static_cast<float>(z);
    cell.low_n = 1;
  } else if (cell.high_n == 0) {
    accumulate(cell.high_z, cell.high_n, z);
  } else if (z > static_cast<double>(cell.high_z) + band_) {
    cell.mid_n = std::min(kCountCap, cell.mid_n + cell.high_n);
    cell.high_z = static_cast<float>(z);
    cell.high_n = 1;
  } else {
    cell.mid_n = std::min(kCountCap, cell.mid_n + 1);
  }

  if (cell.low_n > 0 && cell.high_n > 0 && cell.high_z < cell.low_z) {
    std::swap(cell.low_z, cell.high_z);
    std::swap(cell.low_n, cell.high_n);
  }
  if (cell.low_n > 0 && cell.high_n > 0 &&
    static_cast<double>(cell.high_z - cell.low_z) >= overhead_)
  {
    cell.high_n = 0;
    cell.mid_n = 0;
  }
}

void ElevationMap::clear()
{
  cells_.assign(cells_.size(), Cell{});
}

void ElevationMap::reproject(const Eigen::Isometry3d & delta)
{
  std::vector<Cell> shifted(cells_.size());
  for (int iy = 0; iy < width_; ++iy) {
    for (int ix = 0; ix < width_; ++ix) {
      const Cell & cell = cells_[static_cast<std::size_t>(index_of(ix, iy))];
      if (cell.low_n <= 0 && cell.high_n <= 0) {
        continue;
      }
      const double anchor = cell.low_n > 0 ? cell.low_z : cell.high_z;
      const Eigen::Vector2d xy = center(ix, iy);
      const Eigen::Vector3d moved = delta * Eigen::Vector3d(xy.x(), xy.y(), anchor);
      int nx = 0;
      int ny = 0;
      if (!world_to_cell(moved.x(), moved.y(), nx, ny)) {
        continue;
      }
      Cell & dest = shifted[static_cast<std::size_t>(index_of(nx, ny))];
      if (dest.low_n > 0 || dest.high_n > 0) {
        continue;
      }
      dest = cell;
      const float lift = static_cast<float>(moved.z() - anchor);
      if (dest.low_n > 0) {
        dest.low_z += lift;
      }
      if (dest.high_n > 0) {
        dest.high_z += lift;
      }
    }
  }
  cells_.swap(shifted);
}

ElevationMap::Stance ElevationMap::stance_of(const Cell & cell) const
{
  const bool low_ok = cell.low_n >= min_count_;
  const bool high_ok = cell.high_n >= min_count_;
  if (!low_ok && !high_ok) {
    return Stance::kNone;
  }
  if (!high_ok) {
    return Stance::kLow;
  }
  if (!low_ok) {
    return Stance::kUpper;
  }
  const double gap = static_cast<double>(cell.high_z - cell.low_z);
  if (gap <= obstacle_rise_) {
    return Stance::kUpper;
  }
  if (cell.high_n * 2 >= cell.low_n || cell.mid_n * 2 >= cell.low_n) {
    return Stance::kNone;
  }
  return Stance::kLow;
}

SurfacePath ElevationMap::plan(
  const Eigen::Vector2d & start, const Eigen::Vector2d & goal, const PlanLimits & limits) const
{
  SurfacePath result;
  int start_x = 0;
  int start_y = 0;
  int goal_x = 0;
  int goal_y = 0;
  const double z_min = limits.start_z - limits.max_stance_drop;
  const double z_max = limits.start_z - limits.min_stance_drop;
  if (!snap(start, limits.snap_radius, start_x, start_y, limits.constrain_start_height, z_min, z_max) ||
    !snap(goal, limits.snap_radius, goal_x, goal_y, false, 0.0, 0.0))
  {
    return result;
  }
  if (!stance_fits(start_x, start_y, limits)) {
    return result;
  }

  const int start_index = index_of(start_x, start_y);
  const int goal_index = index_of(goal_x, goal_y);
  const auto append_cell = [this, &result](int ix, int iy) {
      const Eigen::Vector2d xy = center(ix, iy);
      result.path.emplace_back(xy.x(), xy.y(), height(ix, iy));
    };
  if (start_index == goal_index) {
    result.success = true;
    append_cell(start_x, start_y);
    return result;
  }

  const int cell_count = width_ * width_;
  const double infinity = std::numeric_limits<double>::infinity();
  std::vector<double> g_score(static_cast<std::size_t>(cell_count), infinity);
  std::vector<int> parent(static_cast<std::size_t>(cell_count), -1);
  std::vector<char> closed(static_cast<std::size_t>(cell_count), 0);
  std::priority_queue<OpenNode> open;
  g_score[static_cast<std::size_t>(start_index)] = 0.0;
  open.push(OpenNode{start_index, 0.0});

  const int neighbors[8][2] = {
    {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

  while (!open.empty()) {
    const OpenNode current = open.top();
    open.pop();
    if (closed[static_cast<std::size_t>(current.index)]) {
      continue;
    }
    closed[static_cast<std::size_t>(current.index)] = 1;
    if (current.index == goal_index) {
      break;
    }
    const int ix = current.index % width_;
    const int iy = current.index / width_;
    for (const auto & offset : neighbors) {
      const int next_x = ix + offset[0];
      const int next_y = iy + offset[1];
      if (!stance_fits(next_x, next_y, limits)) {
        continue;
      }
      if (offset[0] != 0 && offset[1] != 0) {
        const int side_ax = ix + offset[0];
        const int side_ay = iy;
        const int side_bx = ix;
        const int side_by = iy + offset[1];
        if (!surface(side_ax, side_ay) || !surface(side_bx, side_by)) {
          continue;
        }
        const double here = height(ix, iy);
        if (std::abs(height(side_ax, side_ay) - here) > limits.max_step_height ||
          std::abs(height(side_bx, side_by) - here) > limits.max_step_height)
        {
          continue;
        }
      }
      const int next_index = index_of(next_x, next_y);
      if (closed[static_cast<std::size_t>(next_index)]) {
        continue;
      }
      const double rise = std::abs(height(next_x, next_y) - height(ix, iy));
      if (rise > limits.max_step_height) {
        continue;
      }
      const double horizontal = resolution_ * std::hypot(offset[0], offset[1]);
      const double penalty = limits.max_step_height > 1e-9 ?
        limits.step_weight * rise / limits.max_step_height : 0.0;
      const double tentative = g_score[static_cast<std::size_t>(current.index)] +
        horizontal * (1.0 + penalty);
      if (tentative + 1e-12 >= g_score[static_cast<std::size_t>(next_index)]) {
        continue;
      }
      g_score[static_cast<std::size_t>(next_index)] = tentative;
      parent[static_cast<std::size_t>(next_index)] = current.index;
      const double heuristic = resolution_ * std::hypot(next_x - goal_x, next_y - goal_y);
      open.push(OpenNode{next_index, tentative + heuristic});
    }
  }

  if (!std::isfinite(g_score[static_cast<std::size_t>(goal_index)])) {
    return result;
  }
  std::vector<int> chain;
  for (int index = goal_index; index >= 0; index = parent[static_cast<std::size_t>(index)]) {
    chain.push_back(index);
    if (index == start_index) {
      break;
    }
  }
  result.success = true;
  result.path.reserve(chain.size());
  for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
    append_cell(*it % width_, *it / width_);
  }
  return result;
}

void ElevationMap::visit_surfaces(
  const std::function<void(double x, double y, double z)> & visitor) const
{
  for (int iy = 0; iy < width_; ++iy) {
    for (int ix = 0; ix < width_; ++ix) {
      if (!surface(ix, iy)) {
        continue;
      }
      const Eigen::Vector2d xy = center(ix, iy);
      visitor(xy.x(), xy.y(), height(ix, iy));
    }
  }
}

double ElevationMap::resolution() const
{
  return resolution_;
}

int ElevationMap::width() const
{
  return width_;
}

bool ElevationMap::in_bounds(int ix, int iy) const
{
  return ix >= 0 && iy >= 0 && ix < width_ && iy < width_;
}

bool ElevationMap::world_to_cell(double x, double y, int & ix, int & iy) const
{
  ix = static_cast<int>(std::floor((x - origin_) / resolution_));
  iy = static_cast<int>(std::floor((y - origin_) / resolution_));
  return in_bounds(ix, iy);
}

bool ElevationMap::snap(
  const Eigen::Vector2d & query, double radius, int & ix, int & iy,
  bool limit_height, double z_min, double z_max) const
{
  if (!(radius > 0.0) || !query.allFinite()) {
    return false;
  }
  const int window = static_cast<int>(std::ceil(radius / resolution_));
  int query_x = 0;
  int query_y = 0;
  world_to_cell(query.x(), query.y(), query_x, query_y);
  bool found = false;
  double best = radius;
  for (int dy = -window; dy <= window; ++dy) {
    for (int dx = -window; dx <= window; ++dx) {
      const int cell_x = query_x + dx;
      const int cell_y = query_y + dy;
      if (!surface(cell_x, cell_y)) {
        continue;
      }
      const double cell_z = height(cell_x, cell_y);
      if (limit_height && (cell_z < z_min || cell_z > z_max)) {
        continue;
      }
      const Eigen::Vector2d xy = center(cell_x, cell_y);
      const double distance = (xy - query).norm();
      if (distance <= radius && (!found || distance < best)) {
        best = distance;
        ix = cell_x;
        iy = cell_y;
        found = true;
      }
    }
  }
  return found;
}

bool ElevationMap::surface(int ix, int iy) const
{
  if (!in_bounds(ix, iy)) {
    return false;
  }
  return stance_of(cells_[static_cast<std::size_t>(index_of(ix, iy))]) != Stance::kNone;
}

bool ElevationMap::stance_fits(int ix, int iy, const PlanLimits & limits) const
{
  if (!surface(ix, iy)) {
    return false;
  }
  if (!(limits.footprint_radius > 1e-6)) {
    return true;
  }
  const double stance = height(ix, iy);
  const int window = static_cast<int>(std::ceil(limits.footprint_radius / resolution_));
  for (int dy = -window; dy <= window; ++dy) {
    for (int dx = -window; dx <= window; ++dx) {
      if (std::hypot(dx, dy) * resolution_ > limits.footprint_radius) {
        continue;
      }
      const int cell_x = ix + dx;
      const int cell_y = iy + dy;
      if (!surface(cell_x, cell_y)) {
        return false;
      }
      if (std::abs(height(cell_x, cell_y) - stance) > limits.max_step_height) {
        return false;
      }
    }
  }
  return true;
}

double ElevationMap::height(int ix, int iy) const
{
  const Cell & cell = cells_[static_cast<std::size_t>(index_of(ix, iy))];
  if (stance_of(cell) == Stance::kUpper) {
    return cell.high_z;
  }
  return cell.low_z;
}

Eigen::Vector2d ElevationMap::center(int ix, int iy) const
{
  return Eigen::Vector2d(
    origin_ + (static_cast<double>(ix) + 0.5) * resolution_,
    origin_ + (static_cast<double>(iy) + 0.5) * resolution_);
}

int ElevationMap::index_of(int ix, int iy) const
{
  return iy * width_ + ix;
}

}  // namespace go2_terrain_planner
