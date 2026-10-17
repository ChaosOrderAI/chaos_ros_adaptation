//! Replay protection for MQTT commands, ported from the
//! `replaySet_`/`replayOrder_`/`responseCache_` logic in
//! `orin_gateway/src/mqtt_transport.cpp`.
//!
//! A bounded window of recently seen `message_id`s is kept. A re-delivered
//! command ID is a replay: it is not processed again, but its cached response
//! (and event, if any) is re-published so at-least-once MQTT delivery does
//! not produce duplicate actions while retries still get an answer.

use std::collections::{HashMap, VecDeque};

const REPLAY_WINDOW: usize = 1024;

#[derive(Debug, Default)]
pub struct ReplayCache {
    order: VecDeque<String>,
    entries: HashMap<String, CachedReplies>,
}

/// Payloads cached for an already seen `message_id`. Both are `None` while
/// the original command is still waiting for its ROS 2 result.
#[derive(Debug, Default, Clone, PartialEq, Eq)]
pub struct CachedReplies {
    pub response: Option<String>,
    pub event: Option<String>,
}

impl ReplayCache {
    pub fn new() -> Self {
        Self::default()
    }

    /// `Some` when `message_id` is inside the replay window.
    pub fn lookup(&self, message_id: &str) -> Option<CachedReplies> {
        self.entries.get(message_id).cloned()
    }

    /// Record a new `message_id`, evicting the oldest one past the window.
    pub fn remember(&mut self, message_id: &str) {
        if self.entries.contains_key(message_id) {
            return;
        }
        self.entries
            .insert(message_id.to_string(), CachedReplies::default());
        self.order.push_back(message_id.to_string());
        if self.order.len() > REPLAY_WINDOW {
            let expired = self.order.pop_front().expect("non-empty order");
            self.entries.remove(&expired);
        }
    }

    pub fn cache_response(&mut self, message_id: &str, payload: String) {
        // A pending request may outlive its replay-window entry. Reinsert
        // completed replies through the same bounded eviction path.
        self.remember(message_id);
        self.entries
            .get_mut(message_id)
            .expect("remembered ID")
            .response = Some(payload);
    }

    pub fn cache_event(&mut self, request_id: &str, payload: String) {
        self.remember(request_id);
        self.entries
            .get_mut(request_id)
            .expect("remembered ID")
            .event = Some(payload);
    }

    #[cfg(test)]
    fn len(&self) -> usize {
        self.entries.len()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn duplicate_returns_cached_payloads() {
        let mut cache = ReplayCache::new();
        assert_eq!(cache.lookup("req-0001"), None);
        cache.remember("req-0001");
        cache.cache_response("req-0001", "response-payload".to_string());
        cache.cache_event("req-0001", "event-payload".to_string());

        assert_eq!(
            cache.lookup("req-0001"),
            Some(CachedReplies {
                response: Some("response-payload".to_string()),
                event: Some("event-payload".to_string()),
            })
        );
    }

    #[test]
    fn duplicate_without_cache_has_no_payloads() {
        let mut cache = ReplayCache::new();
        cache.remember("req-0001");
        assert_eq!(
            cache.lookup("req-0001"),
            Some(CachedReplies {
                response: None,
                event: None,
            })
        );
    }

    #[test]
    fn remembering_twice_does_not_grow_window() {
        let mut cache = ReplayCache::new();
        cache.remember("req-0001");
        cache.remember("req-0001");
        assert_eq!(cache.len(), 1);
        assert_eq!(cache.order.len(), 1);
    }

    #[test]
    fn late_replies_reenter_the_bounded_window() {
        let mut cache = ReplayCache::new();
        for batch in 0..5 {
            for i in 0..REPLAY_WINDOW {
                cache.remember(&format!("pending-{batch}-{i}"));
            }
            for i in 0..REPLAY_WINDOW {
                cache.remember(&format!("other-{batch}-{i}"));
            }
            for i in 0..REPLAY_WINDOW {
                let id = format!("pending-{batch}-{i}");
                cache.cache_response(&id, "response".into());
                cache.cache_event(&id, "event".into());
            }
            assert_eq!(cache.entries.len(), REPLAY_WINDOW);
            assert_eq!(cache.order.len(), REPLAY_WINDOW);
            let reply = cache.lookup(&format!("pending-{batch}-0")).unwrap();
            assert_eq!(reply.response.as_deref(), Some("response"));
            assert_eq!(reply.event.as_deref(), Some("event"));
        }
        assert!(cache.lookup("pending-0-0").is_none());
    }

    #[test]
    fn window_expires_oldest() {
        let mut cache = ReplayCache::new();
        for index in 0..REPLAY_WINDOW {
            cache.remember(&format!("req-{index:04}"));
        }
        assert_eq!(cache.len(), REPLAY_WINDOW);

        // Pushing one more expires req-0000.
        cache.remember("req-window");
        assert_eq!(cache.len(), REPLAY_WINDOW);
        assert!(
            cache.lookup("req-0000").is_none(),
            "expired ID should be treated as new again"
        );
        assert!(cache.lookup("req-0001").is_some());
    }
}
