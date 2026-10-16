#!/usr/bin/env python3
"""Read-only SDK example for an already launched demo asset."""
import argparse
import rclpy
from chaos_sdk import Robot


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--asset', default='vehicle_001')
    args = parser.parse_args()
    rclpy.init()
    node = rclpy.create_node('chaos_sdk_example')
    robot = Robot(node, args.asset)
    try:
        if not robot.wait_ready(5.0):
            raise RuntimeError('Runtime description service was not discovered')
        future = robot.describe()
        rclpy.spin_until_future_complete(node, future, timeout_sec=5.0)
        if not future.done():
            raise RuntimeError('Description request timed out')
        description = future.result()
        print(f'{description.asset_id}: {list(description.capabilities)}')
    finally:
        robot.close()
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
