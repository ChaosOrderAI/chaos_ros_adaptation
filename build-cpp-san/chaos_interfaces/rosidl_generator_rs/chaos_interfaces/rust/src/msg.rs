#[cfg(feature = "serde")]
use serde::{Deserialize, Serialize};



// Corresponds to chaos_interfaces__msg__RobotState

// This struct is not documented.
#[allow(missing_docs)]

#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct RobotState {

    // This member is not documented.
    #[allow(missing_docs)]
    pub stamp: builtin_interfaces::msg::Time,


    // This member is not documented.
    #[allow(missing_docs)]
    pub asset_id: std::string::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub model: std::string::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub operational_state: std::string::String,

    /// Connected means recent adapter telemetry, not merely a live ROS node.
    pub connected: bool,


    // This member is not documented.
    #[allow(missing_docs)]
    pub ready: bool,


    // This member is not documented.
    #[allow(missing_docs)]
    pub pose_valid: bool,


    // This member is not documented.
    #[allow(missing_docs)]
    pub pose: geometry_msgs::msg::PoseStamped,


    // This member is not documented.
    #[allow(missing_docs)]
    pub twist: geometry_msgs::msg::TwistStamped,

    /// [0,1], NaN when unknown. Never substitute missing data with zero.
    pub battery_percentage: f32,


    // This member is not documented.
    #[allow(missing_docs)]
    pub detail: std::string::String,

}



impl Default for RobotState {
  fn default() -> Self {
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::msg::rmw::RobotState::default())
  }
}

impl rosidl_runtime_rs::Message for RobotState {
  type RmwMsg = super::msg::rmw::RobotState;

  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> {
    match msg_cow {
      std::borrow::Cow::Owned(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        stamp: builtin_interfaces::msg::Time::into_rmw_message(std::borrow::Cow::Owned(msg.stamp)).into_owned(),
        asset_id: msg.asset_id.as_str().into(),
        model: msg.model.as_str().into(),
        operational_state: msg.operational_state.as_str().into(),
        connected: msg.connected,
        ready: msg.ready,
        pose_valid: msg.pose_valid,
        pose: geometry_msgs::msg::PoseStamped::into_rmw_message(std::borrow::Cow::Owned(msg.pose)).into_owned(),
        twist: geometry_msgs::msg::TwistStamped::into_rmw_message(std::borrow::Cow::Owned(msg.twist)).into_owned(),
        battery_percentage: msg.battery_percentage,
        detail: msg.detail.as_str().into(),
      }),
      std::borrow::Cow::Borrowed(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        stamp: builtin_interfaces::msg::Time::into_rmw_message(std::borrow::Cow::Borrowed(&msg.stamp)).into_owned(),
        asset_id: msg.asset_id.as_str().into(),
        model: msg.model.as_str().into(),
        operational_state: msg.operational_state.as_str().into(),
      connected: msg.connected,
      ready: msg.ready,
      pose_valid: msg.pose_valid,
        pose: geometry_msgs::msg::PoseStamped::into_rmw_message(std::borrow::Cow::Borrowed(&msg.pose)).into_owned(),
        twist: geometry_msgs::msg::TwistStamped::into_rmw_message(std::borrow::Cow::Borrowed(&msg.twist)).into_owned(),
      battery_percentage: msg.battery_percentage,
        detail: msg.detail.as_str().into(),
      })
    }
  }

  fn from_rmw_message(msg: Self::RmwMsg) -> Self {
    Self {
      stamp: builtin_interfaces::msg::Time::from_rmw_message(msg.stamp),
      asset_id: msg.asset_id.to_string(),
      model: msg.model.to_string(),
      operational_state: msg.operational_state.to_string(),
      connected: msg.connected,
      ready: msg.ready,
      pose_valid: msg.pose_valid,
      pose: geometry_msgs::msg::PoseStamped::from_rmw_message(msg.pose),
      twist: geometry_msgs::msg::TwistStamped::from_rmw_message(msg.twist),
      battery_percentage: msg.battery_percentage,
      detail: msg.detail.to_string(),
    }
  }
}


// Corresponds to chaos_interfaces__msg__CommandResult

// This struct is not documented.
#[allow(missing_docs)]

#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct CommandResult {

    // This member is not documented.
    #[allow(missing_docs)]
    pub stamp: builtin_interfaces::msg::Time,


    // This member is not documented.
    #[allow(missing_docs)]
    pub request_id: std::string::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub success: bool,


    // This member is not documented.
    #[allow(missing_docs)]
    pub code: std::string::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub message: std::string::String,

}



impl Default for CommandResult {
  fn default() -> Self {
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::msg::rmw::CommandResult::default())
  }
}

impl rosidl_runtime_rs::Message for CommandResult {
  type RmwMsg = super::msg::rmw::CommandResult;

  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> {
    match msg_cow {
      std::borrow::Cow::Owned(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        stamp: builtin_interfaces::msg::Time::into_rmw_message(std::borrow::Cow::Owned(msg.stamp)).into_owned(),
        request_id: msg.request_id.as_str().into(),
        success: msg.success,
        code: msg.code.as_str().into(),
        message: msg.message.as_str().into(),
      }),
      std::borrow::Cow::Borrowed(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        stamp: builtin_interfaces::msg::Time::into_rmw_message(std::borrow::Cow::Borrowed(&msg.stamp)).into_owned(),
        request_id: msg.request_id.as_str().into(),
      success: msg.success,
        code: msg.code.as_str().into(),
        message: msg.message.as_str().into(),
      })
    }
  }

  fn from_rmw_message(msg: Self::RmwMsg) -> Self {
    Self {
      stamp: builtin_interfaces::msg::Time::from_rmw_message(msg.stamp),
      request_id: msg.request_id.to_string(),
      success: msg.success,
      code: msg.code.to_string(),
      message: msg.message.to_string(),
    }
  }
}


// Corresponds to chaos_interfaces__msg__VelocityCommand
/// ROS time of generation; frame_id must equal the asset's configured base frame.

#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct VelocityCommand {

    // This member is not documented.
    #[allow(missing_docs)]
    pub header: std_msgs::msg::Header,


    // This member is not documented.
    #[allow(missing_docs)]
    pub request_id: std::string::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub lease_token: std::string::String,

    /// Validity from header.stamp, bounded by asset command_timeout_s.
    pub valid_for_s: f64,


    // This member is not documented.
    #[allow(missing_docs)]
    pub velocity: geometry_msgs::msg::Twist,

}



impl Default for VelocityCommand {
  fn default() -> Self {
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::msg::rmw::VelocityCommand::default())
  }
}

impl rosidl_runtime_rs::Message for VelocityCommand {
  type RmwMsg = super::msg::rmw::VelocityCommand;

  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> {
    match msg_cow {
      std::borrow::Cow::Owned(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        header: std_msgs::msg::Header::into_rmw_message(std::borrow::Cow::Owned(msg.header)).into_owned(),
        request_id: msg.request_id.as_str().into(),
        lease_token: msg.lease_token.as_str().into(),
        valid_for_s: msg.valid_for_s,
        velocity: geometry_msgs::msg::Twist::into_rmw_message(std::borrow::Cow::Owned(msg.velocity)).into_owned(),
      }),
      std::borrow::Cow::Borrowed(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        header: std_msgs::msg::Header::into_rmw_message(std::borrow::Cow::Borrowed(&msg.header)).into_owned(),
        request_id: msg.request_id.as_str().into(),
        lease_token: msg.lease_token.as_str().into(),
      valid_for_s: msg.valid_for_s,
        velocity: geometry_msgs::msg::Twist::into_rmw_message(std::borrow::Cow::Borrowed(&msg.velocity)).into_owned(),
      })
    }
  }

  fn from_rmw_message(msg: Self::RmwMsg) -> Self {
    Self {
      header: std_msgs::msg::Header::from_rmw_message(msg.header),
      request_id: msg.request_id.to_string(),
      lease_token: msg.lease_token.to_string(),
      valid_for_s: msg.valid_for_s,
      velocity: geometry_msgs::msg::Twist::from_rmw_message(msg.velocity),
    }
  }
}


// Corresponds to chaos_interfaces__msg__ControlTrace
/// Runtime evidence for offline training; never a hardware execution confirmation.
/// Event publication time and the configured robot base frame.

#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct ControlTrace {

    // This member is not documented.
    #[allow(missing_docs)]
    pub header: std_msgs::msg::Header,


    // This member is not documented.
    #[allow(missing_docs)]
    pub asset_id: std::string::String,

    /// Strictly increasing within one runtime process, including failed publications.
    /// A discontinuity or reset invalidates any previously held training action.
    pub sequence: u64,


    // This member is not documented.
    #[allow(missing_docs)]
    pub monotonic_ns: u64,


    // This member is not documented.
    #[allow(missing_docs)]
    pub request_id: std::string::String,

    /// motion.velocity, motion.stop, navigation.pose, flight.takeoff, or flight.land.
    pub command: std::string::String,

    /// issued, rejected, or invalidated.
    pub stage: std::string::String,

    /// Only issued motion.velocity events may carry a usable velocity label.
    pub velocity_valid: bool,


    // This member is not documented.
    #[allow(missing_docs)]
    pub velocity: geometry_msgs::msg::Twist,

    /// Remaining seconds from header.stamp, bounded by command and lease expiration.
    /// Zero supplies no valid action interval. Invalidation always ends the interval.
    pub valid_for_s: f64,

    /// Machine-readable code only. No lease token, owner, or exception message.
    pub code: std::string::String,

}



impl Default for ControlTrace {
  fn default() -> Self {
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::msg::rmw::ControlTrace::default())
  }
}

impl rosidl_runtime_rs::Message for ControlTrace {
  type RmwMsg = super::msg::rmw::ControlTrace;

  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> {
    match msg_cow {
      std::borrow::Cow::Owned(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        header: std_msgs::msg::Header::into_rmw_message(std::borrow::Cow::Owned(msg.header)).into_owned(),
        asset_id: msg.asset_id.as_str().into(),
        sequence: msg.sequence,
        monotonic_ns: msg.monotonic_ns,
        request_id: msg.request_id.as_str().into(),
        command: msg.command.as_str().into(),
        stage: msg.stage.as_str().into(),
        velocity_valid: msg.velocity_valid,
        velocity: geometry_msgs::msg::Twist::into_rmw_message(std::borrow::Cow::Owned(msg.velocity)).into_owned(),
        valid_for_s: msg.valid_for_s,
        code: msg.code.as_str().into(),
      }),
      std::borrow::Cow::Borrowed(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        header: std_msgs::msg::Header::into_rmw_message(std::borrow::Cow::Borrowed(&msg.header)).into_owned(),
        asset_id: msg.asset_id.as_str().into(),
      sequence: msg.sequence,
      monotonic_ns: msg.monotonic_ns,
        request_id: msg.request_id.as_str().into(),
        command: msg.command.as_str().into(),
        stage: msg.stage.as_str().into(),
      velocity_valid: msg.velocity_valid,
        velocity: geometry_msgs::msg::Twist::into_rmw_message(std::borrow::Cow::Borrowed(&msg.velocity)).into_owned(),
      valid_for_s: msg.valid_for_s,
        code: msg.code.as_str().into(),
      })
    }
  }

  fn from_rmw_message(msg: Self::RmwMsg) -> Self {
    Self {
      header: std_msgs::msg::Header::from_rmw_message(msg.header),
      asset_id: msg.asset_id.to_string(),
      sequence: msg.sequence,
      monotonic_ns: msg.monotonic_ns,
      request_id: msg.request_id.to_string(),
      command: msg.command.to_string(),
      stage: msg.stage.to_string(),
      velocity_valid: msg.velocity_valid,
      velocity: geometry_msgs::msg::Twist::from_rmw_message(msg.velocity),
      valid_for_s: msg.valid_for_s,
      code: msg.code.to_string(),
    }
  }
}


