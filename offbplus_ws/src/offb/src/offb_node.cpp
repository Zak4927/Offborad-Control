#include <ros/ros.h>
#include <sstream>
#include "std_msgs/String.h"

#include <mavros_msgs/State.h>
#include <mavros_msgs/SetMode.h>
#include <mavros_msgs/CommandBool.h>

#include <geometry_msgs/PoseStamped.h>
#include <geometry_msgs/Twist.h>        



//********************************************************************************全局变量
 mavros_msgs::State current_state;               //当前状态（包括飞行模式和旋翼的上解锁）
 std_msgs::String show_mode;                     //当前飞行模式
 std_msgs::String show_armed;                    //当前旋翼的上解锁
 geometry_msgs::PoseStamped current_position;    //当前位置
 geometry_msgs::PoseStamped target_pose;         //目标点位置（包括起点和巡航点）
 geometry_msgs::Twist current_velocity;          //当前速度

  //#########
  //初始化设置        
  //#########
  #define NUM 5      //目标点个数 
  float cruise[NUM][3] = 
  {
    {  0,  0,  5},
    {  5,  0,  5},
    {  5,  5,  5},
    {  0,  5,  5},
    {  0,  0,  5}
  }; //各点坐标 
  float accuracy = 0.2; //目标点误差范围 



//********************************************************************************函数声明
//state come back - 状态回调
void state_cb(const mavros_msgs::State::ConstPtr& msg);  
//position come back - 位置回调  
void position_cb(const geometry_msgs::PoseStamped::ConstPtr& pos);
//velocity come back - 速度回调
void velocity_cb(const geometry_msgs::Twist::ConstPtr& vel);
//设置起点的坐标
void set_starting_point(float x, float y, float z);
//更新巡航点的坐标
void update_cruising_point(float last_x, float last_y, float last_z, float now_x, float now_y, float now_z);
//设置巡航点的坐标
void set_cruising_point(void);
//打印无人机的状态、位置信息
void print_message(void);



//****************************************************************************************************主函数
int main(int argc, char **argv)
{
  //################################
  //节点、话题和服务的建立，用于和飞控互动
  //################################
  ros::init(argc, argv, "offboard_node"); //初始化节点
  ros::NodeHandle nh;                     //节点句柄

  //订阅无人机的当前状态，话题名称mavros/state，消息接收队列大小10，注册回调函数state_cb
  ros::Subscriber state_sub = nh.subscribe<mavros_msgs::State>
	("mavros/state", 10, state_cb);
  //订阅无人机的当前位置，话题名称mavros/local_position/pose，消息接收队列大小10，注册回调函数position_cb
  ros::Subscriber position_sub = nh.subscribe<geometry_msgs::PoseStamped>  
  ("mavros/local_position/pose", 10, position_cb);
  //订阅无人机的当前速度，话题名称mavros/local_velocity/vel，消息接收队列大小10，注册回调函数velocity_cb
  ros::Subscriber velocity_sub = nh.subscribe<geometry_msgs::Twist>  
  ("mavros/setpoint_velocity/cmd_vel_unstamped", 10, velocity_cb);
  //发布目标点的位置，话题名称mavros/setpoint_position/local，消息发布队列大小10
  ros::Publisher local_pos_pub = nh.advertise<geometry_msgs::PoseStamped>
	("mavros/setpoint_position/local", 10); 

  //设置飞行模式的服务，服务名称mavros/set_mode
  ros::ServiceClient set_mode_client = nh.serviceClient<mavros_msgs::SetMode>
	("mavros/set_mode");
  //上解锁旋翼的服务，服务名称mavros/cmd/arming
  ros::ServiceClient arming_client = nh.serviceClient<mavros_msgs::CommandBool>
	("mavros/cmd/arming"); 
  


  //##################
  //等待MAVROS和飞控相连       
  //##################
  ros::Rate rate(20.0);  //发布信息频率须大于2Hz
  
  //ros::ok()值为true或false，为false时可能执行了退出操作 
  //current_state.connected连接成功时值为1
  //当节点正常运行且未连接时（确保在节点正常运行的情况下等待连接）
  while ( ros::ok() && !current_state.connected )
  {
	  ros::spinOnce();     //处理节点订阅话题的所有回调函数
	  rate.sleep();        //睡眠以度过一个循环中剩下的时间
	}



  //#######
  //模式切换
  //#######
  set_starting_point(cruise[0][0], cruise[0][1], cruise[0][2]);  

  //发送一些目标点信息到飞控中，不然会被拒绝切换模式。50为随意取值
  for ( int j = 50; ros::ok() && j > 0; --j )
  {
    local_pos_pub.publish(target_pose); 
    ros::spinOnce();
    rate.sleep();
	}

  //打印,最初为其他飞行模式，旋翼处上锁状态
  show_mode.data = "Another";   
  show_armed.data = "Locked"; 
  //申请飞行模式为"OFFBOARD"
  mavros_msgs::SetMode offb_set_mode;             
  offb_set_mode.request.custom_mode = "OFFBOARD"; 
  //申请解锁旋翼
  mavros_msgs::CommandBool arm_cmd;             
  arm_cmd.request.value = true;    
  //获得最近一次申请的时间               
  ros::Time last_request = ros::Time::now();      
  ros::Time last_print = ros::Time::now();
  
  while ( ros::ok() ) 
  {
    //3秒后切换飞行模式
    if ( current_state.mode != "OFFBOARD" && (ros::Time::now() - last_request > ros::Duration(3.0) ) )
    {
      //当申请和回传消息均表示成功时
      if ( set_mode_client.call(offb_set_mode) && offb_set_mode.response.mode_sent )  
      {
        show_mode.data = "Offboard";
		  }
		  last_request = ros::Time::now();  //更新申请时间     
    }
    
    else 
    {
      //3秒后解锁
      if ( !current_state.armed && (ros::Time::now() - last_request > ros::Duration(3.0) ) )
      {
        //当申请和回传消息均表示成功时
        if ( arming_client.call(arm_cmd) && arm_cmd.response.success )
        {
          show_armed.data = "Unlocked";
			  }
			  last_request = ros::Time::now(); 
	  	}
		}

    set_cruising_point(); //更新目标点
    local_pos_pub.publish(target_pose); 
    ros::spinOnce();
		rate.sleep();  

    if ( (ros::Time::now() - last_print > ros::Duration(1.0) ) )
    {
      print_message();
      last_print = ros::Time::now();
    }
	} 

	return 0;
}



//********************************************************************************函数定义
//state come back
void state_cb(const mavros_msgs::State::ConstPtr& msg)
{
  current_state = *msg;
}

//position come back
void position_cb(const geometry_msgs::PoseStamped::ConstPtr& pos)
{
  current_position = *pos;
}

//velocity come back
void velocity_cb(const geometry_msgs::Twist::ConstPtr& vel)
{
  current_velocity = *vel;
}

//设置目标点的坐标
void set_starting_point(float x, float y, float z)
{
   target_pose.pose.position.x = x;
   target_pose.pose.position.y = y;
   target_pose.pose.position.z = z;
}

//更新目标点的坐标
void update_cruising_point(float last_x, float last_y, float last_z, float now_x, float now_y, float now_z)
{
   //当无人机到达目标点的误差范围内时
   if (    current_position.pose.position.x > (last_x-accuracy) && current_position.pose.position.x < (last_x+accuracy)
        && current_position.pose.position.y > (last_y-accuracy) && current_position.pose.position.y < (last_y+accuracy)
        && current_position.pose.position.z > (last_z-accuracy) && current_position.pose.position.z < (last_z+accuracy) )
        {
           set_starting_point(now_x, now_y, now_z);
        }
}

//设置巡航点的坐标
void set_cruising_point(void)
{
  for ( int i = 0; i < NUM - 1; i++)
  {
    update_cruising_point( cruise[i][0],     cruise[i][1],     cruise[i][2], 
                           cruise[i + 1][0], cruise[i + 1][1], cruise[i + 1][2]);
  }
}

//打印无人机状态、位置
void print_message(void)
{
  ROS_INFO("\n#################### \n");
  ROS_INFO("current       state    \n");
  ROS_INFO("mode        armed      \n");
  ROS_INFO("[%s]        [%s]       \n",
              show_mode.data.c_str() , show_armed.data.c_str() );
  ROS_INFO("#################### \n");
  ROS_INFO("target position        current position     \n");
  ROS_INFO("x      y      z        x      y      z      \n");
  ROS_INFO("[%.2f] [%.2f] [%.2f]   [%.2f] [%.2f] [%.2f] \n",
                         target_pose.pose.position.x,
                         target_pose.pose.position.y,
                         target_pose.pose.position.z,
                         current_position.pose.position.x,
                         current_position.pose.position.y,
                         current_position.pose.position.z);
  ROS_INFO("#################### \n");
  ROS_INFO("current velocity                     \n");
  ROS_INFO("linear                 angular              \n");
  ROS_INFO("x      y      z        x      y      z      \n");
  ROS_INFO("[%.2f] [%.2f] [%.2f]   [%.2f] [%.2f] [%.2f] \n",
                         current_velocity.linear.x,
                         current_velocity.linear.y,
                         current_velocity.linear.z,
                         current_velocity.angular.x,
                         current_velocity.angular.y,
                         current_velocity.angular.z);                      
}