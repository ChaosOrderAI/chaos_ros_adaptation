#!/usr/bin/env python3
"""Read-only SDK example for an already launched demo asset.

The client is native C++ and owns its ROS context, its node and its executor
thread. The calls return futures; ``Future.result()`` blocks until the native
request completed. The GIL is released while the call waits, so feedback and
service callbacks of the same client keep running.
"""
import argparse

from chaos_sdk import Robot


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--asset', default='vehicle_001')
    args = parser.parse_args()
    robot = Robot(args.asset)
    try:
        # wait_ready waits for the discovery of the description service, it
        # does not wait for the robot to be ready.
        if not robot.wait_ready(5.0):
            raise RuntimeError('Runtime description service was not discovered')
        description = robot.describe().result()
        print(f'{description.asset_id}: {list(description.capabilities)}')
    finally:
        # close() resolves every pending future and joins the executor thread.
        robot.close()


if __name__ == '__main__':
    main()
