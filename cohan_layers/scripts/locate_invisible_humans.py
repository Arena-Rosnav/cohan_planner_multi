#!/usr/bin/env python
# Brief: Node for detecting and publishing invisible humans
# Author: Phani Teja Singamaneni

import rospy
import numpy as np
import tf2_ros
import math
import tf2_geometry_msgs
from sensor_msgs.msg import LaserScan
from geometry_msgs.msg import PoseStamped, Point
from cohan_msgs.msg import PointArray
from visualization_msgs.msg import Marker, MarkerArray
from nav_msgs.msg import OccupancyGrid
from utils import *


class LaserContour(object):
  def __init__(self):
    rospy.init_node('laser_coutour')

    self.x = []
    self.y = []
    self.x_vis = []
    self.y_vis = []
    self.corners = [[],[]]
    self.rays = [[],[]]
    self.centers = [[],[]]
    self.locating = False
    self.info = []
    self.map = []

    rospy.Subscriber('base_scan_filtered', LaserScan, self.laserCB)
    rospy.Subscriber("/move_base/local_costmap/costmap", OccupancyGrid, self.mapCB)
    self.pub_invis_human_viz = rospy.Publisher('invisible_humans_markers', MarkerArray, queue_size = 10)
    self.pub_invis_human = rospy.Publisher('invisible_humans', PointArray, queue_size = 10)

    #Intialize tf2 transform listener
    self.tf = tf2_ros.Buffer()
    self.listener = tf2_ros.TransformListener(self.tf)

    # rospy.Timer(rospy.Duration(0.01), self.locate_humans)

    # Keep the node alive
    rospy.spin()

  def laserCB(self, scan):
    self.saved = False
    prev_point = [0.0,0.0]
    if not self.locating:
      self.x = []
      self.y = []
      self.x_vis = []
      self.y_vis = []
      self.corners = [[],[]]
      self.rays = [[],[]]
      for i in range(0,len(scan.ranges)):
        angle = scan.angle_min + (i * scan.angle_increment)
        if abs(angle) < 1.2:
          self.x_vis[len(self.x_vis):] = [scan.ranges[i]*np.cos(angle)]
          self.y_vis[len(self.y_vis):] = [scan.ranges[i]*np.sin(angle)]
          idx = len(self.x_vis) - 1
          if(idx>0):
            dist = np.linalg.norm([prev_point[0]-self.x_vis[idx], prev_point[1]-self.y_vis[idx]])
            check = (abs(prev_point[0]-self.x_vis[idx]) >= 0.5 or abs(prev_point[1]-self.y_vis[idx]) >= 0.5)
            under_rad = min(np.linalg.norm([self.x_vis[idx], self.y_vis[idx]]), np.linalg.norm([prev_point[0], prev_point[1]]))
            if(dist>0.15 and under_rad<=5.0 and check):
              if(np.linalg.norm([prev_point[0], prev_point[1]]) < np.linalg.norm([self.x_vis[idx], self.y_vis[idx]])):
                self.corners[0][len(self.corners[0]):] = [prev_point[0]]
                self.corners[1][len(self.corners[1]):] = [prev_point[1]]
                self.rays[0][len(self.rays[0]):] = [self.x_vis[idx]]
                self.rays[1][len(self.rays[1]):] = [self.y_vis[idx]]
              else:
                self.corners[0][len(self.corners[0]):] = [self.x_vis[idx]]
                self.corners[1][len(self.corners[1]):] = [self.y_vis[idx]]
                self.rays[0][len(self.rays[0]):] = [prev_point[0]]
                self.rays[1][len(self.rays[1]):] = [prev_point[1]]

          prev_point = [self.x_vis[idx], self.y_vis[idx]]

        self.x[len(self.x):] = [scan.ranges[i]*np.cos(angle)]
        self.y[len(self.y):] = [scan.ranges[i]*np.sin(angle)]
      self.locate_humans()
      self.saved = True


  def mapCB(self, map):
    self.info = map.info
    self.map = map.data

  def locate_humans(self):
    # The robot position is same irrespective of the map position as the laser is with respect to laser frame
    # Hence you can use the fixed cordinates for the robot
    #   position:
    #   x: -0.275
    #   y: -0.055
    #   z: -0.1
    # orientation:
    #   x: 0.0
    #   y: 0.0
    #   z: 0.0
    #   w: 1.0
    self.locating = True
    self.centers = [[],[]]
    center_points = []
    marker_array = MarkerArray()
    inv_humans = PointArray()
    inv_humans.header.frame_id = "map"
    if len(self.map) > 1 and len(self.corners[0])>0:
      for i in range(0,len(self.corners[0])):
        x = self.corners[0][i]
        y = self.corners[1][i]
        x1 = self.rays[0][i]
        y1 = self.rays[1][i]

        if math.isnan(x):
          x = 7.0
        if math.isnan(y):
          y = 7.0
        if math.isnan(x1):
          x1 = 7.0
        if math.isnan(y1):
          y1 = 7.0

        l_or_r = checkPoint([-0.275,-0.55], [1.0,0.0],[x,y])
        v_mag = np.linalg.norm([x1-x, y1-y])
        ux = (x1-x)/v_mag
        uy = (y1-y)/v_mag
        xt = x
        yt = y
        alp = 0.1
        center = [0,0]
        hum_rad = 0.1
        while(True):
          if(l_or_r == 1):
            pt = getLeftPoint([x,y],[x1,y1],[xt,yt],dist = hum_rad)
          elif(l_or_r == -1):
            pt = getRightPoint([x,y],[x1,y1],[xt,yt],dist = hum_rad)

          center = [(xt+pt[0])/2, (yt+pt[1])/2]
          l_pt, r_pt = get2Points([xt, yt],pt, radius = hum_rad)
          in_pose_l = PoseStamped()
          in_pose_r = PoseStamped()
          in_pose_temp = PoseStamped()
          in_pose_l.pose.position.x = l_pt[0]
          in_pose_l.pose.position.y = l_pt[1]
          in_pose_l.pose.orientation.w = 1
          in_pose_r.pose.position.x = r_pt[0]
          in_pose_r.pose.position.y = r_pt[1]
          in_pose_r.pose.orientation.w = 1
          in_pose_temp.pose.position.x = center[0]
          in_pose_temp.pose.position.y = center[1]
          in_pose_temp.pose.orientation.w = 1
          point_transform = self.tf.lookup_transform("odom", "base_laser_link", rospy.Time(),rospy.Duration(0.001))
          p1 = tf2_geometry_msgs.do_transform_pose(in_pose_l, point_transform)
          p2 = tf2_geometry_msgs.do_transform_pose(in_pose_r, point_transform)
          p3 = tf2_geometry_msgs.do_transform_pose(in_pose_temp, point_transform)
          mx_l, my_l = worldTomap(p1.pose.position.x,p1.pose.position.y, self.info)
          mx_r, my_r = worldTomap(p2.pose.position.x,p2.pose.position.y, self.info)
          mx_c, my_c = worldTomap(p3.pose.position.x,p3.pose.position.y, self.info)
          # add +0.055 to y to get the pose in map frame
          m_idx_l = getIndex(mx_l, my_l, self.info)
          m_idx_r = getIndex(mx_r, my_r, self.info)
          m_idx_c = getIndex(mx_c, my_c, self.info)
          xt = xt + alp*ux
          yt = yt + alp*uy

          if m_idx_c > len(self.map) - 1 or m_idx_l > len(self.map) - 1 or m_idx_r > len(self.map) - 1:
            continue

          if (self.map[m_idx_l] == 0 and self.map[m_idx_r] == 0 and self.map[m_idx_c] == 0) or abs(xt)>=abs(x1) or abs(yt)>=abs(y1):
            break

        self.centers[0][len(self.centers[0]):] = [center[0]]
        self.centers[1][len(self.centers[1]):] = [center[1]]

        c_point = Point()
        c_point.x = p3.pose.position.x
        c_point.y = p3.pose.position.y + 0.055
        c_point.z = 0.0
        inv_humans.points.append(c_point)

        marker = Marker()
        marker.header.frame_id = "map"
        marker.id = i
        marker.type = marker.CYLINDER
        marker.action = marker.ADD
        marker.pose.orientation.w = 1
        marker.pose.position.x = p3.pose.position.x #(p1.pose.position.x + p2.pose.position.x)/2
        marker.pose.position.y = p3.pose.position.y + 0.055 #(p1.pose.position.y + p2.pose.position.y)/2 + 0.055
        marker.pose.position.z = 0.6
        t = rospy.Duration(0.1)
        marker.lifetime = t
        marker.scale.x = 0.6
        marker.scale.y = 0.6
        marker.scale.z = 1.2
        marker.color.a = 1.0
        marker.color.r = 1.0
        marker_array.markers.append(marker)
      self.pub_invis_human_viz.publish(marker_array)
      self.pub_invis_human.publish(inv_humans)
    self.locating = False



if __name__ == '__main__':
  cont = LaserContour()