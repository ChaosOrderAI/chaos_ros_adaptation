
#[cfg(feature = "serde")]
use serde::{Deserialize, Serialize};


#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__action__NavigatePose_Goal() -> *const std::ffi::c_void;
}

#[link(name = "chaos_interfaces__rosidl_generator_c")]
extern "C" {
    fn chaos_interfaces__action__NavigatePose_Goal__init(msg: *mut NavigatePose_Goal) -> bool;
    fn chaos_interfaces__action__NavigatePose_Goal__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<NavigatePose_Goal>, size: usize) -> bool;
    fn chaos_interfaces__action__NavigatePose_Goal__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<NavigatePose_Goal>);
    fn chaos_interfaces__action__NavigatePose_Goal__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<NavigatePose_Goal>, out_seq: *mut rosidl_runtime_rs::Sequence<NavigatePose_Goal>) -> bool;
}

// Corresponds to chaos_interfaces__action__NavigatePose_Goal
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct NavigatePose_Goal {

    // This member is not documented.
    #[allow(missing_docs)]
    pub lease_token: rosidl_runtime_rs::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub target: geometry_msgs::msg::rmw::PoseStamped,

    /// Unix UTC deadline for starting this goal, not for completing it; zero disables it.
    pub expires_at: builtin_interfaces::msg::rmw::Time,

}



impl Default for NavigatePose_Goal {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !chaos_interfaces__action__NavigatePose_Goal__init(&mut msg as *mut _) {
        panic!("Call to chaos_interfaces__action__NavigatePose_Goal__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for NavigatePose_Goal {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__NavigatePose_Goal__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__NavigatePose_Goal__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__NavigatePose_Goal__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for NavigatePose_Goal {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for NavigatePose_Goal where Self: Sized {
  const TYPE_NAME: &'static str = "chaos_interfaces/action/NavigatePose_Goal";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__action__NavigatePose_Goal() }
  }
}


#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__action__NavigatePose_Result() -> *const std::ffi::c_void;
}

#[link(name = "chaos_interfaces__rosidl_generator_c")]
extern "C" {
    fn chaos_interfaces__action__NavigatePose_Result__init(msg: *mut NavigatePose_Result) -> bool;
    fn chaos_interfaces__action__NavigatePose_Result__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<NavigatePose_Result>, size: usize) -> bool;
    fn chaos_interfaces__action__NavigatePose_Result__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<NavigatePose_Result>);
    fn chaos_interfaces__action__NavigatePose_Result__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<NavigatePose_Result>, out_seq: *mut rosidl_runtime_rs::Sequence<NavigatePose_Result>) -> bool;
}

// Corresponds to chaos_interfaces__action__NavigatePose_Result
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct NavigatePose_Result {

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



impl Default for NavigatePose_Result {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !chaos_interfaces__action__NavigatePose_Result__init(&mut msg as *mut _) {
        panic!("Call to chaos_interfaces__action__NavigatePose_Result__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for NavigatePose_Result {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__NavigatePose_Result__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__NavigatePose_Result__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__NavigatePose_Result__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for NavigatePose_Result {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for NavigatePose_Result where Self: Sized {
  const TYPE_NAME: &'static str = "chaos_interfaces/action/NavigatePose_Result";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__action__NavigatePose_Result() }
  }
}


#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__action__NavigatePose_Feedback() -> *const std::ffi::c_void;
}

#[link(name = "chaos_interfaces__rosidl_generator_c")]
extern "C" {
    fn chaos_interfaces__action__NavigatePose_Feedback__init(msg: *mut NavigatePose_Feedback) -> bool;
    fn chaos_interfaces__action__NavigatePose_Feedback__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<NavigatePose_Feedback>, size: usize) -> bool;
    fn chaos_interfaces__action__NavigatePose_Feedback__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<NavigatePose_Feedback>);
    fn chaos_interfaces__action__NavigatePose_Feedback__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<NavigatePose_Feedback>, out_seq: *mut rosidl_runtime_rs::Sequence<NavigatePose_Feedback>) -> bool;
}

// Corresponds to chaos_interfaces__action__NavigatePose_Feedback
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct NavigatePose_Feedback {

    // This member is not documented.
    #[allow(missing_docs)]
    pub current_pose: geometry_msgs::msg::rmw::PoseStamped,


    // This member is not documented.
    #[allow(missing_docs)]
    pub progress: f32,

}



impl Default for NavigatePose_Feedback {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !chaos_interfaces__action__NavigatePose_Feedback__init(&mut msg as *mut _) {
        panic!("Call to chaos_interfaces__action__NavigatePose_Feedback__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for NavigatePose_Feedback {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__NavigatePose_Feedback__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__NavigatePose_Feedback__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__NavigatePose_Feedback__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for NavigatePose_Feedback {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for NavigatePose_Feedback where Self: Sized {
  const TYPE_NAME: &'static str = "chaos_interfaces/action/NavigatePose_Feedback";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__action__NavigatePose_Feedback() }
  }
}


#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__action__NavigatePose_FeedbackMessage() -> *const std::ffi::c_void;
}

#[link(name = "chaos_interfaces__rosidl_generator_c")]
extern "C" {
    fn chaos_interfaces__action__NavigatePose_FeedbackMessage__init(msg: *mut NavigatePose_FeedbackMessage) -> bool;
    fn chaos_interfaces__action__NavigatePose_FeedbackMessage__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<NavigatePose_FeedbackMessage>, size: usize) -> bool;
    fn chaos_interfaces__action__NavigatePose_FeedbackMessage__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<NavigatePose_FeedbackMessage>);
    fn chaos_interfaces__action__NavigatePose_FeedbackMessage__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<NavigatePose_FeedbackMessage>, out_seq: *mut rosidl_runtime_rs::Sequence<NavigatePose_FeedbackMessage>) -> bool;
}

// Corresponds to chaos_interfaces__action__NavigatePose_FeedbackMessage
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct NavigatePose_FeedbackMessage {

    // This member is not documented.
    #[allow(missing_docs)]
    pub goal_id: unique_identifier_msgs::msg::rmw::UUID,


    // This member is not documented.
    #[allow(missing_docs)]
    pub feedback: super::super::action::rmw::NavigatePose_Feedback,

}



impl Default for NavigatePose_FeedbackMessage {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !chaos_interfaces__action__NavigatePose_FeedbackMessage__init(&mut msg as *mut _) {
        panic!("Call to chaos_interfaces__action__NavigatePose_FeedbackMessage__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for NavigatePose_FeedbackMessage {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__NavigatePose_FeedbackMessage__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__NavigatePose_FeedbackMessage__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__NavigatePose_FeedbackMessage__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for NavigatePose_FeedbackMessage {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for NavigatePose_FeedbackMessage where Self: Sized {
  const TYPE_NAME: &'static str = "chaos_interfaces/action/NavigatePose_FeedbackMessage";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__action__NavigatePose_FeedbackMessage() }
  }
}


#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__action__Flight_Goal() -> *const std::ffi::c_void;
}

#[link(name = "chaos_interfaces__rosidl_generator_c")]
extern "C" {
    fn chaos_interfaces__action__Flight_Goal__init(msg: *mut Flight_Goal) -> bool;
    fn chaos_interfaces__action__Flight_Goal__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<Flight_Goal>, size: usize) -> bool;
    fn chaos_interfaces__action__Flight_Goal__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<Flight_Goal>);
    fn chaos_interfaces__action__Flight_Goal__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<Flight_Goal>, out_seq: *mut rosidl_runtime_rs::Sequence<Flight_Goal>) -> bool;
}

// Corresponds to chaos_interfaces__action__Flight_Goal
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct Flight_Goal {

    // This member is not documented.
    #[allow(missing_docs)]
    pub lease_token: rosidl_runtime_rs::String,

    /// Local altitude above the mock ground; used only for takeoff. Metres.
    pub altitude_m: f64,

    /// Unix UTC deadline for starting this goal, not for completing it; zero disables it.
    pub expires_at: builtin_interfaces::msg::rmw::Time,

}



impl Default for Flight_Goal {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !chaos_interfaces__action__Flight_Goal__init(&mut msg as *mut _) {
        panic!("Call to chaos_interfaces__action__Flight_Goal__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for Flight_Goal {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__Flight_Goal__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__Flight_Goal__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__Flight_Goal__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for Flight_Goal {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for Flight_Goal where Self: Sized {
  const TYPE_NAME: &'static str = "chaos_interfaces/action/Flight_Goal";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__action__Flight_Goal() }
  }
}


#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__action__Flight_Result() -> *const std::ffi::c_void;
}

#[link(name = "chaos_interfaces__rosidl_generator_c")]
extern "C" {
    fn chaos_interfaces__action__Flight_Result__init(msg: *mut Flight_Result) -> bool;
    fn chaos_interfaces__action__Flight_Result__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<Flight_Result>, size: usize) -> bool;
    fn chaos_interfaces__action__Flight_Result__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<Flight_Result>);
    fn chaos_interfaces__action__Flight_Result__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<Flight_Result>, out_seq: *mut rosidl_runtime_rs::Sequence<Flight_Result>) -> bool;
}

// Corresponds to chaos_interfaces__action__Flight_Result
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct Flight_Result {

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



impl Default for Flight_Result {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !chaos_interfaces__action__Flight_Result__init(&mut msg as *mut _) {
        panic!("Call to chaos_interfaces__action__Flight_Result__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for Flight_Result {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__Flight_Result__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__Flight_Result__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__Flight_Result__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for Flight_Result {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for Flight_Result where Self: Sized {
  const TYPE_NAME: &'static str = "chaos_interfaces/action/Flight_Result";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__action__Flight_Result() }
  }
}


#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__action__Flight_Feedback() -> *const std::ffi::c_void;
}

#[link(name = "chaos_interfaces__rosidl_generator_c")]
extern "C" {
    fn chaos_interfaces__action__Flight_Feedback__init(msg: *mut Flight_Feedback) -> bool;
    fn chaos_interfaces__action__Flight_Feedback__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<Flight_Feedback>, size: usize) -> bool;
    fn chaos_interfaces__action__Flight_Feedback__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<Flight_Feedback>);
    fn chaos_interfaces__action__Flight_Feedback__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<Flight_Feedback>, out_seq: *mut rosidl_runtime_rs::Sequence<Flight_Feedback>) -> bool;
}

// Corresponds to chaos_interfaces__action__Flight_Feedback
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct Flight_Feedback {

    // This member is not documented.
    #[allow(missing_docs)]
    pub altitude_m: f64,


    // This member is not documented.
    #[allow(missing_docs)]
    pub progress: f32,

}



impl Default for Flight_Feedback {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !chaos_interfaces__action__Flight_Feedback__init(&mut msg as *mut _) {
        panic!("Call to chaos_interfaces__action__Flight_Feedback__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for Flight_Feedback {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__Flight_Feedback__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__Flight_Feedback__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__Flight_Feedback__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for Flight_Feedback {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for Flight_Feedback where Self: Sized {
  const TYPE_NAME: &'static str = "chaos_interfaces/action/Flight_Feedback";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__action__Flight_Feedback() }
  }
}


#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__action__Flight_FeedbackMessage() -> *const std::ffi::c_void;
}

#[link(name = "chaos_interfaces__rosidl_generator_c")]
extern "C" {
    fn chaos_interfaces__action__Flight_FeedbackMessage__init(msg: *mut Flight_FeedbackMessage) -> bool;
    fn chaos_interfaces__action__Flight_FeedbackMessage__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<Flight_FeedbackMessage>, size: usize) -> bool;
    fn chaos_interfaces__action__Flight_FeedbackMessage__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<Flight_FeedbackMessage>);
    fn chaos_interfaces__action__Flight_FeedbackMessage__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<Flight_FeedbackMessage>, out_seq: *mut rosidl_runtime_rs::Sequence<Flight_FeedbackMessage>) -> bool;
}

// Corresponds to chaos_interfaces__action__Flight_FeedbackMessage
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct Flight_FeedbackMessage {

    // This member is not documented.
    #[allow(missing_docs)]
    pub goal_id: unique_identifier_msgs::msg::rmw::UUID,


    // This member is not documented.
    #[allow(missing_docs)]
    pub feedback: super::super::action::rmw::Flight_Feedback,

}



impl Default for Flight_FeedbackMessage {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !chaos_interfaces__action__Flight_FeedbackMessage__init(&mut msg as *mut _) {
        panic!("Call to chaos_interfaces__action__Flight_FeedbackMessage__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for Flight_FeedbackMessage {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__Flight_FeedbackMessage__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__Flight_FeedbackMessage__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__Flight_FeedbackMessage__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for Flight_FeedbackMessage {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for Flight_FeedbackMessage where Self: Sized {
  const TYPE_NAME: &'static str = "chaos_interfaces/action/Flight_FeedbackMessage";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__action__Flight_FeedbackMessage() }
  }
}




#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__action__NavigatePose_SendGoal_Request() -> *const std::ffi::c_void;
}

#[link(name = "chaos_interfaces__rosidl_generator_c")]
extern "C" {
    fn chaos_interfaces__action__NavigatePose_SendGoal_Request__init(msg: *mut NavigatePose_SendGoal_Request) -> bool;
    fn chaos_interfaces__action__NavigatePose_SendGoal_Request__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<NavigatePose_SendGoal_Request>, size: usize) -> bool;
    fn chaos_interfaces__action__NavigatePose_SendGoal_Request__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<NavigatePose_SendGoal_Request>);
    fn chaos_interfaces__action__NavigatePose_SendGoal_Request__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<NavigatePose_SendGoal_Request>, out_seq: *mut rosidl_runtime_rs::Sequence<NavigatePose_SendGoal_Request>) -> bool;
}

// Corresponds to chaos_interfaces__action__NavigatePose_SendGoal_Request
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct NavigatePose_SendGoal_Request {

    // This member is not documented.
    #[allow(missing_docs)]
    pub goal_id: unique_identifier_msgs::msg::rmw::UUID,


    // This member is not documented.
    #[allow(missing_docs)]
    pub goal: super::super::action::rmw::NavigatePose_Goal,

}



impl Default for NavigatePose_SendGoal_Request {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !chaos_interfaces__action__NavigatePose_SendGoal_Request__init(&mut msg as *mut _) {
        panic!("Call to chaos_interfaces__action__NavigatePose_SendGoal_Request__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for NavigatePose_SendGoal_Request {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__NavigatePose_SendGoal_Request__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__NavigatePose_SendGoal_Request__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__NavigatePose_SendGoal_Request__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for NavigatePose_SendGoal_Request {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for NavigatePose_SendGoal_Request where Self: Sized {
  const TYPE_NAME: &'static str = "chaos_interfaces/action/NavigatePose_SendGoal_Request";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__action__NavigatePose_SendGoal_Request() }
  }
}


#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__action__NavigatePose_SendGoal_Response() -> *const std::ffi::c_void;
}

#[link(name = "chaos_interfaces__rosidl_generator_c")]
extern "C" {
    fn chaos_interfaces__action__NavigatePose_SendGoal_Response__init(msg: *mut NavigatePose_SendGoal_Response) -> bool;
    fn chaos_interfaces__action__NavigatePose_SendGoal_Response__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<NavigatePose_SendGoal_Response>, size: usize) -> bool;
    fn chaos_interfaces__action__NavigatePose_SendGoal_Response__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<NavigatePose_SendGoal_Response>);
    fn chaos_interfaces__action__NavigatePose_SendGoal_Response__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<NavigatePose_SendGoal_Response>, out_seq: *mut rosidl_runtime_rs::Sequence<NavigatePose_SendGoal_Response>) -> bool;
}

// Corresponds to chaos_interfaces__action__NavigatePose_SendGoal_Response
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct NavigatePose_SendGoal_Response {

    // This member is not documented.
    #[allow(missing_docs)]
    pub accepted: bool,


    // This member is not documented.
    #[allow(missing_docs)]
    pub stamp: builtin_interfaces::msg::rmw::Time,

}



impl Default for NavigatePose_SendGoal_Response {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !chaos_interfaces__action__NavigatePose_SendGoal_Response__init(&mut msg as *mut _) {
        panic!("Call to chaos_interfaces__action__NavigatePose_SendGoal_Response__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for NavigatePose_SendGoal_Response {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__NavigatePose_SendGoal_Response__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__NavigatePose_SendGoal_Response__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__NavigatePose_SendGoal_Response__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for NavigatePose_SendGoal_Response {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for NavigatePose_SendGoal_Response where Self: Sized {
  const TYPE_NAME: &'static str = "chaos_interfaces/action/NavigatePose_SendGoal_Response";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__action__NavigatePose_SendGoal_Response() }
  }
}


#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__action__NavigatePose_GetResult_Request() -> *const std::ffi::c_void;
}

#[link(name = "chaos_interfaces__rosidl_generator_c")]
extern "C" {
    fn chaos_interfaces__action__NavigatePose_GetResult_Request__init(msg: *mut NavigatePose_GetResult_Request) -> bool;
    fn chaos_interfaces__action__NavigatePose_GetResult_Request__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<NavigatePose_GetResult_Request>, size: usize) -> bool;
    fn chaos_interfaces__action__NavigatePose_GetResult_Request__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<NavigatePose_GetResult_Request>);
    fn chaos_interfaces__action__NavigatePose_GetResult_Request__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<NavigatePose_GetResult_Request>, out_seq: *mut rosidl_runtime_rs::Sequence<NavigatePose_GetResult_Request>) -> bool;
}

// Corresponds to chaos_interfaces__action__NavigatePose_GetResult_Request
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct NavigatePose_GetResult_Request {

    // This member is not documented.
    #[allow(missing_docs)]
    pub goal_id: unique_identifier_msgs::msg::rmw::UUID,

}



impl Default for NavigatePose_GetResult_Request {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !chaos_interfaces__action__NavigatePose_GetResult_Request__init(&mut msg as *mut _) {
        panic!("Call to chaos_interfaces__action__NavigatePose_GetResult_Request__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for NavigatePose_GetResult_Request {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__NavigatePose_GetResult_Request__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__NavigatePose_GetResult_Request__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__NavigatePose_GetResult_Request__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for NavigatePose_GetResult_Request {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for NavigatePose_GetResult_Request where Self: Sized {
  const TYPE_NAME: &'static str = "chaos_interfaces/action/NavigatePose_GetResult_Request";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__action__NavigatePose_GetResult_Request() }
  }
}


#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__action__NavigatePose_GetResult_Response() -> *const std::ffi::c_void;
}

#[link(name = "chaos_interfaces__rosidl_generator_c")]
extern "C" {
    fn chaos_interfaces__action__NavigatePose_GetResult_Response__init(msg: *mut NavigatePose_GetResult_Response) -> bool;
    fn chaos_interfaces__action__NavigatePose_GetResult_Response__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<NavigatePose_GetResult_Response>, size: usize) -> bool;
    fn chaos_interfaces__action__NavigatePose_GetResult_Response__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<NavigatePose_GetResult_Response>);
    fn chaos_interfaces__action__NavigatePose_GetResult_Response__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<NavigatePose_GetResult_Response>, out_seq: *mut rosidl_runtime_rs::Sequence<NavigatePose_GetResult_Response>) -> bool;
}

// Corresponds to chaos_interfaces__action__NavigatePose_GetResult_Response
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct NavigatePose_GetResult_Response {

    // This member is not documented.
    #[allow(missing_docs)]
    pub status: i8,


    // This member is not documented.
    #[allow(missing_docs)]
    pub result: super::super::action::rmw::NavigatePose_Result,

}



impl Default for NavigatePose_GetResult_Response {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !chaos_interfaces__action__NavigatePose_GetResult_Response__init(&mut msg as *mut _) {
        panic!("Call to chaos_interfaces__action__NavigatePose_GetResult_Response__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for NavigatePose_GetResult_Response {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__NavigatePose_GetResult_Response__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__NavigatePose_GetResult_Response__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__NavigatePose_GetResult_Response__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for NavigatePose_GetResult_Response {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for NavigatePose_GetResult_Response where Self: Sized {
  const TYPE_NAME: &'static str = "chaos_interfaces/action/NavigatePose_GetResult_Response";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__action__NavigatePose_GetResult_Response() }
  }
}


#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__action__Flight_SendGoal_Request() -> *const std::ffi::c_void;
}

#[link(name = "chaos_interfaces__rosidl_generator_c")]
extern "C" {
    fn chaos_interfaces__action__Flight_SendGoal_Request__init(msg: *mut Flight_SendGoal_Request) -> bool;
    fn chaos_interfaces__action__Flight_SendGoal_Request__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<Flight_SendGoal_Request>, size: usize) -> bool;
    fn chaos_interfaces__action__Flight_SendGoal_Request__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<Flight_SendGoal_Request>);
    fn chaos_interfaces__action__Flight_SendGoal_Request__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<Flight_SendGoal_Request>, out_seq: *mut rosidl_runtime_rs::Sequence<Flight_SendGoal_Request>) -> bool;
}

// Corresponds to chaos_interfaces__action__Flight_SendGoal_Request
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct Flight_SendGoal_Request {

    // This member is not documented.
    #[allow(missing_docs)]
    pub goal_id: unique_identifier_msgs::msg::rmw::UUID,


    // This member is not documented.
    #[allow(missing_docs)]
    pub goal: super::super::action::rmw::Flight_Goal,

}



impl Default for Flight_SendGoal_Request {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !chaos_interfaces__action__Flight_SendGoal_Request__init(&mut msg as *mut _) {
        panic!("Call to chaos_interfaces__action__Flight_SendGoal_Request__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for Flight_SendGoal_Request {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__Flight_SendGoal_Request__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__Flight_SendGoal_Request__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__Flight_SendGoal_Request__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for Flight_SendGoal_Request {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for Flight_SendGoal_Request where Self: Sized {
  const TYPE_NAME: &'static str = "chaos_interfaces/action/Flight_SendGoal_Request";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__action__Flight_SendGoal_Request() }
  }
}


#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__action__Flight_SendGoal_Response() -> *const std::ffi::c_void;
}

#[link(name = "chaos_interfaces__rosidl_generator_c")]
extern "C" {
    fn chaos_interfaces__action__Flight_SendGoal_Response__init(msg: *mut Flight_SendGoal_Response) -> bool;
    fn chaos_interfaces__action__Flight_SendGoal_Response__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<Flight_SendGoal_Response>, size: usize) -> bool;
    fn chaos_interfaces__action__Flight_SendGoal_Response__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<Flight_SendGoal_Response>);
    fn chaos_interfaces__action__Flight_SendGoal_Response__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<Flight_SendGoal_Response>, out_seq: *mut rosidl_runtime_rs::Sequence<Flight_SendGoal_Response>) -> bool;
}

// Corresponds to chaos_interfaces__action__Flight_SendGoal_Response
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct Flight_SendGoal_Response {

    // This member is not documented.
    #[allow(missing_docs)]
    pub accepted: bool,


    // This member is not documented.
    #[allow(missing_docs)]
    pub stamp: builtin_interfaces::msg::rmw::Time,

}



impl Default for Flight_SendGoal_Response {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !chaos_interfaces__action__Flight_SendGoal_Response__init(&mut msg as *mut _) {
        panic!("Call to chaos_interfaces__action__Flight_SendGoal_Response__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for Flight_SendGoal_Response {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__Flight_SendGoal_Response__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__Flight_SendGoal_Response__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__Flight_SendGoal_Response__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for Flight_SendGoal_Response {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for Flight_SendGoal_Response where Self: Sized {
  const TYPE_NAME: &'static str = "chaos_interfaces/action/Flight_SendGoal_Response";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__action__Flight_SendGoal_Response() }
  }
}


#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__action__Flight_GetResult_Request() -> *const std::ffi::c_void;
}

#[link(name = "chaos_interfaces__rosidl_generator_c")]
extern "C" {
    fn chaos_interfaces__action__Flight_GetResult_Request__init(msg: *mut Flight_GetResult_Request) -> bool;
    fn chaos_interfaces__action__Flight_GetResult_Request__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<Flight_GetResult_Request>, size: usize) -> bool;
    fn chaos_interfaces__action__Flight_GetResult_Request__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<Flight_GetResult_Request>);
    fn chaos_interfaces__action__Flight_GetResult_Request__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<Flight_GetResult_Request>, out_seq: *mut rosidl_runtime_rs::Sequence<Flight_GetResult_Request>) -> bool;
}

// Corresponds to chaos_interfaces__action__Flight_GetResult_Request
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct Flight_GetResult_Request {

    // This member is not documented.
    #[allow(missing_docs)]
    pub goal_id: unique_identifier_msgs::msg::rmw::UUID,

}



impl Default for Flight_GetResult_Request {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !chaos_interfaces__action__Flight_GetResult_Request__init(&mut msg as *mut _) {
        panic!("Call to chaos_interfaces__action__Flight_GetResult_Request__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for Flight_GetResult_Request {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__Flight_GetResult_Request__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__Flight_GetResult_Request__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__Flight_GetResult_Request__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for Flight_GetResult_Request {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for Flight_GetResult_Request where Self: Sized {
  const TYPE_NAME: &'static str = "chaos_interfaces/action/Flight_GetResult_Request";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__action__Flight_GetResult_Request() }
  }
}


#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__action__Flight_GetResult_Response() -> *const std::ffi::c_void;
}

#[link(name = "chaos_interfaces__rosidl_generator_c")]
extern "C" {
    fn chaos_interfaces__action__Flight_GetResult_Response__init(msg: *mut Flight_GetResult_Response) -> bool;
    fn chaos_interfaces__action__Flight_GetResult_Response__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<Flight_GetResult_Response>, size: usize) -> bool;
    fn chaos_interfaces__action__Flight_GetResult_Response__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<Flight_GetResult_Response>);
    fn chaos_interfaces__action__Flight_GetResult_Response__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<Flight_GetResult_Response>, out_seq: *mut rosidl_runtime_rs::Sequence<Flight_GetResult_Response>) -> bool;
}

// Corresponds to chaos_interfaces__action__Flight_GetResult_Response
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct Flight_GetResult_Response {

    // This member is not documented.
    #[allow(missing_docs)]
    pub status: i8,


    // This member is not documented.
    #[allow(missing_docs)]
    pub result: super::super::action::rmw::Flight_Result,

}



impl Default for Flight_GetResult_Response {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !chaos_interfaces__action__Flight_GetResult_Response__init(&mut msg as *mut _) {
        panic!("Call to chaos_interfaces__action__Flight_GetResult_Response__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for Flight_GetResult_Response {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__Flight_GetResult_Response__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__Flight_GetResult_Response__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__action__Flight_GetResult_Response__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for Flight_GetResult_Response {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for Flight_GetResult_Response where Self: Sized {
  const TYPE_NAME: &'static str = "chaos_interfaces/action/Flight_GetResult_Response";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__action__Flight_GetResult_Response() }
  }
}






#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_service_type_support_handle__chaos_interfaces__action__NavigatePose_SendGoal() -> *const std::ffi::c_void;
}

// Corresponds to chaos_interfaces__action__NavigatePose_SendGoal
#[allow(missing_docs, non_camel_case_types)]
pub struct NavigatePose_SendGoal;

impl rosidl_runtime_rs::Service for NavigatePose_SendGoal {
    type Request = NavigatePose_SendGoal_Request;
    type Response = NavigatePose_SendGoal_Response;

    fn get_type_support() -> *const std::ffi::c_void {
        // SAFETY: No preconditions for this function.
        unsafe { rosidl_typesupport_c__get_service_type_support_handle__chaos_interfaces__action__NavigatePose_SendGoal() }
    }
}




#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_service_type_support_handle__chaos_interfaces__action__NavigatePose_GetResult() -> *const std::ffi::c_void;
}

// Corresponds to chaos_interfaces__action__NavigatePose_GetResult
#[allow(missing_docs, non_camel_case_types)]
pub struct NavigatePose_GetResult;

impl rosidl_runtime_rs::Service for NavigatePose_GetResult {
    type Request = NavigatePose_GetResult_Request;
    type Response = NavigatePose_GetResult_Response;

    fn get_type_support() -> *const std::ffi::c_void {
        // SAFETY: No preconditions for this function.
        unsafe { rosidl_typesupport_c__get_service_type_support_handle__chaos_interfaces__action__NavigatePose_GetResult() }
    }
}




#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_service_type_support_handle__chaos_interfaces__action__Flight_SendGoal() -> *const std::ffi::c_void;
}

// Corresponds to chaos_interfaces__action__Flight_SendGoal
#[allow(missing_docs, non_camel_case_types)]
pub struct Flight_SendGoal;

impl rosidl_runtime_rs::Service for Flight_SendGoal {
    type Request = Flight_SendGoal_Request;
    type Response = Flight_SendGoal_Response;

    fn get_type_support() -> *const std::ffi::c_void {
        // SAFETY: No preconditions for this function.
        unsafe { rosidl_typesupport_c__get_service_type_support_handle__chaos_interfaces__action__Flight_SendGoal() }
    }
}




#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_service_type_support_handle__chaos_interfaces__action__Flight_GetResult() -> *const std::ffi::c_void;
}

// Corresponds to chaos_interfaces__action__Flight_GetResult
#[allow(missing_docs, non_camel_case_types)]
pub struct Flight_GetResult;

impl rosidl_runtime_rs::Service for Flight_GetResult {
    type Request = Flight_GetResult_Request;
    type Response = Flight_GetResult_Response;

    fn get_type_support() -> *const std::ffi::c_void {
        // SAFETY: No preconditions for this function.
        unsafe { rosidl_typesupport_c__get_service_type_support_handle__chaos_interfaces__action__Flight_GetResult() }
    }
}


