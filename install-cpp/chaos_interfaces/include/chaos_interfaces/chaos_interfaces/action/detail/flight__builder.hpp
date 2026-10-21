// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from chaos_interfaces:action/Flight.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "chaos_interfaces/action/flight.hpp"


#ifndef CHAOS_INTERFACES__ACTION__DETAIL__FLIGHT__BUILDER_HPP_
#define CHAOS_INTERFACES__ACTION__DETAIL__FLIGHT__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "chaos_interfaces/action/detail/flight__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace chaos_interfaces
{

namespace action
{

namespace builder
{

class Init_Flight_Goal_expires_at
{
public:
  explicit Init_Flight_Goal_expires_at(::chaos_interfaces::action::Flight_Goal & msg)
  : msg_(msg)
  {}
  ::chaos_interfaces::action::Flight_Goal expires_at(::chaos_interfaces::action::Flight_Goal::_expires_at_type arg)
  {
    msg_.expires_at = std::move(arg);
    return std::move(msg_);
  }

private:
  ::chaos_interfaces::action::Flight_Goal msg_;
};

class Init_Flight_Goal_altitude_m
{
public:
  explicit Init_Flight_Goal_altitude_m(::chaos_interfaces::action::Flight_Goal & msg)
  : msg_(msg)
  {}
  Init_Flight_Goal_expires_at altitude_m(::chaos_interfaces::action::Flight_Goal::_altitude_m_type arg)
  {
    msg_.altitude_m = std::move(arg);
    return Init_Flight_Goal_expires_at(msg_);
  }

private:
  ::chaos_interfaces::action::Flight_Goal msg_;
};

class Init_Flight_Goal_lease_token
{
public:
  Init_Flight_Goal_lease_token()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_Flight_Goal_altitude_m lease_token(::chaos_interfaces::action::Flight_Goal::_lease_token_type arg)
  {
    msg_.lease_token = std::move(arg);
    return Init_Flight_Goal_altitude_m(msg_);
  }

private:
  ::chaos_interfaces::action::Flight_Goal msg_;
};

}  // namespace builder

}  // namespace action

template<typename MessageType>
auto build();

template<>
inline
auto build<::chaos_interfaces::action::Flight_Goal>()
{
  return chaos_interfaces::action::builder::Init_Flight_Goal_lease_token();
}

}  // namespace chaos_interfaces


namespace chaos_interfaces
{

namespace action
{

namespace builder
{

class Init_Flight_Result_message
{
public:
  explicit Init_Flight_Result_message(::chaos_interfaces::action::Flight_Result & msg)
  : msg_(msg)
  {}
  ::chaos_interfaces::action::Flight_Result message(::chaos_interfaces::action::Flight_Result::_message_type arg)
  {
    msg_.message = std::move(arg);
    return std::move(msg_);
  }

private:
  ::chaos_interfaces::action::Flight_Result msg_;
};

class Init_Flight_Result_code
{
public:
  explicit Init_Flight_Result_code(::chaos_interfaces::action::Flight_Result & msg)
  : msg_(msg)
  {}
  Init_Flight_Result_message code(::chaos_interfaces::action::Flight_Result::_code_type arg)
  {
    msg_.code = std::move(arg);
    return Init_Flight_Result_message(msg_);
  }

private:
  ::chaos_interfaces::action::Flight_Result msg_;
};

class Init_Flight_Result_success
{
public:
  Init_Flight_Result_success()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_Flight_Result_code success(::chaos_interfaces::action::Flight_Result::_success_type arg)
  {
    msg_.success = std::move(arg);
    return Init_Flight_Result_code(msg_);
  }

private:
  ::chaos_interfaces::action::Flight_Result msg_;
};

}  // namespace builder

}  // namespace action

template<typename MessageType>
auto build();

template<>
inline
auto build<::chaos_interfaces::action::Flight_Result>()
{
  return chaos_interfaces::action::builder::Init_Flight_Result_success();
}

}  // namespace chaos_interfaces


namespace chaos_interfaces
{

namespace action
{

namespace builder
{

class Init_Flight_Feedback_progress
{
public:
  explicit Init_Flight_Feedback_progress(::chaos_interfaces::action::Flight_Feedback & msg)
  : msg_(msg)
  {}
  ::chaos_interfaces::action::Flight_Feedback progress(::chaos_interfaces::action::Flight_Feedback::_progress_type arg)
  {
    msg_.progress = std::move(arg);
    return std::move(msg_);
  }

private:
  ::chaos_interfaces::action::Flight_Feedback msg_;
};

class Init_Flight_Feedback_altitude_m
{
public:
  Init_Flight_Feedback_altitude_m()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_Flight_Feedback_progress altitude_m(::chaos_interfaces::action::Flight_Feedback::_altitude_m_type arg)
  {
    msg_.altitude_m = std::move(arg);
    return Init_Flight_Feedback_progress(msg_);
  }

private:
  ::chaos_interfaces::action::Flight_Feedback msg_;
};

}  // namespace builder

}  // namespace action

template<typename MessageType>
auto build();

template<>
inline
auto build<::chaos_interfaces::action::Flight_Feedback>()
{
  return chaos_interfaces::action::builder::Init_Flight_Feedback_altitude_m();
}

}  // namespace chaos_interfaces


namespace chaos_interfaces
{

namespace action
{

namespace builder
{

class Init_Flight_SendGoal_Request_goal
{
public:
  explicit Init_Flight_SendGoal_Request_goal(::chaos_interfaces::action::Flight_SendGoal_Request & msg)
  : msg_(msg)
  {}
  ::chaos_interfaces::action::Flight_SendGoal_Request goal(::chaos_interfaces::action::Flight_SendGoal_Request::_goal_type arg)
  {
    msg_.goal = std::move(arg);
    return std::move(msg_);
  }

private:
  ::chaos_interfaces::action::Flight_SendGoal_Request msg_;
};

class Init_Flight_SendGoal_Request_goal_id
{
public:
  Init_Flight_SendGoal_Request_goal_id()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_Flight_SendGoal_Request_goal goal_id(::chaos_interfaces::action::Flight_SendGoal_Request::_goal_id_type arg)
  {
    msg_.goal_id = std::move(arg);
    return Init_Flight_SendGoal_Request_goal(msg_);
  }

private:
  ::chaos_interfaces::action::Flight_SendGoal_Request msg_;
};

}  // namespace builder

}  // namespace action

template<typename MessageType>
auto build();

template<>
inline
auto build<::chaos_interfaces::action::Flight_SendGoal_Request>()
{
  return chaos_interfaces::action::builder::Init_Flight_SendGoal_Request_goal_id();
}

}  // namespace chaos_interfaces


namespace chaos_interfaces
{

namespace action
{

namespace builder
{

class Init_Flight_SendGoal_Response_stamp
{
public:
  explicit Init_Flight_SendGoal_Response_stamp(::chaos_interfaces::action::Flight_SendGoal_Response & msg)
  : msg_(msg)
  {}
  ::chaos_interfaces::action::Flight_SendGoal_Response stamp(::chaos_interfaces::action::Flight_SendGoal_Response::_stamp_type arg)
  {
    msg_.stamp = std::move(arg);
    return std::move(msg_);
  }

private:
  ::chaos_interfaces::action::Flight_SendGoal_Response msg_;
};

class Init_Flight_SendGoal_Response_accepted
{
public:
  Init_Flight_SendGoal_Response_accepted()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_Flight_SendGoal_Response_stamp accepted(::chaos_interfaces::action::Flight_SendGoal_Response::_accepted_type arg)
  {
    msg_.accepted = std::move(arg);
    return Init_Flight_SendGoal_Response_stamp(msg_);
  }

private:
  ::chaos_interfaces::action::Flight_SendGoal_Response msg_;
};

}  // namespace builder

}  // namespace action

template<typename MessageType>
auto build();

template<>
inline
auto build<::chaos_interfaces::action::Flight_SendGoal_Response>()
{
  return chaos_interfaces::action::builder::Init_Flight_SendGoal_Response_accepted();
}

}  // namespace chaos_interfaces


namespace chaos_interfaces
{

namespace action
{

namespace builder
{

class Init_Flight_SendGoal_Event_response
{
public:
  explicit Init_Flight_SendGoal_Event_response(::chaos_interfaces::action::Flight_SendGoal_Event & msg)
  : msg_(msg)
  {}
  ::chaos_interfaces::action::Flight_SendGoal_Event response(::chaos_interfaces::action::Flight_SendGoal_Event::_response_type arg)
  {
    msg_.response = std::move(arg);
    return std::move(msg_);
  }

private:
  ::chaos_interfaces::action::Flight_SendGoal_Event msg_;
};

class Init_Flight_SendGoal_Event_request
{
public:
  explicit Init_Flight_SendGoal_Event_request(::chaos_interfaces::action::Flight_SendGoal_Event & msg)
  : msg_(msg)
  {}
  Init_Flight_SendGoal_Event_response request(::chaos_interfaces::action::Flight_SendGoal_Event::_request_type arg)
  {
    msg_.request = std::move(arg);
    return Init_Flight_SendGoal_Event_response(msg_);
  }

private:
  ::chaos_interfaces::action::Flight_SendGoal_Event msg_;
};

class Init_Flight_SendGoal_Event_info
{
public:
  Init_Flight_SendGoal_Event_info()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_Flight_SendGoal_Event_request info(::chaos_interfaces::action::Flight_SendGoal_Event::_info_type arg)
  {
    msg_.info = std::move(arg);
    return Init_Flight_SendGoal_Event_request(msg_);
  }

private:
  ::chaos_interfaces::action::Flight_SendGoal_Event msg_;
};

}  // namespace builder

}  // namespace action

template<typename MessageType>
auto build();

template<>
inline
auto build<::chaos_interfaces::action::Flight_SendGoal_Event>()
{
  return chaos_interfaces::action::builder::Init_Flight_SendGoal_Event_info();
}

}  // namespace chaos_interfaces


namespace chaos_interfaces
{

namespace action
{

namespace builder
{

class Init_Flight_GetResult_Request_goal_id
{
public:
  Init_Flight_GetResult_Request_goal_id()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  ::chaos_interfaces::action::Flight_GetResult_Request goal_id(::chaos_interfaces::action::Flight_GetResult_Request::_goal_id_type arg)
  {
    msg_.goal_id = std::move(arg);
    return std::move(msg_);
  }

private:
  ::chaos_interfaces::action::Flight_GetResult_Request msg_;
};

}  // namespace builder

}  // namespace action

template<typename MessageType>
auto build();

template<>
inline
auto build<::chaos_interfaces::action::Flight_GetResult_Request>()
{
  return chaos_interfaces::action::builder::Init_Flight_GetResult_Request_goal_id();
}

}  // namespace chaos_interfaces


namespace chaos_interfaces
{

namespace action
{

namespace builder
{

class Init_Flight_GetResult_Response_result
{
public:
  explicit Init_Flight_GetResult_Response_result(::chaos_interfaces::action::Flight_GetResult_Response & msg)
  : msg_(msg)
  {}
  ::chaos_interfaces::action::Flight_GetResult_Response result(::chaos_interfaces::action::Flight_GetResult_Response::_result_type arg)
  {
    msg_.result = std::move(arg);
    return std::move(msg_);
  }

private:
  ::chaos_interfaces::action::Flight_GetResult_Response msg_;
};

class Init_Flight_GetResult_Response_status
{
public:
  Init_Flight_GetResult_Response_status()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_Flight_GetResult_Response_result status(::chaos_interfaces::action::Flight_GetResult_Response::_status_type arg)
  {
    msg_.status = std::move(arg);
    return Init_Flight_GetResult_Response_result(msg_);
  }

private:
  ::chaos_interfaces::action::Flight_GetResult_Response msg_;
};

}  // namespace builder

}  // namespace action

template<typename MessageType>
auto build();

template<>
inline
auto build<::chaos_interfaces::action::Flight_GetResult_Response>()
{
  return chaos_interfaces::action::builder::Init_Flight_GetResult_Response_status();
}

}  // namespace chaos_interfaces


namespace chaos_interfaces
{

namespace action
{

namespace builder
{

class Init_Flight_GetResult_Event_response
{
public:
  explicit Init_Flight_GetResult_Event_response(::chaos_interfaces::action::Flight_GetResult_Event & msg)
  : msg_(msg)
  {}
  ::chaos_interfaces::action::Flight_GetResult_Event response(::chaos_interfaces::action::Flight_GetResult_Event::_response_type arg)
  {
    msg_.response = std::move(arg);
    return std::move(msg_);
  }

private:
  ::chaos_interfaces::action::Flight_GetResult_Event msg_;
};

class Init_Flight_GetResult_Event_request
{
public:
  explicit Init_Flight_GetResult_Event_request(::chaos_interfaces::action::Flight_GetResult_Event & msg)
  : msg_(msg)
  {}
  Init_Flight_GetResult_Event_response request(::chaos_interfaces::action::Flight_GetResult_Event::_request_type arg)
  {
    msg_.request = std::move(arg);
    return Init_Flight_GetResult_Event_response(msg_);
  }

private:
  ::chaos_interfaces::action::Flight_GetResult_Event msg_;
};

class Init_Flight_GetResult_Event_info
{
public:
  Init_Flight_GetResult_Event_info()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_Flight_GetResult_Event_request info(::chaos_interfaces::action::Flight_GetResult_Event::_info_type arg)
  {
    msg_.info = std::move(arg);
    return Init_Flight_GetResult_Event_request(msg_);
  }

private:
  ::chaos_interfaces::action::Flight_GetResult_Event msg_;
};

}  // namespace builder

}  // namespace action

template<typename MessageType>
auto build();

template<>
inline
auto build<::chaos_interfaces::action::Flight_GetResult_Event>()
{
  return chaos_interfaces::action::builder::Init_Flight_GetResult_Event_info();
}

}  // namespace chaos_interfaces


namespace chaos_interfaces
{

namespace action
{

namespace builder
{

class Init_Flight_FeedbackMessage_feedback
{
public:
  explicit Init_Flight_FeedbackMessage_feedback(::chaos_interfaces::action::Flight_FeedbackMessage & msg)
  : msg_(msg)
  {}
  ::chaos_interfaces::action::Flight_FeedbackMessage feedback(::chaos_interfaces::action::Flight_FeedbackMessage::_feedback_type arg)
  {
    msg_.feedback = std::move(arg);
    return std::move(msg_);
  }

private:
  ::chaos_interfaces::action::Flight_FeedbackMessage msg_;
};

class Init_Flight_FeedbackMessage_goal_id
{
public:
  Init_Flight_FeedbackMessage_goal_id()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_Flight_FeedbackMessage_feedback goal_id(::chaos_interfaces::action::Flight_FeedbackMessage::_goal_id_type arg)
  {
    msg_.goal_id = std::move(arg);
    return Init_Flight_FeedbackMessage_feedback(msg_);
  }

private:
  ::chaos_interfaces::action::Flight_FeedbackMessage msg_;
};

}  // namespace builder

}  // namespace action

template<typename MessageType>
auto build();

template<>
inline
auto build<::chaos_interfaces::action::Flight_FeedbackMessage>()
{
  return chaos_interfaces::action::builder::Init_Flight_FeedbackMessage_goal_id();
}

}  // namespace chaos_interfaces

#endif  // CHAOS_INTERFACES__ACTION__DETAIL__FLIGHT__BUILDER_HPP_
