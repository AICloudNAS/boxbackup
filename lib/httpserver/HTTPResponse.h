// --------------------------------------------------------------------------
//
// File
//		Name:    HTTPResponse.h
//		Purpose: Response object for HTTP connections
//		Created: 26/3/04
//
// --------------------------------------------------------------------------

#ifndef HTTPRESPONSE__H
#define HTTPRESPONSE__H

#include <string>
#include <vector>

#include "CollectInBufferStream.h"

class IOStreamGetLine;

// --------------------------------------------------------------------------
//
// Class
//		Name:    HTTPResponse
//		Purpose: Response object for HTTP connections
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
class HTTPResponse : public CollectInBufferStream
{
public:
	HTTPResponse(IOStream* pStreamToSendTo);
	HTTPResponse();
	~HTTPResponse();

	// allow copying, but be very careful with the response stream,
	// you can only read it once! (this class doesn't police it).
	HTTPResponse(const HTTPResponse& rOther)
	: mResponseCode(rOther.mResponseCode),
	  mResponseIsDynamicContent(rOther.mResponseIsDynamicContent),
	  mKeepAlive(rOther.mKeepAlive),
	  mContentType(rOther.mContentType),
	  mExtraHeaders(rOther.mExtraHeaders),
	  mContentLength(rOther.mContentLength),
	  mpStreamToSendTo(rOther.mpStreamToSendTo)
	{
		Write(rOther.GetBuffer(), rOther.GetSize());
	}
		
	HTTPResponse &operator=(const HTTPResponse &rOther)
	{
		Reset();
		Write(rOther.GetBuffer(), rOther.GetSize());
		mResponseCode = rOther.mResponseCode;
		mResponseIsDynamicContent = rOther.mResponseIsDynamicContent;
		mKeepAlive = rOther.mKeepAlive;
		mContentType = rOther.mContentType;
		mExtraHeaders = rOther.mExtraHeaders;
		mContentLength = rOther.mContentLength;
		mpStreamToSendTo = rOther.mpStreamToSendTo;
		return *this;
	}
	
	typedef std::pair<std::string, std::string> Header;

	void SetResponseCode(int Code);
	int GetResponseCode() const { return mResponseCode; }
	void SetContentType(const char *ContentType);
	const std::string& GetContentType() { return mContentType; }
	int64_t GetContentLength() { return mContentLength; }

	void SetAsRedirect(const char *RedirectTo, bool IsLocalURI = true);
	void SetAsNotFound(const char *URI);

	void Send(bool OmitContent = false);
	void SendContinue();
	void Receive(IOStream& rStream, int Timeout = IOStream::TimeOutInfinite);

	// void AddHeader(const char *EntireHeaderLine);
	// void AddHeader(const std::string &rEntireHeaderLine);
	void AddHeader(const char *Header, const char *Value);
	void AddHeader(const char *Header, const std::string &rValue);
	void AddHeader(const std::string &rHeader, const std::string &rValue);
	bool GetHeader(const std::string& rName, std::string* pValueOut) const
	{
		for (std::vector<Header>::const_iterator
			i  = mExtraHeaders.begin();
			i != mExtraHeaders.end(); i++)
		{
			if (i->first == rName)
			{
				*pValueOut = i->second;
				return true;
			}
		}
		return false;
	}
	std::string GetHeaderValue(const std::string& rName)
	{
		std::string value;
		if (!GetHeader(rName, &value))
		{
			THROW_EXCEPTION(CommonException, ConfigNoKey);
		}
		return value;
	}

	// Set dynamic content flag, default is content is dynamic
	void SetResponseIsDynamicContent(bool IsDynamic) {mResponseIsDynamicContent = IsDynamic;}
	// Set keep alive control, default is to mark as to be closed
	void SetKeepAlive(bool KeepAlive) {mKeepAlive = KeepAlive;}
	bool IsKeepAlive() {return mKeepAlive;}

	void SetCookie(const char *Name, const char *Value, const char *Path = "/", int ExpiresAt = 0);

	enum
	{
		Code_OK = 200,
		Code_NoContent = 204,
		Code_MovedPermanently = 301,
		Code_Found = 302,	// redirection
		Code_NotModified = 304,
		Code_TemporaryRedirect = 307,
		Code_MethodNotAllowed = 400,
		Code_Unauthorized = 401,
		Code_Forbidden = 403,
		Code_NotFound = 404,
		Code_InternalServerError = 500,
		Code_NotImplemented = 501
	};

	static const char *ResponseCodeToString(int ResponseCode);
	
	void WriteStringDefang(const char *String, unsigned int StringLen);
	void WriteStringDefang(const std::string &rString) {WriteStringDefang(rString.c_str(), rString.size());}

	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    HTTPResponse::WriteString(const std::string &)
	//		Purpose: Write a string to the response (simple sugar function)
	//		Created: 9/4/04
	//
	// --------------------------------------------------------------------------
	void WriteString(const std::string &rString)
	{
		Write(rString.c_str(), rString.size());
	}

	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    HTTPResponse::SetDefaultURIPrefix(const std::string &)
	//		Purpose: Set default prefix used to local redirections
	//		Created: 26/3/04
	//
	// --------------------------------------------------------------------------
	static void SetDefaultURIPrefix(const std::string &rPrefix)
	{
		msDefaultURIPrefix = rPrefix;
	}

private:
	int mResponseCode;
	bool mResponseIsDynamicContent;
	bool mKeepAlive;
	std::string mContentType;
	std::vector<Header> mExtraHeaders;
	int64_t mContentLength; // only used when reading response from stream
	IOStream* mpStreamToSendTo; // nonzero only when constructed with a stream
	
	static std::string msDefaultURIPrefix;

	void ParseHeaders(IOStreamGetLine &rGetLine, int Timeout);
};

#endif // HTTPRESPONSE__H

