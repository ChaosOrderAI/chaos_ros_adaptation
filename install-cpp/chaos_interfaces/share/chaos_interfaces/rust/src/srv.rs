#[cfg(feature = "serde")]
use serde::{Deserialize, Serialize};




// Corresponds to chaos_interfaces__srv__GetDescription_Request

// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct GetDescription_Request {

    // This member is not documented.
    #[allow(missing_docs)]
    pub structure_needs_at_least_one_member: u8,

}



impl Default for GetDescription_Request {
  fn default() -> Self {
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::srv::rmw::GetDescription_Request::default())
  }
}

impl rosidl_runtime_rs::Message for GetDescription_Request {
  type RmwMsg = super::srv::rmw::GetDescription_Request;

  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> {
    match msg_cow {
      std::borrow::Cow::Owned(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        structure_needs_at_least_one_member: msg.structure_needs_at_least_one_member,
      }),
      std::borrow::Cow::Borrowed(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
      structure_needs_at_least_one_member: msg.structure_needs_at_least_one_member,
      })
    }
  }

  fn from_rmw_message(msg: Self::RmwMsg) -> Self {
    Self {
      structure_needs_at_least_one_member: msg.structure_needs_at_least_one_member,
    }
  }
}


// Corresponds to chaos_interfaces__srv__GetDescription_Response

// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct GetDescription_Response {

    // This member is not documented.
    #[allow(missing_docs)]
    pub asset_id: std::string::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub model: std::string::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub model_version: std::string::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub capabilities: Vec<std::string::String>,


    // This member is not documented.
    #[allow(missing_docs)]
    pub base_frame: std::string::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub odom_frame: std::string::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub map_frame: std::string::String,


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
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::srv::rmw::GetDescription_Response::default())
  }
}

impl rosidl_runtime_rs::Message for GetDescription_Response {
  type RmwMsg = super::srv::rmw::GetDescription_Response;

  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> {
    match msg_cow {
      std::borrow::Cow::Owned(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        asset_id: msg.asset_id.as_str().into(),
        model: msg.model.as_str().into(),
        model_version: msg.model_version.as_str().into(),
        capabilities: msg.capabilities
          .into_iter()
          .map(|elem| elem.as_str().into())
          .collect(),
        base_frame: msg.base_frame.as_str().into(),
        odom_frame: msg.odom_frame.as_str().into(),
        map_frame: msg.map_frame.as_str().into(),
        linear_limit_mps: msg.linear_limit_mps,
        angular_limit_rps: msg.angular_limit_rps,
        command_timeout_s: msg.command_timeout_s,
        lease_max_s: msg.lease_max_s,
      }),
      std::borrow::Cow::Borrowed(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        asset_id: msg.asset_id.as_str().into(),
        model: msg.model.as_str().into(),
        model_version: msg.model_version.as_str().into(),
        capabilities: msg.capabilities
          .iter()
          .map(|elem| elem.as_str().into())
          .collect(),
        base_frame: msg.base_frame.as_str().into(),
        odom_frame: msg.odom_frame.as_str().into(),
        map_frame: msg.map_frame.as_str().into(),
      linear_limit_mps: msg.linear_limit_mps,
      angular_limit_rps: msg.angular_limit_rps,
      command_timeout_s: msg.command_timeout_s,
      lease_max_s: msg.lease_max_s,
      })
    }
  }

  fn from_rmw_message(msg: Self::RmwMsg) -> Self {
    Self {
      asset_id: msg.asset_id.to_string(),
      model: msg.model.to_string(),
      model_version: msg.model_version.to_string(),
      capabilities: msg.capabilities
          .into_iter()
          .map(|elem| elem.to_string())
          .collect(),
      base_frame: msg.base_frame.to_string(),
      odom_frame: msg.odom_frame.to_string(),
      map_frame: msg.map_frame.to_string(),
      linear_limit_mps: msg.linear_limit_mps,
      angular_limit_rps: msg.angular_limit_rps,
      command_timeout_s: msg.command_timeout_s,
      lease_max_s: msg.lease_max_s,
    }
  }
}


// Corresponds to chaos_interfaces__srv__ControlLease_Request

// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct ControlLease_Request {

    // This member is not documented.
    #[allow(missing_docs)]
    pub operation: std::string::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub owner: std::string::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub token: std::string::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub duration_s: f64,

    /// Unix UTC deadline for accepting this request; zero means no transport deadline.
    pub expires_at: builtin_interfaces::msg::Time,

}



impl Default for ControlLease_Request {
  fn default() -> Self {
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::srv::rmw::ControlLease_Request::default())
  }
}

impl rosidl_runtime_rs::Message for ControlLease_Request {
  type RmwMsg = super::srv::rmw::ControlLease_Request;

  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> {
    match msg_cow {
      std::borrow::Cow::Owned(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        operation: msg.operation.as_str().into(),
        owner: msg.owner.as_str().into(),
        token: msg.token.as_str().into(),
        duration_s: msg.duration_s,
        expires_at: builtin_interfaces::msg::Time::into_rmw_message(std::borrow::Cow::Owned(msg.expires_at)).into_owned(),
      }),
      std::borrow::Cow::Borrowed(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        operation: msg.operation.as_str().into(),
        owner: msg.owner.as_str().into(),
        token: msg.token.as_str().into(),
      duration_s: msg.duration_s,
        expires_at: builtin_interfaces::msg::Time::into_rmw_message(std::borrow::Cow::Borrowed(&msg.expires_at)).into_owned(),
      })
    }
  }

  fn from_rmw_message(msg: Self::RmwMsg) -> Self {
    Self {
      operation: msg.operation.to_string(),
      owner: msg.owner.to_string(),
      token: msg.token.to_string(),
      duration_s: msg.duration_s,
      expires_at: builtin_interfaces::msg::Time::from_rmw_message(msg.expires_at),
    }
  }
}


// Corresponds to chaos_interfaces__srv__ControlLease_Response

// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct ControlLease_Response {

    // This member is not documented.
    #[allow(missing_docs)]
    pub success: bool,


    // This member is not documented.
    #[allow(missing_docs)]
    pub code: std::string::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub message: std::string::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub token: std::string::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub remaining_s: f64,

}



impl Default for ControlLease_Response {
  fn default() -> Self {
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::srv::rmw::ControlLease_Response::default())
  }
}

impl rosidl_runtime_rs::Message for ControlLease_Response {
  type RmwMsg = super::srv::rmw::ControlLease_Response;

  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> {
    match msg_cow {
      std::borrow::Cow::Owned(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        success: msg.success,
        code: msg.code.as_str().into(),
        message: msg.message.as_str().into(),
        token: msg.token.as_str().into(),
        remaining_s: msg.remaining_s,
      }),
      std::borrow::Cow::Borrowed(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
      success: msg.success,
        code: msg.code.as_str().into(),
        message: msg.message.as_str().into(),
        token: msg.token.as_str().into(),
      remaining_s: msg.remaining_s,
      })
    }
  }

  fn from_rmw_message(msg: Self::RmwMsg) -> Self {
    Self {
      success: msg.success,
      code: msg.code.to_string(),
      message: msg.message.to_string(),
      token: msg.token.to_string(),
      remaining_s: msg.remaining_s,
    }
  }
}


// Corresponds to chaos_interfaces__srv__Stop_Request

// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct Stop_Request {

    // This member is not documented.
    #[allow(missing_docs)]
    pub request_id: std::string::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub lease_token: std::string::String,

    /// Unix UTC deadline for accepting this request; zero means no transport deadline.
    pub expires_at: builtin_interfaces::msg::Time,

}



impl Default for Stop_Request {
  fn default() -> Self {
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::srv::rmw::Stop_Request::default())
  }
}

impl rosidl_runtime_rs::Message for Stop_Request {
  type RmwMsg = super::srv::rmw::Stop_Request;

  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> {
    match msg_cow {
      std::borrow::Cow::Owned(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        request_id: msg.request_id.as_str().into(),
        lease_token: msg.lease_token.as_str().into(),
        expires_at: builtin_interfaces::msg::Time::into_rmw_message(std::borrow::Cow::Owned(msg.expires_at)).into_owned(),
      }),
      std::borrow::Cow::Borrowed(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        request_id: msg.request_id.as_str().into(),
        lease_token: msg.lease_token.as_str().into(),
        expires_at: builtin_interfaces::msg::Time::into_rmw_message(std::borrow::Cow::Borrowed(&msg.expires_at)).into_owned(),
      })
    }
  }

  fn from_rmw_message(msg: Self::RmwMsg) -> Self {
    Self {
      request_id: msg.request_id.to_string(),
      lease_token: msg.lease_token.to_string(),
      expires_at: builtin_interfaces::msg::Time::from_rmw_message(msg.expires_at),
    }
  }
}


// Corresponds to chaos_interfaces__srv__Stop_Response

// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct Stop_Response {

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



impl Default for Stop_Response {
  fn default() -> Self {
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::srv::rmw::Stop_Response::default())
  }
}

impl rosidl_runtime_rs::Message for Stop_Response {
  type RmwMsg = super::srv::rmw::Stop_Response;

  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> {
    match msg_cow {
      std::borrow::Cow::Owned(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        success: msg.success,
        code: msg.code.as_str().into(),
        message: msg.message.as_str().into(),
      }),
      std::borrow::Cow::Borrowed(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
      success: msg.success,
        code: msg.code.as_str().into(),
        message: msg.message.as_str().into(),
      })
    }
  }

  fn from_rmw_message(msg: Self::RmwMsg) -> Self {
    Self {
      success: msg.success,
      code: msg.code.to_string(),
      message: msg.message.to_string(),
    }
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


