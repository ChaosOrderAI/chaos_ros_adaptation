#[cfg(feature = "serde")]
use serde::{Deserialize, Serialize};



#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__srv__GetDescription_Request() -> *const std::ffi::c_void;
}

#[link(name = "chaos_interfaces__rosidl_generator_c")]
extern "C" {
    fn chaos_interfaces__srv__GetDescription_Request__init(msg: *mut GetDescription_Request) -> bool;
    fn chaos_interfaces__srv__GetDescription_Request__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<GetDescription_Request>, size: usize) -> bool;
    fn chaos_interfaces__srv__GetDescription_Request__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<GetDescription_Request>);
    fn chaos_interfaces__srv__GetDescription_Request__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<GetDescription_Request>, out_seq: *mut rosidl_runtime_rs::Sequence<GetDescription_Request>) -> bool;
}

// Corresponds to chaos_interfaces__srv__GetDescription_Request
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct GetDescription_Request {

    // This member is not documented.
    #[allow(missing_docs)]
    pub structure_needs_at_least_one_member: u8,

}



impl Default for GetDescription_Request {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !chaos_interfaces__srv__GetDescription_Request__init(&mut msg as *mut _) {
        panic!("Call to chaos_interfaces__srv__GetDescription_Request__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for GetDescription_Request {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__srv__GetDescription_Request__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__srv__GetDescription_Request__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__srv__GetDescription_Request__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for GetDescription_Request {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for GetDescription_Request where Self: Sized {
  const TYPE_NAME: &'static str = "chaos_interfaces/srv/GetDescription_Request";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__srv__GetDescription_Request() }
  }
}


#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__srv__GetDescription_Response() -> *const std::ffi::c_void;
}

#[link(name = "chaos_interfaces__rosidl_generator_c")]
extern "C" {
    fn chaos_interfaces__srv__GetDescription_Response__init(msg: *mut GetDescription_Response) -> bool;
    fn chaos_interfaces__srv__GetDescription_Response__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<GetDescription_Response>, size: usize) -> bool;
    fn chaos_interfaces__srv__GetDescription_Response__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<GetDescription_Response>);
    fn chaos_interfaces__srv__GetDescription_Response__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<GetDescription_Response>, out_seq: *mut rosidl_runtime_rs::Sequence<GetDescription_Response>) -> bool;
}

// Corresponds to chaos_interfaces__srv__GetDescription_Response
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct GetDescription_Response {

    // This member is not documented.
    #[allow(missing_docs)]
    pub asset_id: rosidl_runtime_rs::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub model: rosidl_runtime_rs::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub model_version: rosidl_runtime_rs::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub capabilities: rosidl_runtime_rs::Sequence<rosidl_runtime_rs::String>,


    // This member is not documented.
    #[allow(missing_docs)]
    pub base_frame: rosidl_runtime_rs::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub odom_frame: rosidl_runtime_rs::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub map_frame: rosidl_runtime_rs::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub linear_limit_mps: f64,


    // This member is not documented.
    #[allow(missing_docs)]
    pub angular_limit_rps: f64,


    // This member is not documented.
    #[allow(missing_docs)]
    pub command_timeout_s: f64,


    // This member is not documented.
    #[allow(missing_docs)]
    pub lease_max_s: f64,

}



impl Default for GetDescription_Response {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !chaos_interfaces__srv__GetDescription_Response__init(&mut msg as *mut _) {
        panic!("Call to chaos_interfaces__srv__GetDescription_Response__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for GetDescription_Response {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__srv__GetDescription_Response__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__srv__GetDescription_Response__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__srv__GetDescription_Response__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for GetDescription_Response {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for GetDescription_Response where Self: Sized {
  const TYPE_NAME: &'static str = "chaos_interfaces/srv/GetDescription_Response";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__srv__GetDescription_Response() }
  }
}


#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__srv__ControlLease_Request() -> *const std::ffi::c_void;
}

#[link(name = "chaos_interfaces__rosidl_generator_c")]
extern "C" {
    fn chaos_interfaces__srv__ControlLease_Request__init(msg: *mut ControlLease_Request) -> bool;
    fn chaos_interfaces__srv__ControlLease_Request__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<ControlLease_Request>, size: usize) -> bool;
    fn chaos_interfaces__srv__ControlLease_Request__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<ControlLease_Request>);
    fn chaos_interfaces__srv__ControlLease_Request__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<ControlLease_Request>, out_seq: *mut rosidl_runtime_rs::Sequence<ControlLease_Request>) -> bool;
}

// Corresponds to chaos_interfaces__srv__ControlLease_Request
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct ControlLease_Request {

    // This member is not documented.
    #[allow(missing_docs)]
    pub operation: rosidl_runtime_rs::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub owner: rosidl_runtime_rs::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub token: rosidl_runtime_rs::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub duration_s: f64,

    /// Unix UTC deadline for accepting this request; zero means no transport deadline.
    pub expires_at: builtin_interfaces::msg::rmw::Time,

}



impl Default for ControlLease_Request {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !chaos_interfaces__srv__ControlLease_Request__init(&mut msg as *mut _) {
        panic!("Call to chaos_interfaces__srv__ControlLease_Request__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for ControlLease_Request {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__srv__ControlLease_Request__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__srv__ControlLease_Request__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__srv__ControlLease_Request__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for ControlLease_Request {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for ControlLease_Request where Self: Sized {
  const TYPE_NAME: &'static str = "chaos_interfaces/srv/ControlLease_Request";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__srv__ControlLease_Request() }
  }
}


#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__srv__ControlLease_Response() -> *const std::ffi::c_void;
}

#[link(name = "chaos_interfaces__rosidl_generator_c")]
extern "C" {
    fn chaos_interfaces__srv__ControlLease_Response__init(msg: *mut ControlLease_Response) -> bool;
    fn chaos_interfaces__srv__ControlLease_Response__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<ControlLease_Response>, size: usize) -> bool;
    fn chaos_interfaces__srv__ControlLease_Response__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<ControlLease_Response>);
    fn chaos_interfaces__srv__ControlLease_Response__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<ControlLease_Response>, out_seq: *mut rosidl_runtime_rs::Sequence<ControlLease_Response>) -> bool;
}

// Corresponds to chaos_interfaces__srv__ControlLease_Response
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct ControlLease_Response {

    // This member is not documented.
    #[allow(missing_docs)]
    pub success: bool,


    // This member is not documented.
    #[allow(missing_docs)]
    pub code: rosidl_runtime_rs::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub message: rosidl_runtime_rs::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub token: rosidl_runtime_rs::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub remaining_s: f64,

}



impl Default for ControlLease_Response {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !chaos_interfaces__srv__ControlLease_Response__init(&mut msg as *mut _) {
        panic!("Call to chaos_interfaces__srv__ControlLease_Response__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for ControlLease_Response {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__srv__ControlLease_Response__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__srv__ControlLease_Response__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__srv__ControlLease_Response__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for ControlLease_Response {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for ControlLease_Response where Self: Sized {
  const TYPE_NAME: &'static str = "chaos_interfaces/srv/ControlLease_Response";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__srv__ControlLease_Response() }
  }
}


#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__srv__Stop_Request() -> *const std::ffi::c_void;
}

#[link(name = "chaos_interfaces__rosidl_generator_c")]
extern "C" {
    fn chaos_interfaces__srv__Stop_Request__init(msg: *mut Stop_Request) -> bool;
    fn chaos_interfaces__srv__Stop_Request__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<Stop_Request>, size: usize) -> bool;
    fn chaos_interfaces__srv__Stop_Request__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<Stop_Request>);
    fn chaos_interfaces__srv__Stop_Request__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<Stop_Request>, out_seq: *mut rosidl_runtime_rs::Sequence<Stop_Request>) -> bool;
}

// Corresponds to chaos_interfaces__srv__Stop_Request
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct Stop_Request {

    // This member is not documented.
    #[allow(missing_docs)]
    pub request_id: rosidl_runtime_rs::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub lease_token: rosidl_runtime_rs::String,

    /// Unix UTC deadline for accepting this request; zero means no transport deadline.
    pub expires_at: builtin_interfaces::msg::rmw::Time,

}



impl Default for Stop_Request {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !chaos_interfaces__srv__Stop_Request__init(&mut msg as *mut _) {
        panic!("Call to chaos_interfaces__srv__Stop_Request__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for Stop_Request {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__srv__Stop_Request__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__srv__Stop_Request__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__srv__Stop_Request__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for Stop_Request {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for Stop_Request where Self: Sized {
  const TYPE_NAME: &'static str = "chaos_interfaces/srv/Stop_Request";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__srv__Stop_Request() }
  }
}


#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__srv__Stop_Response() -> *const std::ffi::c_void;
}

#[link(name = "chaos_interfaces__rosidl_generator_c")]
extern "C" {
    fn chaos_interfaces__srv__Stop_Response__init(msg: *mut Stop_Response) -> bool;
    fn chaos_interfaces__srv__Stop_Response__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<Stop_Response>, size: usize) -> bool;
    fn chaos_interfaces__srv__Stop_Response__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<Stop_Response>);
    fn chaos_interfaces__srv__Stop_Response__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<Stop_Response>, out_seq: *mut rosidl_runtime_rs::Sequence<Stop_Response>) -> bool;
}

// Corresponds to chaos_interfaces__srv__Stop_Response
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct Stop_Response {

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



impl Default for Stop_Response {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !chaos_interfaces__srv__Stop_Response__init(&mut msg as *mut _) {
        panic!("Call to chaos_interfaces__srv__Stop_Response__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for Stop_Response {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__srv__Stop_Response__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__srv__Stop_Response__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { chaos_interfaces__srv__Stop_Response__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for Stop_Response {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for Stop_Response where Self: Sized {
  const TYPE_NAME: &'static str = "chaos_interfaces/srv/Stop_Response";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__chaos_interfaces__srv__Stop_Response() }
  }
}






#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_service_type_support_handle__chaos_interfaces__srv__GetDescription() -> *const std::ffi::c_void;
}

// Corresponds to chaos_interfaces__srv__GetDescription
#[allow(missing_docs, non_camel_case_types)]
pub struct GetDescription;

impl rosidl_runtime_rs::Service for GetDescription {
    type Request = GetDescription_Request;
    type Response = GetDescription_Response;

    fn get_type_support() -> *const std::ffi::c_void {
        // SAFETY: No preconditions for this function.
        unsafe { rosidl_typesupport_c__get_service_type_support_handle__chaos_interfaces__srv__GetDescription() }
    }
}




#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_service_type_support_handle__chaos_interfaces__srv__ControlLease() -> *const std::ffi::c_void;
}

// Corresponds to chaos_interfaces__srv__ControlLease
#[allow(missing_docs, non_camel_case_types)]
pub struct ControlLease;

impl rosidl_runtime_rs::Service for ControlLease {
    type Request = ControlLease_Request;
    type Response = ControlLease_Response;

    fn get_type_support() -> *const std::ffi::c_void {
        // SAFETY: No preconditions for this function.
        unsafe { rosidl_typesupport_c__get_service_type_support_handle__chaos_interfaces__srv__ControlLease() }
    }
}




#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_service_type_support_handle__chaos_interfaces__srv__Stop() -> *const std::ffi::c_void;
}

// Corresponds to chaos_interfaces__srv__Stop
#[allow(missing_docs, non_camel_case_types)]
pub struct Stop;

impl rosidl_runtime_rs::Service for Stop {
    type Request = Stop_Request;
    type Response = Stop_Response;

    fn get_type_support() -> *const std::ffi::c_void {
        // SAFETY: No preconditions for this function.
        unsafe { rosidl_typesupport_c__get_service_type_support_handle__chaos_interfaces__srv__Stop() }
    }
}


