
#include "xmlrpcpp/XmlRpcServerConnection.h"

#include "xmlrpcpp/XmlRpcSocket.h"
#include "xmlrpcpp/XmlRpc.h"
#ifndef MAKEDEPEND
# include <stdio.h>
# include <stdlib.h>
#ifndef _WINDOWS
	# include <strings.h>
#endif
# include <string.h>
#endif

using namespace XmlRpc;

// Static data
const char XmlRpcServerConnection::METHODNAME_TAG[] = "<methodName>";
const char XmlRpcServerConnection::PARAMS_TAG[] = "<params>";
const char XmlRpcServerConnection::PARAMS_ETAG[] = "</params>";
const char XmlRpcServerConnection::PARAM_TAG[] = "<param>";
const char XmlRpcServerConnection::PARAM_ETAG[] = "</param>";

const std::string XmlRpcServerConnection::SYSTEM_MULTICALL = "system.multicall";
const std::string XmlRpcServerConnection::METHODNAME = "methodName";
const std::string XmlRpcServerConnection::PARAMS = "params";

const std::string XmlRpcServerConnection::FAULTCODE = "faultCode";
const std::string XmlRpcServerConnection::FAULTSTRING = "faultString";



// The server delegates handling client requests to a serverConnection object.
//负责处理与客户端的交互
XmlRpcServerConnection::XmlRpcServerConnection(int fd, XmlRpcServer* server, bool deleteOnClose /*= false*/) :
  XmlRpcSource(fd, deleteOnClose)
{
  XmlRpcUtil::log(2,"XmlRpcServerConnection: new socket %d.", fd);
  _server = server;
  _connectionState = READ_HEADER;//先读取客户端发来的header
  _contentLength = 0;//正文长度
  _bytesWritten = 0;//已经写入的字节数量
  _keepAlive = true;
}


XmlRpcServerConnection::~XmlRpcServerConnection()
{
  XmlRpcUtil::log(4,"XmlRpcServerConnection dtor.");
  _server->removeConnection(this);//从服务器的连接列表中删除
}


// Handle input on the server socket by accepting the connection
// and reading the rpc request. Return true to continue to monitor
// the socket for events, false to remove it from the dispatcher.
unsigned
XmlRpcServerConnection::handleEvent(unsigned /*eventType*/)
{
  //每个连接内部存储了一个自己的状态
  //各个阶段的实现是采用的状态机切换的方式
  //返回0将从server的dispacher中删除
  if (_connectionState == READ_HEADER)
    if ( ! readHeader()) return 0;

  if (_connectionState == READ_REQUEST)
    if ( ! readRequest()) return 0;

  if (_connectionState == WRITE_RESPONSE)
    if ( ! writeResponse()) return 0;

  return (_connectionState == WRITE_RESPONSE) 
        ? XmlRpcDispatch::WritableEvent : XmlRpcDispatch::ReadableEvent;
}


bool
XmlRpcServerConnection::readHeader()
{
  // Read available data
  //读取已经到来的数据
  bool eof;
  if ( ! XmlRpcSocket::nbRead(this->getfd(), _header, &eof)) {
    // Its only an error if we already have read some data
    //读取出现错误
    if (_header.length() > 0)
      XmlRpcUtil::error("XmlRpcServerConnection::readHeader: error while reading header (%s).",XmlRpcSocket::getErrorMsg().c_str());
    return false;
  }
  //在header中检索每个字段
  XmlRpcUtil::log(4, "XmlRpcServerConnection::readHeader: read %d bytes.", _header.length());
  char *hp = (char*)_header.c_str();  // Start of header
  char *ep = hp + _header.length();   // End of string
  char *bp = 0;                       // Start of body
  char *lp = 0;                       // Start of content-length value
  char *kp = 0;                       // Start of connection value

  //逐个对比关键字，提取出对应的结构
  //这样估计是为了提高效率，一次迭代对比出各个结构位置
  for (char *cp = hp; (bp == 0) && (cp < ep); ++cp) {
	if ((ep - cp > 16) && (strncasecmp(cp, "Content-length: ", 16) == 0))
	  lp = cp + 16;
	else if ((ep - cp > 12) && (strncasecmp(cp, "Connection: ", 12) == 0))
	  kp = cp + 12;
	else if ((ep - cp > 4) && (strncmp(cp, "\r\n\r\n", 4) == 0))//windows??
	  bp = cp + 4;
	else if ((ep - cp > 2) && (strncmp(cp, "\n\n", 2) == 0))//linux??
	  bp = cp + 2;
  }

  // If we haven't gotten the entire header yet, return (keep reading)
  //如果没有找"\r\n\r\n"那么就是没有获取到整个header
  if (bp == 0) {
    // EOF in the middle of a request is an error, otherwise its ok
    //读到一半网络断开
    if (eof) {
      XmlRpcUtil::log(4, "XmlRpcServerConnection::readHeader: EOF");
      if (_header.length() > 0)
        XmlRpcUtil::error("XmlRpcServerConnection::readHeader: EOF while reading header");
      return false;   // Either way we close the connection
    }
    //因为是流读取，需要保证读取的完整性机制，这里通过检测bp是否读到来确定header的完整性
    return true;  // Keep reading继续读取
  }

  // Decode content length
  if (lp == 0) {
    XmlRpcUtil::error("XmlRpcServerConnection::readHeader: No Content-length specified");
    return false;   // We could try to figure it out by parsing as we read, but for now...
  }
  
  //获取到长度，为下一步读取请求使用
  _contentLength = atoi(lp);

  if (_contentLength <= 0) {
    XmlRpcUtil::error("XmlRpcServerConnection::readHeader: Invalid Content-length specified (%d).", _contentLength);
    return false;
  }
  	
  XmlRpcUtil::log(3, "XmlRpcServerConnection::readHeader: specified content length is %d.", _contentLength);

  // Otherwise copy non-header data to request buffer and set state to read request.
  //将多余的非header数据保存，readrequest阶段会使用，重要！
  _request = bp;

  // Parse out any interesting bits from the header (HTTP version, connection)
  
  _keepAlive = true;
  if (_header.find("HTTP/1.0") != std::string::npos) {
    if (kp == 0 || strncasecmp(kp, "keep-alive", 10) != 0)
      _keepAlive = false;           // Default for HTTP 1.0 is to close the connection
  } else {
    if (kp != 0 && strncasecmp(kp, "close", 5) == 0)
      _keepAlive = false;
  }
  XmlRpcUtil::log(3, "KeepAlive: %d", _keepAlive);


  _header = ""; //清空header数据
  _connectionState = READ_REQUEST;//转换到读取request状态
  return true;    // Continue monitoring this source
}

bool
XmlRpcServerConnection::readRequest()
{
  // If we dont have the entire request yet, read available data
  //以readheader阶段读取到的content的length作为本次读取的完整性条件
  if (int(_request.length()) < _contentLength) {
    bool eof;
	//读取出现错误
    if ( ! XmlRpcSocket::nbRead(this->getfd(), _request, &eof)) {
      XmlRpcUtil::error("XmlRpcServerConnection::readRequest: read error (%s).",XmlRpcSocket::getErrorMsg().c_str());
      return false;
    }

    // If we haven't gotten the entire request yet, return (keep reading)
    if (int(_request.length()) < _contentLength) {
      if (eof) {
        XmlRpcUtil::error("XmlRpcServerConnection::readRequest: EOF while reading request");
        return false;   // Either way we close the connection
      }
      return true;//继续读取
    }
  }

  // Otherwise, parse and dispatch the request
  XmlRpcUtil::log(3, "XmlRpcServerConnection::readRequest read %d bytes.", _request.length());
  //XmlRpcUtil::log(5, "XmlRpcServerConnection::readRequest:\n%s\n", _request.c_str());

  _connectionState = WRITE_RESPONSE;//切换到下一个阶段进行响应

  return true;    // Continue monitoring this source
}


bool
XmlRpcServerConnection::writeResponse()
{
  if (_response.length() == 0) {
    //执行注册的函数
    executeRequest();
    _bytesWritten = 0;
    if (_response.length() == 0) {
      XmlRpcUtil::error("XmlRpcServerConnection::writeResponse: empty response.");
      return false;
    }
  }

  // Try to write the response
  //将响应写入socket，可能需要多次
  if ( ! XmlRpcSocket::nbWrite(this->getfd(), _response, &_bytesWritten)) {
    XmlRpcUtil::error("XmlRpcServerConnection::writeResponse: write error (%s).",XmlRpcSocket::getErrorMsg().c_str());
    return false;
  }
  XmlRpcUtil::log(3, "XmlRpcServerConnection::writeResponse: wrote %d of %d bytes.", _bytesWritten, _response.length());

  // Prepare to read the next request
  //将response长度作为结束写入状态的标准
  if (_bytesWritten == int(_response.length())) {
    //全部恢复
    _header = "";
    _request = "";
    _response = "";
    _connectionState = READ_HEADER;//恢复到READ_HEADER状态
  }
  //keepalive参数是从文件头中解析出来，说明了一次调用完成后是否保持该连接
  return _keepAlive;    // Continue monitoring this source if true
}

// Run the method, generate _response string
void
XmlRpcServerConnection::executeRequest()
{
  XmlRpcValue params, resultValue;
  std::string methodName = parseRequest(params);
  XmlRpcUtil::log(2, "XmlRpcServerConnection::executeRequest: server calling method '%s'", 
                    methodName.c_str());

  try {

    if ( ! executeMethod(methodName, params, resultValue) &&
         ! executeMulticall(methodName, params, resultValue))
      generateFaultResponse(methodName + ": unknown method name");
    else
      generateResponse(resultValue.toXml());//将结果转化为xml结果

  } catch (const XmlRpcException& fault) {
    XmlRpcUtil::log(2, "XmlRpcServerConnection::executeRequest: fault %s.",
                    fault.getMessage().c_str()); 
    generateFaultResponse(fault.getMessage(), fault.getCode());
  }
}

// Parse the method name and the argument values from the request.
//解析发来的body，得出函数名称，解析得到参数
std::string
XmlRpcServerConnection::parseRequest(XmlRpcValue& params)
{
  int offset = 0;   // Number of chars parsed from the request

  std::string methodName = XmlRpcUtil::parseTag(METHODNAME_TAG, _request, &offset);

  if (methodName.size() > 0 && XmlRpcUtil::findTag(PARAMS_TAG, _request, &offset))
  {
    int nArgs = 0;
    while (XmlRpcUtil::nextTagIs(PARAM_TAG, _request, &offset)) {
      params[nArgs++] = XmlRpcValue(_request, &offset);
      (void) XmlRpcUtil::nextTagIs(PARAM_ETAG, _request, &offset);
    }

    (void) XmlRpcUtil::nextTagIs(PARAMS_ETAG, _request, &offset);
  }

  return methodName;
}

// Execute a named method with the specified params.
bool
XmlRpcServerConnection::executeMethod(const std::string& methodName, 
                                      XmlRpcValue& params, XmlRpcValue& result)
{
  //从已经注册的函数中找到函数
  XmlRpcServerMethod* method = _server->findMethod(methodName);

  if ( ! method) return false;
  //执行
  method->execute(params, result);

  // Ensure a valid result value
  if ( ! result.valid())
      result = std::string();

  return true;
}

// Execute multiple calls and return the results in an array.
bool
XmlRpcServerConnection::executeMulticall(const std::string& methodName, 
                                         XmlRpcValue& params, XmlRpcValue& result)
{
  if (methodName != SYSTEM_MULTICALL) return false;

  // There ought to be 1 parameter, an array of structs
  if (params.size() != 1 || params[0].getType() != XmlRpcValue::TypeArray)
    throw XmlRpcException(SYSTEM_MULTICALL + ": Invalid argument (expected an array)");

  int nc = params[0].size();
  result.setSize(nc);

  for (int i=0; i<nc; ++i) {

    if ( ! params[0][i].hasMember(METHODNAME) ||
         ! params[0][i].hasMember(PARAMS)) {
      result[i][FAULTCODE] = -1;
      result[i][FAULTSTRING] = SYSTEM_MULTICALL +
              ": Invalid argument (expected a struct with members methodName and params)";
      continue;
    }

    const std::string& methodName = params[0][i][METHODNAME];
    XmlRpcValue& methodParams = params[0][i][PARAMS];

    XmlRpcValue resultValue;
    resultValue.setSize(1);
    try {
      if ( ! executeMethod(methodName, methodParams, resultValue[0]) &&
           ! executeMulticall(methodName, params, resultValue[0]))
      {
        result[i][FAULTCODE] = -1;
        result[i][FAULTSTRING] = methodName + ": unknown method name";
      }
      else
        result[i] = resultValue;

    } catch (const XmlRpcException& fault) {
        result[i][FAULTCODE] = fault.getCode();
        result[i][FAULTSTRING] = fault.getMessage();
    }
  }

  return true;
}


// Create a response from results xml
void
XmlRpcServerConnection::generateResponse(std::string const& resultXml)
{
  const char RESPONSE_1[] = 
    "<?xml version=\"1.0\"?>\r\n"
    "<methodResponse><params><param>\r\n\t";
  const char RESPONSE_2[] =
    "\r\n</param></params></methodResponse>\r\n";

  std::string body = RESPONSE_1 + resultXml + RESPONSE_2;
  std::string header = generateHeader(body);

  _response = header + body;
  XmlRpcUtil::log(5, "XmlRpcServerConnection::generateResponse:\n%s\n", _response.c_str()); 
}

// Prepend http headers
std::string
XmlRpcServerConnection::generateHeader(std::string const& body)
{
  std::string header = 
    "HTTP/1.1 200 OK\r\n"
    "Server: ";
  header += XMLRPC_VERSION;
  header += "\r\n"
    "Content-Type: text/xml\r\n"
    "Content-length: ";

  char buffLen[40];
#ifdef _MSC_VER
  sprintf_s(buffLen,40,"%d\r\n\r\n", (int)body.size());
#else
  sprintf(buffLen,"%d\r\n\r\n", (int)body.size());
#endif

  return header + buffLen;
}


void
XmlRpcServerConnection::generateFaultResponse(std::string const& errorMsg, int errorCode)
{
  const char RESPONSE_1[] = 
    "<?xml version=\"1.0\"?>\r\n"
    "<methodResponse><fault>\r\n\t";
  const char RESPONSE_2[] =
    "\r\n</fault></methodResponse>\r\n";

  XmlRpcValue faultStruct;
  faultStruct[FAULTCODE] = errorCode;
  faultStruct[FAULTSTRING] = errorMsg;
  std::string body = RESPONSE_1 + faultStruct.toXml() + RESPONSE_2;
  std::string header = generateHeader(body);

  _response = header + body;
}

