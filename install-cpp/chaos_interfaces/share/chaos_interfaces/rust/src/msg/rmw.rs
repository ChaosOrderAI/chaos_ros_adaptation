#[cfg(feature = "serde")]
use serde::{Deserialize, Serialize};


#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__msg__RobotState() -> *const std::ffi::c_void;
}

#[link(name = "chaos_interfaces__rosidl_generator_c")]
extern "C" {
    fn chaos_interfaces__msg__RobotState__init(msg: *mut RobotState) -> bool;
    fn chaos_interfaces__msg__RobotState__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<RobotState>, size: usize) -> bool;
    fn chaos_interfaces__msg__RobotState__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<RobotState>);
    fn chaos_interfaces__msg__RobotState__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<RobotState>, out_seq: *mut rosidl_runtime_rs::Sequence<RobotState>) -> bool;
}

// Corresponds to chaos_interfaces__msg__RobotState
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct RobotState {

    // This member is not documented.
    #[allow(missing_docs)]
    pub stamp: builtin_interfaces::msg::rmw::Time,


    // This member is not documented.
    #[allow(missing_docs)]
    pub asset_id: rosidl_runtime_rs::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub model: rosidl_runtime_rs::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub operational_state: rosidl_runtime_rs::String,

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
    pub pose: geometry_msgs::msg::rmw::PoseStamped,


    // This member is not documented.
    #[allow(missing_docs)]
    pub twist: geometry_msgs::msg::rmw::TwistStamped,

    /// [0,1], NaN when unknown. Never substitute missing data with zero.
    pub battery_percentage: f32,


    // This member is not documented.
    #[allow(missing_docs)]
    pub detail: rosidl_runtime_rs::String,

}



impl Default for RobotState {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !chaos_interfaces__msg__RobotState__init(&mut msg as *mut _) {
        panic!("Call to chaos_interfaces__msg__RobotState__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for RobotState {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__msg__RobotState__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__msg__RobotState__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__msg__RobotState__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for RobotState {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for RobotState where Self: Sized {
  const TYPE_NAME: &'static str = "chaos_interfaces/msg/RobotState";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__msg__RobotState() }
  }
}


#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__msg__CommandResult() -> *const std::ffi::c_void;
}

#[link(name = "chaos_interfaces__rosidl_generator_c")]
extern "C" {
    fn chaos_interfaces__msg__CommandResult__init(msg: *mut CommandResult) -> bool;
    fn chaos_interfaces__msg__CommandResult__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<CommandResult>, size: usize) -> bool;
    fn chaos_interfaces__msg__CommandResult__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<CommandResult>);
    fn chaos_interfaces__msg__CommandResult__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<CommandResult>, out_seq: *mut rosidl_runtime_rs::Sequence<CommandResult>) -> bool;
}

// Corresponds to chaos_interfaces__msg__CommandResult
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct CommandResult {

    // This member is not documented.
    #[allow(missing_docs)]
    pub stamp: builtin_interfaces::msg::rmw::Time,


    // This member is not documented.
    #[allow(missing_docs)]
    pub request_id: rosidl_runtime_rs::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub success: bool,


    // This member is not documented.
    #[allow(missing_docs)]
    pub code: rosidl_runtime_rs::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub message: rosidl_runtime_rs::String,

}



impl Default for CommandResult {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !chaos_interfaces__msg__CommandResult__init(&mut msg as *mut _) {
        panic!("Call to chaos_interfaces__msg__CommandResult__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for CommandResult {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__msg__CommandResult__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__msg__CommandResult__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__msg__CommandResult__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for CommandResult {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for CommandResult where Self: Sized {
  const TYPE_NAME: &'static str = "chaos_interfaces/msg/CommandResult";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__msg__CommandResult() }
  }
}


#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__msg__VelocityCommand() -> *const std::ffi::c_void;
}

#[link(name = "chaos_interfaces__rosidl_generator_c")]
extern "C" {
    fn chaos_interfaces__msg__VelocityCommand__init(msg: *mut VelocityCommand) -> bool;
    fn chaos_interfaces__msg__VelocityCommand__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<VelocityCommand>, size: usize) -> bool;
    fn chaos_interfaces__msg__VelocityCommand__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<VelocityCommand>);
    fn chaos_interfaces__msg__VelocityCommand__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<VelocityCommand>, out_seq: *mut rosidl_runtime_rs::Sequence<VelocityCommand>) -> bool;
}

// Corresponds to chaos_interfaces__msg__VelocityCommand
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]

/// ROS time of generation; frame_id must equal the asset's configured base frame.

#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct VelocityCommand {

    // This member is not documented.
    #[allow(missing_docs)]
    pub header: std_msgs::msg::rmw::Header,


    // This member is not documented.
    #[allow(missing_docs)]
    pub request_id: rosidl_runtime_rs::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub lease_token: rosidl_runtime_rs::String,

    /// Validity from header.stamp, bounded by asset command_timeout_s.
    pub valid_for_s: f64,


    // This member is not documented.
    #[allow(missing_docs)]
    pub velocity: geometry_msgs::msg::rmw::Twist,

}



impl Default for VelocityCommand {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !chaos_interfaces__msg__VelocityCommand__init(&mut msg as *mut _) {
        panic!("Call to chaos_interfaces__msg__VelocityCommand__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for VelocityCommand {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__msg__VelocityCommand__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__msg__VelocityCommand__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__msg__VelocityCommand__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for VelocityCommand {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for VelocityCommand where Self: Sized {
  const TYPE_NAME: &'static str = "chaos_interfaces/msg/VelocityCommand";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__msg__VelocityCommand() }
  }
}


#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__msg__ControlTrace() -> *const std::ffi::c_void;
}

#[link(name = "chaos_interfaces__rosidl_generator_c")]
extern "C" {
    fn chaos_interfaces__msg__ControlTrace__init(msg: *mut ControlTrace) -> bool;
    fn chaos_interfaces__msg__ControlTrace__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<ControlTrace>, size: usize) -> bool;
    fn chaos_interfaces__msg__ControlTrace__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<ControlTrace>);
    fn chaos_interfaces__msg__ControlTrace__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<ControlTrace>, out_seq: *mut rosidl_runtime_rs::Sequence<ControlTrace>) -> bool;
}

// Corresponds to chaos_interfaces__msg__ControlTrace
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]

/// Runtime evidence for offline training; never a hardware execution confirmation.
/// Event publication time and the configured robot base frame.

#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct ControlTrace {

    // This member is not documented.
    #[allow(missing_docs)]
    pub header: std_msgs::msg::rmw::Header,


    // This member is not documented.
    #[allow(missing_docs)]
    pub asset_id: rosidl_runtime_rs::String,

    /// Strictly increasing within one runtime process, including failed publications.
    /// A discontinuity or reset invalidates any previously held training action.
    pub sequence: u64,


    // This member is not documented.
    #[allow(missing_docs)]
    pub monotonic_ns: u64,


    // This member is not documented.
    #[allow(missing_docs)]
    pub request_id: rosidl_runtime_rs::String,

    /// motion.velocity, motion.stop, navigation.pose, flight.takeoff, or flight.land.
    pub command: rosidl_runtime_rs::String,

    /// issued, rejected, or invalidated.
    pub stage: rosidl_runtime_rs::String,

    /// Only issued motion.velocity events may carry a usable velocity label.
    pub velocity_valid: bool,


    // This member is not documented.
    #[allow(missing_docs)]
    pub velocity: geometry_msgs::msg::rmw::Twist,

    /// Remaining seconds from header.stamp, bounded by command and lease expiration.
    /// Zero supplies no valid action interval. Invalidation always ends the interval.
    pub valid_for_s: f64,

    /// Machine-readable code only. No lease token, owner, or exception message.
    pub code: rosidl_runtime_rs::String,

}



impl Default for ControlTrace {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !chaos_interfaces__msg__ControlTrace__init(&mut msg as *mut _) {
        panic!("Call to chaos_interfaces__msg__ControlTrace__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for ControlTrace {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__msg__ControlTrace__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__msg__ControlTrace__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__msg__ControlTrace__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for ControlTrace {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for ControlTrace where Self: Sized {
  const TYPE_NAME: &'static str = "chaos_interfaces/msg/ControlTrace";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__msg__ControlTrace() }
  }
}


