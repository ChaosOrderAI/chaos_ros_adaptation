#include "go2_terrain_planner/elevation_planner.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace
{

void fill_patch(
  go2_terrain_planner::ElevationMap & map,
  double x0, double x1, double y0, double y1, double z)
{
  const double step = 0.05;
  for (double y = y0; y <= y1; y += step) {
    for (double x = x0; x <= x1; x += step) {
      map.insert(Eigen::Vector3d(x, y, z));
    }
  }
}

go2_terrain_planner::PlanLimits limits_with_step(double max_step)
{
  go2_terrain_planner::PlanLimits limits;
  limits.max_step_height = max_step;
  limits.snap_radius = 0.4;
  limits.step_weight = 2.0;
  return limits;
}

}  // namespace

TEST(ElevationPlanner, StairPathRisesWithTheTreads)
{
  go2_terrain_planner::ElevationMap map(0.1, 5.0);
  const double rise = 0.15;
  const double run = 0.30;
  for (int step = 0; step < 5; ++step) {
    const double x0 = step * run;
    fill_patch(map, x0 + 0.02, x0 + run - 0.03, -0.2, 0.2, step * rise);
  }
  // A few leg-height returns on the first tread must not lift that cell.
  for (int i = 0; i < 4; ++i) {
    map.insert(Eigen::Vector3d(0.15, 0.0, 0.55));
  }

  const auto planned = map.plan(
    Eigen::Vector2d(0.15, 0.0), Eigen::Vector2d(1.35, 0.0), limits_with_step(0.16));
  ASSERT_TRUE(planned.success);
  ASSERT_GE(planned.path.size(), 2U);
  EXPECT_LT(planned.path.front().z(), 0.05);
  EXPECT_GT(planned.path.back().z(), 0.55);
  for (std::size_t i = 1; i < planned.path.size(); ++i) {
    const double dz = planned.path[i].z() - planned.path[i - 1].z();
    EXPECT_GE(dz, -1e-6);
    EXPECT_LE(std::abs(dz), 0.16 + 1e-6);
  }
}

TEST(ElevationPlanner, WallTallerThanOneStepBlocksThePath)
{
  go2_terrain_planner::ElevationMap map(0.1, 5.0);
  fill_patch(map, 0.02, 1.97, -0.2, 0.2, 0.0);
  for (double z = 0.2; z <= 1.0; z += 0.2) {
    fill_patch(map, 0.92, 1.17, -0.2, 0.2, z);
  }

  const auto planned = map.plan(
    Eigen::Vector2d(0.3, 0.0), Eigen::Vector2d(1.7, 0.0), limits_with_step(0.16));
  EXPECT_FALSE(planned.success);
  EXPECT_TRUE(planned.path.empty());
}

TEST(ElevationPlanner, UnknownGoalIsNotReached)
{
  go2_terrain_planner::ElevationMap map(0.1, 5.0);
  fill_patch(map, -0.2, 0.2, -0.2, 0.2, 0.0);
  const auto planned = map.plan(
    Eigen::Vector2d(0.0, 0.0), Eigen::Vector2d(3.0, 0.0), limits_with_step(0.16));
  EXPECT_FALSE(planned.success);
}

TEST(ElevationPlanner, StartSnapRejectsASurfaceAboveTheBase)
{
  go2_terrain_planner::ElevationMap map(0.1, 5.0);
  fill_patch(map, -0.05, 0.15, -0.05, 0.05, 0.40);
  fill_patch(map, 0.30, 0.55, -0.10, 0.10, 0.0);
  auto limits = limits_with_step(0.16);
  limits.constrain_start_height = true;
  limits.start_z = 0.35;
  limits.min_stance_drop = 0.02;
  limits.max_stance_drop = 0.80;

  const auto planned = map.plan(
    Eigen::Vector2d(0.05, 0.0), Eigen::Vector2d(0.40, 0.0), limits);
  ASSERT_TRUE(planned.success);
  EXPECT_LT(planned.path.front().z(), 0.05);
}

TEST(ElevationPlanner, DiagonalDoesNotCutABlockedCorner)
{
  go2_terrain_planner::ElevationMap map(0.1, 5.0);
  map.insert(Eigen::Vector3d(0.05, 0.05, 0.0));
  map.insert(Eigen::Vector3d(0.15, 0.15, 0.0));
  const auto planned = map.plan(
    Eigen::Vector2d(0.05, 0.05), Eigen::Vector2d(0.15, 0.15), limits_with_step(0.16));
  EXPECT_FALSE(planned.success);
}

TEST(ElevationPlanner, FootprintRejectsAnIsolatedCell)
{
  go2_terrain_planner::ElevationMap map(0.1, 5.0);
  fill_patch(map, 0.02, 0.08, 0.02, 0.08, 0.0);
  auto limits = limits_with_step(0.16);
  limits.footprint_radius = 0.22;
  const auto planned = map.plan(
    Eigen::Vector2d(0.05, 0.05), Eigen::Vector2d(0.05, 0.05), limits);
  EXPECT_FALSE(planned.success);
}

TEST(ElevationPlanner, ReprojectMovesTheSurfaceWithTheMapFrame)
{
  go2_terrain_planner::ElevationMap map(0.1, 5.0);
  fill_patch(map, 0.95, 1.15, -0.05, 0.05, 0.20);
  Eigen::Isometry3d delta = Eigen::Isometry3d::Identity();
  delta.translation() = Eigen::Vector3d(1.0, 0.0, 0.0);
  map.reproject(delta);

  const auto moved = map.plan(
    Eigen::Vector2d(2.05, 0.0), Eigen::Vector2d(2.05, 0.0), limits_with_step(0.16));
  ASSERT_TRUE(moved.success);
  EXPECT_NEAR(moved.path.front().x(), 2.05, 0.11);
  EXPECT_NEAR(moved.path.front().z(), 0.20, 0.02);

  const auto stale = map.plan(
    Eigen::Vector2d(1.05, 0.0), Eigen::Vector2d(1.05, 0.0), limits_with_step(0.16));
  EXPECT_FALSE(stale.success);
}
