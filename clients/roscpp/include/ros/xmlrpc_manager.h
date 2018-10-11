/*
 * Copyright (C) 2009, Willow Garage, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the names of Willow Garage, Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived from
 *     this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ROSCPP_XMLRPC_MANAGER_H
#define ROSCPP_XMLRPC_MANAGER_H

#include <string>
#include <set>
#include <boost/function.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>
#include <boost/enable_shared_from_this.hpp>

#include "common.h"
#include "xmlrpcpp/XmlRpc.h"

#include <ros/time.h>


namespace ros
{

/**
 * \brief internal
 */
namespace xmlrpc
{
XmlRpc::XmlRpcValue responseStr(int code, const std::string& msg, const std::string& response);
XmlRpc::XmlRpcValue responseInt(int code, const std::string& msg, int response);
XmlRpc::XmlRpcValue responseBool(int code, const std::string& msg, bool response);
}

class XMLRPCCallWrapper;
typedef boost::shared_ptr<XMLRPCCallWrapper> XMLRPCCallWrapperPtr;


//异步连接暂时没搞太清楚
class ROSCPP_DECL ASyncXMLRPCConnection : public boost::enable_shared_from_this<ASyncXMLRPCConnection>
{
public:
  virtual ~ASyncXMLRPCConnection() {}

  virtual void addToDispatch(XmlRpc::XmlRpcDispatch* disp) = 0;//???
  virtual void removeFromDispatch(XmlRpc::XmlRpcDispatch* disp) = 0;//???

  virtual bool check() = 0;//???
};
typedef boost::shared_ptr<ASyncXMLRPCConnection> ASyncXMLRPCConnectionPtr;
typedef std::set<ASyncXMLRPCConnectionPtr> S_ASyncXMLRPCConnection;


//类似一个包装器，给客户端附加一些管理信息
class ROSCPP_DECL CachedXmlRpcClient
{
public:
  CachedXmlRpcClient(XmlRpc::XmlRpcClient *c)
  : in_use_(false)
  , client_(c)
  {
  }

  bool in_use_;//标明客户端是否在使用中
  ros::SteadyTime last_use_time_; // for reaping为了回收客户端，超时回收
  XmlRpc::XmlRpcClient* client_; //被管理的客户端

  static const ros::WallDuration s_zombie_time_; // how long before it is toasted回收时用的超时时间
};

class XMLRPCManager;
typedef boost::shared_ptr<XMLRPCManager> XMLRPCManagerPtr;

typedef boost::function<void(XmlRpc::XmlRpcValue&, XmlRpc::XmlRpcValue&)> XMLRPCFunc;//给服务器注册函数的函数原型


//1管理了所有的与其他服务器连接的客户端、2管理了一个服务器、注册了回调方法、3管理了与该服务器相关的客户端连接
class ROSCPP_DECL XMLRPCManager
{
public:
  static const XMLRPCManagerPtr& instance();

  XMLRPCManager();
  ~XMLRPCManager();

  /** @brief Validate an XML/RPC response
   *
   * @param method The RPC method that was invoked.
   * @param response The resonse that was received.
   * @param payload The payload that was received.
   *
   * @return true if validation succeeds, false otherwise.
   *
   * @todo Consider making this private.
   */
  bool validateXmlrpcResponse(const std::string& method, 
			      XmlRpc::XmlRpcValue &response, XmlRpc::XmlRpcValue &payload);

  /**
   * @brief Get the xmlrpc server URI of this node
   */
  inline const std::string& getServerURI() const { return uri_; }
  inline uint32_t getServerPort() const { return port_; }

  XmlRpc::XmlRpcClient* getXMLRPCClient(const std::string& host, const int port, const std::string& uri);
  void releaseXMLRPCClient(XmlRpc::XmlRpcClient* c);
  //异步连接？？
  void addASyncConnection(const ASyncXMLRPCConnectionPtr& conn);
  void removeASyncConnection(const ASyncXMLRPCConnectionPtr& conn);
  //绑定回调函数
  bool bind(const std::string& function_name, const XMLRPCFunc& cb);
  //解除已经绑定的函数
  void unbind(const std::string& function_name);

  void start();
  void shutdown();

  bool isShuttingDown() { return shutting_down_; }

private:
  void serverThreadFunc();//线程回调函数

  std::string uri_;
  int port_;//服务器端口号
  boost::thread server_thread_;//服务器线程对象

#if defined(__APPLE__)
  // OSX has problems with lots of concurrent xmlrpc calls
  boost::mutex xmlrpc_call_mutex_;
#endif
  XmlRpc::XmlRpcServer server_;//提供xml服务器
  typedef std::vector<CachedXmlRpcClient> V_CachedXmlRpcClient;//多使用usingc++11
  V_CachedXmlRpcClient clients_;//manager中管理的所有客户端
  boost::mutex clients_mutex_;//锁保护

  bool shutting_down_;

  ros::WallDuration master_retry_timeout_;


  //服务器多线程中，相关的数据结构一定记得锁保护

  //为了加快添加删除连接的操作，以免锁阻塞
  S_ASyncXMLRPCConnection added_connections_;
  boost::mutex added_connections_mutex_;//锁保护
  S_ASyncXMLRPCConnection removed_connections_;
  boost::mutex removed_connections_mutex_;//锁保护

  S_ASyncXMLRPCConnection connections_;//服务器与其他客户端对应所有的连接


  struct FunctionInfo
  {
    std::string name;
    XMLRPCFunc function;
    XMLRPCCallWrapperPtr wrapper;
  };
  typedef std::map<std::string, FunctionInfo> M_StringToFuncInfo;
  boost::mutex functions_mutex_;//锁保护
  M_StringToFuncInfo functions_;//所有的注册方法，由名字索引

  volatile bool unbind_requested_;
};

}

#endif
