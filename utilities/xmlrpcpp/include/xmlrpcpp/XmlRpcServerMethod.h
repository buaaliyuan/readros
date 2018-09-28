
#ifndef _XMLRPCSERVERMETHOD_H_
#define _XMLRPCSERVERMETHOD_H_
//
// XmlRpc++ Copyright (c) 2002-2003 by Chris Morley
//
#if defined(_MSC_VER)
# pragma warning(disable:4786)    // identifier was truncated in debug info
#endif

#include "xmlrpcpp/XmlRpcDecl.h"

#ifndef MAKEDEPEND
# include <string>
#endif

namespace XmlRpc {

  // Representation of a parameter or result value
  class XmlRpcValue;

  // The XmlRpcServer processes client requests to call RPCs
  class XmlRpcServer;

  //! Abstract class representing a single RPC method
  //抽象基类，代表了单独的RPC方法
  class XMLRPCPP_DECL XmlRpcServerMethod {
  public:
    //! Constructor
	//添加该方法到一个服务器中
    XmlRpcServerMethod(std::string const& name, XmlRpcServer* server = 0);
    //! Destructor
    virtual ~XmlRpcServerMethod();

    //! Returns the name of the method
    std::string& name() { return _name; }

    //! Execute the method. Subclasses must provide a definition for this method.
	//命令模式，提供执行命令的接口
    virtual void execute(XmlRpcValue& params, XmlRpcValue& result) = 0;

    //! Returns a help string for the method.
    //! Subclasses should define this method if introspection is being used.
    virtual std::string help() { return std::string(); }

  protected:
    std::string _name;
    XmlRpcServer* _server;
  };
} // namespace XmlRpc

#endif // _XMLRPCSERVERMETHOD_H_
