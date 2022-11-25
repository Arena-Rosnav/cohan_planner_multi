#!/usr/bin/env python3


import rospy
from cohan_msgs.msg import (
    TrackedAgent,
    TrackedAgents,
    TrackedSegment,
    TrackedSegmentType,
)
from ford_msgs.msg import Clusters


class ArenaAgents(object):
    def __init__(
        self,
    ):
        self.Segment_Type = TrackedSegmentType.TORSO

    def AgentsPub(self):
        rospy.init_node("arena_agents", anonymous=True)
        self.arena_agents_sub = rospy.Subscriber("/obst_odom", Clusters, self.ArenaCB)
        self.tracked_agents_pub = rospy.Publisher(
            "/tracked_agents", TrackedAgents, queue_size=1
        )
        rospy.spin()

    def ArenaCB(self, msg):
        tracked_agents = TrackedAgents()
        for agent_id in range(len(msg.mean_points)):
            agent_segment = TrackedSegment()
            agent_segment.type = self.Segment_Type
            agent_segment.pose.pose.position = msg.mean_points[agent_id]
            agent_segment.twist.twist.linear = msg.velocities[agent_id]
            tracked_agent = TrackedAgent()
            tracked_agent.track_id = agent_id
            tracked_agent.type = 1  # 1 for human 0 for robot
            tracked_agent.segments.append(agent_segment)
            tracked_agents.agents.append(tracked_agent)
        if tracked_agents.agents:
            tracked_agents.header.stamp = rospy.Time.now()
            tracked_agents.header.frame_id = "map"
            self.tracked_agents_pub.publish(tracked_agents)


if __name__ == "__main__":
    agents = ArenaAgents()
    agents.AgentsPub()
