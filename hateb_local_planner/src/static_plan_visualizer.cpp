/*********************************************************************
 *
 * Software License Agreement (BSD License)
 *
 * Copyright (c) 2020 LAAS/CNRS
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of the institute nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * Author: Phani Teja Singamaneni (email:ptsingaman@laas.fr)
 *********************************************************************/
#define NAME "HATebStaticPlanVisualizer"
#define GET_PLAN_SRV "move_base/GlobalPlanner/make_plan"
#define OPTIMIZE_SRV "move_base/HATebLocalPlannerROS/optimize"
#define AGENTS_SUB "/tracked_agents"
#define ROBOT_GOAL_SUB "/clicked_point"
#define DEFAULT_AGENT_PART cohan_msgs::TrackedSegmentType::TORSO

#include <hateb_local_planner/static_plan_visualizer.h>

namespace hateb_local_planner
{
    StaticPlanVisualization::StaticPlanVisualization(tf2_ros::Buffer &tf2_) : initialized_(false), predict_behind_robot_(true), got_robot_plan(false),
                                                                              got_agent_plan(false), tf_(tf2_), tfListener_(tf_)
    {
    }

    StaticPlanVisualization::~StaticPlanVisualization()
    {
    }

    void StaticPlanVisualization::initialize()
    {
        if (!initialized_)
        {
            ros::NodeHandle nh("~");

            ns_ = ros::this_node::getNamespace();

            std::string get_plan_srv_name = std::string(GET_PLAN_SRV);
            std::string optimize_srv_name = std::string(OPTIMIZE_SRV);
            if (ns_ != "")
            {
                get_plan_srv_name = ns_ + get_plan_srv_name;
                optimize_srv_name = ns_ + optimize_srv_name;
            }

            getPlan_client = nh.serviceClient<nav_msgs::GetPlan>(get_plan_srv_name, true);
            optimize_client = nh.serviceClient<hateb_local_planner::Optimize>(optimize_srv_name, true);
            agents_sub_ = nh.subscribe(AGENTS_SUB, 1, &StaticPlanVisualization::UpdateStartPoses, this);
            robot_goal_sub_ = nh.subscribe(ROBOT_GOAL_SUB, 1, &StaticPlanVisualization::UpdateGoalsAndOptimize, this);
            optimize_srv_ = nh.advertiseService("optimize_srv", &StaticPlanVisualization::optimize_srv, this);
            initialized_ = true;
        }
    }

    void StaticPlanVisualization::UpdateStartPoses(const cohan_msgs::TrackedAgents &tracked_agents)
    {
        agents_start_poses.clear();
        // tf2_ros::TransformListener tfListener_(tf_);
        tracked_agents_ = tracked_agents;
        for (auto &agent : tracked_agents_.agents)
        {
            for (auto &segment : agent.segments)
            {
                if (segment.type == DEFAULT_AGENT_PART)
                {
                    geometry_msgs::PoseStamped hum_pose;
                    hum_pose.pose = segment.pose.pose;
                    hum_pose.header.frame_id = "map";
                    hum_pose.header.stamp = ros::Time::now();
                    agents_start_poses.push_back(hum_pose);
                }
            }
        }

        try
        {
            std::string base;
            ros::param::param<std::string>("robot_base_frame", base, "base_footprint");
            std::string ros_ns = ros::this_node::getNamespace();
            if (ros_ns != "/")
                base = ros_ns.substr(1, ros_ns.length()) + "/" + base;
            robot_to_map_tf = tf_.lookupTransform("map", base, ros::Time(0));
        }
        catch (tf2::TransformException &ex)
        {
            ROS_WARN("%s", ex.what());
            ros::Duration(1.0).sleep();
        }
        robot_start_pose.header = robot_to_map_tf.header;
        robot_start_pose.pose.position.x = robot_to_map_tf.transform.translation.x;
        robot_start_pose.pose.position.y = robot_to_map_tf.transform.translation.y;
        robot_start_pose.pose.position.z = robot_to_map_tf.transform.translation.z;
        robot_start_pose.pose.orientation.x = robot_to_map_tf.transform.rotation.x;
        robot_start_pose.pose.orientation.y = robot_to_map_tf.transform.rotation.y;
        robot_start_pose.pose.orientation.z = robot_to_map_tf.transform.rotation.z;
        robot_start_pose.pose.orientation.w = robot_to_map_tf.transform.rotation.w;
    }

    void StaticPlanVisualization::UpdateGoalsAndOptimize(const geometry_msgs::PointStamped &robot_goal_point)
    {
        robot_goal_.pose.position = robot_goal_point.point;
        robot_goal_.header = robot_goal_point.header;
        robot_goal_.pose.orientation = robot_start_pose.pose.orientation;
        auto now = ros::Time::now();
        nav_msgs::GetPlan agent_plan_srv, robot_plan_srv;
        cohan_msgs::AgentPathArray hum_path_arr;
        hum_path_arr.header.frame_id = "map";
        hum_path_arr.header.stamp = now;

        // get global robot_plan
        robot_plan_srv.request.start = robot_start_pose;
        robot_plan_srv.request.goal = robot_goal_;
        if (getPlan_client.call(robot_plan_srv))
        {
            if (robot_plan_srv.response.plan.poses.size() > 0)
                got_robot_plan = true;
            else
                got_robot_plan = false;
        }

        int idx = 0;
        for (auto &agent : tracked_agents_.agents)
        {
            if (agent.track_id == 1)
            {
                if (predict_behind_robot_)
                {
                    tf2::Transform behind_tr, robot_to_map_tf_;
                    behind_tr.setOrigin(tf2::Vector3(-0.5, 0.0, 0.0));
                    // behind_tr.setRotation(tf2::Quaternion(3.1416,0.0,0.0));
                    tf2::fromMsg(robot_to_map_tf.transform, robot_to_map_tf_);
                    behind_tr = robot_to_map_tf_ * behind_tr;
                    geometry_msgs::Pose behind_pose;
                    tf2::toMsg(behind_tr, behind_pose);

                    geometry_msgs::PoseStamped agent_goal;
                    agent_goal.header.frame_id = "map";
                    agent_goal.header.stamp = now;
                    agent_goal.pose = behind_pose;

                    agents_goals_.push_back(agent_goal);

                    agent_plan_srv.request.start = agents_start_poses[idx];
                    agent_plan_srv.request.goal = agent_goal;

                    if (getPlan_client.call(agent_plan_srv))
                    {
                        if (agent_plan_srv.response.plan.poses.size() > 0)
                            got_agent_plan = true;
                        else
                            got_agent_plan = false;
                    }

                    cohan_msgs::AgentPath temp;
                    temp.header = agent_goal.header;
                    temp.id = agent.track_id;
                    temp.path = agent_plan_srv.response.plan;
                    hum_path_arr.paths.push_back(temp);
                }
            }
            else
            {
                agents_goals_.push_back(agents_start_poses[idx]);
            }
            idx++;
        }

        agents_plans = hum_path_arr;
        robot_plan = robot_plan_srv.response.plan;

        if (got_agent_plan && got_robot_plan)
        {
            hateb_local_planner::Optimize optim_srv;

            optim_srv.request.robot_plan = robot_plan_srv.response.plan;
            optim_srv.request.agent_path_array = hum_path_arr;

            if (optimize_client.call(optim_srv))
            {
                if (optim_srv.response.success)
                {
                    std::cout << optim_srv.response.message << '\n';
                    std::cout << optim_srv.response.cmd_vel << '\n';
                }
                else
                    ROS_INFO("Optimization failed !!");
            }
        }
    }

    bool StaticPlanVisualization::optimize_srv(std_srvs::SetBool::Request &req, std_srvs::SetBool::Response &res)
    {
        if (got_agent_plan && got_robot_plan)
        {
            hateb_local_planner::Optimize optim_srv;

            optim_srv.request.robot_plan = robot_plan;
            if (req.data)
                optim_srv.request.agent_path_array = agents_plans;
            else
            {
                nav_msgs::GetPlan agent_plan_srv, robot_plan_srv;
                cohan_msgs::AgentPathArray hum_path_arr;
                hum_path_arr.header.frame_id = "map";
                hum_path_arr.header.stamp = ros::Time::now();

                cohan_msgs::AgentPath temp;
                temp.header = agents_start_poses[0].header;
                temp.id = 1;
                temp.path.poses.push_back(agents_start_poses[0]);
                hum_path_arr.paths.push_back(temp);

                optim_srv.request.agent_path_array = hum_path_arr;
            }

            if (optimize_client.call(optim_srv))
            {
                if (optim_srv.response.success)
                {
                    res.success = true;
                    res.message = optim_srv.response.message;
                    std::cout << optim_srv.response.message << '\n';
                }
                else
                {
                    res.success = false;
                    res.message = "Optimization failed..!!";
                    ROS_INFO("Optimization failed !!");
                }
            }
        }

        return true;
    }

} // namespace hateb_local_planner

int main(int argc, char **argv)
{
    ros::init(argc, argv, NAME);

    tf2_ros::Buffer tf2;

    hateb_local_planner::StaticPlanVisualization static_viz(tf2);
    static_viz.initialize();

    ros::spin();

    return 0;
}
