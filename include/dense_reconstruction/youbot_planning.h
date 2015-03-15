/* Copyright (c) 2015, Stefan Isler, islerstefan@bluewin.ch
*
This file is part of dense_reconstruction, a ROS package for...well,

dense_reconstruction is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
dense_reconstruction is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU Lesser General Public License for more details.
You should have received a copy of the GNU Lesser General Public License
along with dense_reconstruction. If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "ros/ros.h"
#include <moveit/move_group_interface/move_group.h>
#include <moveit/planning_scene_monitor/planning_scene_monitor.h>
#include <moveit/robot_state/robot_state.h>

#include <Eigen/Core>
#include <Eigen/StdVector>

#include <tf/transform_listener.h>

#include <sensor_msgs/CameraInfo.h>
#include <std_msgs/String.h>

#include <movements/core>
#include <movements/ros_movements.h>
#include <movements/translation.h>
#include <movements/linear_movement.h>
#include <movements/in_out_spiral.h>

#include <actionlib/client/simple_action_client.h>
#include <control_msgs/FollowJointTrajectoryAction.h>

#include "dense_reconstruction/robot_planning_interface.h"

typedef actionlib::SimpleActionClient<control_msgs::FollowJointTrajectoryAction> ActionClient;

namespace dense_reconstruction
{
  
/// class that autonomously extracts hand-eye pose correspondences in a robotic setup and estimates the hand-eye-calibration from it
class YoubotPlanner: public RobotPlanningInterface
{
public:
  class SpaceInfo;
  
  /// constructor
  /** Initializes the autonomous hand eye calibrator by reading all parameters from the parameter server
   * @param _n handle of the node the calibrator runs in
   * @throws ROS_FATAL if not all necessary parameters are given and shuts down the node
   */
  YoubotPlanner( ros::NodeHandle* _n );
  
  /** returns the name of the global planning frame (currently "dr_origin" for 'dense reconstruction origin) and does all calculations needed in order to set up the tf tree for that frame, e.g. initialize SVO, save transformation from SVO frame (world) to (dr_origin) etc.
   */
  virtual std::string initializePlanningFrame();
    
  /** initializes the robot's planning space
   * @param _info information the robot needs to setup its planning space
   */
  virtual bool initializePlanningSpace( PlanningSpaceInitializationInfo& _info );
  
  /** returns the current view */
  virtual View getCurrentView();
  
  /** returns the view space that is available for planning (the idea is that it consists exclusively of poses that are considered to be reachable aforehand by the robot, considering its restraints
   * @param _space pointer to the ViewSpace object that should be filled
   * @return false if it failed
   */
  virtual bool getPlanningSpace( ViewSpace* _space );
  
  /** returns a sub space of the view space with all view points available within a certain range _distance of _view
   * @param _space pointer to the ViewSpace object that should be filled
   * @param _view view from which to take the distance
   * @return false if it failed
   */
  virtual bool getSubPlanningSpace( ViewSpace* _space, View& _view, double _distance );
  
  /** executes the actions needed to retrieve new data, e.g. scanning movements until remode converges
   * @return information about what happened (data received, receival failed )
   */
  virtual RobotPlanningInterface::ReceiveInfo retrieveData();
  
  /** returns the cost to move from the current view to the indicated view
   * @param _target_view the next view
   * @return cost to move to that view
   */
  virtual RobotPlanningInterface::MovementCost calculateCost( View& _target_view );
  
   /** tells the robot to get the camera to a new view
   * @param _target_view where to move to
   * @return false if the operation failed
   */
  virtual bool moveTo( View& _target_view );
  
  /**
   * calculate arm grid points based on given resolution
   * @param _y_res resolution on y-axis [points/m]
   * @param _z_res resolution on z-axis [points/m]
   * @throws std::invalid_argument if negative or zero resolutions are given
   * @param _joint_values joint values for links 2,3 & 4 that achieve the poses in the grid
   * @param _grid (y/z) grid in a vector containing only poses achievable by the youbot arm (checked for self-collisions) (relative poses with respect to arm_link_2 position in the current configuration)
   */
  void calculateArmGrid( double _y_res, double _z_res, std::vector< Eigen::Vector3d, Eigen::aligned_allocator<Eigen::Vector3d> >& _joint_values, std::vector< Eigen::Vector2d, Eigen::aligned_allocator<Eigen::Vector2d> >* _grid=nullptr );
  
  /**
   * attempts to calculate a joint trajectory for a given arm joint configuration, returns true if a trajectory was successfully calculated
   * @param _joint_values the arm joint configuration (joint_2,joint_3,joint_4)
   * @param _radius max radius of the spiral created
   * @param _joint_trajectory vector with the joint values that make up the trajectory (joint 2, joint 3, joint 4)
   * @param _planning_attempts max number of planning attempts before fail is returned
   */
  bool calculateScanTrajectory( Eigen::Vector3d _joint_values, double _radius, std::vector< Eigen::Vector3d, Eigen::aligned_allocator<Eigen::Vector3d> >& _joint_trajectory, int _planning_attempts=10 );
  
  /**
   * returns the pose of a link _link_name in robot state _state, in the planning frame
   */
  movements::Pose linkPoseInRobotState( std::string _name, robot_state::RobotState& _state );
  
  
  /** Calls computeCartesianPath(...) for a given robot state _state to build a moveit plan for the movements path. The movements path must be for the link _link_name. If planning fails for _planning_attempts times, the function tries to localize path points that are a problem and removes them. The maximal number or percentage of points that can be removed before the function returns failure can be specified in _max_dropoff: currently only works if the resolution (max distance between points) of the path passed is less than eef_step used in the cartesian path calculation inside the function (currently 0.1m)
   * @param _waypoints path for the link
   * @param _link_name name of the link for which the path is planned
   * @param _state a robot state
   * @param _joint_values returned path (values for joints 2,3 and 4)
   * @param _planning_attempts number of planning attempts to be taken if planning fails before giving up
   * @param _max_dropoff If less than 1: Represents the percentage of the maximal number of points that may be dropped to find a valid cartesian path, if equal or higher than 1 it represents the absolute number of points that may be dropped, if _max_dropoff<=0 no filter stage is run
   * @return true if plan could be calculated, false if not
   */
  bool filteredPlanFromMovementsPathForRobotState( const movements::PoseVector& _waypoints, std::string _link_name, const robot_state::RobotState& _state, std::vector< Eigen::Vector3d, Eigen::aligned_allocator<Eigen::Vector3d> >& _joint_values, int _planning_attempts=3, double _max_dropoff = 0.2 );
  
  /// runs one step of the autonomous calibration process
  /** The method iterates through the joint space and estimates the hand-eye transformation along the way.
  * Based on the hand-eye transformation it also estimates the position of the calibration target, e.g.
  * the checkerboard in order to skip joint positions where the target is expected not to be visible. The
  * current estimate is printed to the console. At each run another joint position is assumed.
  * @return false if all positions were covered
  */
  bool runSingleIteration();
  
  /** attempts to get a new end effector pose from tf (transform from end effector frame to robot base)
   * @param _max_wait_time the maximal time to wait for a new transformation to be available
   * @return empty Pose() if no complete tf tree was published for the transformation in the given time
   */
  movements::Pose getEndEffectorPoseFromTF( ros::Duration _max_wait_time= ros::Duration(5.0) );
  
  /** loads pose of link _link relative to the planning frame (using tf, not moveit)
   * That is transform to transforms entities in _link frame to the planning frame
   */
  movements::Pose getCurrentLinkPose( std::string _link );
  
  /** plans and executes a scanning movement from the current position of arm_link_4
   * @param _max_dropoff see filteredPlanFromMovementsPath
   * @return true if successful, false if not
   */
  bool makeScan( double _max_dropoff=0.2 );
  
  /** moves the base to the target position on a circular (but with varying radius) trajectory around the given _center
   * @return true if successful, false if not
   */
  bool moveBaseCircularlyTo( movements::Pose _target_position, movements::Pose _center, double _radial_speed );
  
  /** commands the base to move to a specific position, doesn't check for success though!
   */
  void moveBaseTo( double _x, double _y, double _theta );
  
  /** executes a trajectory on the base (positions only atm)
   * @param _path the path to follow
   * @param _dt time to pass between sending commands [s]
   */
  bool executeMovementsTrajectoryOnBase( movements::PoseVector& _path, double _dt );
  
  /** attempts to create a cartesian path following the given poses given some constraints
   * @return true if planning and execution were successful
   */
  bool executeMovementsPath( movements::PoseVector& _path, moveit_msgs::Constraints* _constraints=nullptr, double _max_dropoff=0.2 );
  
  /** execute a moveit plan */
  bool executeMoveItPlan( moveit::planning_interface::MoveGroup::Plan& _plan );
  
  /** Calls computeCartesianPath(...) to build a moveit plan for the movements path for the end effector. Path constraints are cleared afterwards, this will affect all constraints set for the robot_ object!
   * @param _waypoints path for the end effector
   * @param _plan returned path
   * @param _planning_attempts number of planning attempts to be taken if planning fails before giving up
   * @param _path_constraints path constraints to use during planning
   * @return true if plan could be calculated, false if not
   */
  bool planFromMovementsPath( movements::PoseVector& _waypoints, moveit::planning_interface::MoveGroup::Plan& _plan, moveit_msgs::Constraints* _path_constraints=nullptr, int _planning_attempts=3 );
  
  /** Calls computeCartesianPath(...) to build a moveit plan for the movements path for the end effector. Path constraints are cleared afterwards, this will affect all constraints set for the robot_ object! If planning fails for _planning_attempts times, the function tries to localize path points that are a problem and removes them. The maximal number or percentage of points that can be removed before the function returns failure can be specified in _max_dropoff: currently only works if the resolution (max distance between points) of the path passed is less than eef_step used in the cartesian path calculation inside the function (currently 0.1m)
   * @param _waypoints path for the end effector
   * @param _plan returned path
   * @param _planning_attempts number of planning attempts to be taken if planning fails before giving up
   * @param _path_constraints path constraints to use during planning
   * @param _max_dropoff If less than 1: Represents the percentage of the maximal number of points that may be dropped to find a valid cartesian path, if equal or higher than 1 it represents the absolute number of points that may be dropped, if _max_dropoff<=0 no filter stage is run
   * @return true if plan could be calculated, false if not
   */
  bool filteredPlanFromMovementsPath( movements::PoseVector& _waypoints, moveit::planning_interface::MoveGroup::Plan& _plan, moveit_msgs::Constraints* _path_constraints=nullptr, int _planning_attempts=3, double _max_dropoff = 0.2 );
  
  /** creates a static orientation constraint for the end effector based on the pose _base_pose that can be added to a moveit_msgs::Constraints by pushing it onto its orientation_constraints vector 
   * @param _base_pose pose whose orientation will be used to construct the constraint
   * @param _weight weight that is set for the constraint (importance if several constraints were to contradict each other)
   * @param _x_axis_tolerance tolerance for the x axis
   * @param _y_axis_tolerance tolerance for the y axis
   * @param _z_axis_tolerance tolerance for the z axis
   */
  moveit_msgs::OrientationConstraint getFixedEEFLinkOrientationConstraint( movements::Pose& _base_pose, int _weight=100, double _x_axis_tolerance=0.05, double _y_axis_tolerance=0.05, double _z_axis_tolerance=0.05 );
  
  /** sets the end effector planning frame
   */
  void setEndEffectorPlanningFrame( std::string _name );
  
  /** Attempts to set up the tf structure in order to combine the svo and robot trees. The origin is currently equal to the odom origin, a fixed transform is being setup between the svo frame and the dr_origin frame by using the transform between one SVO pose at startup and the robot tree, waits until SVO cam_pos is available on /tf */
  void initializeTF();
  
  /** loads an upper arm trajectory form files saved with saveUpperArmTrajectoryPositions */
  moveit_msgs::RobotTrajectory loadUpperArmTrajectory( std::string _filename );
  
  /** saves upper arm trajectory positions to file (links 2 through 5)*/
  void saveUpperArmTrajectoryPositions( std::string _filename, const moveit_msgs::RobotTrajectory& _trajectory );
  
private:
  ros::NodeHandle* ros_node_;
  ros::ServiceClient eye_client_;
  ros::ServiceClient hand_client_;
  ActionClient base_trajectory_sender_;
  boost::shared_ptr<moveit_msgs::RobotTrajectory> spin_trajectory_;
  
  double base_move_angle_; // angle step size [rad] the base is moved between reconstruction steps
  double base_radial_speed_; // base speed to move [rad/s]
  movements::Pose base_movement_center_; // center around which the base moves in the base_controller control frame
  // the radius is calculated automatically by taking the distance between the movement center and the position when the movement is started
    
  ros::Publisher remode_commander_;
  std_msgs::String remode_stopandsmooth_;
  std_msgs::String remode_start_;
  
  std::string planning_group_; // the group for which planning is done
  std::string base_planning_frame_; /// relative base frame for end effector calculations
  std::string end_effector_planning_frame_; /// name of the frame for which the end effector pose shall be controlled
  
  boost::shared_ptr<planning_scene_monitor::PlanningSceneMonitor> scene_;
  boost::shared_ptr<moveit::planning_interface::MoveGroup> robot_;
  
  tf::TransformListener tf_listener_;
    
  /// plans and executes a plan to the currently loaded target - blocks until completion
  /** completion means that the robot state is closer to the target than set in the tolerance and its velocity is approximately zero in all joint
   * @return true if movement was executed successfully
   */
  bool planAndMove();
  
  /// returns whether MoveIt believes the robot state represented in _robot to be free of collisions or not given the current scene (but without the calibration pattern)
  /**
   * @param _robot robot state to check
   * @param _scene the current scene
   */
  bool isCollisionFree( planning_scene_monitor::LockedPlanningSceneRO& _scene, robot_state::RobotState& _robot );
  
  
};

class YoubotPlanner::SpaceInfo: public RobotPlanningInterface::PlanningSpaceInitializationInfo::RobotSpaceInfo
{
public:
  Eigen::Vector3d approximate_relative_object_position_; // approximate position of the object to be reconstructed relative to the initial position of the robot
  virtual std::string type();
  
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW
};

}