#pragma once
#include "TV8Container.h"
#include "SoyOpenglWindow.h"


class TMediaPacket;
class TMediaExtractor;
class TMediaExtractorParams;



namespace ApiMedia
{
	void	Bind(TV8Container& Container);
}

class TMediaWrapper
{
public:
	TMediaWrapper() :
		mContainer		( nullptr )
	{
	}
	
	static v8::Local<v8::FunctionTemplate>	CreateTemplate(TV8Container& Container);

	static void								Constructor(const v8::FunctionCallbackInfo<v8::Value>& Arguments);
	
	static v8::Local<v8::Value>				EnumDevices(const v8::CallbackInfo& Arguments);
	
public:
	std::shared_ptr<V8Storage<v8::Object>>	mHandle;
	TV8Container*				mContainer;
};


class TMediaSourceWrapper
{
public:
	TMediaSourceWrapper() :
		mContainer		( nullptr )
	{
	}
	
	static v8::Local<v8::FunctionTemplate>	CreateTemplate(TV8Container& Container);
	
	static void								Constructor(const v8::FunctionCallbackInfo<v8::Value>& Arguments);
	
	void									OnNewFrame(size_t StreamIndex);
	void									OnNewFrame(const TMediaPacket& FramePacket);
	
	static std::shared_ptr<TMediaExtractor>	AllocExtractor(const TMediaExtractorParams& Params);

public:
	std::shared_ptr<V8Storage<v8::Object>>		mHandle;
	std::shared_ptr<V8Storage<v8::Function>>	mOnFrameFilter;
	TV8Container*						mContainer;
	
	std::shared_ptr<TMediaExtractor>	mExtractor;
};
