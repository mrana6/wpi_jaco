/*!
 * \jaco_arm_trajectory_node.h
 * \brief Provides for trajectory execution and gripper control of the JACO arm.
 *
 * jaco_arm_trajectory_node creates a ROS node that provides trajectory execution and gripper 
 * control through the Kinova API, and smooth trajectory following through a velocity controller.
 *
 * \author David Kent, WPI - davidkent@wpi.edu
 * \author Mitchell Wills, WPI - mwills@wpi.edu
 */

#ifndef JACO_ARM_TRAJECTORY_NODE_H_
#define JACO_ARM_TRAJECTORY_NODE_H_

#include <ros/ros.h>

#include <actionlib/server/simple_action_server.h>
#include <boost/foreach.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <control_msgs/FollowJointTrajectoryAction.h>
#include <control_msgs/GripperCommandAction.h>
#include <ecl/geometry.hpp>
#include <wpi_jaco_msgs/AngularCommand.h>
#include <wpi_jaco_msgs/CartesianCommand.h>
#include <wpi_jaco_msgs/EStop.h>
#include <wpi_jaco_msgs/GetAngularPosition.h>
#include <wpi_jaco_msgs/GetCartesianPosition.h>
#include <wpi_jaco_msgs/HomeArmAction.h>
#include <wpi_jaco_msgs/JacoFK.h>
#include <wpi_jaco_msgs/QuaternionToEuler.h>
#include <sensor_msgs/JointState.h>
#include <std_srvs/Empty.h>

#include <jaco_sdk/Kinova.API.UsbCommandLayerUbuntu.h>

#define NUM_JACO_JOINTS 6
#define NUM_JACO_FINGER_JOINTS 2
#define NUM_JOINTS (NUM_JACO_JOINTS+NUM_JACO_FINGER_JOINTS)

#define LARGE_ACTUATOR_VELOCITY 0.8378*0.8 //maximum velocity of large actuator (joints 1-3) (rad/s)
#define SMALL_ACTUATOR_VELOCITY 1.0472*0.8 //maximum velocity of small actuator (joints 4-6) (rad/s)
#define TIME_SCALING_FACTOR 1.5 //keep the trajectory at a followable speed

#define DEG_TO_RAD (M_PI/180)
#define RAD_TO_DEG (180/M_PI)

//gains for trajectory follower
#define KP 300.0
#define KV 20.0
#define ERROR_THRESHOLD .03 //threshold in radians for combined joint error to consider motion a success

//gains for finger controller
#define KP_F 7.5
#define KV_F 0.05
#define KI_F 0.1
#define FINGER_ERROR_THRESHOLD 1 //threshold in the JACO API's finger position units to consider a finger position reached

//control types
#define ANGULAR_CONTROL 1
#define CARTESIAN_CONTROL 2

#define NO_ERROR 1 //no error from Kinova API

#define MAX_SPEED_FINGER  3000.0
#define ARM_NAME          "mico"
#define FINGER_SCALE      (1.0/120.0)

namespace jaco
{

/*!
 * \class jacoArmTrajectoryController
 * \brief Provides for trajectory execution and gripper control of the JACO arm.
 *
 * jacoArmTrajectoryController creates a ROS node that provides trajectory execution and gripper 
 * control through the Kinova API, and smooth trajectory following through a velocity controller.
 */
class JacoArmTrajectoryController
{
private:
  // Messages
  ros::Publisher joint_state_pub_; //!< publisher for joint states
  ros::Publisher cartesianCmdPublisher; //!< publisher for Cartesian arm commands
  ros::Publisher angularCmdPublisher; //!< publisher for angular arm commands
  ros::Subscriber cartesianCmdSubscriber; //!< subscriber for Cartesian arm commands
  ros::Subscriber angularCmdSubscriber; //!< subscriber for angular arm commands

  // Services
  ros::ServiceClient jaco_fk_client; //!< forward kinematics client
  ros::ServiceClient qe_client; //!< quaternion to euler (XYZ) conversion client
  ros::ServiceServer angularPositionServer; //!< service server to get the joint positions
  ros::ServiceServer cartesianPositionServer; //!< service server to get end effector pose
  ros::ServiceServer eStopServer; //!< service server for software estop and restart
  ros::ServiceServer eraseTrajectoriesServer;

  ros::Timer joint_state_timer_; //!< timer for joint state publisher

  // Actionlib
  actionlib::SimpleActionServer<control_msgs::FollowJointTrajectoryAction> trajectory_server_; //!< point-to-point trajectory follower
  actionlib::SimpleActionServer<control_msgs::FollowJointTrajectoryAction> smooth_trajectory_server_; //!< smooth point-to-point trajectory follower based on Cartesian end effector positions
  actionlib::SimpleActionServer<control_msgs::FollowJointTrajectoryAction> smooth_joint_trajectory_server; //!< smooth point-to-point trajectory follower based on joint velocity control
  actionlib::SimpleActionServer<control_msgs::GripperCommandAction> gripper_server_; //!< gripper command action server
  actionlib::SimpleActionServer<wpi_jaco_msgs::HomeArmAction> home_arm_server;

  boost::recursive_mutex api_mutex;
  
  double max_curvature;

  bool eStopEnabled;

public:
  /**
   * \brief Constructor
   * @param nh ROS node handle
   * @param pnh ROS private node handle
   */
  JacoArmTrajectoryController(ros::NodeHandle nh, ros::NodeHandle pnh);

  /**
   * \brief Destructor
   */
  virtual ~JacoArmTrajectoryController();

  /**
   * \brief Reads joint states from the arm and publishes them as a JointState message
   */
  void update_joint_states();

  /**
   * \brief move the arm to the home position using the Kinova API home call
   * @param goal action goal
   */
  void home_arm(const wpi_jaco_msgs::HomeArmGoalConstPtr &goal);

  /**
   * \brief Callback for the arm_controller, executes a joint angle trajectory
   * @param goal action goal
   */
  void execute_trajectory(const control_msgs::FollowJointTrajectoryGoalConstPtr &goal);

  /**
   * \brief Callback for the smooth_arm_controller, executes a smoother Cartesian trajectory 
   
   * The trajectory is generated by converting joint angle trajectories to 
   * end effector cartesian trajectories and smoothed automatically by the JACO's
   * Cartesian position controller.
   
   * NOTE: the trajectories must not fall within constraints defined internally
   * on the JACO for singularity avoidance
   * @param goal action goal
   */
  void execute_smooth_trajectory(const control_msgs::FollowJointTrajectoryGoalConstPtr &goal);

  /**
   * \brief Callback for the joint_velocity_controller, executes a smoothed trajectory with velocity control
   *
   * The trajectory is generated by interpolating a set of joint trajectory points and smoothing the corners
   * using an acceleration constraint. The trajectory is then followed by using a velocity controller implemented
   * in this node which sends joint velocity commands to the arm.
   * @param goal action goal
   */
  void execute_joint_trajectory(const control_msgs::FollowJointTrajectoryGoalConstPtr &goal);

  /**
   * \brief Callback for the gripper_server_, executes a gripper command
   * @param goal action goal
   */
  void execute_gripper(const control_msgs::GripperCommandGoalConstPtr &goal);

private:
  std::vector<std::string> joint_names;
  double joint_pos[NUM_JOINTS];
  double joint_vel[NUM_JOINTS];
  double joint_eff[NUM_JOINTS];

  unsigned int controlType; //current state of control

  /**
   * \brief Callback for sending an angular command to the arm
   * @param msg angular command and info
   */
  void angularCmdCallback(const wpi_jaco_msgs::AngularCommand& msg);

  /**
   * \brief Callback for sending a Cartesian command to the arm
   * @param msg Cartesian command and info
   */
  void cartesianCmdCallback(const wpi_jaco_msgs::CartesianCommand& msg);

  /**
  * \brief Control with finger velocity inputs to reach a given position
  * @param f1 position of finger 1
  * @param f2 position of finger 2
  * @param f3 position of finger 3
  */
  void fingerPositionControl(float f1, float f2, float f3);

  /**
   *\brief Stripped-down angular trajectory point sending to the arm
   *
   * This is designed for trajectory followers, which need a quick response
   * @param point angular trajectory point to send to the arm
   * @param erase if true, clear the trajectory point stack before sending point
   */
  void executeAngularTrajectoryPoint(TrajectoryPoint point, bool erase);

  /**
   * \brief Stripped-down Cartesian trajectory point sending to the arm
   *
   * This is designed for trajectory followers, which need a quick response
   * trajectory followers that need very quick response
   * @param point Cartesian trajectory point to send to the arm
   * @param erase if true, clear the trajectory point stack before sending point
   */
  void executeCartesianTrajectoryPoint(TrajectoryPoint point, bool erase);

  /**
   * \brief Service callback for getting the current joint positions of the arm and fingers
   *
   * This allows other nodes to request the joint positions when needed, rather than listening to the joint_state
   * topic.  The positions are given in radians on [-pi, pi].
   * @param req empty service request
   * @param res service response including joint positions
   * @return true on success
   */
  bool getAngularPosition(wpi_jaco_msgs::GetAngularPosition::Request &req, wpi_jaco_msgs::GetAngularPosition::Response &res);

  /**
   * \brief Service callback for getting the current Cartesian pose of the end effector
   *
   * This allows other nodes to get the pose which is normally only accessible
   * through the Kinova API
   * @param req empty service request
   * @param res service response including the end effector pose
   * @return true on success
   */
  bool getCartesianPosition(wpi_jaco_msgs::GetCartesianPosition::Request &req,
                            wpi_jaco_msgs::GetCartesianPosition::Response &res);

  /**
  * \brief Callback for enabling/disabling the software emergency stop
  *
  * @param req service request
  * @param res service response
  * @return true on success
  */
  bool eStopCallback(wpi_jaco_msgs::EStop::Request &req, wpi_jaco_msgs::EStop::Response &res);

  /**
  * \brief Callback for erasing trajectories currently running on the arm
  *
  * @param req empty service request
  * @param res empty service response
  */
  bool eraseTrajectoriesCallback(std_srvs::Empty::Request &req, std_srvs::Empty::Response &res);
};

}

#endif
