//! MQTT <-> ROS 2 adapter binary.
//!
//! Usage: mqtt_ros2_adapter [--config PATH | --check-config PATH]

mod adapter;
mod config;
mod envelope;
mod forwarding;
mod mqtt;
mod replay;
mod ros2;
mod routes;

use std::collections::HashMap;
use std::path::PathBuf;
use std::process::ExitCode;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;

use futures::StreamExt;
use tokio::sync::{mpsc, watch};
use tracing::{error, info, warn};

use crate::adapter::Adapter;
use crate::config::{load_config, AdapterConfig};
use crate::mqtt::spawn_manager;
use crate::ros2::TypedPublisher;
use crate::routes::{configured_routes, RouteTable};

const COMMAND_CHANNEL_CAPACITY: usize = 64;
const MQTT_SHUTDOWN_TIMEOUT: Duration = Duration::from_secs(3);

fn parse_cli_args(args: impl IntoIterator<Item = String>) -> Result<(PathBuf, bool), String> {
    let mut args = args.into_iter();
    let Some(arg) = args.next() else {
        return Ok((PathBuf::from("config.json"), false));
    };
    let check_only = arg == "--check-config";
    let path = match arg.as_str() {
        "-c" | "--config" | "--check-config" => match args.next() {
            Some(path) => PathBuf::from(path),
            None => return Err(format!("missing value for {arg}")),
        },
        _ if !arg.starts_with('-') => PathBuf::from(arg),
        _ => return Err(format!("unknown argument: {arg}")),
    };
    if let Some(extra) = args.next() {
        return Err(format!("unexpected argument: {extra}"));
    }
    Ok((path, check_only))
}

fn build_route_table(config: &AdapterConfig) -> Result<RouteTable, String> {
    RouteTable::new(configured_routes(config.route_profile, &config.routes)?)
}

fn main() -> ExitCode {
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| tracing_subscriber::EnvFilter::new("info")),
        )
        .init();

    let (config_path, check_only) = match parse_cli_args(std::env::args().skip(1)) {
        Ok(args) => args,
        Err(error) => {
            eprintln!("{error}\nusage: mqtt_ros2_adapter [--config PATH | --check-config PATH]");
            return ExitCode::from(2);
        }
    };
    let config = match load_config(&config_path) {
        Ok(config) => config,
        Err(error) => {
            error!(path = %config_path.display(), %error, "failed to load configuration");
            return ExitCode::FAILURE;
        }
    };
    let route_table = match build_route_table(&config) {
        Ok(table) => table,
        Err(error) => {
            error!(%error, "invalid route configuration");
            return ExitCode::FAILURE;
        }
    };

    if check_only {
        info!(path = %config_path.display(), "configuration valid; no ROS or MQTT connection started");
        return ExitCode::SUCCESS;
    }

    // r2r binds message types at build time from the ament index; the ROS 2
    // environment must be sourced (AMENT_PREFIX_PATH) before building or
    // running. Fail early with a clear message instead of a linker/init error.
    if std::env::var_os("AMENT_PREFIX_PATH").is_none() {
        error!("AMENT_PREFIX_PATH is not set; source /opt/ros/<distro>/setup.bash first");
        return ExitCode::FAILURE;
    }

    let context = match r2r::Context::create() {
        Ok(context) => context,
        Err(error) => {
            error!(%error, "failed to create ROS 2 context");
            return ExitCode::FAILURE;
        }
    };
    let mut node = match r2r::Node::create(context, &config.ros2.node_name, "") {
        Ok(node) => node,
        Err(error) => {
            error!(%error, "failed to create ROS 2 node");
            return ExitCode::FAILURE;
        }
    };
    let qos_depth = config.ros2.qos_depth;
    let make_qos = || r2r::QosProfile::default().keep_last(qos_depth).reliable();

    let mut publishers = HashMap::new();
    for route in route_table.iter() {
        let topic = format!("{}/{}", config.ros2.topic_prefix, route.topic_suffix);
        match TypedPublisher::create(&mut node, route, &topic, make_qos()) {
            Ok(publisher) => {
                publishers.insert(route.command.clone(), publisher);
            }
            Err(error) => {
                error!(%error, topic = %topic, "failed to create ROS 2 publisher");
                return ExitCode::FAILURE;
            }
        }
    }

    let result_topic = format!("{}/command/result", config.ros2.topic_prefix);
    let forwarding = match forwarding::TopicForwarding::prepare(&mut node, &config.topic_mappings) {
        Ok(forwarding) => forwarding,
        Err(error) => {
            error!(%error, "failed to initialize configured topic forwarding");
            return ExitCode::FAILURE;
        }
    };
    let mut result_subscriber =
        match node.subscribe::<r2r::std_msgs::msg::String>(&result_topic, make_qos()) {
            Ok(subscriber) => subscriber,
            Err(error) => {
                error!(%error, topic = %result_topic, "failed to subscribe to ROS 2 result topic");
                return ExitCode::FAILURE;
            }
        };

    // Spin the ROS 2 node on a dedicated thread; subscriptions are drained by
    // an async task below. The node must outlive every publisher, so it is
    // only dropped after the adapter loop has finished.
    let spin_stop = Arc::new(AtomicBool::new(false));
    let spin_thread = {
        let spin_stop = spin_stop.clone();
        std::thread::spawn(move || {
            while !spin_stop.load(Ordering::Relaxed) {
                node.spin_once(Duration::from_millis(10));
            }
        })
    };

    let runtime = match tokio::runtime::Runtime::new() {
        Ok(runtime) => runtime,
        Err(error) => {
            error!(%error, "failed to start async runtime");
            return ExitCode::FAILURE;
        }
    };

    let exit_code = runtime.block_on(async {
        let (command_tx, command_rx) = mpsc::channel::<Vec<u8>>(COMMAND_CHANNEL_CAPACITY);
        let (result_tx, result_rx) = mpsc::channel::<String>(COMMAND_CHANNEL_CAPACITY);
        let (forward_tx, forward_rx) = mpsc::channel(COMMAND_CHANNEL_CAPACITY);

        let (mqtt_handle, mut mqtt_task) = match spawn_manager(&config, command_tx, forward_tx) {
            Ok(spawned) => spawned,
            Err(error) => {
                error!(%error, "failed to set up MQTT transport");
                return ExitCode::FAILURE;
            }
        };

        // Forward ROS 2 result messages into the adapter loop.
        tokio::spawn(async move {
            while let Some(message) = result_subscriber.next().await {
                if result_tx.send(message.data).await.is_err() {
                    break;
                }
            }
        });

        let (shutdown_tx, shutdown_rx) = watch::channel(false);
        tokio::spawn(async move {
            let mut sigint =
                tokio::signal::unix::signal(tokio::signal::unix::SignalKind::interrupt())
                    .expect("install SIGINT handler");
            let mut sigterm =
                tokio::signal::unix::signal(tokio::signal::unix::SignalKind::terminate())
                    .expect("install SIGTERM handler");
            tokio::select! {
                _ = sigint.recv() => {}
                _ = sigterm.recv() => {}
            }
            info!("signal received, shutting down");
            let _ = shutdown_tx.send(true);
        });

        let forwarding_tasks =
            forwarding.start(mqtt_handle.clone(), forward_rx, shutdown_rx.clone());
        let adapter = Adapter::new(&config, route_table, publishers, mqtt_handle);
        // `run` consumes the adapter; dropping its MQTT handle lets the
        // manager flush queued replies and disconnect cleanly.
        adapter.run(command_rx, result_rx, shutdown_rx).await;
        for task in forwarding_tasks {
            task.abort();
            let _ = task.await;
        }
        // The manager wakes immediately on handle drop and waits for PUBACKs
        // within its own deadline. Keep an outer bound and abort on overrun.
        match tokio::time::timeout(MQTT_SHUTDOWN_TIMEOUT, &mut mqtt_task).await {
            Ok(Ok(())) => {}
            Ok(Err(error)) => {
                error!(%error, "MQTT manager task failed");
                return ExitCode::FAILURE;
            }
            Err(_) => {
                warn!("MQTT manager did not stop in time; aborting it");
                mqtt_task.abort();
                let _ = mqtt_task.await;
            }
        }
        ExitCode::SUCCESS
    });

    spin_stop.store(true, Ordering::Relaxed);
    if spin_thread.join().is_err() {
        error!("ROS 2 spin thread panicked");
    }
    exit_code
}

#[cfg(test)]
mod cli_tests {
    use super::*;

    fn parse(args: &[&str]) -> Result<(PathBuf, bool), String> {
        parse_cli_args(args.iter().map(|value| value.to_string()))
    }

    #[test]
    fn check_config_is_explicit_and_legacy_arguments_still_work() {
        assert_eq!(parse(&[]).unwrap(), (PathBuf::from("config.json"), false));
        for args in [&["--config", "asset.json"][..], &["-c", "asset.json"], &["asset.json"]] {
            assert_eq!(parse(args).unwrap(), (PathBuf::from("asset.json"), false));
        }
        assert_eq!(parse(&["--check-config", "asset.json"]).unwrap(),
                   (PathBuf::from("asset.json"), true));
        assert!(parse(&["--check-config"]).is_err());
        assert!(parse(&["--check-config", "asset.json", "extra"]).is_err());
        assert!(parse(&["--unknown"]).is_err());
    }
}
