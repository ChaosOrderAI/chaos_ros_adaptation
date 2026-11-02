#include "chaos_sim/world.hpp"

#include <mujoco/mujoco.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>

namespace chaos::sim {
namespace {
void require(bool condition, const std::string& message) {
  if (!condition) throw std::invalid_argument(message);
}

bool finite(const mjtNum* values, int size) {
  for (int i = 0; i < size; ++i) {
    if (!std::isfinite(values[i])) return false;
  }
  return true;
}
}  // namespace

struct World::Impl {
  std::unique_ptr<mjModel, decltype(&mj_deleteModel)> model{nullptr, mj_deleteModel};
  std::unique_ptr<mjData, decltype(&mj_deleteData)> data{nullptr, mj_deleteData};
  double kp, kd;
  int base_body = -1;
  int home_key = -1;
  std::vector<std::string> names;
  std::unordered_map<std::string, std::size_t> indices;
  std::vector<int> joints, qpos_indices, dof_indices;
  std::vector<double> targets, candidate;
  std::vector<std::uint8_t> seen;

  explicit Impl(const WorldConfig& config) : kp(config.kp), kd(config.kd) {
    require(!config.model_path.empty(), "model_path must not be empty");
    require(std::isfinite(kp) && kp >= 0 && std::isfinite(kd) && kd >= 0,
            "kp and kd must be finite and nonnegative");
    char error[2048]{};
    std::unique_ptr<mjSpec, decltype(&mj_deleteSpec)> spec{
        mj_parseXML(config.model_path.c_str(), nullptr, error, sizeof(error)), mj_deleteSpec};
    if (!spec) throw std::runtime_error("Cannot parse model: " + std::string(error));
    // Set before the first compile: recompiling a visual spec after changing this
    // flag can invalidate mesh/material indices.
    spec->compiler.discardvisual = config.discard_visual;
    model.reset(mj_compile(spec.get(), nullptr));
    if (!model) throw std::runtime_error("Cannot compile model: " + std::string(mjs_getError(spec.get())));
    spec.reset();

    const auto* m = model.get();
    require(std::isfinite(m->opt.timestep) && m->opt.timestep > 0,
            "Model timestep must be finite and positive");
    require(m->nu > 0 && m->na == 0, "Model must have torque motors without activation dynamics");
    int free_count = 0;
    for (int j = 0; j < m->njnt; ++j) {
      if (m->jnt_type[j] == mjJNT_FREE) {
        ++free_count;
        base_body = m->jnt_bodyid[j];
        require(m->body_parentid[base_body] == 0 && m->body_jntnum[base_body] == 1,
                "Free joint must belong to a root body");
      } else {
        require(m->jnt_type[j] == mjJNT_HINGE || m->jnt_type[j] == mjJNT_SLIDE,
                "Only free-root and hinge/slide joints are supported");
      }
    }
    require(free_count == 1, "Model must have exactly one floating root");
    require(!(m->opt.disableflags & mjDSBL_ACTUATION) && m->opt.disableactuator == 0,
            "Model must enable all actuators");
    names.reserve(m->nu);
    indices.reserve(m->nu);
    joints.reserve(m->nu);
    qpos_indices.reserve(m->nu);
    dof_indices.reserve(m->nu);
    for (int a = 0; a < m->nu; ++a) {
      require(m->actuator_trntype[a] == mjTRN_JOINT && m->actuator_trnid[2 * a + 1] == -1 &&
                  m->actuator_dyntype[a] == mjDYN_NONE &&
                  m->actuator_gaintype[a] == mjGAIN_FIXED &&
                  m->actuator_biastype[a] == mjBIAS_NONE &&
                  m->actuator_gainprm[mjNGAIN * a] == 1,
              "Actuators must be direct fixed-gain unit torque motors");
      for (int k = 0; k < 6; ++k) {
        require(m->actuator_gear[6 * a + k] == (k == 0 ? 1 : 0),
                "Actuator gear must be [1,0,0,0,0,0]");
      }
      const int joint = m->actuator_trnid[2 * a];
      require(joint >= 0 && joint < m->njnt &&
                  (m->jnt_type[joint] == mjJNT_HINGE || m->jnt_type[joint] == mjJNT_SLIDE),
              "Actuator must drive a hinge/slide joint");
      const char* name = mj_id2name(m, mjOBJ_JOINT, joint);
      require(name && *name, "Actuated joints must be named");
      require(indices.emplace(name, a).second, "Each joint must have exactly one actuator");
      if (m->jnt_limited[joint]) {
        require(finite(m->jnt_range + 2 * joint, 2) &&
                    m->jnt_range[2 * joint] < m->jnt_range[2 * joint + 1],
                "Joint range must be finite and ordered");
      }
      require(m->actuator_ctrllimited[a],
              "Actuators must declare a finite control range");
      require(finite(m->actuator_ctrlrange + 2 * a, 2) &&
                  m->actuator_ctrlrange[2 * a] < m->actuator_ctrlrange[2 * a + 1],
              "Motor control range must be finite and ordered");
      names.emplace_back(name);
      joints.push_back(joint);
      qpos_indices.push_back(m->jnt_qposadr[joint]);
      dof_indices.push_back(m->jnt_dofadr[joint]);
    }
    targets.resize(m->nu);
    candidate.resize(m->nu);
    seen.resize(m->nu);
    home_key = mj_name2id(m, mjOBJ_KEY, "home");
    data.reset(mj_makeData(m));
    if (!data) throw std::runtime_error("Cannot allocate MuJoCo data");
  }

  void checkFinite() const {
    const auto* m = model.get();
    const auto* d = data.get();
    if (!std::isfinite(d->time) || !finite(d->qpos, m->nq) ||
        !finite(d->qvel, m->nv) || !finite(d->qacc, m->nv) ||
        !finite(d->actuator_force, m->nu)) {
      throw std::runtime_error("Non-finite MuJoCo state");
    }
  }
};

World::World(const WorldConfig& config) : impl_(std::make_unique<Impl>(config)) { reset(); }
World::~World() = default;
const std::vector<std::string>& World::jointNames() const { return impl_->names; }
double World::timestep() const { return impl_->model->opt.timestep; }
std::size_t World::modelBytes() const { return impl_->model->nbuffer; }
std::size_t World::arenaBytes() const { return impl_->data->narena; }

bool World::setTargets(const std::vector<std::string>& names,
                       const std::vector<double>& positions, std::string& error) {
  auto& w = *impl_;
  if (names.empty() || names.size() != positions.size() || names.size() > w.names.size()) {
    error = "Targets require matching nonempty name/position arrays of at most nu entries";
    return false;
  }
  std::copy(w.targets.begin(), w.targets.end(), w.candidate.begin());
  std::fill(w.seen.begin(), w.seen.end(), 0);
  for (std::size_t i = 0; i < names.size(); ++i) {
    const auto found = w.indices.find(names[i]);
    if (found == w.indices.end()) { error = "Unknown joint: " + names[i]; return false; }
    const auto a = found->second;
    if (w.seen[a]) { error = "Duplicate joint: " + names[i]; return false; }
    if (!std::isfinite(positions[i])) { error = "Non-finite target: " + names[i]; return false; }
    const int j = w.joints[a];
    if (w.model->jnt_limited[j] &&
        (positions[i] < w.model->jnt_range[2 * j] || positions[i] > w.model->jnt_range[2 * j + 1])) {
      error = "Target outside joint range: " + names[i];
      return false;
    }
    w.seen[a] = 1;
    w.candidate[a] = positions[i];
  }
  w.targets.swap(w.candidate);
  error.clear();
  return true;
}

void World::hold() {
  auto& w = *impl_;
  w.checkFinite();
  for (std::size_t a = 0; a < w.targets.size(); ++a) w.targets[a] = w.data->qpos[w.qpos_indices[a]];
}

void World::reset() {
  auto& w = *impl_;
  if (w.home_key >= 0) mj_resetDataKeyframe(w.model.get(), w.data.get(), w.home_key);
  else mj_resetData(w.model.get(), w.data.get());
  w.data->time = 0;
  mju_zero(w.data->qvel, w.model->nv);
  mju_zero(w.data->qacc, w.model->nv);
  mju_zero(w.data->qfrc_applied, w.model->nv);
  mju_zero(w.data->xfrc_applied, 6 * w.model->nbody);
  mju_zero(w.data->ctrl, w.model->nu);
  mj_forward(w.model.get(), w.data.get());
  hold();
}

void World::step() {
  auto& w = *impl_;
  w.checkFinite();
  for (std::size_t a = 0; a < w.targets.size(); ++a) {
    double torque = w.kp * (w.targets[a] - w.data->qpos[w.qpos_indices[a]]) -
                    w.kd * w.data->qvel[w.dof_indices[a]];
    if (!std::isfinite(torque)) throw std::runtime_error("Non-finite PD torque");
    if (w.model->actuator_ctrllimited[a]) {
      torque = std::clamp(torque, w.model->actuator_ctrlrange[2 * a],
                          w.model->actuator_ctrlrange[2 * a + 1]);
    }
    w.data->ctrl[a] = torque;
  }
  std::array<int, 4> warnings{};
  for (int i = 0; i < 4; ++i) warnings[i] = w.data->warning[mjWARN_BADQPOS + i].number;
  mj_step(w.model.get(), w.data.get());
  w.checkFinite();
  for (int i = 0; i < 4; ++i) {
    if (warnings[i] != w.data->warning[mjWARN_BADQPOS + i].number) {
      throw std::runtime_error("MuJoCo rejected unstable/non-finite dynamics");
    }
  }
}

void World::readState(State& state) {
  auto& w = *impl_;
  w.checkFinite();
  mj_forward(w.model.get(), w.data.get());
  w.checkFinite();
  state.position.resize(w.names.size());
  state.velocity.resize(w.names.size());
  state.effort.resize(w.names.size());
  for (std::size_t a = 0; a < w.names.size(); ++a) {
    state.position[a] = w.data->qpos[w.qpos_indices[a]];
    state.velocity[a] = w.data->qvel[w.dof_indices[a]];
    state.effort[a] = w.data->qfrc_actuator[w.dof_indices[a]];
  }
  std::copy_n(w.data->xpos + 3 * w.base_body, 3, state.base_position.begin());
  const auto* quat = w.data->xquat + 4 * w.base_body;
  state.base_orientation = {quat[1], quat[2], quat[3], quat[0]};
  mjtNum velocity[6]{};
  mj_objectVelocity(w.model.get(), w.data.get(), mjOBJ_XBODY, w.base_body, velocity, 1);
  for (int i = 0; i < 3; ++i) {
    state.base_twist[i] = velocity[i + 3];
    state.base_twist[i + 3] = velocity[i];
  }
  state.simulation_time = w.data->time;
}

}  // namespace chaos::sim
