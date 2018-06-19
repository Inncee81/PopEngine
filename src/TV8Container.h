#pragma once
//#include "PopTrack.h"
#include <memory>
#include <functional>

class PopV8Allocator;

//	forward decalrations
namespace v8
{
	class Platform;
	class Isolate;
	class Context;
	
	//	our wrapper
	class CallbackInfo;
	
	template<typename T>
	class Local;
}


#include "include/libplatform/libplatform.h"
#include "include/v8.h"


class v8::CallbackInfo
{
public:
	CallbackInfo(const v8::FunctionCallbackInfo<v8::Value>& Params) :
	mParams	( Params )
	{
	}
	
	const v8::FunctionCallbackInfo<v8::Value>& mParams;
};



class TV8Container
{
public:
	TV8Container();
	
	void		LoadScript(const std::string& Source);
	template<const char* FunctionName>
	void		BindFunction(std::function<void(v8::CallbackInfo&)> Function)
	{
		static std::function<void(v8::CallbackInfo&)> FunctionCache = Function;
		auto RawFunction = [](const v8::FunctionCallbackInfo<v8::Value>& Paramsv8)
		{
			v8::CallbackInfo Params( Paramsv8 );
			FunctionCache( Params );
		};
		BindRawFunction( FunctionName, RawFunction );
	}
	void		ExecuteFunc(const std::string& FunctionName);

private:
	void		BindRawFunction(const char* FunctionName,void(*RawFunction)(const v8::FunctionCallbackInfo<v8::Value>&));

public:
	v8::Persistent<v8::Context>		mContext;		//	our "document", keep adding scripts toit
	v8::Isolate*					mIsolate;
	std::shared_ptr<v8::Platform>	mPlatform;
	std::shared_ptr<PopV8Allocator>	mAllocator;
};


