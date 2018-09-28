
#include "xmlrpcpp/XmlRpcClient.h"

#include "xmlrpcpp/XmlRpcSocket.h"
#include "xmlrpcpp/XmlRpcUtil.h"
#include "xmlrpcpp/XmlRpcValue.h"

#include <stdio.h>
#include <stdlib.h>
#ifndef _WINDOWS
	# include <strings.h>
#endif
#include <string.h>


using namespace XmlRpc;

// Static data
const char XmlRpcClient::REQUEST_BEGIN[] = 
  "<?xml version=\"1.0\"?>\r\n"
  "<methodCall><methodName>";
const char XmlRpcClient::REQUEST_END_METHODNAME[] = "</methodName>\r\n";
const char XmlRpcClient::PARAMS_TAG[] = "<params>";
const char XmlRpcClient::PARAMS_ETAG[] = "</params>";
const char XmlRpcClient::PARAM_TAG[] = "<param>";
const char XmlRpcClient::PARAM_ETAG[] =  "</param>";
const char XmlRpcClient::REQUEST_END[] = "</methodCall>\r\n";
const char XmlRpcClient::METHODRESPONSE_TAG[] = "<methodResponse>";
const char XmlRpcClient::FAULT_TAG[] = "<fault>";


const char * XmlRpcClient::connectionStateStr(ClientConnectionState state) {
  switch(state) {
    case NO_CONNECTION:
      return "NO_CONNECTION";
    case CONNECTING:
      return "CONNECTING";
    case WRITE_REQUEST:
      return "WRITE_REQUEST";
    case READ_HEADER:
      return "READ_HEADER";
    case READ_RESPONSE:
      return "READ_RESPONSE";
    case IDLE:
      return "IDLE";
    default:
      return "UNKNOWN";
  }
}

XmlRpcClient::XmlRpcClient(const char* host, int port, const char* uri/*=0*/)
  : _connectionState(NO_CONNECTION),
  _host(host),
  _port(port),
  _sendAttempts(0),
  _bytesWritten(0),
  _executing(false),
  _eof(false),
  _isFault(false),
  _contentLength(0)
{
  XmlRpcUtil::log(1, "XmlRpcClient new client: host %s, port %d.", host, port);

  if (uri)
    _uri = uri;
  else
    _uri = "/RPC2";

  // Default to keeping the connection open until an explicit close is done
  setKeepOpen();
}


XmlRpcClient::~XmlRpcClient()
{
  this->close();
}

// Close the owned fd
void
XmlRpcClient::close()
{
  XmlRpcUtil::log(4, "XmlRpcClient::close: fd %d.", getfd());
  _connectionState = NO_CONNECTION;//保持状态
  _disp.exit();
  _disp.removeSource(this);
  //父类关闭
  XmlRpcSource::close();
}


// Clear the referenced flag even if exceptions or errors occur.
struct ClearFlagOnExit {
  ClearFlagOnExit(bool& flag) : _flag(flag) {}
  ~ClearFlagOnExit() { _flag = false; }
  bool& _flag;
};

// Execute the named procedure on the remote server.
// Params should be an array of the arguments for the method.
// Returns true if the request was sent and a result received (although the result
// might be a fault).
bool
XmlRpcClient::execute(const char* method, XmlRpcValue const& params, XmlRpcValue& result)
{
  XmlRpcUtil::log(1, "XmlRpcClient::execute: method %s (_connectionState %s).", method, connectionStateStr(_connectionState));

  // This is not a thread-safe operation, if you want to do multithreading, use separate
  // clients for each thread. If you want to protect yourself from multiple threads
  // accessing the same client, replace this code with a real mutex.
  if (_executing)
    return false;

  _executing = true;
  ClearFlagOnExit cf(_executing);

  _sendAttempts = 0;
  _isFault = false;

  if ( ! setupConnection())//设置连接，为写入请求，也就是发送请求做好准备
    return false;

  if ( ! generateRequest(method, params))//生成xml形式的请求格式
    return false;

  result.clear();//	清空之前的返回结果
  
  
  double msTime = -1.0;   // Process until exit is called
  //开始阻塞调用
  _disp.work(msTime);

  if (_connectionState != IDLE || ! parseResponse(result))
    return false;

  XmlRpcUtil::log(1, "XmlRpcClient::execute: method %s completed.", method);
  _response = "";
  return true;
}

// Execute the named procedure on the remote server, non-blocking.
// Params should be an array of the arguments for the method.
// Returns true if the request was sent and a result received (although the result
// might be a fault).
bool
XmlRpcClient::executeNonBlock(const char* method, XmlRpcValue const& params)
{
  XmlRpcUtil::log(1, "XmlRpcClient::executeNonBlock: method %s (_connectionState %s).", method, connectionStateStr(_connectionState));

  // This is not a thread-safe operation, if you want to do multithreading, use separate
  // clients for each thread. If you want to protect yourself from multiple threads
  // accessing the same client, replace this code with a real mutex.
  if (_executing)
    return false;

  _executing = true;
  ClearFlagOnExit cf(_executing);

  _sendAttempts = 0;
  _isFault = false;

  if ( ! setupConnection())
    return false;

  if ( ! generateRequest(method, params))
    return false;
//异步调用的话结果如何获得
  return true;
}

bool
XmlRpcClient::executeCheckDone(XmlRpcValue& result)
{
  result.clear();
  // Are we done yet?
  // If we lost connection, the call failed.
  if (_connectionState == NO_CONNECTION) {
    return true;
  }

  // Otherwise, assume the call is still in progress.
  if (_connectionState != IDLE) {
    return false;
  }

  if (! parseResponse(result))
  {
    // Hopefully the caller can determine that parsing failed.
  }
  //XmlRpcUtil::log(1, "XmlRpcClient::execute: method %s completed.", method);
  _response = "";
  return true;
}

// XmlRpcSource interface implementation
// Handle server responses. Called by the event dispatcher during execute.
//分发器最终会根据读或者写请求回调到这个函数中
unsigned
XmlRpcClient::handleEvent(unsigned eventType)
{
//如果发生异常处理
  if (eventType == XmlRpcDispatch::Exception)
  {
    if (_connectionState == WRITE_REQUEST && _bytesWritten == 0)
      XmlRpcUtil::error("Error in XmlRpcClient::handleEvent: could not connect to server (%s).", 
                       XmlRpcSocket::getErrorMsg().c_str());
    else
      XmlRpcUtil::error("Error in XmlRpcClient::handleEvent (state %s): %s.", 
                        connectionStateStr(_connectionState),
                        XmlRpcSocket::getErrorMsg().c_str());
    return 0;
  }

  //写入请求
  if (_connectionState == WRITE_REQUEST)
    if ( ! writeRequest()) return 0;

//读取服务器返回的header，用以确定将来读取respoonse的长度
  if (_connectionState == READ_HEADER)
    if ( ! readHeader()) return 0;
//读取响应体
  if (_connectionState == READ_RESPONSE)
    if ( ! readResponse()) return 0;

  // This should probably always ask for Exception events too
  return (_connectionState == WRITE_REQUEST) 
        ? XmlRpcDispatch::WritableEvent : XmlRpcDispatch::ReadableEvent;
 
 //整个流程为：1.处于writeRequest状态，生成请求xml，发送请求（可能分多次），发送成功切换为readHeader状态。期间出现错误close
 //2.读取header（可能分多次），解析出响应体的长度，转换为readResponse状态。期间出现错误close
 //3.读取response（可能分多次），需要根据第二部的长度来获取
 
 //注意：
 //每次用的socket可能不是一个socket，如果上次断开socket就会创建并建立连接
 //不同状态的切换实现了一个类似与状态机的过程，这个过程对于客户端来说很重要
 //客户端实现了不同步骤的具体做的事情，而事件分发器则驱动这些状态变化进行
 
}


// Create the socket connection to the server if necessary
//创建连接到server的connection
bool
XmlRpcClient::setupConnection()
{
  // If an error occurred last time through, or if the server closed the connection, close our end
  if ((_connectionState != NO_CONNECTION && _connectionState != IDLE) || _eof)
    close();

  _eof = false;
  //如果本来就连接好了就不用重新建立连接，每个客户端维持一个连接状态
  if (_connectionState == NO_CONNECTION)
	  if (! doConnect()) {//创建socket，设置非阻塞，连接服务器
		return false;
	  }

  // Prepare to write the request
  //准备写请求状态
  _connectionState = WRITE_REQUEST;
  _bytesWritten = 0;//初始化已经写入变量

  // Notify the dispatcher to listen on this source (calls handleEvent when the socket is writable)
  _disp.removeSource(this);       // Make sure nothing is left over去除上次留在分发其中的客户端
  //将自己添加到分发器中
  _disp.addSource(this, XmlRpcDispatch::WritableEvent | XmlRpcDispatch::Exception);

  return true;
}


// Connect to the xmlrpc server
bool
XmlRpcClient::doConnect()
{
  int fd = XmlRpcSocket::socket();
  if (fd < 0)
  {
    XmlRpcUtil::error("Error in XmlRpcClient::doConnect: Could not create socket (%s).", XmlRpcSocket::getErrorMsg().c_str());
    return false;
  }

  XmlRpcUtil::log(3, "XmlRpcClient::doConnect: fd %d.", fd);
  this->setfd(fd);

  // Don't block on connect/reads/writes
  if ( ! XmlRpcSocket::setNonBlocking(fd))
  {
    this->close();
    XmlRpcUtil::error("Error in XmlRpcClient::doConnect: Could not set socket to non-blocking IO mode (%s).", XmlRpcSocket::getErrorMsg().c_str());
    return false;
  }

  if ( ! XmlRpcSocket::connect(fd, _host, _port))
  {
    this->close();
    XmlRpcUtil::error("Error in XmlRpcClient::doConnect: Could not connect to server (%s).", XmlRpcSocket::getErrorMsg().c_str());
    return false;
  }

  return true;
}

// Encode the request to call the specified method with the specified parameters into xml
//将请求转换为xml格式
bool
XmlRpcClient::generateRequest(const char* methodName, XmlRpcValue const& params)
{
  std::string body = REQUEST_BEGIN;
  body += methodName;
  body += REQUEST_END_METHODNAME;

  // If params is an array, each element is a separate parameter
  if (params.valid()) {
    body += PARAMS_TAG;
    if (params.getType() == XmlRpcValue::TypeArray)
    {
      for (int i=0; i<params.size(); ++i) {
        body += PARAM_TAG;
        body += params[i].toXml();
        body += PARAM_ETAG;
      }
    }
    else
    {
      body += PARAM_TAG;
      body += params.toXml();
      body += PARAM_ETAG;
    }
      
    body += PARAMS_ETAG;
  }
  body += REQUEST_END;

  std::string header = generateHeader(body.length());
  XmlRpcUtil::log(4, "XmlRpcClient::generateRequest: header is %d bytes, content-length is %d.", 
                  header.length(), body.length());

  _request = header + body;//请求包括两个部分一个是header一个是body
  return true;
}

// Prepend http headers
std::string
XmlRpcClient::generateHeader(size_t length) const
{
  //POST "uri" HTTP/1.1
  //User-Agent: "XMLRPC++ 0.7"
  //Host: "host:port"
  //Content-Type: text/xml
  //Content-length: len
  //
  //
  std::string header = 
    "POST " + _uri + " HTTP/1.1\r\n"
    "User-Agent: ";
  header += XMLRPC_VERSION;
  header += "\r\nHost: ";
  header += _host;

  char buff[40];
  snprintf(buff,40,":%d\r\n", _port);

  header += buff;
  header += "Content-Type: text/xml\r\nContent-length: ";

  snprintf(buff,40,"%zu\r\n\r\n", length);

  return header + buff;
}

bool
XmlRpcClient::writeRequest()
{
  if (_bytesWritten == 0)
    XmlRpcUtil::log(5, "XmlRpcClient::writeRequest (attempt %d):\n%s\n", _sendAttempts+1, _request.c_str());

  // Try to write the request
  if ( ! XmlRpcSocket::nbWrite(this->getfd(), _request, &_bytesWritten)) {
    XmlRpcUtil::error("Error in XmlRpcClient::writeRequest: write error (%s).",XmlRpcSocket::getErrorMsg().c_str());
    // If the write fails, we had an unrecoverable error. Close the socket.
	//如果写入请求遇到了错误，则关闭连接
    close();
    return false;
  }
    
  XmlRpcUtil::log(3, "XmlRpcClient::writeRequest: wrote %d of %d bytes.", _bytesWritten, _request.length());

  // Wait for the result
  //可能需要多次写入
  if (_bytesWritten == int(_request.length())) {
    _header = "";
    _response = "";
    _connectionState = READ_HEADER;//写入完成，客户端切换到读取header模式
  } else {
    // On partial write, remove the portion of the output that was written from
    // the request buffer.
	//如果只是部分请求已经写入，则去除已经写入的部分内容，下次继续写入
    _request = _request.substr(_bytesWritten);
    _bytesWritten = 0;
  }
  return true;
}


// Read the header from the response
bool
XmlRpcClient::readHeader()
{
  // Read available data
  if ( ! XmlRpcSocket::nbRead(this->getfd(), _header, &_eof) ||
       (_eof && _header.length() == 0)) {

    // If we haven't read any data yet and this is a keep-alive connection, the server may
    // have timed out, so we try one more time.
    if (getKeepOpen() && _header.length() == 0 && _sendAttempts++ == 0) {
      XmlRpcUtil::log(4, "XmlRpcClient::readHeader: re-trying connection");
      XmlRpcSource::close();
      _connectionState = NO_CONNECTION;
      _eof = false;
	  //重新连接
      return setupConnection();
    }

    XmlRpcUtil::error("Error in XmlRpcClient::readHeader: error while reading "
                      "header (%s) on fd %d.",
                      XmlRpcSocket::getErrorMsg().c_str(), getfd());
    // Read failed; this means the socket is in an unrecoverable state.
    // Close the socket.
    close();
    return false;
  }

  XmlRpcUtil::log(4, "XmlRpcClient::readHeader: client has read %d bytes", _header.length());

  char *hp = (char*)_header.c_str();  // Start of header
  char *ep = hp + _header.length();   // End of string
  char *bp = 0;                       // Start of body
  char *lp = 0;                       // Start of content-length value

  //查找头的一些属性
  for (char *cp = hp; (bp == 0) && (cp < ep); ++cp) {
    if ((ep - cp > 16) && (strncasecmp(cp, "Content-length: ", 16) == 0))
      lp = cp + 16;
    else if ((ep - cp > 4) && (strncmp(cp, "\r\n\r\n", 4) == 0))
      bp = cp + 4;
    else if ((ep - cp > 2) && (strncmp(cp, "\n\n", 2) == 0))
      bp = cp + 2;
  }

  // If we haven't gotten the entire header yet, return (keep reading)
  //没有找到继续读
  if (bp == 0) {
    if (_eof)          // EOF in the middle of a response is an error
    {
      XmlRpcUtil::error("Error in XmlRpcClient::readHeader: EOF while reading header");
      close();
      return false;   // Close the connection
    }
    
    return true;  // Keep reading
  }

  // Decode content length
  if (lp == 0) {
    XmlRpcUtil::error("Error XmlRpcClient::readHeader: No Content-length specified");
    // Close the socket because we can't make further use of it.
    close();
    return false;   // We could try to figure it out by parsing as we read, but for now...
  }
//解析得到contentlength
  _contentLength = atoi(lp);
  if (_contentLength <= 0) {
    XmlRpcUtil::error("Error in XmlRpcClient::readHeader: Invalid Content-length specified (%d).", _contentLength);
    // Close the socket because we can't make further use of it.
    close();
    return false;
  }
  	
  XmlRpcUtil::log(4, "client read content length: %d", _contentLength);

  // Otherwise copy non-header data to response buffer and set state to read response.
  _response = bp;
  _header = "";   // should parse out any interesting bits from the header (connection, etc)...
  _connectionState = READ_RESPONSE;
  return true;    // Continue monitoring this source
}

    
bool
XmlRpcClient::readResponse()
{
  // If we dont have the entire response yet, read available data
  if (int(_response.length()) < _contentLength) {
    std::string buff;
    if ( ! XmlRpcSocket::nbRead(this->getfd(), buff, &_eof)) {
      XmlRpcUtil::error("Error in XmlRpcClient::readResponse: read error (%s).",XmlRpcSocket::getErrorMsg().c_str());
      // nbRead returned an error, indicating that the socket is in a bad state.
      // close it and stop monitoring this client.
      close();
      return false;
    }
    _response += buff;//组成完整的response

    // If we haven't gotten the entire _response yet, return (keep reading)
	//长度不够继续读取
    if (int(_response.length()) < _contentLength) {
	//随时检测eof
      if (_eof) {
        XmlRpcUtil::error("Error in XmlRpcClient::readResponse: EOF while reading response");
        // nbRead returned an eof, indicating that the socket is disconnected.
		//eof表示socket断开连接
        // close it and stop monitoring this client.
		//关闭这个客户端
        close();
        return false;
      }
      return true;
    }
  }

  // Otherwise, parse and return the result
  XmlRpcUtil::log(3, "XmlRpcClient::readResponse (read %d bytes)", _response.length());
  XmlRpcUtil::log(5, "response:\n%s", _response.c_str());

  _connectionState = IDLE;

  return false;    // Stop monitoring this source (causes return from work)
}


// Convert the response xml into a result value
bool
XmlRpcClient::parseResponse(XmlRpcValue& result)
{
  // Parse response xml into result
  int offset = 0;
  if ( ! XmlRpcUtil::findTag(METHODRESPONSE_TAG,_response,&offset)) {
    XmlRpcUtil::error("Error in XmlRpcClient::parseResponse: Invalid response - no methodResponse. Response:\n%s", _response.c_str());
    return false;
  }

  // Expect either <params><param>... or <fault>...
  if ((XmlRpcUtil::nextTagIs(PARAMS_TAG,_response,&offset) &&
       XmlRpcUtil::nextTagIs(PARAM_TAG,_response,&offset)) ||
      (XmlRpcUtil::nextTagIs(FAULT_TAG,_response,&offset) && (_isFault = true)))
  {
    if ( ! result.fromXml(_response, &offset)) {
      XmlRpcUtil::error("Error in XmlRpcClient::parseResponse: Invalid response value. Response:\n%s", _response.c_str());
      _response = "";
      return false;
    }
  } else {
    XmlRpcUtil::error("Error in XmlRpcClient::parseResponse: Invalid response - no param or fault tag. Response:\n%s", _response.c_str());
    _response = "";
    return false;
  }
      
  _response = "";
  return result.valid();
}

