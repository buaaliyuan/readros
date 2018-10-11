// this file modified by Morgan Quigley on 22 Apr 2008.
// added features: server can be opened on port 0 and you can read back
// what port the OS gave you

#ifndef _XMLRPCSERVER_H_
#define _XMLRPCSERVER_H_
//
// XmlRpc++ Copyright (c) 2002-2003 by Chris Morley
//
#if defined(_MSC_VER)
# pragma warning(disable:4786)    // identifier was truncated in debug info
#endif

#ifndef MAKEDEPEND
# include <map>
# include <string>
# include <vector>
# if defined(_WINDOWS)
#  include <winsock2.h>
# else
#  include <poll.h>
# endif
#endif

#include "xmlrpcpp/XmlRpcDispatch.h"
#include "xmlrpcpp/XmlRpcSource.h"
#include "xmlrpcpp/XmlRpcDecl.h"

namespace XmlRpc {


  // An abstract class supporting XML RPC methods
  class XmlRpcServerMethod;

  // Class representing connections to specific clients
  class XmlRpcServerConnection;

  // Class representing argument and result values
  class XmlRpcValue;


  //! A class to handle XML RPC requests
  //用来处理xmlrpc请求，包括连接请求和调用请求
  class XMLRPCPP_DECL XmlRpcServer : public XmlRpcSource {
  public:
    //! Create a server object.
    XmlRpcServer();
    //! Destructor.
    virtual ~XmlRpcServer();

    //! Specify whether introspection is enabled or not. Default is not enabled.
    void enableIntrospection(bool enabled=true);

    //! Add a command to the RPC server
	//添加一个xmlrpc方法到服务器中
    void addMethod(XmlRpcServerMethod* method);

    //! Remove a command from the RPC server
	//删除一个xmlrpc方法
    void removeMethod(XmlRpcServerMethod* method);

    //! Remove a command from the RPC server by name
	//删除xmlrpc方法
    void removeMethod(const std::string& methodName);

    //! Look up a method by name
	//查找一个rpc方法
    XmlRpcServerMethod* findMethod(const std::string& name) const;

    //! Create a socket, bind to the specified port, and
    //! set it in listen mode to make it available for clients.
	//创建一个socket，绑定到特定的port，监听到来的客户端
    bool bindAndListen(int port, int backlog = 5);

    //! Process client requests for the specified time
	//处理客户端
    void work(double msTime);

    //! Temporarily stop processing client requests and exit the work() method.
	//暂时停止处理客户端请求
    void exit();

    //! Close all connections with clients and the socket file descriptor
    void shutdown();

    //! Introspection support
    void listMethods(XmlRpcValue& result);

    // XmlRpcSource interface implementation

    //! Handle client connection requests
    virtual unsigned handleEvent(unsigned eventType);

    //! Remove a connection from the dispatcher
	//删除connection
    virtual void removeConnection(XmlRpcServerConnection*);

    inline int get_port() { return _port; }

    XmlRpcDispatch *get_dispatch() { return &_disp; }

  protected:

    //! Accept a client connection request
	//接受一个请求
    virtual unsigned acceptConnection();

    //! Create a new connection object for processing requests from a specific client.
    //创建一个连接用来处理客户端到来的请求
    virtual XmlRpcServerConnection* createConnection(int socket);

    //! Count number of free file descriptors
	//计算未被使用的fd？？
    int countFreeFDs();

    // Whether the introspection API is supported by this server
    bool _introspectionEnabled;

    // Event dispatcher
	//事件分发器，负责对事件进行分发处理
    XmlRpcDispatch _disp;

    // Collection of methods. This could be a set keyed on method name if we wanted...
	//存储了注册到该服务器下的rpc方法
    typedef std::map< std::string, XmlRpcServerMethod* > MethodMap;
    MethodMap _methods;

    // system methods
    XmlRpcServerMethod* _listMethods;
    XmlRpcServerMethod* _methodHelp;

    int _port;

    // Flag indicating that accept had an error and needs to be retried.
	//accept出现错误
    bool _accept_error;
    // If we cannot accept(), retry after this many seconds. Hopefully there
    // will be more free file descriptors later.
    static const double ACCEPT_RETRY_INTERVAL_SEC;
    // Retry time for accept.
    double _accept_retry_time_sec;

    // Minimum number of free file descriptors before rejecting clients.
    static const int FREE_FD_BUFFER;
    // List of all file descriptors, used for counting open files.
    std::vector<struct pollfd> pollfds;
  };
} // namespace XmlRpc

#endif //_XMLRPCSERVER_H_
