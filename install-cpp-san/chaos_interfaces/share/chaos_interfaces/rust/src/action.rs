
#[cfg(feature = "serde")]
use serde::{Deserialize, Serialize};



// Corresponds to chaos_interfaces__action__NavigatePose_Goal

// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct NavigatePose_Goal {

    // This member is not documented.
    #[allow(missing_docs)]
    pub lease_token: std::string::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub target: geometry_msgs::msg::PoseStamped,

    /// Unix UTC deadline for starting this goal, not for completing it; zero disables it.
    pub expires_at: builtin_interfaces::msg::Time,

}



impl Default for NavigatePose_Goal {
  fn default() -> Self {
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::action::rmw::NavigatePose_Goal::default())
  }
}

impl rosidl_runtime_rs::Message for NavigatePose_Goal {
  type RmwMsg = super::action::rmw::NavigatePose_Goal;

  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> {
    match msg_cow {
      std::borrow::Cow::Owned(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        lease_token: msg.lease_token.as_str().into(),
        target: geometry_msgs::msg::PoseStamped::into_rmw_message(std::borrow::Cow::Owned(msg.target)).into_owned(),
        expires_at: builtin_interfaces::msg::Time::into_rmw_message(std::borrow::Cow::Owned(msg.expires_at)).into_owned(),
      }),
      std::borrow::Cow::Borrowed(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        lease_token: msg.lease_token.as_str().into(),
        target: geometry_msgs::msg::PoseStamped::into_rmw_message(std::borrow::Cow::Borrowed(&msg.target)).into_owned(),
        expires_at: builtin_interfaces::msg::Time::into_rmw_message(std::borrow::Cow::Borrowed(&msg.expires_at)).into_owned(),
      })
    }
  }

  fn from_rmw_message(msg: Self::RmwMsg) -> Self {
    Self {
      lease_token: msg.lease_token.to_string(),
      target: geometry_msgs::msg::PoseStamped::from_rmw_message(msg.target),
      expires_at: builtin_interfaces::msg::Time::from_rmw_message(msg.expires_at),
    }
  }
}


// Corresponds to chaos_interfaces__action__NavigatePose_Result

// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct NavigatePose_Result {

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



impl Default for NavigatePose_Result {
  fn default() -> Self {
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::action::rmw::NavigatePose_Result::default())
  }
}

impl rosidl_runtime_rs::Message for NavigatePose_Result {
  type RmwMsg = super::action::rmw::NavigatePose_Result;

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


// Corresponds to chaos_interfaces__action__NavigatePose_Feedback

// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct NavigatePose_Feedback {

    // This member is not documented.
    #[allow(missing_docs)]
    pub current_pose: geometry_msgs::msg::PoseStamped,


    // This member is not documented.
    #[allow(missing_docs)]
    pub progress: f32,

}



impl Default for NavigatePose_Feedback {
  fn default() -> Self {
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::action::rmw::NavigatePose_Feedback::default())
  }
}

impl rosidl_runtime_rs::Message for NavigatePose_Feedback {
  type RmwMsg = super::action::rmw::NavigatePose_Feedback;

  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> {
    match msg_cow {
      std::borrow::Cow::Owned(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        current_pose: geometry_msgs::msg::PoseStamped::into_rmw_message(std::borrow::Cow::Owned(msg.current_pose)).into_owned(),
        progress: msg.progress,
      }),
      std::borrow::Cow::Borrowed(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        current_pose: geometry_msgs::msg::PoseStamped::into_rmw_message(std::borrow::Cow::Borrowed(&msg.current_pose)).into_owned(),
      progress: msg.progress,
      })
    }
  }

  fn from_rmw_message(msg: Self::RmwMsg) -> Self {
    Self {
      current_pose: geometry_msgs::msg::PoseStamped::from_rmw_message(msg.current_pose),
      progress: msg.progress,
    }
  }
}


// Corresponds to chaos_interfaces__action__NavigatePose_FeedbackMessage

// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct NavigatePose_FeedbackMessage {

    // This member is not documented.
    #[allow(missing_docs)]
    pub goal_id: unique_identifier_msgs::msg::UUID,


    // This member is not documented.
    #[allow(missing_docs)]
    pub feedback: super::action::NavigatePose_Feedback,

}



impl Default for NavigatePose_FeedbackMessage {
  fn default() -> Self {
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::action::rmw::NavigatePose_FeedbackMessage::default())
  }
}

impl rosidl_runtime_rs::Message for NavigatePose_FeedbackMessage {
  type RmwMsg = super::action::rmw::NavigatePose_FeedbackMessage;

  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> {
    match msg_cow {
      std::borrow::Cow::Owned(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        goal_id: unique_identifier_msgs::msg::UUID::into_rmw_message(std::borrow::Cow::Owned(msg.goal_id)).into_owned(),
        feedback: super::action::NavigatePose_Feedback::into_rmw_message(std::borrow::Cow::Owned(msg.feedback)).into_owned(),
      }),
      std::borrow::Cow::Borrowed(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        goal_id: unique_identifier_msgs::msg::UUID::into_rmw_message(std::borrow::Cow::Borrowed(&msg.goal_id)).into_owned(),
        feedback: super::action::NavigatePose_Feedback::into_rmw_message(std::borrow::Cow::Borrowed(&msg.feedback)).into_owned(),
      })
    }
  }

  fn from_rmw_message(msg: Self::RmwMsg) -> Self {
    Self {
      goal_id: unique_identifier_msgs::msg::UUID::from_rmw_message(msg.goal_id),
      feedback: super::action::NavigatePose_Feedback::from_rmw_message(msg.feedback),
    }
  }
}


// Corresponds to chaos_interfaces__action__Flight_Goal

// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct Flight_Goal {

    // This member is not documented.
    #[allow(missing_docs)]
    pub lease_token: std::string::String,

    /// Local altitude above the mock ground; used only for takeoff. Metres.
    pub altitude_m: f64,

    /// Unix UTC deadline for starting this goal, not for completing it; zero disables it.
    pub expires_at: builtin_interfaces::msg::Time,

}



impl Default for Flight_Goal {
  fn default() -> Self {
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::action::rmw::Flight_Goal::default())
  }
}

impl rosidl_runtime_rs::Message for Flight_Goal {
  type RmwMsg = super::action::rmw::Flight_Goal;

  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> {
    match msg_cow {
      std::borrow::Cow::Owned(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        lease_token: msg.lease_token.as_str().into(),
        altitude_m: msg.altitude_m,
        expires_at: builtin_interfaces::msg::Time::into_rmw_message(std::borrow::Cow::Owned(msg.expires_at)).into_owned(),
      }),
      std::borrow::Cow::Borrowed(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        lease_token: msg.lease_token.as_str().into(),
      altitude_m: msg.altitude_m,
        expires_at: builtin_interfaces::msg::Time::into_rmw_message(std::borrow::Cow::Borrowed(&msg.expires_at)).into_owned(),
      })
    }
  }

  fn from_rmw_message(msg: Self::RmwMsg) -> Self {
    Self {
      lease_token: msg.lease_token.to_string(),
      altitude_m: msg.altitude_m,
      expires_at: builtin_interfaces::msg::Time::from_rmw_message(msg.expires_at),
    }
  }
}


// Corresponds to chaos_interfaces__action__Flight_Result

// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct Flight_Result {

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



impl Default for Flight_Result {
  fn default() -> Self {
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::action::rmw::Flight_Result::default())
  }
}

impl rosidl_runtime_rs::Message for Flight_Result {
  type RmwMsg = super::action::rmw::Flight_Result;

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


// Corresponds to chaos_interfaces__action__Flight_Feedback

// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
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
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::action::rmw::Flight_Feedback::default())
  }
}

impl rosidl_runtime_rs::Message for Flight_Feedback {
  type RmwMsg = super::action::rmw::Flight_Feedback;

  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> {
    match msg_cow {
      std::borrow::Cow::Owned(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        altitude_m: msg.altitude_m,
        progress: msg.progress,
      }),
      std::borrow::Cow::Borrowed(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
      altitude_m: msg.altitude_m,
      progress: msg.progress,
      })
    }
  }

  fn from_rmw_message(msg: Self::RmwMsg) -> Self {
    Self {
      altitude_m: msg.altitude_m,
      progress: msg.progress,
    }
  }
}


// Corresponds to chaos_interfaces__action__Flight_FeedbackMessage

// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct Flight_FeedbackMessage {

    // This member is not documented.
    #[allow(missing_docs)]
    pub goal_id: unique_identifier_msgs::msg::UUID,


    // This member is not documented.
    #[allow(missing_docs)]
    pub feedback: super::action::Flight_Feedback,

}



impl Default for Flight_FeedbackMessage {
  fn default() -> Self {
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::action::rmw::Flight_FeedbackMessage::default())
  }
}

impl rosidl_runtime_rs::Message for Flight_FeedbackMessage {
  type RmwMsg = super::action::rmw::Flight_FeedbackMessage;

  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> {
    match msg_cow {
      std::borrow::Cow::Owned(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        goal_id: unique_identifier_msgs::msg::UUID::into_rmw_message(std::borrow::Cow::Owned(msg.goal_id)).into_owned(),
        feedback: super::action::Flight_Feedback::into_rmw_message(std::borrow::Cow::Owned(msg.feedback)).into_owned(),
      }),
      std::borrow::Cow::Borrowed(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        goal_id: unique_identifier_msgs::msg::UUID::into_rmw_message(std::borrow::Cow::Borrowed(&msg.goal_id)).into_owned(),
        feedback: super::action::Flight_Feedback::into_rmw_message(std::borrow::Cow::Borrowed(&msg.feedback)).into_owned(),
      })
    }
  }

  fn from_rmw_message(msg: Self::RmwMsg) -> Self {
    Self {
      goal_id: unique_identifier_msgs::msg::UUID::from_rmw_message(msg.goal_id),
      feedback: super::action::Flight_Feedback::from_rmw_message(msg.feedback),
    }
  }
}






// Corresponds to chaos_interfaces__action__NavigatePose_SendGoal_Request

// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct NavigatePose_SendGoal_Request {

    // This member is not documented.
    #[allow(missing_docs)]
    pub goal_id: unique_identifier_msgs::msg::UUID,


    // This member is not documented.
    #[allow(missing_docs)]
    pub goal: super::action::NavigatePose_Goal,

}



impl Default for NavigatePose_SendGoal_Request {
  fn default() -> Self {
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::action::rmw::NavigatePose_SendGoal_Request::default())
  }
}

impl rosidl_runtime_rs::Message for NavigatePose_SendGoal_Request {
  type RmwMsg = super::action::rmw::NavigatePose_SendGoal_Request;

  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> {
    match msg_cow {
      std::borrow::Cow::Owned(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        goal_id: unique_identifier_msgs::msg::UUID::into_rmw_message(std::borrow::Cow::Owned(msg.goal_id)).into_owned(),
        goal: super::action::NavigatePose_Goal::into_rmw_message(std::borrow::Cow::Owned(msg.goal)).into_owned(),
      }),
      std::borrow::Cow::Borrowed(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        goal_id: unique_identifier_msgs::msg::UUID::into_rmw_message(std::borrow::Cow::Borrowed(&msg.goal_id)).into_owned(),
        goal: super::action::NavigatePose_Goal::into_rmw_message(std::borrow::Cow::Borrowed(&msg.goal)).into_owned(),
      })
    }
  }

  fn from_rmw_message(msg: Self::RmwMsg) -> Self {
    Self {
      goal_id: unique_identifier_msgs::msg::UUID::from_rmw_message(msg.goal_id),
      goal: super::action::NavigatePose_Goal::from_rmw_message(msg.goal),
    }
  }
}


// Corresponds to chaos_interfaces__action__NavigatePose_SendGoal_Response

// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct NavigatePose_SendGoal_Response {

    // This member is not documented.
    #[allow(missing_docs)]
    pub accepted: bool,


    // This member is not documented.
    #[allow(missing_docs)]
    pub stamp: builtin_interfaces::msg::Time,

}



impl Default for NavigatePose_SendGoal_Response {
  fn default() -> Self {
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::action::rmw::NavigatePose_SendGoal_Response::default())
  }
}

impl rosidl_runtime_rs::Message for NavigatePose_SendGoal_Response {
  type RmwMsg = super::action::rmw::NavigatePose_SendGoal_Response;

  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> {
    match msg_cow {
      std::borrow::Cow::Owned(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        accepted: msg.accepted,
        stamp: builtin_interfaces::msg::Time::into_rmw_message(std::borrow::Cow::Owned(msg.stamp)).into_owned(),
      }),
      std::borrow::Cow::Borrowed(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
      accepted: msg.accepted,
        stamp: builtin_interfaces::msg::Time::into_rmw_message(std::borrow::Cow::Borrowed(&msg.stamp)).into_owned(),
      })
    }
  }

  fn from_rmw_message(msg: Self::RmwMsg) -> Self {
    Self {
      accepted: msg.accepted,
      stamp: builtin_interfaces::msg::Time::from_rmw_message(msg.stamp),
    }
  }
}


// Corresponds to chaos_interfaces__action__NavigatePose_GetResult_Request

// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct NavigatePose_GetResult_Request {

    // This member is not documented.
    #[allow(missing_docs)]
    pub goal_id: unique_identifier_msgs::msg::UUID,

}



impl Default for NavigatePose_GetResult_Request {
  fn default() -> Self {
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::action::rmw::NavigatePose_GetResult_Request::default())
  }
}

impl rosidl_runtime_rs::Message for NavigatePose_GetResult_Request {
  type RmwMsg = super::action::rmw::NavigatePose_GetResult_Request;

  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> {
    match msg_cow {
      std::borrow::Cow::Owned(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        goal_id: unique_identifier_msgs::msg::UUID::into_rmw_message(std::borrow::Cow::Owned(msg.goal_id)).into_owned(),
      }),
      std::borrow::Cow::Borrowed(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        goal_id: unique_identifier_msgs::msg::UUID::into_rmw_message(std::borrow::Cow::Borrowed(&msg.goal_id)).into_owned(),
      })
    }
  }

  fn from_rmw_message(msg: Self::RmwMsg) -> Self {
    Self {
      goal_id: unique_identifier_msgs::msg::UUID::from_rmw_message(msg.goal_id),
    }
  }
}


// Corresponds to chaos_interfaces__action__NavigatePose_GetResult_Response

// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct NavigatePose_GetResult_Response {

    // This member is not documented.
    #[allow(missing_docs)]
    pub status: i8,


    // This member is not documented.
    #[allow(missing_docs)]
    pub result: super::action::NavigatePose_Result,

}



impl Default for NavigatePose_GetResult_Response {
  fn default() -> Self {
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::action::rmw::NavigatePose_GetResult_Response::default())
  }
}

impl rosidl_runtime_rs::Message for NavigatePose_GetResult_Response {
  type RmwMsg = super::action::rmw::NavigatePose_GetResult_Response;

  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> {
    match msg_cow {
      std::borrow::Cow::Owned(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        status: msg.status,
        result: super::action::NavigatePose_Result::into_rmw_message(std::borrow::Cow::Owned(msg.result)).into_owned(),
      }),
      std::borrow::Cow::Borrowed(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
      status: msg.status,
        result: super::action::NavigatePose_Result::into_rmw_message(std::borrow::Cow::Borrowed(&msg.result)).into_owned(),
      })
    }
  }

  fn from_rmw_message(msg: Self::RmwMsg) -> Self {
    Self {
      status: msg.status,
      result: super::action::NavigatePose_Result::from_rmw_message(msg.result),
    }
  }
}


// Corresponds to chaos_interfaces__action__Flight_SendGoal_Request

// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct Flight_SendGoal_Request {

    // This member is not documented.
    #[allow(missing_docs)]
    pub goal_id: unique_identifier_msgs::msg::UUID,


    // This member is not documented.
    #[allow(missing_docs)]
    pub goal: super::action::Flight_Goal,

}



impl Default for Flight_SendGoal_Request {
  fn default() -> Self {
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::action::rmw::Flight_SendGoal_Request::default())
  }
}

impl rosidl_runtime_rs::Message for Flight_SendGoal_Request {
  type RmwMsg = super::action::rmw::Flight_SendGoal_Request;

  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> {
    match msg_cow {
      std::borrow::Cow::Owned(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        goal_id: unique_identifier_msgs::msg::UUID::into_rmw_message(std::borrow::Cow::Owned(msg.goal_id)).into_owned(),
        goal: super::action::Flight_Goal::into_rmw_message(std::borrow::Cow::Owned(msg.goal)).into_owned(),
      }),
      std::borrow::Cow::Borrowed(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        goal_id: unique_identifier_msgs::msg::UUID::into_rmw_message(std::borrow::Cow::Borrowed(&msg.goal_id)).into_owned(),
        goal: super::action::Flight_Goal::into_rmw_message(std::borrow::Cow::Borrowed(&msg.goal)).into_owned(),
      })
    }
  }

  fn from_rmw_message(msg: Self::RmwMsg) -> Self {
    Self {
      goal_id: unique_identifier_msgs::msg::UUID::from_rmw_message(msg.goal_id),
      goal: super::action::Flight_Goal::from_rmw_message(msg.goal),
    }
  }
}


// Corresponds to chaos_interfaces__action__Flight_SendGoal_Response

// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct Flight_SendGoal_Response {

    // This member is not documented.
    #[allow(missing_docs)]
    pub accepted: bool,


    // This member is not documented.
    #[allow(missing_docs)]
    pub stamp: builtin_interfaces::msg::Time,

}



impl Default for Flight_SendGoal_Response {
  fn default() -> Self {
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::action::rmw::Flight_SendGoal_Response::default())
  }
}

impl rosidl_runtime_rs::Message for Flight_SendGoal_Response {
  type RmwMsg = super::action::rmw::Flight_SendGoal_Response;

  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> {
    match msg_cow {
      std::borrow::Cow::Owned(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        accepted: msg.accepted,
        stamp: builtin_interfaces::msg::Time::into_rmw_message(std::borrow::Cow::Owned(msg.stamp)).into_owned(),
      }),
      std::borrow::Cow::Borrowed(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
      accepted: msg.accepted,
        stamp: builtin_interfaces::msg::Time::into_rmw_message(std::borrow::Cow::Borrowed(&msg.stamp)).into_owned(),
      })
    }
  }

  fn from_rmw_message(msg: Self::RmwMsg) -> Self {
    Self {
      accepted: msg.accepted,
      stamp: builtin_interfaces::msg::Time::from_rmw_message(msg.stamp),
    }
  }
}


// Corresponds to chaos_interfaces__action__Flight_GetResult_Request

// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct Flight_GetResult_Request {

    // This member is not documented.
    #[allow(missing_docs)]
    pub goal_id: unique_identifier_msgs::msg::UUID,

}



impl Default for Flight_GetResult_Request {
  fn default() -> Self {
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::action::rmw::Flight_GetResult_Request::default())
  }
}

impl rosidl_runtime_rs::Message for Flight_GetResult_Request {
  type RmwMsg = super::action::rmw::Flight_GetResult_Request;

  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> {
    match msg_cow {
      std::borrow::Cow::Owned(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        goal_id: unique_identifier_msgs::msg::UUID::into_rmw_message(std::borrow::Cow::Owned(msg.goal_id)).into_owned(),
      }),
      std::borrow::Cow::Borrowed(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        goal_id: unique_identifier_msgs::msg::UUID::into_rmw_message(std::borrow::Cow::Borrowed(&msg.goal_id)).into_owned(),
      })
    }
  }

  fn from_rmw_message(msg: Self::RmwMsg) -> Self {
    Self {
      goal_id: unique_identifier_msgs::msg::UUID::from_rmw_message(msg.goal_id),
    }
  }
}


// Corresponds to chaos_interfaces__action__Flight_GetResult_Response

// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct Flight_GetResult_Response {

    // This member is not documented.
    #[allow(missing_docs)]
    pub status: i8,


    // This member is not documented.
    #[allow(missing_docs)]
    pub result: super::action::Flight_Result,

}



impl Default for Flight_GetResult_Response {
  fn default() -> Self {
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::action::rmw::Flight_GetResult_Response::default())
  }
}

impl rosidl_runtime_rs::Message for Flight_GetResult_Response {
  type RmwMsg = super::action::rmw::Flight_GetResult_Response;

  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> {
    match msg_cow {
      std::borrow::Cow::Owned(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        status: msg.status,
        result: super::action::Flight_Result::into_rmw_message(std::borrow::Cow::Owned(msg.result)).into_owned(),
      }),
      std::borrow::Cow::Borrowed(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
      status: msg.status,
        result: super::action::Flight_Result::into_rmw_message(std::borrow::Cow::Borrowed(&msg.result)).into_owned(),
      })
    }
  }

  fn from_rmw_message(msg: Self::RmwMsg) -> Self {
    Self {
      status: msg.status,
      result: super::action::Flight_Result::from_rmw_message(msg.result),
    }
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






#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_action_type_support_handle__chaos_interfaces__action__NavigatePose() -> *const std::ffi::c_void;
}

// Corresponds to chaos_interfaces__action__NavigatePose
#[allow(missing_docs, non_camel_case_types)]
pub struct NavigatePose;

impl rosidl_runtime_rs::Action for NavigatePose {
  // --- Associated types for client library users ---
  /// The goal message defined in the action definition.
  type Goal = NavigatePose_Goal;

  /// The result message defined in the action definition.
  type Result = NavigatePose_Result;

  /// The feedback message defined in the action definition.
  type Feedback = NavigatePose_Feedback;

  // --- Associated types for client library implementation ---
  /// The feedback message with generic fields which wraps the feedback message.
  type FeedbackMessage = super::action::NavigatePose_FeedbackMessage;

  /// The send_goal service using a wrapped version of the goal message as a request.
  type SendGoalService = super::action::NavigatePose_SendGoal;

  /// The generic service to cancel a goal.
  type CancelGoalService = action_msgs::srv::rmw::CancelGoal;

  /// The get_result service using a wrapped version of the result message as a response.
  type GetResultService = super::action::NavigatePose_GetResult;

  // --- Methods for client library implementation ---
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_action_type_support_handle__chaos_interfaces__action__NavigatePose() }
  }

  fn create_goal_request(
    goal_id: &[u8; 16],
    goal: super::action::rmw::NavigatePose_Goal,
  ) -> super::action::rmw::NavigatePose_SendGoal_Request {
   super::action::rmw::NavigatePose_SendGoal_Request {
      goal_id: unique_identifier_msgs::msg::rmw::UUID { uuid: *goal_id },
      goal,
    }
  }

  fn split_goal_request(
    request: super::action::rmw::NavigatePose_SendGoal_Request,
  ) -> (
    [u8; 16],
   super::action::rmw::NavigatePose_Goal,
  ) {
    (request.goal_id.uuid, request.goal)
  }

  fn create_goal_response(
    accepted: bool,
    stamp: (i32, u32),
  ) -> super::action::rmw::NavigatePose_SendGoal_Response {
   super::action::rmw::NavigatePose_SendGoal_Response {
      accepted,
      stamp: builtin_interfaces::msg::rmw::Time {
        sec: stamp.0,
        nanosec: stamp.1,
      },
    }
  }

  fn get_goal_response_accepted(
    response: &super::action::rmw::NavigatePose_SendGoal_Response,
  ) -> bool {
    response.accepted
  }

  fn get_goal_response_stamp(
    response: &super::action::rmw::NavigatePose_SendGoal_Response,
  ) -> (i32, u32) {
    (response.stamp.sec, response.stamp.nanosec)
  }

  fn create_feedback_message(
    goal_id: &[u8; 16],
    feedback: super::action::rmw::NavigatePose_Feedback,
  ) -> super::action::rmw::NavigatePose_FeedbackMessage {
    let mut message = super::action::rmw::NavigatePose_FeedbackMessage::default();
    message.goal_id.uuid = *goal_id;
    message.feedback = feedback;
    message
  }

  fn split_feedback_message(
    feedback: super::action::rmw::NavigatePose_FeedbackMessage,
  ) -> (
    [u8; 16],
   super::action::rmw::NavigatePose_Feedback,
  ) {
    (feedback.goal_id.uuid, feedback.feedback)
  }

  fn create_result_request(
    goal_id: &[u8; 16],
  ) -> super::action::rmw::NavigatePose_GetResult_Request {
   super::action::rmw::NavigatePose_GetResult_Request {
      goal_id: unique_identifier_msgs::msg::rmw::UUID { uuid: *goal_id },
    }
  }

  fn get_result_request_uuid(
    request: &super::action::rmw::NavigatePose_GetResult_Request,
  ) -> &[u8; 16] {
    &request.goal_id.uuid
  }

  fn create_result_response(
    status: i8,
    result: super::action::rmw::NavigatePose_Result,
  ) -> super::action::rmw::NavigatePose_GetResult_Response {
   super::action::rmw::NavigatePose_GetResult_Response {
      status,
      result,
    }
  }

  fn split_result_response(
    response: super::action::rmw::NavigatePose_GetResult_Response
  ) -> (
    i8,
   super::action::rmw::NavigatePose_Result,
  ) {
    (response.status, response.result)
  }
}




#[link(name = "chaos_interfaces__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_action_type_support_handle__chaos_interfaces__action__Flight() -> *const std::ffi::c_void;
}

// Corresponds to chaos_interfaces__action__Flight
#[allow(missing_docs, non_camel_case_types)]
pub struct Flight;

impl rosidl_runtime_rs::Action for Flight {
  // --- Associated types for client library users ---
  /// The goal message defined in the action definition.
  type Goal = Flight_Goal;

  /// The result message defined in the action definition.
  type Result = Flight_Result;

  /// The feedback message defined in the action definition.
  type Feedback = Flight_Feedback;

  // --- Associated types for client library implementation ---
  /// The feedback message with generic fields which wraps the feedback message.
  type FeedbackMessage = super::action::Flight_FeedbackMessage;

  /// The send_goal service using a wrapped version of the goal message as a request.
  type SendGoalService = super::action::Flight_SendGoal;

  /// The generic service to cancel a goal.
  type CancelGoalService = action_msgs::srv::rmw::CancelGoal;

  /// The get_result service using a wrapped version of the result message as a response.
  type GetResultService = super::action::Flight_GetResult;

  // --- Methods for client library implementation ---
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_action_type_support_handle__chaos_interfaces__action__Flight() }
  }

  fn create_goal_request(
    goal_id: &[u8; 16],
    goal: super::action::rmw::Flight_Goal,
  ) -> super::action::rmw::Flight_SendGoal_Request {
   super::action::rmw::Flight_SendGoal_Request {
      goal_id: unique_identifier_msgs::msg::rmw::UUID { uuid: *goal_id },
      goal,
    }
  }

  fn split_goal_request(
    request: super::action::rmw::Flight_SendGoal_Request,
  ) -> (
    [u8; 16],
   super::action::rmw::Flight_Goal,
  ) {
    (request.goal_id.uuid, request.goal)
  }

  fn create_goal_response(
    accepted: bool,
    stamp: (i32, u32),
  ) -> super::action::rmw::Flight_SendGoal_Response {
   super::action::rmw::Flight_SendGoal_Response {
      accepted,
      stamp: builtin_interfaces::msg::rmw::Time {
        sec: stamp.0,
        nanosec: stamp.1,
      },
    }
  }

  fn get_goal_response_accepted(
    response: &super::action::rmw::Flight_SendGoal_Response,
  ) -> bool {
    response.accepted
  }

  fn get_goal_response_stamp(
    response: &super::action::rmw::Flight_SendGoal_Response,
  ) -> (i32, u32) {
    (response.stamp.sec, response.stamp.nanosec)
  }

  fn create_feedback_message(
    goal_id: &[u8; 16],
    feedback: super::action::rmw::Flight_Feedback,
  ) -> super::action::rmw::Flight_FeedbackMessage {
    let mut message = super::action::rmw::Flight_FeedbackMessage::default();
    message.goal_id.uuid = *goal_id;
    message.feedback = feedback;
    message
  }

  fn split_feedback_message(
    feedback: super::action::rmw::Flight_FeedbackMessage,
  ) -> (
    [u8; 16],
   super::action::rmw::Flight_Feedback,
  ) {
    (feedback.goal_id.uuid, feedback.feedback)
  }

  fn create_result_request(
    goal_id: &[u8; 16],
  ) -> super::action::rmw::Flight_GetResult_Request {
   super::action::rmw::Flight_GetResult_Request {
      goal_id: unique_identifier_msgs::msg::rmw::UUID { uuid: *goal_id },
    }
  }

  fn get_result_request_uuid(
    request: &super::action::rmw::Flight_GetResult_Request,
  ) -> &[u8; 16] {
    &request.goal_id.uuid
  }

  fn create_result_response(
    status: i8,
    result: super::action::rmw::Flight_Result,
  ) -> super::action::rmw::Flight_GetResult_Response {
   super::action::rmw::Flight_GetResult_Response {
      status,
      result,
    }
  }

  fn split_result_response(
    response: super::action::rmw::Flight_GetResult_Response
  ) -> (
    i8,
   super::action::rmw::Flight_Result,
  ) {
    (response.status, response.result)
  }
}


