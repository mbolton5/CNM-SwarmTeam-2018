#include <ros/ros.h>

// ROS libraries
#include <angles/angles.h>
#include <random_numbers/random_numbers.h>
#include <tf/transform_datatypes.h>
#include <tf/transform_listener.h>
#include <message_filters/subscriber.h>
#include <message_filters/synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>

// ROS messages
#include <std_msgs/Float32.h>
#include <std_msgs/Int16.h>
#include <std_msgs/UInt8.h>
#include <std_msgs/String.h>
#include <sensor_msgs/Joy.h>
#include <sensor_msgs/Range.h>
#include <geometry_msgs/Pose2D.h>
#include <geometry_msgs/Twist.h>
#include <nav_msgs/Odometry.h>
#include <apriltags_ros/AprilTagDetectionArray.h>
#include <std_msgs/Float32MultiArray.h>
#include "swarmie_msgs/Waypoint.h"

// Include Controllers
#include "LogicController.h"
#include <vector>

#include "Point.h"
#include "Tag.h"

// To handle shutdown signals so the node quits
// properly in response to "rosnode kill"
#include <ros/ros.h>
#include <signal.h>

#include <exception> // For exception handling

using namespace std;

// Define Exceptions
// Define an exception to be thrown if the user tries to create
// a RangeShape using invalid dimensions
class ROSAdapterRangeShapeInvalidTypeException : public std::exception {
public:
  ROSAdapterRangeShapeInvalidTypeException(std::string msg) {
    this->msg = msg;
  }

  virtual const char* what() const throw()
  {
    std::string message = "Invalid RangeShape type provided: " + msg;
    return message.c_str();
  }

private:
  std::string msg;
  std::string Msg; 		//sortOrder
};


// Random number generator
random_numbers::RandomNumberGenerator* rng;

// Create logic controller

LogicController logicController;

void humanTime();

// Behaviours Logic Functions
void sendDriveCommand(double linearVel, double angularVel);
void openFingers(); // Open fingers to 90 degrees
void closeFingers();// Close fingers to 0 degrees
void raiseWrist();  // Return wrist back to 0 degrees
void lowerWrist();  // Lower wrist to 50 degrees
void resultHandler();
void CNMFirstBoot();        //StartOrder
void sortOrder();     //SortOrder

Point updateCenterLocation();
void transformMapCentertoOdom();


// Numeric Variables for rover positioning
geometry_msgs::Pose2D currentLocation;          //current location of robot
geometry_msgs::Pose2D currentLocationMap;       //current location on MAP
geometry_msgs::Pose2D currentLocationAverage;   //???

geometry_msgs::Pose2D centerLocation;           //location of center location
geometry_msgs::Pose2D centerLocationMap;        //location of center on map
geometry_msgs::Pose2D centerLocationOdom;       //location of center ODOM
geometry_msgs::Pose2D centerLocationMapRef;

int currentMode = 0;
const float behaviourLoopTimeStep = 0.1; // time between the behaviour loop calls
const float status_publish_interval = 1;
const float heartbeat_publish_interval = 2;
const float waypointTolerance = 0.1; //10 cm tolerance.

// used for calling code once but not in main
bool initilized = false;

float linearVelocity = 0;
float angularVelocity = 0;

float prevWrist = 0;
float prevFinger = 0;
long int startTime = 0;
float minutesTime = 0;
float hoursTime = 0;


float drift_tolerance = 0.5; // meters

Result result;

std_msgs::String msg;
std_msgs::String Msg;			//sortOrder
swarmie_msgs::Waypoint wmsg;


geometry_msgs::Twist velocity;
char host[128];
string publishedName;
char prev_state_machine[128];

// Publishers
ros::Publisher stateMachinePublish;
ros::Publisher status_publisher;
ros::Publisher fingerAnglePublish;
ros::Publisher wristAnglePublish;
ros::Publisher infoLogPublisher;
ros::Publisher driveControlPublish;
ros::Publisher heartbeatPublisher;
// Publishes swarmie_msgs::Waypoint messages on "/<robot>/waypooints"
// to indicate when waypoints have been reached.
ros::Publisher waypointFeedbackPublisher;
//AJH added publisher declaration for manual waypoint publisher
ros::Publisher manualWaypointPublisher;
ros::Publisher startOrderPub;		//startOrder
ros::Publisher sortOrderPub;		//SortOrder

// Subscribers
ros::Subscriber joySubscriber;
ros::Subscriber modeSubscriber;
ros::Subscriber targetSubscriber;
ros::Subscriber odometrySubscriber;
ros::Subscriber mapSubscriber;
ros::Subscriber virtualFenceSubscriber;
// manualWaypointSubscriber listens on "/<robot>/waypoints/cmd" for
// swarmie_msgs::Waypoint messages.
ros::Subscriber manualWaypointSubscriber;
ros::Subscriber startOrderSub;			//startOrder
ros::Subscriber sortOrderSub;			//SortOrder

// Timers
ros::Timer stateMachineTimer;
ros::Timer publish_status_timer;
ros::Timer publish_heartbeat_timer;

// records time for delays in sequanced actions, 1 second resolution.
time_t timerStartTime;

// An initial delay to allow the rover to gather enough position data to
// average its location.
unsigned int startDelayInSeconds = 30;
float timerTimeElapsed = 0;

//Transforms
tf::TransformListener *tfListener;

// OS Signal Handler
void sigintEventHandler(int signal);

//Callback handlers
void joyCmdHandler(const sensor_msgs::Joy::ConstPtr& message);
void modeHandler(const std_msgs::UInt8::ConstPtr& message);
void targetHandler(const apriltags_ros::AprilTagDetectionArray::ConstPtr& tagInfo);
void odometryHandler(const nav_msgs::Odometry::ConstPtr& message);
void mapHandler(const nav_msgs::Odometry::ConstPtr& message);
void virtualFenceHandler(const std_msgs::Float32MultiArray& message);
void manualWaypointHandler(const swarmie_msgs::Waypoint& message);
void behaviourStateMachine(const ros::TimerEvent&);
void publishStatusTimerEventHandler(const ros::TimerEvent& event);
void publishHeartBeatTimerEventHandler(const ros::TimerEvent& event);
void sonarHandler(const sensor_msgs::Range::ConstPtr& sonarLeft, const sensor_msgs::Range::ConstPtr& sonarCenter, const sensor_msgs::Range::ConstPtr& sonarRight);
void startOrderHandler(const std_msgs::String& msg);			//startOrder
void sortOrderHandler(const std_msgs::String& msg);	//SortOrder

// Converts the time passed as reported by ROS (which takes Gazebo simulation rate into account) into milliseconds as an integer.
long int getROSTimeInMilliSecs();




/* CNM added code --------------------------------------------------------------
------------------------------------------------------------------------------*/

Point cnmCenterLocation;
void CNMAVGCenter(Point cnmCenterLocation);       //Avergages derived center locations
bool purgeMap = false;


//Actual Center Array
float CenterXCoordinates[20]; //was 10 for 2017
float CenterYCoordinates[20]; //was 10 for 2017


//INITIAL NEST SEARCH
bool cnmFirstBootProtocol = true;
bool cnmHasWaitedInitialAmount = false;
bool cnmInitialPositioningComplete = false;
bool cnmHasMovedForward = false;
bool cnmHasTurned180 = false;

//VAIRABLES FOR //startOrder
bool testCount = true;		//bool for window to increment
float cnmStartOrder = 0;           //startCheck
bool sortTrigger = true;
bool sortTrigger1 = true;
bool sortTrigger2 = true;

// IP address test @@@@
#include <arpa/inet.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <stdio.h>

char ip_string[20];
char *ip;

char* getip ()
{
    struct ifaddrs *ifap, *ifa;
    struct sockaddr_in *sa;
    char *addr;

    getifaddrs (&ifap);
    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr->sa_family==AF_INET) {
            sa = (struct sockaddr_in *) ifa->ifa_addr;
            addr = inet_ntoa(sa->sin_addr);
            printf("Interface: %s\tAddress: %s\n", ifa->ifa_name, addr);
            sprintf(ip_string, "%s", addr);
            printf("%s \n", ip_string);
        }
    }

    freeifaddrs(ifap);
    return ip_string;
}
/* @@@@ */





int main(int argc, char **argv) {


  /* @@@ */
    ip = getip();
    cout << "@@@@" << ip_string << endl;

  /* @@@ */



  gethostname(host, sizeof (host));
  string hostname(host);

  if (argc >= 2) {
    publishedName = argv[1];
    cout << "Welcome to the world of tomorrow " << publishedName
         << "!  Behaviour turnDirectionule started." << endl;
  } else {
    publishedName = hostname;
    cout << "No Name Selected. Default is: " << publishedName << endl;
  }

  // NoSignalHandler so we can catch SIGINT ourselves and shutdown the node
  ros::init(argc, argv, (publishedName + "_BEHAVIOUR"), ros::init_options::NoSigintHandler);
  ros::NodeHandle mNH;

  // Register the SIGINT event handler so the node can shutdown properly
  signal(SIGINT, sigintEventHandler);

  joySubscriber = mNH.subscribe((publishedName + "/joystick"), 10, joyCmdHandler);
  modeSubscriber = mNH.subscribe((publishedName + "/mode"), 1, modeHandler);
  targetSubscriber = mNH.subscribe((publishedName + "/targets"), 10, targetHandler);
  odometrySubscriber = mNH.subscribe((publishedName + "/odom/filtered"), 10, odometryHandler);
  mapSubscriber = mNH.subscribe((publishedName + "/odom/ekf"), 10, mapHandler);
  virtualFenceSubscriber = mNH.subscribe(("/virtualFence"), 10, virtualFenceHandler);
  manualWaypointSubscriber = mNH.subscribe((publishedName + "/waypoints/cmd"), 10, manualWaypointHandler);
  message_filters::Subscriber<sensor_msgs::Range> sonarLeftSubscriber(mNH, (publishedName + "/sonarLeft"), 10);
  message_filters::Subscriber<sensor_msgs::Range> sonarCenterSubscriber(mNH, (publishedName + "/sonarCenter"), 10);
  message_filters::Subscriber<sensor_msgs::Range> sonarRightSubscriber(mNH, (publishedName + "/sonarRight"), 10);

  startOrderPub = mNH.advertise<std_msgs::String>("startOrder", 1000);			//startOrder
    sortOrderPub = mNH.advertise<std_msgs::String>("sortOrder", 1000);			//sortOrder

  status_publisher = mNH.advertise<std_msgs::String>((publishedName + "/status"), 1, true);
  stateMachinePublish = mNH.advertise<std_msgs::String>((publishedName + "/state_machine"), 1, true);
  fingerAnglePublish = mNH.advertise<std_msgs::Float32>((publishedName + "/fingerAngle/cmd"), 1, true);
  wristAnglePublish = mNH.advertise<std_msgs::Float32>((publishedName + "/wristAngle/cmd"), 1, true);
  infoLogPublisher = mNH.advertise<std_msgs::String>("/infoLog", 1, true);
  driveControlPublish = mNH.advertise<geometry_msgs::Twist>((publishedName + "/driveControl"), 10);
  heartbeatPublisher = mNH.advertise<std_msgs::String>((publishedName + "/behaviour/heartbeat"), 1, true);
  manualWaypointPublisher = mNH.advertise<swarmie_msgs::Waypoint>((publishedName + "/waypoints/cmd"), 10, true);
  waypointFeedbackPublisher = mNH.advertise<swarmie_msgs::Waypoint>((publishedName + "/waypoints"), 1, true);

   startOrderSub = mNH.subscribe("startOrder", 1000, &startOrderHandler);			//startOrder
   sortOrderSub = mNH.subscribe("sortOrder", 1000, &sortOrderHandler);				//sortOrder

  publish_status_timer = mNH.createTimer(ros::Duration(status_publish_interval), publishStatusTimerEventHandler);
  stateMachineTimer = mNH.createTimer(ros::Duration(behaviourLoopTimeStep), behaviourStateMachine);

  publish_heartbeat_timer = mNH.createTimer(ros::Duration(heartbeat_publish_interval), publishHeartBeatTimerEventHandler);

  typedef message_filters::sync_policies::ApproximateTime<sensor_msgs::Range, sensor_msgs::Range, sensor_msgs::Range> sonarSyncPolicy;

  message_filters::Synchronizer<sonarSyncPolicy> sonarSync(sonarSyncPolicy(10), sonarLeftSubscriber, sonarCenterSubscriber, sonarRightSubscriber);
  sonarSync.registerCallback(boost::bind(&sonarHandler, _1, _2, _3));

  tfListener = new tf::TransformListener();
  std_msgs::String msg;
  msg.data = "Log Started";
  infoLogPublisher.publish(msg);

  stringstream ss;
  ss << "Rover start delay set to " << startDelayInSeconds << " seconds";
  msg.data = ss.str();
  infoLogPublisher.publish(msg);

  if(currentMode != 2 && currentMode != 3)
  {
    // ensure the logic controller starts in the correct mode.
    logicController.SetModeManual();
  }

  timerStartTime = time(0);


ss << "IP Address running"<< ip<< "Identity";
        msg.data = ss.str();
        infoLogPublisher.publish(msg);


  ros::spin();

  return EXIT_SUCCESS;
}


// This is the top-most logic control block organised as a state machine.
// This function calls the dropOff, pickUp, and search controllers.
// This block passes the goal location to the proportional-integral-derivative
// controllers in the abridge package.
void behaviourStateMachine(const ros::TimerEvent&)
{

if (timerTimeElapsed > 31)
{
    CNMFirstBoot();               //StartOrder
    //Point wp; 
    //AJH: empty for now because our subscriber calls a handler
    //that actually supplies the point (for now)
    wmsg.ACTION_ADD;
    wmsg.x = 0;
    wmsg.y = 0;
    manualWaypointPublisher.publish(wmsg);
}

if (timerTimeElapsed > 33)
{
    sortOrder();
}

/*if (sortTrigger1 == false)
{
  if (sortTrigger2)
  {
     sortTrigger2 = false;
        std_msgs::String msg;
        msg.data = "sortTriggerHandler running ";
        infoLogPublisher.publish(msg);
  }
}*/
  std_msgs::String stateMachineMsg;

  // time since timerStartTime was set to current time
  timerTimeElapsed = time(0) - timerStartTime;

  // init code goes here. (code that runs only once at start of
  // auto mode but wont work in main goes here)
  if (!initilized)
  {

    if (timerTimeElapsed > startDelayInSeconds)
    {



      //Create a new location object for CNMAVGCenter
    //  geometry_msgs::Pose2D location;

      // initialization has run
      initilized = true;
      //TODO: this just sets center to 0 over and over and needs to change
      Point centerOdom;
      centerOdom.x = 1.3 * cos(currentLocation.theta);
      centerOdom.y = 1.3 * sin(currentLocation.theta);
      centerOdom.theta = centerLocation.theta;
      logicController.SetCenterLocationOdom(centerOdom);

      Point centerMap;
      centerMap.x = currentLocationMap.x + (1.3 * cos(currentLocation.theta));
      centerMap.y = currentLocationMap.y + (1.3 * sin(currentLocation.theta));
      centerMap.theta = centerLocationMap.theta;
      logicController.SetCenterLocationMap(centerMap);

      Point cnmCenterMap;
      //cnmCenterMap = currentLocationMap;
      cnmCenterMap.x = currentLocationMap.x + (1.3 * cos(currentLocation.theta));
      cnmCenterMap.y = currentLocationMap.y + (1.3 * sin(currentLocation.theta));
      cnmCenterMap.theta = centerLocationMap.theta;
      CNMAVGCenter(cnmCenterMap);

      centerLocationMap.x = centerMap.x;
      centerLocationMap.y = centerMap.y;

      centerLocationOdom.x = centerOdom.x;
      centerLocationOdom.y = centerOdom.y;

      startTime = getROSTimeInMilliSecs();
    }

    else
    {
      return;
    }

  }

  // Robot is in automode
  if (currentMode == 2 || currentMode == 3)
  {

    humanTime();

    //update the time used by all the controllers
    logicController.SetCurrentTimeInMilliSecs( getROSTimeInMilliSecs() );

    //update center location
    logicController.SetCenterLocationOdom( updateCenterLocation() );

    //ask logic controller for the next set of actuator commands
    result = logicController.DoWork();

    bool wait = false;

    //if a wait behaviour is thrown sit and do nothing untill logicController is ready
    if (result.type == behavior)
    {
      if (result.b == wait)
      {
        wait = true;
      }
    }

    //do this when wait behaviour happens
    if (wait)
    {
      sendDriveCommand(0.0,0.0);
      std_msgs::Float32 angle;

      angle.data = prevFinger;
      fingerAnglePublish.publish(angle);
      angle.data = prevWrist;
      wristAnglePublish.publish(angle);
    }

    //normally interpret logic controllers actuator commands and deceminate them over the appropriate ROS topics
    else
    {

      sendDriveCommand(result.pd.left,result.pd.right);


      //Alter finger and wrist angle is told to reset with last stored value if currently has -1 value
      std_msgs::Float32 angle;
      if (result.fingerAngle != -1)
      {
        angle.data = result.fingerAngle;
        fingerAnglePublish.publish(angle);
        prevFinger = result.fingerAngle;
      }

      if (result.wristAngle != -1)
      {
        angle.data = result.wristAngle;
        wristAnglePublish.publish(angle);
        prevWrist = result.wristAngle;
      }
    }

    //publishHandeling here
    //logicController.getPublishData(); suggested


    //adds a blank space between sets of debugging data to easily tell one tick from the next
    cout << endl;

  }

  // mode is NOT auto
  else
  {
    humanTime();

    logicController.SetCurrentTimeInMilliSecs( getROSTimeInMilliSecs() );

    // publish current state for the operator to see
    stateMachineMsg.data = "WAITING";

    // poll the logicController to get the waypoints that have been
    // reached.
    std::vector<int> cleared_waypoints = logicController.GetClearedWaypoints();

    for(std::vector<int>::iterator it = cleared_waypoints.begin();
        it != cleared_waypoints.end(); it++)
    {
      swarmie_msgs::Waypoint wpt;
      wpt.action = swarmie_msgs::Waypoint::ACTION_REACHED;
      wpt.id = *it;
      waypointFeedbackPublisher.publish(wpt);
    }
    result = logicController.DoWork();
    if(result.type != behavior || result.b != wait)
    {
      // if the logic controller requested that the robot drive, then
      // drive. Otherwise there are no manual waypoints and the robot
      // should sit idle. (ie. only drive according to joystick
      // input).
      sendDriveCommand(result.pd.left,result.pd.right);
    }
  }

  // publish state machine string for user, only if it has changed, though
  if (strcmp(stateMachineMsg.data.c_str(), prev_state_machine) != 0)
  {
    stateMachinePublish.publish(stateMachineMsg);
    sprintf(prev_state_machine, "%s", stateMachineMsg.data.c_str());
  }
}

void sendDriveCommand(double left, double right)
{
  velocity.linear.x = left,
      velocity.angular.z = right;

  // publish the drive commands
  driveControlPublish.publish(velocity);
}

/*************************
 * ROS CALLBACK HANDLERS *
 *************************/

void targetHandler(const apriltags_ros::AprilTagDetectionArray::ConstPtr& message) {

  // Don't pass April tag data to the logic controller if the robot is not in autonomous mode.
  // This is to make sure autonomous behaviours are not triggered while the rover is in manual mode.
  if(currentMode == 0 || currentMode == 1)
  {
    return;
  }

  if (message->detections.size() > 0) {
    vector<Tag> tags;

    for (int i = 0; i < message->detections.size(); i++) {

      // Package up the ROS AprilTag data into our own type that does not rely on ROS.
      Tag loc;
      loc.setID( message->detections[i].id );

      // Pass the position of the AprilTag
      geometry_msgs::PoseStamped tagPose = message->detections[i].pose;
      loc.setPosition( make_tuple( tagPose.pose.position.x,
				   tagPose.pose.position.y,
				   tagPose.pose.position.z ) );

      // Pass the orientation of the AprilTag
      loc.setOrientation( ::boost::math::quaternion<float>( tagPose.pose.orientation.x,
							    tagPose.pose.orientation.y,
							    tagPose.pose.orientation.z,
							    tagPose.pose.orientation.w ) );
      tags.push_back(loc);
    }

    logicController.SetAprilTags(tags);
  }

}

void modeHandler(const std_msgs::UInt8::ConstPtr& message) {
  currentMode = message->data;
  if(currentMode == 2 || currentMode == 3) {
    logicController.SetModeAuto();
  }
  else {
    logicController.SetModeManual();
  }
  sendDriveCommand(0.0, 0.0);
}

void sonarHandler(const sensor_msgs::Range::ConstPtr& sonarLeft, const sensor_msgs::Range::ConstPtr& sonarCenter, const sensor_msgs::Range::ConstPtr& sonarRight) {

  logicController.SetSonarData(sonarLeft->range, sonarCenter->range, sonarRight->range);

}

void odometryHandler(const nav_msgs::Odometry::ConstPtr& message) {
  //Get (x,y) location directly from pose
  currentLocation.x = message->pose.pose.position.x;
  currentLocation.y = message->pose.pose.position.y;

  //Get theta rotation by converting quaternion orientation to pitch/roll/yaw
  tf::Quaternion q(message->pose.pose.orientation.x, message->pose.pose.orientation.y, message->pose.pose.orientation.z, message->pose.pose.orientation.w);
  tf::Matrix3x3 m(q);
  double roll, pitch, yaw;
  m.getRPY(roll, pitch, yaw);
  currentLocation.theta = yaw;

  linearVelocity = message->twist.twist.linear.x;
  angularVelocity = message->twist.twist.angular.z;


  Point currentLoc;
  currentLoc.x = currentLocation.x;
  currentLoc.y = currentLocation.y;
  currentLoc.theta = currentLocation.theta;
  logicController.SetPositionData(currentLoc);
  logicController.SetVelocityData(linearVelocity, angularVelocity);
}

// Allows a virtual fence to be defined and enabled or disabled through ROS
void virtualFenceHandler(const std_msgs::Float32MultiArray& message)
{
  // Read data from the message array
  // The first element is an integer indicating the shape type
  // 0 = Disable the virtual fence
  // 1 = circle
  // 2 = rectangle
  int shape_type = static_cast<int>(message.data[0]); // Shape type

  if (shape_type == 0)
  {
    logicController.setVirtualFenceOff();
  }
  else
  {
    // Elements 2 and 3 are the x and y coordinates of the range center
    Point center;
    center.x = message.data[1]; // Range center x
    center.y = message.data[2]; // Range center y

    // If the shape type is "circle" then element 4 is the radius, if rectangle then width
    switch ( shape_type )
    {
    case 1: // Circle
    {
      if ( message.data.size() != 4 ) throw ROSAdapterRangeShapeInvalidTypeException("Wrong number of parameters for circle shape type in ROSAdapter.cpp:virtualFenceHandler()");
      float radius = message.data[3];
      logicController.setVirtualFenceOn( new RangeCircle(center, radius) );
      break;
    }
    case 2: // Rectangle
    {
      if ( message.data.size() != 5 ) throw ROSAdapterRangeShapeInvalidTypeException("Wrong number of parameters for rectangle shape type in ROSAdapter.cpp:virtualFenceHandler()");
      float width = message.data[3];
      float height = message.data[4];
      logicController.setVirtualFenceOn( new RangeRectangle(center, width, height) );
      break;
    }
    default:
    { // Unknown shape type specified
      throw ROSAdapterRangeShapeInvalidTypeException("Unknown Shape type in ROSAdapter.cpp:virtualFenceHandler()");
    }
    }
  }
}

void mapHandler(const nav_msgs::Odometry::ConstPtr& message) {
  //Get (x,y) location directly from pose
  currentLocationMap.x = message->pose.pose.position.x;
  currentLocationMap.y = message->pose.pose.position.y;

  //Get theta rotation by converting quaternion orientation to pitch/roll/yaw
  tf::Quaternion q(message->pose.pose.orientation.x, message->pose.pose.orientation.y, message->pose.pose.orientation.z, message->pose.pose.orientation.w);
  tf::Matrix3x3 m(q);
  double roll, pitch, yaw;
  m.getRPY(roll, pitch, yaw);
  currentLocationMap.theta = yaw;

  linearVelocity = message->twist.twist.linear.x;
  angularVelocity = message->twist.twist.angular.z;

  Point curr_loc;
  curr_loc.x = currentLocationMap.x;
  curr_loc.y = currentLocationMap.y;
  curr_loc.theta = currentLocationMap.theta;
  logicController.SetMapPositionData(curr_loc);
  logicController.SetMapVelocityData(linearVelocity, angularVelocity);
}

void joyCmdHandler(const sensor_msgs::Joy::ConstPtr& message) {
  const int max_motor_cmd = 255;
  if (currentMode == 0 || currentMode == 1) {
    float linear  = abs(message->axes[4]) >= 0.1 ? message->axes[4]*max_motor_cmd : 0.0;
    float angular = abs(message->axes[3]) >= 0.1 ? message->axes[3]*max_motor_cmd : 0.0;

    float left = linear - angular;
    float right = linear + angular;

    if(left > max_motor_cmd) {
      left = max_motor_cmd;
    }
    else if(left < -max_motor_cmd) {
      left = -max_motor_cmd;
    }

    if(right > max_motor_cmd) {
      right = max_motor_cmd;
    }
    else if(right < -max_motor_cmd) {
      right = -max_motor_cmd;
    }

    sendDriveCommand(left, right);
  }
}


void publishStatusTimerEventHandler(const ros::TimerEvent&) {
  std_msgs::String msg;
  msg.data = "online";
  status_publisher.publish(msg);
}

void manualWaypointHandler(const swarmie_msgs::Waypoint& message) {
  Point wp;
  wp.x = currentLocation.x;//message.x;
  wp.y = currentLocation.y;//message.y;
  wp.theta = 0.0;
  switch(message.action) {
  case swarmie_msgs::Waypoint::ACTION_ADD:
    logicController.AddManualWaypoint(wp, message.id);
    infoLogPublisher.publish("Entering manual mode to reach waypoint ");
    //AJH: if we add a manual waypoint, we switch to manual mode
    logicController.SetModeManual();
  case swarmie_msgs::Waypoint::ACTION_REACHED:
    //AJH: if we have reached our waypoint, we switch to auto mode
    infoLogPublisher.publish("Entering auto mode after reaching waypoint ");
    logicController.SetModeAuto();
    break;
  case swarmie_msgs::Waypoint::ACTION_REMOVE:
    logicController.RemoveManualWaypoint(message.id);
    break;
  }
}

void sigintEventHandler(int sig) {
  // All the default sigint handler does is call shutdown()
  ros::shutdown();
}

void publishHeartBeatTimerEventHandler(const ros::TimerEvent&) {
  std_msgs::String msg;
  msg.data = "";
  heartbeatPublisher.publish(msg);
}

long int getROSTimeInMilliSecs()
{
  // Get the current time according to ROS (will be zero for simulated clock until the first time message is recieved).
  ros::Time t = ros::Time::now();

  // Convert from seconds and nanoseconds to milliseconds.
  return t.sec*1e3 + t.nsec/1e6;

}


Point updateCenterLocation()
{
  transformMapCentertoOdom();

  Point tmp;
  tmp.x = centerLocationOdom.x;
  tmp.y = centerLocationOdom.y;

  return tmp;
}

void transformMapCentertoOdom()
{

  // map frame
  geometry_msgs::PoseStamped mapPose;

  // setup msg to represent the center location in map frame
  mapPose.header.stamp = ros::Time::now();

  mapPose.header.frame_id = publishedName + "/map";
  mapPose.pose.orientation = tf::createQuaternionMsgFromRollPitchYaw(0, 0, centerLocationMap.theta);
  mapPose.pose.position.x = centerLocationMap.x;
  mapPose.pose.position.y = centerLocationMap.y;
  geometry_msgs::PoseStamped odomPose;
  string x = "";

  try
  { //attempt to get the transform of the center point in map frame to odom frame.
    tfListener->waitForTransform(publishedName + "/map", publishedName + "/odom", ros::Time::now(), ros::Duration(1.0));
    tfListener->transformPose(publishedName + "/odom", mapPose, odomPose);
  }

  catch(tf::TransformException& ex) {
    ROS_INFO("Received an exception trying to transform a point from \"map\" to \"odom\": %s", ex.what());
    x = "Exception thrown " + (string)ex.what();
    std_msgs::String msg;
    stringstream ss;
    ss << "Exception in mapAverage() " + (string)ex.what();
    msg.data = ss.str();
    infoLogPublisher.publish(msg);
    cout << msg.data << endl;
  }

  // Use the position and orientation provided by the ros transform.
  centerLocationMapRef.x = odomPose.pose.position.x; //set centerLocation in odom frame
  centerLocationMapRef.y = odomPose.pose.position.y;

 // cout << "x ref : "<< centerLocationMapRef.x << " y ref : " << centerLocationMapRef.y << endl;

  float xdiff = centerLocationMapRef.x - centerLocationOdom.x;
  float ydiff = centerLocationMapRef.y - centerLocationOdom.y;

  float diff = hypot(xdiff, ydiff);

  if (diff > drift_tolerance)
  {
    centerLocationOdom.x += xdiff/diff;
    centerLocationOdom.y += ydiff/diff;
  }

  //cout << "center x diff : " << centerLocationMapRef.x - centerLocationOdom.x << " center y diff : " << centerLocationMapRef.y - centerLocationOdom.y << endl;
  //cout << hypot(centerLocationMapRef.x - centerLocationOdom.x, centerLocationMapRef.y - centerLocationOdom.y) << endl;

}

void humanTime() {

  float timeDiff = (getROSTimeInMilliSecs()-startTime)/1e3;
  if (timeDiff >= 60) {
    minutesTime++;
    startTime += 60  * 1e3;
    if (minutesTime >= 60) {
      hoursTime++;
      minutesTime -= 60;
    }
  }
  timeDiff = floor(timeDiff*10)/10;

  double intP, frac;
  frac = modf(timeDiff, &intP);
  timeDiff -= frac;
  frac = round(frac*10);
  if (frac > 9) {
    frac = 0;
  }

  //cout << "System has been Running for :: " << hoursTime << " : hours " << minutesTime << " : minutes " << timeDiff << "." << frac << " : seconds" << endl; //you can remove or comment this out it just gives indication something is happening to the log file
}


void startOrderHandler(const std_msgs::String& msg)		//startOrder
{
    if(testCount == false)
    {
	cnmStartOrder++;
    }

   if(cnmStartOrder == 1)
   {
       std_msgs::String msg;
        msg.data = "count is 1";
        infoLogPublisher.publish(msg);
   }
   if(cnmStartOrder == 2)
   {
     std_msgs::String msg;
        msg.data = "count is 2";
        infoLogPublisher.publish(msg);
   }
   if(cnmStartOrder == 3)
   {
     std_msgs::String msg;
        msg.data = "count is 3";
        infoLogPublisher.publish(msg);
   }
   if(cnmStartOrder == 4)
   {
     std_msgs::String msg;
        msg.data = "count is 4";
        infoLogPublisher.publish(msg);
   }
   if(cnmStartOrder == 5)
   {
     std_msgs::String msg;
        msg.data = "count is 5";
        infoLogPublisher.publish(msg);
   }
   if(cnmStartOrder == 6)
   {
     std_msgs::String msg;
        msg.data = "count is 6";
        infoLogPublisher.publish(msg);
   }
}

void CNMFirstBoot()
{
    //FIRST TIME IN THIS FUNCTION

    if(testCount)		//startOrder
    {
        testCount = false;
      /*  std_msgs::String msg;
        msg.data = "first boot running ";
        infoLogPublisher.publish(msg);*/

	startOrderPub.publish(msg);
    }
}

void sortOrder()
{
  if(sortTrigger)
  {
    sortTrigger = false;
    std::string str(ip);
    msg.data = publishedName;
    //msg.data = ip;
    sortOrderPub.publish(msg);
   //     msg.data = "sortTrigger is running ";
   //     infoLogPublisher.publish(msg);
  }
}

void sortOrderHandler(const std_msgs::String& msg)
{
  string msg_name = string(msg.data);
  if(msg_name == publishedName){
    Msg.data = string("That's my published name! I am " + publishedName);
    infoLogPublisher.publish(Msg);
  }
  else{
    Msg.data = string("Boo, that's not my published name! I'm not " + msg_name + ", I'm " + publishedName);
    infoLogPublisher.publish(Msg);
  }
  /*
  stringstream ff;
  ff << "MY ID is: "<< cnmStartOrder << "  ID received: " << msg;
    Msg.data = ff.str();
    infoLogPublisher.publish(Msg);
  // sortTrigger1 = false;
  */
}

void CNMAVGCenter(Point newCenter)
{

    //NOTES ON THIS FUNCTION:
    //- Takes a derived center point, puts it in an array of other
    //  center points and averages them together... allowing us to
    //  build a more dynamic center location (able to adjust with drift)

    std_msgs::String msg;
    msg.data = "Averaging Center GPS Location";
    infoLogPublisher.publish(msg);

    const int ASIZE = 20; //was 10 for 2017
    static int index = 0;

    static bool reached10 = false;

    if(purgeMap)
    {
	purgeMap = false;
	reached10 = false;

	index = 0;
    }

    CenterXCoordinates[index] = newCenter.x;
    CenterYCoordinates[index] = newCenter.y;

    if(index >= ASIZE)
    {
        if(!reached10) { reached10 = true; }
        index = 0;
    }
    else
    {
        index++;
    }

    float avgX = 0;
    float avgY = 0;

    if(!reached10)
    {
        for(int i = 0; i < index; i++)
        {
            avgX += CenterXCoordinates[i];
            avgY += CenterYCoordinates[i];
        }

        avgX = (avgX / index);
        avgY = (avgY / index);
    }
    else
    {
        for(int i = 0; i < ASIZE; i++)
        {
            avgX += CenterXCoordinates[i];
            avgY += CenterYCoordinates[i];
        }

        avgX = (avgX / ASIZE);
        avgY = (avgY / ASIZE);
    }

    //UPDATE CENTER LOCATION
    //---------------------------------------------
    cnmCenterLocation.x = (avgX);
    cnmCenterLocation.y = (avgY);
    logicController.cnmSetCenterLocationMAP(cnmCenterLocation);


    msg.data = "Averaging Center GPS Location Complete!";
    infoLogPublisher.publish(msg);

    stringstream ff;
    ff << "Center Postion is X: "<< cnmCenterLocation.x << "  Y: " << cnmCenterLocation.y;
           Msg.data = ff.str();
           infoLogPublisher.publish(Msg);


}
