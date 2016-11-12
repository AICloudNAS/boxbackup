// --------------------------------------------------------------------------
//
// File
//		Name:    S3Client.h
//		Purpose: Amazon S3 client helper implementation class
//		Created: 09/01/2009
//
// --------------------------------------------------------------------------

#ifndef S3CLIENT__H
#define S3CLIENT__H

#include <string>
#include <map>

#include "HTTPRequest.h"
#include "SocketStream.h"

class HTTPResponse;
class HTTPServer;
class IOStream;

// --------------------------------------------------------------------------
//
// Class
//		Name:    S3Client
//		Purpose: Amazon S3 client helper implementation class
//		Created: 09/01/2009
//
// --------------------------------------------------------------------------
class S3Client
{
	public:
	S3Client(HTTPServer* pSimulator, const std::string& rHostName,
		const std::string& rAccessKey, const std::string& rSecretKey)
	: mpSimulator(pSimulator),
	  mHostName(rHostName),
	  mAccessKey(rAccessKey),
	  mSecretKey(rSecretKey),
	  mNetworkTimeout(30000)
	{ }

	S3Client(const std::string& HostName, int Port, const std::string& rAccessKey,
		const std::string& rSecretKey, const std::string& VirtualHostName = "")
	: mpSimulator(NULL),
	  mHostName(HostName),
	  mPort(Port),
	  mVirtualHostName(VirtualHostName),
	  mAccessKey(rAccessKey),
	  mSecretKey(rSecretKey),
	  mNetworkTimeout(30000)
	{ }
		
	HTTPResponse GetObject(const std::string& rObjectURI);
	HTTPResponse HeadObject(const std::string& rObjectURI);
	HTTPResponse PutObject(const std::string& rObjectURI,
		IOStream& rStreamToSend, const char* pContentType = NULL);
	void CheckResponse(const HTTPResponse& response, const std::string& message) const;
	int GetNetworkTimeout() const { return mNetworkTimeout; }

	private:
	HTTPServer* mpSimulator;
	// mHostName is the network address that we will connect to (e.g. localhost):
	std::string mHostName;
	int mPort;
	// mVirtualHostName is the Host header that we will send, e.g.
	// "quotes.s3.amazonaws.com". If empty, mHostName will be used as a default.
	std::string mVirtualHostName;
	std::auto_ptr<SocketStream> mapClientSocket;
	std::string mAccessKey, mSecretKey;
	int mNetworkTimeout; // milliseconds

	HTTPResponse FinishAndSendRequest(HTTPRequest::Method Method,
		const std::string& rRequestURI,
		IOStream* pStreamToSend = NULL,
		const char* pStreamContentType = NULL);
	HTTPResponse SendRequest(HTTPRequest& rRequest,
		IOStream* pStreamToSend = NULL,
		const char* pStreamContentType = NULL);
};

#endif // S3CLIENT__H

