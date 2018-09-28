#ifndef _XMLRPCSERVERCONNECTION_H_
#define _XMLRPCSERVERCONNECTION_H_
//
// XmlRpc++ Copyright (c) 2002-2003 by Chris Morley
//
#if defined(_MSC_VER)
# pragma warning(disable:4786)    // identifier was truncated in debug info
#endif

#ifndef MAKEDEPEND
# include <string>
#endif

#include "xmlrpcpp/XmlRpcValue.h"
#include "xmlrpcpp/XmlRpcSource.h"
#include "xmlrpcpp/XmlRpcDecl.h"

namespace XmlRpc {


  // The server waits for client connections and provides methods
  class XmlRpcServer;
  class XmlRpcServerMethod;

  //! A class to handle XML RPC requests from a particular client
  //连接用来处理来自客户端的请求
  class XMLRPCPP_DECL XmlRpcServerConnection : public XmlRpcSource {
  public:
    // Static data
    static const char METHODNAME_TAG[];
    static const char PARAMS_TAG[];
    static const char PARAMS_ETAG[];
    static const char PARAM_TAG[];
    static const char PARAM_ETAG[];

    static const std::string SYSTEM_MULTICALL;
    static const std::string METHODNAME;
    static const std::string PARAMS;

    static const std::string FAULTCODE;
    static const std::string FAULTSTRING;

    //! Constructor
    XmlRpcServerConnection(int fd, XmlRpcServer* server, bool deleteOnClose = false);
    //! Destructor
    virtual ~XmlRpcServerConnection();

    // XmlRpcSource interface implementation
    //! Handle IO on the client connection socket.
    //!   @param eventType Type of IO event that occurred. @see XmlRpcDispatch::EventType.
    virtual unsigned handleEvent(unsigned eventType);

  protected:

    bool readHeader();
    bool readRequest();
    bool writeResponse();

    // Parses the request, runs the method, generates the response xml.
	//解析请求，执行方法，生成响应xml
    virtual void executeRequest();

    // Parse the methodName and parameters from the request.
	//解析方法名称和参数
    std::string parseRequest(XmlRpcValue& params);

    // Execute a named method with the specified params.
	//执行一个方法
    bool executeMethod(const std::string& methodName, XmlRpcValue& params, XmlRpcValue& result);

    // Execute multiple calls and return the results in an array.
	//执行多个方法
    bool executeMulticall(const std::string& methodName, XmlRpcValue& params, XmlRpcValue& result);

    // Construct a response from the result XML.
    void generateResponse(std::string const& resultXml);
    void generateFaultResponse(std::string const& msg, int errorCode = -1);
    std::string generateHeader(std::string const& body);


    // The XmlRpc server that accepted this connection
	//与该connection绑定的服务器
    XmlRpcServer* _server;

    // Possible IO states for the connection
	//读取header，读取请求，写响应
    enum ServerConnectionState { READ_HEADER, READ_REQUEST, WRITE_RESPONSE };
    ServerConnectionState _connectionState;

    // Request headers
    std::string _header;

    // Number of bytes expected in the request body (parsed from header)
    int _contentLength;

    // Request body
    std::string _request;

    // Response
    std::string _response;

    // Number of bytes of the response written so far
    int _bytesWritten;

    // Whether to keep the current client connection open for further requests
    bool _keepAlive;
  };
} // namespace XmlRpc

#endif // _XMLRPCSERVERCONNECTION_H_
