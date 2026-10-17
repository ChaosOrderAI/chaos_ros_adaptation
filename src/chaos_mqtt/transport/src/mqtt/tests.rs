use super::*;
use bytes::BytesMut;
use rumqttc::{ConnAck, ConnectReturnCode, Packet, PubAck, Publish, SubAck};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{TcpListener, TcpStream};
use tokio::task::JoinHandle;
use tokio::time::timeout;

const TEST_TIMEOUT: Duration = Duration::from_secs(5);
const TEST_PACKET_LIMIT: usize = 2 * 1024 * 1024;

#[tokio::test]
async fn latest_telemetry_never_occupies_or_blocks_the_reliable_queue() {
    let (tx, mut rx) = mpsc::channel(1);
    let handle = MqttHandle::new(Arc::new(AtomicBool::new(false)), tx);
    handle.publish(response("first-response")).await.unwrap();
    for index in 0..10000 {
        handle.publish_latest(PublishRequest::response("telemetry".into(), index.to_string())).unwrap();
    }
    assert_eq!(rx.recv().await.unwrap().payload, "first-response");
    assert!(rx.try_recv().is_err());
    assert_eq!(handle.sender.latest.pop().unwrap().payload, "9999");
    assert!(handle.sender.latest.pop().is_none());
}

#[tokio::test]
async fn terminal_event_supersedes_unsent_feedback_and_latest_topics_are_bounded() {
    let (tx, mut rx) = mpsc::channel(1);
    let handle = MqttHandle::new(Arc::new(AtomicBool::new(false)), tx);
    handle.publish_latest(PublishRequest::response("events".into(), "feedback".into())).unwrap();
    handle.publish(PublishRequest::response("events".into(), "terminal".into())).await.unwrap();
    assert!(handle.sender.latest.pop().is_none());
    assert_eq!(rx.recv().await.unwrap().payload, "terminal");
    for index in 0..MAX_LATEST_TOPICS {
        handle.publish_latest(PublishRequest::response(format!("topic-{index}"), "sample".into())).unwrap();
    }
    assert!(handle.publish_latest(PublishRequest::response("overflow".into(), "sample".into())).is_err());
    // Updating a known topic is allowed even when all slots are used.
    handle.publish_latest(PublishRequest::response("topic-0".into(), "newest".into())).unwrap();
    assert_eq!(handle.sender.latest.pop().unwrap().payload, "newest");
}

// A small wire-level broker lets these tests inject disconnects and refusals
// without external services or credentials. Production still requires mTLS.
async fn read_packet(stream: &mut TcpStream) -> Packet {
    timeout(TEST_TIMEOUT, async {
        let mut frame = BytesMut::new();
        frame.extend_from_slice(&[stream.read_u8().await.unwrap()]);
        let mut remaining = 0usize;
        let mut multiplier = 1;
        loop {
            let byte = stream.read_u8().await.unwrap();
            frame.extend_from_slice(&[byte]);
            remaining += usize::from(byte & 127) * multiplier;
            if byte & 128 == 0 {
                break;
            }
            multiplier *= 128;
            assert!(multiplier <= 128 * 128 * 128);
        }
        let header = frame.len();
        frame.resize(header + remaining, 0);
        stream.read_exact(&mut frame[header..]).await.unwrap();
        Packet::read(&mut frame, TEST_PACKET_LIMIT).unwrap()
    })
    .await
    .expect("MQTT packet arrived before deadline")
}

async fn write_packet(stream: &mut TcpStream, packet: Packet) {
    let mut bytes = BytesMut::new();
    packet.write(&mut bytes, TEST_PACKET_LIMIT).unwrap();
    timeout(TEST_TIMEOUT, stream.write_all(&bytes))
        .await
        .unwrap()
        .unwrap();
}

fn start_manager(
    listener: &TcpListener,
    command_capacity: usize,
) -> (MqttHandle, mpsc::Receiver<Vec<u8>>, JoinHandle<()>) {
    let (handle, commands, _, task) =
        start_configured(listener, command_capacity, AdapterConfig::default());
    (handle, commands, task)
}

fn start_configured(
    listener: &TcpListener,
    command_capacity: usize,
    mut config: AdapterConfig,
) -> (
    MqttHandle,
    mpsc::Receiver<Vec<u8>>,
    mpsc::Receiver<ForwardedMessage>,
    JoinHandle<()>,
) {
    config.mqtt.host = "127.0.0.1".into();
    config.mqtt.port = listener.local_addr().unwrap().port();
    config.mqtt.client_id = "test-adapter".into();
    config.mqtt.command_topic = "test/commands".into();
    let config = TransportConfig::from_adapter(&config);
    let mut options = MqttOptions::new(&config.mqtt.client_id, &config.mqtt.host, config.mqtt.port);
    configure_delivery(&mut options, config.packet_limit);
    let (command_tx, command_rx) = mpsc::channel(command_capacity);
    let (forward_tx, forward_rx) = mpsc::channel(command_capacity);
    let (tx, rx) = mpsc::channel(MAX_OUTBOX);
    let connected = Arc::new(AtomicBool::new(false));
    let handle = MqttHandle::new(connected.clone(), tx);
    let stopped = handle.sender.stopped.subscribe();
    let task = tokio::spawn(manager_loop(
        config,
        options,
        Ingress {
            commands: command_tx,
            forwarded: forward_tx,
        },
        rx,
        handle.sender.latest.clone(),
        connected,
        stopped,
    ));
    (handle, command_rx, forward_rx, task)
}

async fn handshake(listener: &TcpListener, session_present: bool) -> (TcpStream, u16) {
    let (stream, subscribe) = handshake_all(listener, session_present).await;
    (stream, subscribe.pkid)
}

async fn handshake_all(
    listener: &TcpListener,
    session_present: bool,
) -> (TcpStream, rumqttc::Subscribe) {
    let (mut stream, _) = timeout(TEST_TIMEOUT, listener.accept())
        .await
        .unwrap()
        .unwrap();
    let Packet::Connect(connect) = read_packet(&mut stream).await else {
        panic!("expected CONNECT")
    };
    assert!(
        !connect.clean_session,
        "unacknowledged commands require a persistent session"
    );
    write_packet(
        &mut stream,
        Packet::ConnAck(ConnAck::new(ConnectReturnCode::Success, session_present)),
    )
    .await;
    let Packet::Subscribe(subscribe) = read_packet(&mut stream).await else {
        panic!("expected SUBSCRIBE")
    };
    assert_eq!(subscribe.filters[0].path, "test/commands");
    (stream, subscribe)
}

async fn grant(stream: &mut TcpStream, id: u16, code: SubscribeReasonCode) {
    write_packet(stream, Packet::SubAck(SubAck::new(id, vec![code]))).await;
}

async fn ready(listener: &TcpListener, session_present: bool) -> TcpStream {
    let (mut stream, id) = handshake(listener, session_present).await;
    grant(
        &mut stream,
        id,
        SubscribeReasonCode::Success(QoS::AtLeastOnce),
    )
    .await;
    stream
}

fn response(payload: impl Into<String>) -> PublishRequest {
    PublishRequest::response("test/responses".into(), payload.into())
}

async fn receive_publish(stream: &mut TcpStream) -> Publish {
    match read_packet(stream).await {
        Packet::Publish(publish) => publish,
        packet => panic!("expected PUBLISH, got {packet:?}"),
    }
}

async fn acknowledge(stream: &mut TcpStream, id: u16) {
    write_packet(stream, Packet::PubAck(PubAck::new(id))).await;
}

async fn finish(handle: MqttHandle, stream: &mut TcpStream, task: JoinHandle<()>) {
    drop(handle);
    assert!(matches!(read_packet(stream).await, Packet::Disconnect));
    timeout(TEST_TIMEOUT, task).await.unwrap().unwrap();
}

#[tokio::test]
async fn disconnected_telemetry_coalesces_and_replies_take_priority_on_reconnect() {
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let (handle, _commands, task) = start_manager(&listener, 4);
    for index in 0..10000 {
        handle.publish_latest(PublishRequest::response("state".into(), index.to_string())).unwrap();
    }
    handle.publish(response("terminal-result")).await.unwrap();
    let mut stream = ready(&listener, false).await;
    let terminal = receive_publish(&mut stream).await;
    assert_eq!(terminal.topic, "test/responses");
    assert_eq!(terminal.payload.as_ref(), b"terminal-result");
    acknowledge(&mut stream, terminal.pkid).await;
    let state = receive_publish(&mut stream).await;
    assert_eq!(state.topic, "state");
    assert_eq!(state.payload.as_ref(), b"9999");
    acknowledge(&mut stream, state.pkid).await;
    finish(handle, &mut stream, task).await;
}

#[tokio::test]
async fn unacknowledged_telemetry_does_not_fill_reply_inflight_slots() {
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let (handle, _commands, task) = start_manager(&listener, 4);
    let mut stream = ready(&listener, false).await;
    handle.publish_latest(PublishRequest::response("state".into(), "initial".into())).unwrap();
    let first = receive_publish(&mut stream).await;
    // Hold this topic's PUBACK while many newer samples arrive.
    for index in 0..10000 {
        handle.publish_latest(PublishRequest::response("state".into(), index.to_string())).unwrap();
    }
    handle.publish(response("terminal-result")).await.unwrap();
    let terminal = receive_publish(&mut stream).await;
    assert_eq!(terminal.topic, "test/responses");
    assert_eq!(terminal.payload.as_ref(), b"terminal-result");
    acknowledge(&mut stream, terminal.pkid).await;
    acknowledge(&mut stream, first.pkid).await;
    let newest = receive_publish(&mut stream).await;
    assert_eq!(newest.payload.as_ref(), b"9999");
    acknowledge(&mut stream, newest.pkid).await;
    finish(handle, &mut stream, task).await;
}

#[tokio::test]
async fn unconfirmed_response_survives_disconnect_and_session_loss() {
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let (handle, _commands, task) = start_manager(&listener, 4);
    handle.publish(response("first")).await.unwrap();
    let (mut stream, id) = handshake(&listener, false).await;
    assert!(
        !handle.connected.load(Ordering::SeqCst),
        "CONNACK is not subscription readiness"
    );
    grant(
        &mut stream,
        id,
        SubscribeReasonCode::Success(QoS::AtLeastOnce),
    )
    .await;
    let first = receive_publish(&mut stream).await;
    assert_eq!(first.payload.as_ref(), b"first");
    drop(stream); // No PUBACK; simulate a lost broker session on reconnect.
    handle.publish(response("second")).await.unwrap();
    let mut stream = ready(&listener, false).await;
    for expected in [b"first".as_slice(), b"second".as_slice()] {
        let publish = receive_publish(&mut stream).await;
        assert_eq!(publish.payload.as_ref(), expected);
        acknowledge(&mut stream, publish.pkid).await;
    }
    finish(handle, &mut stream, task).await;
}

async fn subscription_refusal(code: SubscribeReasonCode) {
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let (handle, _commands, task) = start_manager(&listener, 4);
    handle
        .publish(response("queued until subscription succeeds"))
        .await
        .unwrap();
    let (mut stream, id) = handshake(&listener, false).await;
    grant(&mut stream, id, code).await;
    // The rejected connection must close without publishing the queued reply.
    assert_eq!(
        timeout(TEST_TIMEOUT, stream.read_u8())
            .await
            .unwrap()
            .unwrap_err()
            .kind(),
        std::io::ErrorKind::UnexpectedEof
    );
    assert!(!handle.connected.load(Ordering::SeqCst));
    let mut stream = ready(&listener, true).await;
    let publish = receive_publish(&mut stream).await;
    assert_eq!(
        publish.payload.as_ref(),
        b"queued until subscription succeeds"
    );
    acknowledge(&mut stream, publish.pkid).await;
    finish(handle, &mut stream, task).await;
}

#[tokio::test]
async fn rejected_subscription_retries_without_losing_response() {
    subscription_refusal(SubscribeReasonCode::Failure).await;
}

#[tokio::test]
async fn qos_zero_subscription_is_not_reported_as_ready() {
    subscription_refusal(SubscribeReasonCode::Success(QoS::AtMostOnce)).await;
}

#[tokio::test]
async fn full_size_payloads_cross_the_transport_and_oversized_commands_are_acknowledged() {
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let (handle, mut commands, task) = start_manager(&listener, 4);
    let mut stream = ready(&listener, false).await;
    for (id, size) in [(1, MAX_PAYLOAD_BYTES), (2, MAX_PAYLOAD_BYTES + 1)] {
        let mut publish = Publish::new("test/commands", QoS::AtLeastOnce, vec![b'x'; size]);
        publish.pkid = id;
        write_packet(&mut stream, Packet::Publish(publish)).await;
        assert!(matches!(read_packet(&mut stream).await, Packet::PubAck(ack) if ack.pkid == id));
        if size == MAX_PAYLOAD_BYTES {
            assert_eq!(commands.recv().await.unwrap().len(), MAX_PAYLOAD_BYTES);
        } else {
            assert!(commands.try_recv().is_err());
        }
    }
    handle
        .publish(response("x".repeat(MAX_PAYLOAD_BYTES + 128)))
        .await
        .unwrap();
    let publish = receive_publish(&mut stream).await;
    assert_eq!(publish.payload.len(), MAX_PAYLOAD_BYTES + 128);
    acknowledge(&mut stream, publish.pkid).await;
    finish(handle, &mut stream, task).await;
}

#[tokio::test]
async fn saturated_response_queues_resume_without_loss() {
    const COUNT: usize = MAX_OUTBOX * 2 + 80;
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let (handle, _commands, task) = start_manager(&listener, 4);
    let mut stream = ready(&listener, false).await;
    let producer = {
        let handle = handle.clone();
        tokio::spawn(async move {
            for index in 0..COUNT {
                handle.publish(response(index.to_string())).await.unwrap();
            }
        })
    };
    let mut first = Vec::new();
    for _ in 0..MAX_INFLIGHT {
        first.push(receive_publish(&mut stream).await);
    }
    assert!(
        !producer.is_finished(),
        "bounded queues must backpressure the producer"
    );
    let mut received = std::collections::HashSet::new();
    // ACK order need not match send order.
    for publish in first.into_iter().rev() {
        assert!(received.insert(publish.payload.to_vec()));
        acknowledge(&mut stream, publish.pkid).await;
    }
    for _ in usize::from(MAX_INFLIGHT)..COUNT {
        let publish = receive_publish(&mut stream).await;
        assert!(
            received.insert(publish.payload.to_vec()),
            "duplicate response in a stable connection"
        );
        acknowledge(&mut stream, publish.pkid).await;
    }
    timeout(TEST_TIMEOUT, producer).await.unwrap().unwrap();
    assert_eq!(received.len(), COUNT);
    finish(handle, &mut stream, task).await;
}

#[tokio::test]
async fn full_command_queue_does_not_block_response_delivery() {
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let (handle, mut commands, task) = start_manager(&listener, 1);
    let mut stream = ready(&listener, false).await;
    let mut first = Publish::new("test/commands", QoS::AtLeastOnce, b"first".to_vec());
    first.pkid = 1;
    write_packet(&mut stream, Packet::Publish(first)).await;
    assert!(matches!(read_packet(&mut stream).await, Packet::PubAck(ack) if ack.pkid == 1));
    let mut second = Publish::new("test/commands", QoS::AtLeastOnce, b"second".to_vec());
    second.pkid = 2;
    write_packet(&mut stream, Packet::Publish(second.clone())).await;
    // This following packet is acknowledged after the full-queue command
    // has been handled, making the congestion setup deterministic.
    let mut oversized = Publish::new(
        "test/commands",
        QoS::AtLeastOnce,
        vec![0; MAX_PAYLOAD_BYTES + 1],
    );
    oversized.pkid = 3;
    write_packet(&mut stream, Packet::Publish(oversized)).await;
    assert!(matches!(read_packet(&mut stream).await, Packet::PubAck(ack) if ack.pkid == 3));
    // A pending telemetry sample must not keep the outbox nonempty and
    // prevent reconnect/redelivery of the unacknowledged command.
    handle.publish_latest(PublishRequest::response("state".into(), "latest".into())).unwrap();
    handle
        .publish(response("outgoing while command queue is full"))
        .await
        .unwrap();
    let publish = receive_publish(&mut stream).await;
    assert_eq!(publish.topic, "test/responses");
    acknowledge(&mut stream, publish.pkid).await;
    assert_eq!(commands.recv().await.unwrap(), b"first");
    let mut stream = ready(&listener, true).await;
    let state = receive_publish(&mut stream).await;
    assert_eq!(state.topic, "state");
    acknowledge(&mut stream, state.pkid).await;
    second.dup = true;
    write_packet(&mut stream, Packet::Publish(second)).await;
    assert!(matches!(read_packet(&mut stream).await, Packet::PubAck(ack) if ack.pkid == 2));
    assert_eq!(commands.recv().await.unwrap(), b"second");
    finish(handle, &mut stream, task).await;
}

#[tokio::test]
async fn shutdown_is_bounded_with_unconfirmed_responses() {
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let (handle, _commands, task) = start_manager(&listener, 4);
    let mut stream = ready(&listener, false).await;
    handle
        .publish(response("never acknowledged"))
        .await
        .unwrap();
    receive_publish(&mut stream).await;
    let connected = handle.connected.clone();
    drop(handle);
    timeout(Duration::from_secs(3), task)
        .await
        .expect("shutdown respects deadline")
        .unwrap();
    assert!(!connected.load(Ordering::SeqCst));
}

fn mapped_config() -> AdapterConfig {
    serde_json::from_value(serde_json::json!({
        "network":{"reconnect_initial_ms":100,"reconnect_max_ms":200},
        "topic_mappings":[
            {"name":"input", "direction":"mqtt_to_ros2", "mqtt_topic":"data/input",
             "ros_topic":"/sensor/input", "msg_type":"std_msgs/msg/String", "max_payload_bytes":200000},
            {"name":"fast", "direction":"mqtt_to_ros2", "mqtt_topic":"data/fast",
             "ros_topic":"/sensor/fast", "msg_type":"std_msgs/msg/Float64", "mqtt":{"qos":0}},
            {"name":"output", "direction":"ros2_to_mqtt", "mqtt_topic":"data/output",
             "ros_topic":"/sensor/output", "msg_type":"std_msgs/msg/Float64"}
        ]
    })).unwrap()
}

#[tokio::test]
async fn mapped_topics_preserve_names_qos_and_large_payloads() {
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let (handle, mut commands, mut forwarded, task) =
        start_configured(&listener, 4, mapped_config());
    let (mut stream, subscribe) = handshake_all(&listener, false).await;
    assert_eq!(
        subscribe
            .filters
            .iter()
            .map(|filter| (filter.path.as_str(), filter.qos))
            .collect::<Vec<_>>(),
        vec![
            ("test/commands", QoS::AtLeastOnce),
            ("data/input", QoS::AtLeastOnce),
            ("data/fast", QoS::AtMostOnce)
        ]
    );
    write_packet(
        &mut stream,
        Packet::SubAck(SubAck::new(
            subscribe.pkid,
            vec![
                SubscribeReasonCode::Success(QoS::AtLeastOnce),
                SubscribeReasonCode::Success(QoS::AtLeastOnce),
                SubscribeReasonCode::Success(QoS::AtMostOnce),
            ],
        )),
    )
    .await;

    let body = serde_json::json!({"data":"x".repeat(100_000)}).to_string();
    let mut publish = Publish::new("data/input", QoS::AtLeastOnce, body.as_bytes());
    publish.pkid = 1;
    write_packet(&mut stream, Packet::Publish(publish)).await;
    assert!(matches!(read_packet(&mut stream).await, Packet::PubAck(ack) if ack.pkid == 1));
    let message = forwarded.recv().await.unwrap();
    assert_eq!(message.topic, "data/input");
    assert_eq!(message.payload, body.as_bytes());
    assert!(
        commands.try_recv().is_err(),
        "mapped input must not enter the command-envelope parser"
    );

    write_packet(
        &mut stream,
        Packet::Publish(Publish::new(
            "data/fast",
            QoS::AtMostOnce,
            br#"{"data":2.0}"#.to_vec(),
        )),
    )
    .await;
    let message = timeout(TEST_TIMEOUT, forwarded.recv())
        .await
        .unwrap()
        .unwrap();
    assert_eq!(message.topic, "data/fast");
    let mut command = Publish::new("test/commands", QoS::AtLeastOnce, b"legacy".to_vec());
    command.pkid = 2;
    write_packet(&mut stream, Packet::Publish(command)).await;
    assert!(matches!(read_packet(&mut stream).await, Packet::PubAck(ack) if ack.pkid == 2));
    assert_eq!(commands.recv().await.unwrap(), b"legacy");
    finish(handle, &mut stream, task).await;
}

#[tokio::test]
async fn partial_subscription_rejection_keeps_connection_unready() {
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let (handle, _commands, _forwarded, task) = start_configured(&listener, 4, mapped_config());
    handle
        .publish(response("wait for all subscriptions"))
        .await
        .unwrap();
    let (mut stream, subscribe) = handshake_all(&listener, false).await;
    write_packet(
        &mut stream,
        Packet::SubAck(SubAck::new(
            subscribe.pkid,
            vec![
                SubscribeReasonCode::Success(QoS::AtLeastOnce),
                SubscribeReasonCode::Failure,
                SubscribeReasonCode::Success(QoS::AtMostOnce),
            ],
        )),
    )
    .await;
    assert_eq!(
        timeout(TEST_TIMEOUT, stream.read_u8())
            .await
            .unwrap()
            .unwrap_err()
            .kind(),
        std::io::ErrorKind::UnexpectedEof
    );
    assert!(!handle.connected.load(Ordering::SeqCst));
    let (mut stream, subscribe) = handshake_all(&listener, true).await;
    let codes = subscribe
        .filters
        .iter()
        .map(|filter| SubscribeReasonCode::Success(filter.qos))
        .collect();
    write_packet(
        &mut stream,
        Packet::SubAck(SubAck::new(subscribe.pkid, codes)),
    )
    .await;
    let publish = receive_publish(&mut stream).await;
    acknowledge(&mut stream, publish.pkid).await;
    finish(handle, &mut stream, task).await;
}

#[tokio::test]
async fn mixed_qos_and_retain_flags_reach_the_broker_without_stalling() {
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let (handle, _commands, task) = start_manager(&listener, 4);
    let mut stream = ready(&listener, false).await;
    for index in 0..48 {
        handle
            .publish(PublishRequest {
                topic: "data/telemetry".into(),
                payload: index.to_string(),
                qos: if index % 2 == 0 {
                    QoS::AtMostOnce
                } else {
                    QoS::AtLeastOnce
                },
                retain: index % 2 == 0,
            })
            .await
            .unwrap();
    }
    for index in 0..48 {
        let publish = receive_publish(&mut stream).await;
        assert_eq!(publish.payload.as_ref(), index.to_string().as_bytes());
        assert_eq!(publish.retain, index % 2 == 0);
        if index % 2 == 0 {
            assert_eq!(publish.qos, QoS::AtMostOnce);
            assert_eq!(publish.pkid, 0);
        } else {
            assert_eq!(publish.qos, QoS::AtLeastOnce);
            acknowledge(&mut stream, publish.pkid).await;
        }
    }
    finish(handle, &mut stream, task).await;
}

#[tokio::test]
async fn explicit_device_binding_does_not_fall_back_to_the_default_route() {
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let network: NetworkConfig = serde_json::from_value(serde_json::json!({
        "access":"wifi","interface":"mqtt-no-device","connection_timeout_seconds":1
    }))
    .unwrap();
    let options = MqttOptions::new(
        "bound-client",
        "127.0.0.1",
        listener.local_addr().unwrap().port(),
    );
    let mut connection = new_connection(options, &network);
    let event = timeout(TEST_TIMEOUT, connection.events.recv())
        .await
        .unwrap()
        .unwrap();
    assert!(
        event.is_err(),
        "a missing interface must fail instead of connecting over another network"
    );
}
