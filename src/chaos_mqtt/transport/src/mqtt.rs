//! MQTT transport based on `rumqttc`, mirroring `MqttTransport` in
//! `orin_gateway/src/mqtt_transport.cpp`:
//!
//! - mutual TLS (CA + client certificate/private key), TLS enforced
//! - QoS 1 for command/response/event traffic; configurable QoS 0/1 for mappings
//! - automatic reconnect with exponential backoff (1 s .. 30 s), matching
//!   `mosquitto_reconnect_delay_set(client, 1, 30, true)`
//! - password supplied through the environment variable named by
//!   `mqtt.password_environment` (same as the C++ gateway). A username with
//!   a missing or empty password aborts startup instead of connecting.
//!
//! The manager owns the connection lifecycle. Commands received from the
//! broker are forwarded as raw payloads over `command_tx`; the adapter hands
//! back payloads to publish over `publish_rx`.

use std::collections::{HashMap, VecDeque};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use rumqttc::{
    AsyncClient, ConnectionError, Event, Incoming, MqttOptions, Outgoing, QoS, SubscribeReasonCode,
    TlsConfiguration, Transport,
};
use tokio::sync::{mpsc, watch, Notify};
use tokio::time::Instant;
use tracing::{info, warn};

use crate::config::{AdapterConfig, Direction, MqttConfig, NetworkConfig};
use crate::envelope::MAX_PAYLOAD_BYTES;

const OUTGOING_CHANNEL_CAPACITY: usize = 64;
const MAX_OUTBOX: usize = 256;
const MAX_INFLIGHT: u16 = 32;
const MAX_LATEST_TOPICS: usize = 128;

/// Separate bounded slots ensure telemetry never consumes the reply queue.
/// Producers replace unsent samples without waiting on broker availability.
#[derive(Default)]
struct LatestPublishes {
    values: Mutex<(HashMap<String, PublishRequest>, VecDeque<String>)>,
    ready: Notify,
}

impl LatestPublishes {
    fn push(&self, request: PublishRequest) -> Result<(), String> {
        let mut values = self.values.lock().expect("latest publish lock");
        if !values.0.contains_key(&request.topic) {
            if values.0.len() >= MAX_LATEST_TOPICS {
                return Err("too many latest-only MQTT topics".into());
            }
            values.1.push_back(request.topic.clone());
        }
        values.0.insert(request.topic.clone(), request);
        drop(values);
        self.ready.notify_one();
        Ok(())
    }

    fn remove(&self, topic: &str) {
        let mut values = self.values.lock().expect("latest publish lock");
        if values.0.remove(topic).is_some() {
            values.1.retain(|queued| queued != topic);
        }
    }

    fn pop_available(&self, eligible: impl Fn(&str) -> bool) -> Option<PublishRequest> {
        let mut values = self.values.lock().expect("latest publish lock");
        let index = values.1.iter().position(|topic| eligible(topic))?;
        let topic = values.1.remove(index)?;
        values.0.remove(&topic)
    }

    #[cfg(test)]
    fn pop(&self) -> Option<PublishRequest> {
        self.pop_available(|_| true)
    }
}

#[derive(Debug, Clone)]
pub struct PublishRequest {
    pub topic: String,
    pub payload: String,
    pub qos: QoS,
    pub retain: bool,
}

impl PublishRequest {
    pub fn response(topic: String, payload: String) -> Self {
        Self {
            topic,
            payload,
            qos: QoS::AtLeastOnce,
            retain: false,
        }
    }
}

#[derive(Debug)]
pub struct ForwardedMessage {
    pub topic: String,
    pub payload: Vec<u8>,
}

#[derive(Clone, Copy, Debug)]
enum Target {
    Command,
    Forwarded,
}

struct Subscription {
    topic: String,
    qos: QoS,
    max_payload_bytes: usize,
    target: Target,
}

struct TransportConfig {
    mqtt: MqttConfig,
    network: NetworkConfig,
    subscriptions: Vec<Subscription>,
    packet_limit: usize,
}

impl TransportConfig {
    fn from_adapter(config: &AdapterConfig) -> Self {
        let mut subscriptions = vec![Subscription {
            topic: config.mqtt.command_topic.clone(),
            qos: QoS::AtLeastOnce,
            max_payload_bytes: MAX_PAYLOAD_BYTES,
            target: Target::Command,
        }];
        for mapping in &config.topic_mappings {
            if mapping.direction == Direction::MqttToRos2 {
                subscriptions.push(Subscription {
                    topic: mapping.mqtt_topic.clone(),
                    qos: mapping.mqtt.qos(),
                    max_payload_bytes: mapping.max_payload_bytes,
                    target: Target::Forwarded,
                });
            }
        }
        let largest_payload = config
            .topic_mappings
            .iter()
            .map(|mapping| mapping.max_payload_bytes)
            .max()
            .unwrap_or(MAX_PAYLOAD_BYTES)
            .max(MAX_PAYLOAD_BYTES);
        let subscribe_bytes = 7 + subscriptions
            .iter()
            .map(|sub| sub.topic.len() + 3)
            .sum::<usize>();
        Self {
            mqtt: config.mqtt.clone(),
            network: config.network.clone(),
            subscriptions,
            packet_limit: (largest_payload + u16::MAX as usize + 1024).max(subscribe_bytes),
        }
    }
}

struct Ingress {
    commands: mpsc::Sender<Vec<u8>>,
    forwarded: mpsc::Sender<ForwardedMessage>,
}

impl Ingress {
    fn send(&self, target: Target, topic: &str, payload: Vec<u8>) -> Result<(), String> {
        match target {
            Target::Command => self
                .commands
                .try_send(payload)
                .map_err(|error| error.to_string()),
            Target::Forwarded => self
                .forwarded
                .try_send(ForwardedMessage {
                    topic: topic.into(),
                    payload,
                })
                .map_err(|error| error.to_string()),
        }
    }

    async fn wait_capacity(&self, target: Target) {
        match target {
            Target::Command => {
                let _ = self.commands.reserve().await;
            }
            Target::Forwarded => {
                let _ = self.forwarded.reserve().await;
            }
        }
    }
}

const SHUTDOWN_FLUSH_TIMEOUT: Duration = Duration::from_secs(2);

struct PublishSender {
    tx: mpsc::Sender<PublishRequest>,
    latest: Arc<LatestPublishes>,
    stopped: watch::Sender<bool>,
}

impl Drop for PublishSender {
    fn drop(&mut self) {
        let _ = self.stopped.send(true);
    }
}

#[derive(Clone)]
pub struct MqttHandle {
    pub connected: Arc<AtomicBool>,
    sender: Arc<PublishSender>,
}

impl MqttHandle {
    pub fn new(connected: Arc<AtomicBool>, tx: mpsc::Sender<PublishRequest>) -> Self {
        let (stopped, _) = watch::channel(false);
        Self {
            connected,
            sender: Arc::new(PublishSender { tx, stopped, latest: Arc::default() }),
        }
    }

    /// Backpressure bounds memory without discarding replies. The manager
    /// never waits for the adapter's command channel, so it can keep polling.
    pub async fn publish(
        &self,
        request: PublishRequest,
    ) -> Result<(), mpsc::error::SendError<PublishRequest>> {
        // In particular, a terminal event supersedes unsent feedback on the
        // same topic. Already submitted MQTT packets retain their wire order.
        self.sender.latest.remove(&request.topic);
        self.sender.tx.send(request).await
    }

    pub fn publish_latest(&self, request: PublishRequest) -> Result<(), String> {
        if self.sender.tx.is_closed() {
            return Err("MQTT manager stopped".into());
        }
        self.sender.latest.push(request)
    }
}

fn configure_delivery(options: &mut MqttOptions, packet_limit: usize) {
    options.set_max_packet_size(packet_limit, packet_limit);
    options.set_clean_session(false);
    options.set_manual_acks(true);
    options.set_inflight(MAX_INFLIGHT);
}

fn required_password(
    username: &str,
    environment: &str,
    password: Option<&str>,
) -> Result<Option<String>, String> {
    if username.is_empty() {
        return Ok(None);
    }
    match password {
        Some(value) if !value.is_empty() => Ok(Some(value.to_string())),
        _ => Err(format!(
            "MQTT username is set but {environment} is missing or empty"
        )),
    }
}

fn build_options(transport: &TransportConfig) -> Result<MqttOptions, String> {
    let config = &transport.mqtt;
    let mut options = MqttOptions::new(&config.client_id, &config.host, config.port);
    configure_delivery(&mut options, transport.packet_limit);
    options.set_keep_alive(Duration::from_secs(u64::from(config.keep_alive_seconds)));

    let ca = std::fs::read(&config.ca_file).map_err(|error| {
        format!(
            "cannot read mqtt.ca_file {}: {error}",
            config.ca_file.display()
        )
    })?;
    let client_cert = std::fs::read(&config.client_certificate_file).map_err(|error| {
        format!(
            "cannot read mqtt.client_certificate_file {}: {error}",
            config.client_certificate_file.display()
        )
    })?;
    let client_key = std::fs::read(&config.client_key_file).map_err(|error| {
        format!(
            "cannot read mqtt.client_key_file {}: {error}",
            config.client_key_file.display()
        )
    })?;
    options.set_transport(Transport::Tls(TlsConfiguration::Simple {
        ca,
        alpn: None,
        client_auth: Some((client_cert, client_key)),
    }));

    let looked_up = std::env::var(&config.password_environment).ok();
    if let Some(password) = required_password(
        &config.username,
        &config.password_environment,
        looked_up.as_deref(),
    )? {
        options.set_credentials(&config.username, &password);
    }

    Ok(options)
}

/// Spawn the connection manager task. Returns a handle used to publish and
/// the task handle, which completes once every `MqttHandle` is dropped and
/// the queued publishes have been flushed.
pub fn spawn_manager(
    config: &AdapterConfig,
    command_tx: mpsc::Sender<Vec<u8>>,
    forward_tx: mpsc::Sender<ForwardedMessage>,
) -> Result<(MqttHandle, tokio::task::JoinHandle<()>), String> {
    let config = TransportConfig::from_adapter(config);
    if let Some(interface) = &config.network.interface {
        if !std::path::Path::new("/sys/class/net")
            .join(interface)
            .exists()
        {
            return Err(format!("network interface {interface:?} does not exist"));
        }
    }
    let options = build_options(&config)?;
    let (publish_tx, publish_rx) = mpsc::channel::<PublishRequest>(OUTGOING_CHANNEL_CAPACITY * 4);
    let connected = Arc::new(AtomicBool::new(false));
    let handle = MqttHandle::new(connected.clone(), publish_tx);
    let stopped = handle.sender.stopped.subscribe();
    let task = tokio::spawn(manager_loop(
        config,
        options,
        Ingress {
            commands: command_tx,
            forwarded: forward_tx,
        },
        publish_rx,
        handle.sender.latest.clone(),
        connected,
        stopped,
    ));
    Ok((handle, task))
}

struct Connection {
    client: AsyncClient,
    events: mpsc::Receiver<Result<Event, ConnectionError>>,
    task: tokio::task::JoinHandle<()>,
}

impl Drop for Connection {
    fn drop(&mut self) {
        self.task.abort();
    }
}

fn new_connection(options: MqttOptions, network: &NetworkConfig) -> Connection {
    let (client, mut eventloop) = AsyncClient::new(options, OUTGOING_CHANNEL_CAPACITY);
    eventloop.set_network_options(network.options());
    let (tx, events) = mpsc::channel(OUTGOING_CHANNEL_CAPACITY);
    // poll() can be inside a TCP/TLS handshake or a write. Keep it alive
    // across manager events rather than cancelling it in select! branches.
    let task = tokio::spawn(async move {
        loop {
            let event = eventloop.poll().await;
            let failed = event.is_err();
            if tx.send(event).await.is_err() || failed {
                break;
            }
        }
    });
    Connection {
        client,
        events,
        task,
    }
}

/// Application-owned until PUBACK: rebuilding an MQTT connection cannot
/// discard unacknowledged responses, even if the broker loses its session.
struct QueuedPublish {
    request: PublishRequest,
    queued: bool,
    packet_id: Option<u16>,
    latest: bool,
}

#[derive(Default)]
struct Outbox(VecDeque<QueuedPublish>);

impl Outbox {
    fn push(&mut self, request: PublishRequest) {
        self.0.push_back(QueuedPublish {
            request,
            queued: false,
            packet_id: None,
            latest: false,
        });
    }

    fn push_latest(&mut self, request: PublishRequest) {
        self.push(request);
        self.0.back_mut().expect("pushed publish").latest = true;
    }

    fn pump(&mut self, client: &AsyncClient) {
        let mut inflight = self.0.iter().filter(|entry| entry.queued).count();
        for entry in &mut self.0 {
            if inflight >= usize::from(MAX_INFLIGHT) {
                break;
            }
            if entry.queued {
                continue;
            }
            if client
                .try_publish(
                    &entry.request.topic,
                    entry.request.qos,
                    entry.request.retain,
                    entry.request.payload.as_bytes(),
                )
                .is_err()
            {
                break; // Retry after polling frees space in rumqttc's queue.
            }
            entry.queued = true;
            inflight += 1;
        }
    }

    fn sent(&mut self, packet_id: u16) {
        if let Some(index) = self
            .0
            .iter()
            .position(|entry| entry.queued && entry.packet_id.is_none())
        {
            if packet_id == 0 {
                // QoS 0 has no PUBACK; completion means written to the socket.
                self.0.remove(index);
            } else {
                self.0[index].packet_id = Some(packet_id);
            }
        }
    }

    fn acknowledge(&mut self, packet_id: u16) {
        if let Some(index) = self
            .0
            .iter()
            .position(|entry| entry.packet_id == Some(packet_id))
        {
            self.0.remove(index);
        }
    }

    fn reconnect(&mut self) {
        // State/feedback will be replaced by the most recent sample. Only
        // reliable queued replies and terminal events survive reconnect.
        self.0.retain(|entry| !entry.latest);
        for entry in &mut self.0 {
            entry.queued = false;
            entry.packet_id = None;
        }
    }
}

async fn flush_and_disconnect(connection: &mut Connection, deadline: Instant) {
    if connection.client.try_disconnect().is_err() {
        return;
    }
    let drain = async {
        while let Some(event) = connection.events.recv().await {
            match event {
                Ok(Event::Outgoing(Outgoing::Disconnect)) | Err(_) => break,
                _ => {}
            }
        }
    };
    let _ = tokio::time::timeout_at(deadline, drain).await;
}

async fn manager_loop(
    config: TransportConfig,
    options: MqttOptions,
    ingress: Ingress,
    mut publish_rx: mpsc::Receiver<PublishRequest>,
    latest: Arc<LatestPublishes>,
    connected: Arc<AtomicBool>,
    mut stopped: watch::Receiver<bool>,
) {
    let mut connection = Some(new_connection(options.clone(), &config.network));
    let mut outbox = Outbox::default();
    let initial_backoff = Duration::from_millis(config.network.reconnect_initial_ms);
    let max_backoff = Duration::from_millis(config.network.reconnect_max_ms);
    let mut backoff = initial_backoff;
    let mut retry_at = None;
    let mut shutdown_at = None;
    let mut input_closed = false;
    let mut subscription_id = None;
    let mut retry_target = None;
    info!(host = %config.mqtt.host, port = config.mqtt.port, access = ?config.network.access, interface = ?config.network.interface, "connecting to MQTT broker");

    loop {
        let latest_ready = connected.load(Ordering::SeqCst) && !input_closed && retry_target.is_none()
            && publish_rx.is_empty() && outbox.0.len() < MAX_OUTBOX
            && outbox.0.iter().filter(|entry| entry.latest).count() < usize::from(MAX_INFLIGHT / 2);
        if latest_ready {
            if let Some(request) = latest.pop_available(|topic| !outbox.0.iter()
                .any(|entry| entry.latest && entry.request.topic == topic)) {
                outbox.push_latest(request);
            }
        }
        if connected.load(Ordering::SeqCst) {
            if let Some(connection) = &connection {
                outbox.pump(&connection.client);
            }
        }
        if input_closed && outbox.0.is_empty() {
            if connected.load(Ordering::SeqCst) {
                if let Some(connection) = &mut connection {
                    flush_and_disconnect(
                        connection,
                        shutdown_at.unwrap_or_else(|| Instant::now() + SHUTDOWN_FLUSH_TIMEOUT),
                    )
                    .await;
                }
            }
            break;
        }
        let mut reconnect = false;
        // Disabled timer branches use an arbitrary future instant; they are
        // recreated whenever state changes and are never polled while disabled.
        let idle_deadline = Instant::now() + max_backoff;
        tokio::select! {
            _ = latest.ready.notified(), if latest_ready => {},
            _ = stopped.changed(), if shutdown_at.is_none() => {
                shutdown_at = Some(Instant::now() + SHUTDOWN_FLUSH_TIMEOUT);
                // A final reconnect can deliver queued replies during shutdown.
                retry_at = None;
                if connection.is_none() { connection = Some(new_connection(options.clone(), &config.network)); }
            }
            _ = tokio::time::sleep_until(shutdown_at.unwrap_or(idle_deadline)), if shutdown_at.is_some() => {
                warn!(queued = outbox.0.len() + publish_rx.len(), "MQTT shutdown deadline reached; unconfirmed replies remain");
                break;
            }
            _ = tokio::time::sleep_until(retry_at.unwrap_or(idle_deadline)), if retry_at.is_some() => {
                retry_at = None;
                connection = Some(new_connection(options.clone(), &config.network));
            }
            event = async { connection.as_mut().expect("active connection").events.recv().await }, if connection.is_some() => match event {
                None => reconnect = true,
                Some(Ok(Event::Incoming(Incoming::ConnAck(_)))) => {
                    // Readiness means the command subscription is accepted,
                    // rather than merely that authentication succeeded.
                    if let Err(error) = connection.as_ref().unwrap().client.try_subscribe_many(config.subscriptions.iter().map(|sub| rumqttc::SubscribeFilter::new(sub.topic.clone(), sub.qos))) {
                        warn!(%error, "cannot queue command subscription");
                        reconnect = true;
                    }
                }
                Some(Ok(Event::Outgoing(Outgoing::Subscribe(id)))) => subscription_id = Some(id),
                Some(Ok(Event::Incoming(Incoming::SubAck(ack)))) => {
                    if subscription_id != Some(ack.pkid)
                        || ack.return_codes != config.subscriptions.iter().map(|sub| SubscribeReasonCode::Success(sub.qos)).collect::<Vec<_>>() {
                        warn!(codes = ?ack.return_codes, "MQTT subscription rejected or requested QoS unavailable");
                        reconnect = true;
                    } else {
                        info!("MQTT connected; all subscriptions ready");
                        connected.store(true, Ordering::SeqCst);
                        backoff = initial_backoff;
                    }
                }
                Some(Ok(Event::Incoming(Incoming::Publish(publish)))) => {
                    if shutdown_at.is_some() {
                        continue; // Leave new commands unacknowledged for the next session.
                    }
                    if let Some(sub) = config.subscriptions.iter().find(|sub| sub.topic == publish.topic) {
                        if publish.payload.len() <= sub.max_payload_bytes {
                            if retry_target.is_some() { continue; }
                            if let Err(error) = ingress.send(sub.target, &publish.topic, publish.payload.to_vec()) {
                                if publish.qos == QoS::AtMostOnce {
                                    warn!(%error, topic = %publish.topic, "input queue full; dropping best-effort QoS 0 message");
                                    continue;
                                }
                                warn!(%error, topic = %publish.topic, "input queue unavailable; deferring unacknowledged messages until replies drain");
                                retry_target = Some(sub.target);
                                continue;
                            }
                        } else {
                            warn!(bytes = publish.payload.len(), topic = %publish.topic, "discarding oversized MQTT payload");
                        }
                    } else {
                        warn!(topic = %publish.topic, "discarding unexpected MQTT topic");
                    }
                    if !reconnect && connection.as_ref().unwrap().client.try_ack(&publish).is_err() {
                        reconnect = true;
                    }
                }
                Some(Ok(Event::Outgoing(Outgoing::Publish(id)))) => outbox.sent(id),
                Some(Ok(Event::Incoming(Incoming::PubAck(ack)))) => outbox.acknowledge(ack.pkid),
                Some(Ok(_)) => {}
                Some(Err(error)) => {
                    warn!(%error, "MQTT connection lost");
                    reconnect = true;
                }
            },
            // Drain responses before reconnecting for unacknowledged commands.
            // This breaks the cycle when the adapter is itself backpressured.
            _ = ingress.wait_capacity(retry_target.unwrap_or(Target::Command)), if retry_target.is_some() && outbox.0.is_empty() && publish_rx.is_empty() && shutdown_at.is_none() => {
                reconnect = true;
            }
            request = publish_rx.recv(), if !input_closed && outbox.0.len() < MAX_OUTBOX => {
                match request {
                    Some(request) => outbox.push(request),
                    None => input_closed = true,
                }
            }
        }
        if reconnect {
            connected.store(false, Ordering::SeqCst);
            outbox.reconnect();
            subscription_id = None;
            retry_target = None;
            connection = None;
            info!(?backoff, "reconnecting to MQTT broker");
            retry_at = Some(Instant::now() + backoff);
            backoff = (backoff * 2).min(max_backoff);
        }
    }
    connected.store(false, Ordering::SeqCst);
}

#[cfg(test)]
mod tests;
